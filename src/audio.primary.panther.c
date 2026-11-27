#define LOG_TAG "audio.primary.panther"
#include "bionic_ext.h"
#include <hardware/audio.h>
#include <hardware/hardware.h>
#include <system/audio.h>
#include <tinyalsa/asoundlib.h>

/* Pixel 7 (gs201) AoC ALSA card & device IDs from audio_platform_configuration.xml */
#define AOC_PCM_LOW_LATENCY   1
#define AOC_PCM_DEEP_BUFFER   5
#define AOC_PCM_MMAP_OUT      0
#define AOC_PCM_VOIP_OUT      16
#define AOC_PCM_RECORD        8
#define AOC_PCM_VOIP_IN       17
#define AOC_PCM_VOICE_DL      4
#define AOC_PCM_VOICE_UL      11

#define DEFAULT_RATE          48000
#define DEFAULT_CHANNELS      2
#define OUT_PERIOD_FRAMES     480   /* 10 ms @ 48 kHz */
#define OUT_PERIOD_COUNT      4
#define IN_PERIOD_FRAMES      960   /* 20 ms @ 48 kHz */
#define IN_PERIOD_COUNT       4

struct panther_audio_dev;

typedef struct panther_stream_out {
    struct audio_stream_out stream;
    struct panther_audio_dev *dev;
    pthread_mutex_t lock;
    struct pcm *pcm;
    struct pcm_config cfg;
    int hw_32bit;
    int32_t *conv_buf;
    size_t conv_buf_frames;
    int standby;
    int pcm_device;
    audio_devices_t devices;
    audio_output_flags_t flags;
    uint64_t frames_written;
    float vol_l;
    float vol_r;
} panther_stream_out_t;

typedef struct panther_stream_in {
    struct audio_stream_in stream;
    struct panther_audio_dev *dev;
    pthread_mutex_t lock;
    struct pcm *pcm;
    struct pcm_config cfg;
    int standby;
    int pcm_device;
    audio_devices_t devices;
    audio_source_t source;
    uint32_t frames_lost;
} panther_stream_in_t;

typedef struct panther_audio_dev {
    struct audio_hw_device hw_dev;
    pthread_mutex_t lock;
    int card;
    struct mixer *mixer;
    audio_mode_t mode;
    audio_devices_t out_device;
    audio_devices_t in_device;
    float master_volume;
    float voice_volume;
    bool master_mute;
    bool mic_mute;
    int mixer_inited;
    panther_stream_out_t *primary_out;
    panther_stream_in_t *active_in;
} panther_audio_dev_t;

static void mix_set_int(struct mixer *m, const char *name, int val) {
    if (!m) return;
    struct mixer_ctl *ctl = mixer_get_ctl_by_name(m, name);
    if (!ctl) return;
    unsigned int n = mixer_ctl_get_num_values(ctl);
    for (unsigned int i = 0; i < n; i++) {
        mixer_ctl_set_value(ctl, i, val);
    }
}

static void mix_set_idx(struct mixer *m, const char *name, unsigned int idx, int val) {
    if (!m) return;
    struct mixer_ctl *ctl = mixer_get_ctl_by_name(m, name);
    if (ctl) mixer_ctl_set_value(ctl, idx, val);
}

static void mix_set_enum(struct mixer *m, const char *name, const char *val) {
    if (!m) return;
    struct mixer_ctl *ctl = mixer_get_ctl_by_name(m, name);
    if (ctl) mixer_ctl_set_enum_by_string(ctl, val);
}

static int find_aoc_card(void) {
    FILE *f = fopen("/proc/asound/cards", "r");
    if (!f) return 0;
    char line[256];
    int found = -1;
    int first_valid = -1;
    while (fgets(line, sizeof(line), f)) {
        int id = -1;
        if (sscanf(line, " %d [", &id) == 1 && id >= 0) {
            if (first_valid < 0) first_valid = id;
            if (strstr(line, "aoc") || strstr(line, "AoC") || strstr(line, "google")) {
                found = id;
                break;
            }
        }
    }
    fclose(f);
    return (found >= 0) ? found : (first_valid >= 0 ? first_valid : 0);
}

static void ensure_mixer_locked(panther_audio_dev_t *adev) {
    if (adev->card < 0) adev->card = find_aoc_card();
    if (!adev->mixer) adev->mixer = mixer_open((unsigned int)adev->card);
    if (adev->mixer && !adev->mixer_inited) {
        /* Base TDM & Cirrus CS35L41 defaults matching vendor/etc/mixer_paths.xml */
        mix_set_int(adev->mixer, "PCM Stream Wait Time in MSec", 100);
        mix_set_int(adev->mixer, "Voice PCM Stream Wait Time in MSec", 500);
        mix_set_enum(adev->mixer, "TDM_0_RX Chan", "Four");
        mix_set_enum(adev->mixer, "TDM_0_RX Format", "S32_LE");
        mix_set_enum(adev->mixer, "TDM_0_RX Sample Rate", "SR_48K");
        mix_set_enum(adev->mixer, "TDM_0_TX Chan", "Four");
        mix_set_enum(adev->mixer, "TDM_0_TX Format", "S32_LE");
        mix_set_enum(adev->mixer, "TDM_0_TX Sample Rate", "SR_48K");
        mix_set_int(adev->mixer, "ASPRX1 Slot Position", 0);
        mix_set_int(adev->mixer, "ASPRX2 Slot Position", 1);
        mix_set_enum(adev->mixer, "Audio Capture Mic Source", "Builtin_MIC");
        mix_set_int(adev->mixer, "MIC DC Blocker", 1);
        adev->mixer_inited = 1;
        ALOGI("AoC mixer initialized on card %d (%s)", adev->card,
              mixer_get_name(adev->mixer) ? mixer_get_name(adev->mixer) : "aoc");
    }
}

static void apply_output_route_locked(panther_audio_dev_t *adev, audio_devices_t dev, int pcm_dev) {
    ensure_mixer_locked(adev);
    if (!adev->mixer) return;

    const char *ep_name = (pcm_dev == AOC_PCM_DEEP_BUFFER) ? "EP6" :
                          (pcm_dev == AOC_PCM_MMAP_OUT)    ? "EP1" :
                          (pcm_dev == AOC_PCM_VOIP_OUT)    ? "VOIP" : "EP2";
    char ctl_buf[64];

    /* Clear previous FE mixers for this endpoint */
    snprintf(ctl_buf, sizeof(ctl_buf), "TDM_0_RX Mixer %s", ep_name);
    mix_set_int(adev->mixer, ctl_buf, 0);
    snprintf(ctl_buf, sizeof(ctl_buf), "BT_RX Mixer %s", ep_name);
    mix_set_int(adev->mixer, ctl_buf, 0);
    snprintf(ctl_buf, sizeof(ctl_buf), "USB_RX Mixer %s", ep_name);
    mix_set_int(adev->mixer, ctl_buf, 0);

    if (dev & AUDIO_DEVICE_OUT_ALL_SCO) {
        snprintf(ctl_buf, sizeof(ctl_buf), "BT_RX Mixer %s", ep_name);
        mix_set_int(adev->mixer, ctl_buf, 1);
    } else if (dev & AUDIO_DEVICE_OUT_ALL_USB) {
        snprintf(ctl_buf, sizeof(ctl_buf), "USB_RX Mixer %s", ep_name);
        mix_set_int(adev->mixer, ctl_buf, 1);
    } else if (dev & AUDIO_DEVICE_OUT_EARPIECE) {
        mix_set_enum(adev->mixer, "PCM Source", "ASP");
        mix_set_int(adev->mixer, "AMP PCM Gain", 6);
        mix_set_int(adev->mixer, "R Main AMP Enable Switch", 0);
        mix_set_int(adev->mixer, "Main AMP Enable Switch", 1);
        snprintf(ctl_buf, sizeof(ctl_buf), "TDM_0_RX Mixer %s", ep_name);
        mix_set_int(adev->mixer, ctl_buf, 1);
    } else {
        /* Speaker (default dual Cirrus CS35L41 amps) */
        mix_set_enum(adev->mixer, "Boost Peak Current Limit", "2.50A");
        mix_set_enum(adev->mixer, "R Boost Peak Current Limit", "2.50A");
        mix_set_int(adev->mixer, "Main AMP Enable Switch", 1);
        mix_set_int(adev->mixer, "R Main AMP Enable Switch", 1);
        mix_set_enum(adev->mixer, "Fast Use Case Delta File", "fast_switch3.txt");
        mix_set_int(adev->mixer, "Fast Use Case Switch Enable", 1);
        mix_set_enum(adev->mixer, "R Fast Use Case Delta File", "fast_switch4.txt");
        mix_set_int(adev->mixer, "R Fast Use Case Switch Enable", 1);
        snprintf(ctl_buf, sizeof(ctl_buf), "TDM_0_RX Mixer %s", ep_name);
        mix_set_int(adev->mixer, ctl_buf, 1);
    }
}

static void apply_input_route_locked(panther_audio_dev_t *adev, audio_devices_t dev) {
    ensure_mixer_locked(adev);
    if (!adev->mixer) return;
    mix_set_int(adev->mixer, "EP4 TX Mixer INTERNAL_MIC_TX", 0);
    mix_set_int(adev->mixer, "EP4 TX Mixer USB_TX", 0);
    mix_set_int(adev->mixer, "EP4 TX Mixer BT_TX", 0);

    if (dev & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
        mix_set_int(adev->mixer, "EP4 TX Mixer BT_TX", 1);
    } else if (dev & AUDIO_DEVICE_IN_USB_DEVICE) {
        mix_set_int(adev->mixer, "EP4 TX Mixer USB_TX", 1);
    } else {
        mix_set_int(adev->mixer, "Mic Spatial Module Enable", 1);
        mix_set_idx(adev->mixer, "BUILDIN MIC ID CAPTURE LIST", 0, 0);
        mix_set_idx(adev->mixer, "BUILDIN MIC ID CAPTURE LIST", 1, 3);
        mix_set_idx(adev->mixer, "BUILDIN MIC ID CAPTURE LIST", 2, 2);
        mix_set_idx(adev->mixer, "BUILDIN MIC ID CAPTURE LIST", 3, -1);
        mix_set_int(adev->mixer, "MIC DC Blocker", 1);
        mix_set_int(adev->mixer, "MIC Record Soft Gain (dB)", adev->mic_mute ? -96 : 22);
        mix_set_int(adev->mixer, "EP4 TX Mixer INTERNAL_MIC_TX", 1);
        mix_set_int(adev->mixer, "EP1 TX Mixer INTERNAL_MIC_TX", 1);
    }
}

static int parse_kv_int(const char *kv, const char *key, int *out) {
    const char *p = strstr(kv, key);
    if (!p) return 0;
    p += strlen(key);
    if (*p != '=') return 0;
    *out = (int)strtol(p + 1, NULL, 0);
    return 1;
}

/* ================= Output Stream ================= */

static uint32_t out_get_sample_rate(const struct audio_stream *s) {
    const panther_stream_out_t *out = (const panther_stream_out_t *)s;
    return out->cfg.rate;
}
static int out_set_sample_rate(struct audio_stream *s, uint32_t rate) {
    (void)s; (void)rate; return -ENOSYS;
}
static size_t out_get_buffer_size(const struct audio_stream *s) {
    const panther_stream_out_t *out = (const panther_stream_out_t *)s;
    return out->cfg.period_size * out->cfg.channels * 2; /* 16-bit client frames */
}
static audio_channel_mask_t out_get_channels(const struct audio_stream *s) {
    (void)s; return AUDIO_CHANNEL_OUT_STEREO;
}
static audio_format_t out_get_format(const struct audio_stream *s) {
    (void)s; return AUDIO_FORMAT_PCM_16_BIT;
}
static int out_set_format(struct audio_stream *s, audio_format_t fmt) {
    (void)s; (void)fmt; return -ENOSYS;
}
static int out_standby(struct audio_stream *s) {
    panther_stream_out_t *out = (panther_stream_out_t *)s;
    pthread_mutex_lock(&out->lock);
    if (out->pcm) {
        pcm_close(out->pcm);
        out->pcm = NULL;
    }
    out->standby = 1;
    pthread_mutex_unlock(&out->lock);
    return 0;
}
static int out_dump(const struct audio_stream *s, int fd) {
    (void)s; (void)fd; return 0;
}
static audio_devices_t out_get_device(const struct audio_stream *s) {
    return ((const panther_stream_out_t *)s)->devices;
}
static int out_set_device(struct audio_stream *s, audio_devices_t dev) {
    panther_stream_out_t *out = (panther_stream_out_t *)s;
    pthread_mutex_lock(&out->lock);
    out->devices = dev;
    pthread_mutex_lock(&out->dev->lock);
    out->dev->out_device = dev;
    apply_output_route_locked(out->dev, dev, out->pcm_device);
    pthread_mutex_unlock(&out->dev->lock);
    pthread_mutex_unlock(&out->lock);
    return 0;
}
static int out_set_parameters(struct audio_stream *s, const char *kv) {
    panther_stream_out_t *out = (panther_stream_out_t *)s;
    int val = 0;
    if (kv && parse_kv_int(kv, AUDIO_PARAMETER_STREAM_ROUTING, &val) && val != 0) {
        out_set_device(s, (audio_devices_t)val);
    }
    (void)out;
    return 0;
}
static char *out_get_parameters(const struct audio_stream *s, const char *keys) {
    (void)s; (void)keys; return strdup("");
}
static int out_add_effect(const struct audio_stream *s, effect_handle_t e) {
    (void)s; (void)e; return 0;
}
static int out_remove_effect(const struct audio_stream *s, effect_handle_t e) {
    (void)s; (void)e; return 0;
}
static uint32_t out_get_latency(const struct audio_stream_out *s) {
    const panther_stream_out_t *out = (const panther_stream_out_t *)s;
    return (out->cfg.period_size * out->cfg.period_count * 1000) / out->cfg.rate;
}
static int out_set_volume(struct audio_stream_out *s, float l, float r) {
    panther_stream_out_t *out = (panther_stream_out_t *)s;
    out->vol_l = l; out->vol_r = r;
    return -ENOSYS; /* let AudioFlinger apply SW gain unless HW DSP needed */
}

static int out_start_pcm_locked(panther_stream_out_t *out) {
    pthread_mutex_lock(&out->dev->lock);
    apply_output_route_locked(out->dev, out->devices, out->pcm_device);
    int card = out->dev->card;
    pthread_mutex_unlock(&out->dev->lock);

    int candidates[3] = { out->pcm_device, AOC_PCM_DEEP_BUFFER, 0 };
    for (int i = 0; i < 3; i++) {
        int dev_id = candidates[i];
        struct pcm_config c16 = out->cfg;
        c16.format = PCM_FORMAT_S16_LE;
        struct pcm *p = pcm_open((unsigned int)card, (unsigned int)dev_id, PCM_OUT, &c16);
        if (p && pcm_is_ready(p)) {
            out->pcm = p;
            out->pcm_device = dev_id;
            out->hw_32bit = 0;
            out->standby = 0;
            ALOGI("Opened AoC playback card=%d dev=%d S16_LE %uHz %uch", card, dev_id, c16.rate, c16.channels);
            return 0;
        }
        if (p) pcm_close(p);

        struct pcm_config c32 = out->cfg;
        c32.format = PCM_FORMAT_S32_LE;
        p = pcm_open((unsigned int)card, (unsigned int)dev_id, PCM_OUT, &c32);
        if (p && pcm_is_ready(p)) {
            out->pcm = p;
            out->pcm_device = dev_id;
            out->hw_32bit = 1;
            out->standby = 0;
            ALOGI("Opened AoC playback card=%d dev=%d S32_LE %uHz %uch", card, dev_id, c32.rate, c32.channels);
            return 0;
        }
        if (p) pcm_close(p);
    }
    return -ENODEV;
}

static ssize_t out_write(struct audio_stream_out *s, const void *buffer, size_t bytes) {
    panther_stream_out_t *out = (panther_stream_out_t *)s;
    size_t frames = bytes / (out->cfg.channels * 2);
    if (frames == 0) return 0;

    pthread_mutex_lock(&out->lock);
    if (out->standby || !out->pcm) {
        if (out_start_pcm_locked(out) < 0) {
            pthread_mutex_unlock(&out->lock);
            /* Avoid spinning AudioFlinger when AoC card isn't ready yet */
            usleep((unsigned int)((frames * 1000000ULL) / out->cfg.rate));
            out->frames_written += frames;
            return (ssize_t)bytes;
        }
    }

    int rc;
    if (out->hw_32bit) {
        size_t samples = frames * out->cfg.channels;
        if (out->conv_buf_frames < frames) {
            int32_t *nb = (int32_t *)realloc(out->conv_buf, samples * sizeof(int32_t));
            if (!nb) {
                pthread_mutex_unlock(&out->lock);
                return -ENOMEM;
            }
            out->conv_buf = nb;
            out->conv_buf_frames = frames;
        }
        const int16_t *src = (const int16_t *)buffer;
        if (out->dev->master_mute) {
            memset(out->conv_buf, 0, samples * sizeof(int32_t));
        } else {
            for (size_t i = 0; i < samples; i++) {
                out->conv_buf[i] = ((int32_t)src[i]) << 16;
            }
        }
        rc = pcm_write(out->pcm, out->conv_buf, (unsigned int)(samples * sizeof(int32_t)));
    } else {
        rc = pcm_write(out->pcm, buffer, (unsigned int)bytes);
    }
    out->frames_written += frames;
    pthread_mutex_unlock(&out->lock);

    if (rc != 0) {
        out_standby(&out->stream.common);
        return (ssize_t)bytes;
    }
    return (ssize_t)bytes;
}

static int out_get_render_position(const struct audio_stream_out *s, uint32_t *dsp_frames) {
    const panther_stream_out_t *out = (const panther_stream_out_t *)s;
    if (!dsp_frames) return -EINVAL;
    *dsp_frames = (uint32_t)out->frames_written;
    return 0;
}

static int out_get_next_write_timestamp(const struct audio_stream_out *s, int64_t *ts) {
    (void)s; (void)ts; return -EINVAL;
}

static int out_get_presentation_position(const struct audio_stream_out *s, uint64_t *frames, struct timespec *ts) {
    const panther_stream_out_t *out = (const panther_stream_out_t *)s;
    if (!frames || !ts) return -EINVAL;
    *frames = out->frames_written;
    clock_gettime(CLOCK_MONOTONIC, ts);
    return 0;
}

/* ================= Input Stream ================= */

static uint32_t in_get_sample_rate(const struct audio_stream *s) {
    return ((const panther_stream_in_t *)s)->cfg.rate;
}
static int in_set_sample_rate(struct audio_stream *s, uint32_t rate) {
    (void)s; (void)rate; return -ENOSYS;
}
static size_t in_get_buffer_size(const struct audio_stream *s) {
    const panther_stream_in_t *in = (const panther_stream_in_t *)s;
    return in->cfg.period_size * in->cfg.channels * 2;
}
static audio_channel_mask_t in_get_channels(const struct audio_stream *s) {
    const panther_stream_in_t *in = (const panther_stream_in_t *)s;
    return (in->cfg.channels == 1) ? AUDIO_CHANNEL_IN_MONO : AUDIO_CHANNEL_IN_STEREO;
}
static audio_format_t in_get_format(const struct audio_stream *s) {
    (void)s; return AUDIO_FORMAT_PCM_16_BIT;
}
static int in_set_format(struct audio_stream *s, audio_format_t f) {
    (void)s; (void)f; return -ENOSYS;
}
static int in_standby(struct audio_stream *s) {
    panther_stream_in_t *in = (panther_stream_in_t *)s;
    pthread_mutex_lock(&in->lock);
    if (in->pcm) {
        pcm_close(in->pcm);
        in->pcm = NULL;
    }
    in->standby = 1;
    pthread_mutex_unlock(&in->lock);
    return 0;
}
static int in_dump(const struct audio_stream *s, int fd) { (void)s; (void)fd; return 0; }
static audio_devices_t in_get_device(const struct audio_stream *s) {
    return ((const panther_stream_in_t *)s)->devices;
}
static int in_set_device(struct audio_stream *s, audio_devices_t dev) {
    panther_stream_in_t *in = (panther_stream_in_t *)s;
    pthread_mutex_lock(&in->lock);
    in->devices = dev;
    pthread_mutex_lock(&in->dev->lock);
    in->dev->in_device = dev;
    apply_input_route_locked(in->dev, dev);
    pthread_mutex_unlock(&in->dev->lock);
    pthread_mutex_unlock(&in->lock);
    return 0;
}
static int in_set_parameters(struct audio_stream *s, const char *kv) {
    int val = 0;
    if (kv && parse_kv_int(kv, AUDIO_PARAMETER_STREAM_ROUTING, &val) && val != 0) {
        in_set_device(s, (audio_devices_t)val);
    }
    return 0;
}
static char *in_get_parameters(const struct audio_stream *s, const char *k) { (void)s; (void)k; return strdup(""); }
static int in_add_effect(const struct audio_stream *s, effect_handle_t e) { (void)s; (void)e; return 0; }
static int in_remove_effect(const struct audio_stream *s, effect_handle_t e) { (void)s; (void)e; return 0; }
static int in_set_gain(struct audio_stream_in *s, float g) { (void)s; (void)g; return 0; }

static ssize_t in_read(struct audio_stream_in *s, void *buffer, size_t bytes) {
    panther_stream_in_t *in = (panther_stream_in_t *)s;
    pthread_mutex_lock(&in->lock);
    if (in->standby || !in->pcm) {
        pthread_mutex_lock(&in->dev->lock);
        apply_input_route_locked(in->dev, in->devices);
        int card = in->dev->card;
        pthread_mutex_unlock(&in->dev->lock);

        int devs[2] = { in->pcm_device, AOC_PCM_VOIP_IN };
        for (int i = 0; i < 2 && !in->pcm; i++) {
            struct pcm *p = pcm_open((unsigned int)card, (unsigned int)devs[i], PCM_IN, &in->cfg);
            if (p && pcm_is_ready(p)) {
                in->pcm = p;
                in->standby = 0;
            } else if (p) {
                pcm_close(p);
            }
        }
        if (!in->pcm) {
            pthread_mutex_unlock(&in->lock);
            memset(buffer, 0, bytes);
            size_t frames = bytes / (in->cfg.channels * 2);
            usleep((unsigned int)((frames * 1000000ULL) / in->cfg.rate));
            return (ssize_t)bytes;
        }
    }
    int rc = pcm_read(in->pcm, buffer, (unsigned int)bytes);
    if (in->dev->mic_mute) memset(buffer, 0, bytes);
    pthread_mutex_unlock(&in->lock);
    if (rc != 0) {
        in_standby(&in->stream.common);
        memset(buffer, 0, bytes);
    }
    return (ssize_t)bytes;
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *s) {
    panther_stream_in_t *in = (panther_stream_in_t *)s;
    uint32_t v = in->frames_lost;
    in->frames_lost = 0;
    return v;
}

/* ================= Device Interface ================= */

static uint32_t adev_get_supported_devices(const struct audio_hw_device *dev) {
    (void)dev;
    return AUDIO_DEVICE_OUT_EARPIECE | AUDIO_DEVICE_OUT_SPEAKER |
           AUDIO_DEVICE_OUT_WIRED_HEADSET | AUDIO_DEVICE_OUT_WIRED_HEADPHONE |
           AUDIO_DEVICE_OUT_ALL_SCO | AUDIO_DEVICE_IN_BUILTIN_MIC |
           AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET;
}

static int adev_init_check(const struct audio_hw_device *dev) {
    panther_audio_dev_t *adev = (panther_audio_dev_t *)dev;
    pthread_mutex_lock(&adev->lock);
    ensure_mixer_locked(adev);
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume) {
    panther_audio_dev_t *adev = (panther_audio_dev_t *)dev;
    pthread_mutex_lock(&adev->lock);
    adev->voice_volume = volume;
    ensure_mixer_locked(adev);
    if (adev->mixer) {
        int val = (int)(volume * 100.0f);
        mix_set_int(adev->mixer, "Digital PCM Volume", 600 + val * 2);
    }
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static int adev_set_master_volume(struct audio_hw_device *dev, float volume) {
    panther_audio_dev_t *adev = (panther_audio_dev_t *)dev;
    adev->master_volume = volume;
    return -ENOSYS;
}

static int adev_get_master_volume(struct audio_hw_device *dev, float *volume) {
    panther_audio_dev_t *adev = (panther_audio_dev_t *)dev;
    if (volume) *volume = adev->master_volume;
    return -ENOSYS;
}

static int adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode) {
    panther_audio_dev_t *adev = (panther_audio_dev_t *)dev;
    pthread_mutex_lock(&adev->lock);
    adev->mode = mode;
    ensure_mixer_locked(adev);
    if (adev->mixer) {
        if (mode == AUDIO_MODE_IN_CALL) {
            mix_set_int(adev->mixer, "TDM_0_RX Mixer EP5", 1);
            mix_set_int(adev->mixer, "EP4 TX Mixer INTERNAL_MIC_TX", 1);
        } else {
            mix_set_int(adev->mixer, "TDM_0_RX Mixer EP5", 0);
        }
    }
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state) {
    panther_audio_dev_t *adev = (panther_audio_dev_t *)dev;
    pthread_mutex_lock(&adev->lock);
    adev->mic_mute = state;
    if (adev->mixer) {
        mix_set_int(adev->mixer, "Incall Mic Mute", state ? 1 : 0);
        mix_set_int(adev->mixer, "MIC Record Soft Gain (dB)", state ? -96 : 22);
    }
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state) {
    const panther_audio_dev_t *adev = (const panther_audio_dev_t *)dev;
    if (state) *state = adev->mic_mute;
    return 0;
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kv) {
    (void)dev; (void)kv; return 0;
}
static char *adev_get_parameters(const struct audio_hw_device *dev, const char *keys) {
    (void)dev; (void)keys; return strdup("");
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev, const struct audio_config *cfg) {
    (void)dev;
    int ch = cfg ? audio_channel_count_from_in_mask(cfg->channel_mask) : 1;
    if (ch <= 0) ch = 1;
    return (size_t)IN_PERIOD_FRAMES * (size_t)ch * 2;
}

static int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out,
                                   const char *address) {
    (void)handle; (void)address;
    panther_audio_dev_t *adev = (panther_audio_dev_t *)dev;
    panther_stream_out_t *out = (panther_stream_out_t *)calloc(1, sizeof(*out));
    if (!out) return -ENOMEM;

    out->dev = adev;
    pthread_mutex_init(&out->lock, NULL);
    out->standby = 1;
    out->devices = devices ? devices : AUDIO_DEVICE_OUT_SPEAKER;
    out->flags = flags;
    out->pcm_device = (flags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER) ? AOC_PCM_DEEP_BUFFER : AOC_PCM_LOW_LATENCY;
    out->cfg.channels = DEFAULT_CHANNELS;
    out->cfg.rate = DEFAULT_RATE;
    out->cfg.period_size = OUT_PERIOD_FRAMES;
    out->cfg.period_count = OUT_PERIOD_COUNT;
    out->cfg.format = PCM_FORMAT_S16_LE;
    out->vol_l = out->vol_r = 1.0f;

    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.get_device = out_get_device;
    out->stream.common.set_device = out_set_device;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_effect;
    out->stream.common.remove_audio_effect = out_remove_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
    out->stream.get_presentation_position = out_get_presentation_position;

    if (config) {
        config->sample_rate = out->cfg.rate;
        config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
        config->format = AUDIO_FORMAT_PCM_16_BIT;
    }

    pthread_mutex_lock(&adev->lock);
    adev->primary_out = out;
    pthread_mutex_unlock(&adev->lock);
    *stream_out = &out->stream;
    return 0;
}

static void adev_close_output_stream(struct audio_hw_device *dev, struct audio_stream_out *s) {
    panther_audio_dev_t *adev = (panther_audio_dev_t *)dev;
    panther_stream_out_t *out = (panther_stream_out_t *)s;
    out_standby(&s->common);
    pthread_mutex_lock(&adev->lock);
    if (adev->primary_out == out) adev->primary_out = NULL;
    pthread_mutex_unlock(&adev->lock);
    free(out->conv_buf);
    pthread_mutex_destroy(&out->lock);
    free(out);
}

static int adev_open_input_stream(struct audio_hw_device *dev,
                                  audio_io_handle_t handle,
                                  audio_devices_t devices,
                                  struct audio_config *config,
                                  struct audio_stream_in **stream_in,
                                  audio_input_flags_t flags,
                                  const char *address,
                                  audio_source_t source) {
    (void)handle; (void)flags; (void)address;
    panther_audio_dev_t *adev = (panther_audio_dev_t *)dev;
    panther_stream_in_t *in = (panther_stream_in_t *)calloc(1, sizeof(*in));
    if (!in) return -ENOMEM;

    in->dev = adev;
    pthread_mutex_init(&in->lock, NULL);
    in->standby = 1;
    in->devices = devices ? devices : AUDIO_DEVICE_IN_BUILTIN_MIC;
    in->source = source;
    in->pcm_device = AOC_PCM_RECORD;
    in->cfg.channels = (config && config->channel_mask == AUDIO_CHANNEL_IN_STEREO) ? 2 : 1;
    in->cfg.rate = DEFAULT_RATE;
    in->cfg.period_size = IN_PERIOD_FRAMES;
    in->cfg.period_count = IN_PERIOD_COUNT;
    in->cfg.format = PCM_FORMAT_S16_LE;

    in->stream.common.get_sample_rate = in_get_sample_rate;
    in->stream.common.set_sample_rate = in_set_sample_rate;
    in->stream.common.get_buffer_size = in_get_buffer_size;
    in->stream.common.get_channels = in_get_channels;
    in->stream.common.get_format = in_get_format;
    in->stream.common.set_format = in_set_format;
    in->stream.common.standby = in_standby;
    in->stream.common.dump = in_dump;
    in->stream.common.get_device = in_get_device;
    in->stream.common.set_device = in_set_device;
    in->stream.common.set_parameters = in_set_parameters;
    in->stream.common.get_parameters = in_get_parameters;
    in->stream.common.add_audio_effect = in_add_effect;
    in->stream.common.remove_audio_effect = in_remove_effect;
    in->stream.set_gain = in_set_gain;
    in->stream.read = in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;

    if (config) {
        config->sample_rate = in->cfg.rate;
        config->channel_mask = (in->cfg.channels == 1) ? AUDIO_CHANNEL_IN_MONO : AUDIO_CHANNEL_IN_STEREO;
        config->format = AUDIO_FORMAT_PCM_16_BIT;
    }
    pthread_mutex_lock(&adev->lock);
    adev->active_in = in;
    pthread_mutex_unlock(&adev->lock);
    *stream_in = &in->stream;
    return 0;
}

static void adev_close_input_stream(struct audio_hw_device *dev, struct audio_stream_in *s) {
    panther_audio_dev_t *adev = (panther_audio_dev_t *)dev;
    panther_stream_in_t *in = (panther_stream_in_t *)s;
    in_standby(&s->common);
    pthread_mutex_lock(&adev->lock);
    if (adev->active_in == in) adev->active_in = NULL;
    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_destroy(&in->lock);
    free(in);
}

static int adev_dump(const struct audio_hw_device *dev, int fd) { (void)dev; (void)fd; return 0; }
static int adev_set_master_mute(struct audio_hw_device *dev, bool mute) {
    panther_audio_dev_t *adev = (panther_audio_dev_t *)dev;
    adev->master_mute = mute;
    if (adev->mixer) mix_set_int(adev->mixer, "Incall Sink Mute", mute ? 1 : 0);
    return 0;
}
static int adev_get_master_mute(struct audio_hw_device *dev, bool *mute) {
    panther_audio_dev_t *adev = (panther_audio_dev_t *)dev;
    if (mute) *mute = adev->master_mute;
    return 0;
}

static int adev_close(struct hw_device_t *device) {
    panther_audio_dev_t *adev = (panther_audio_dev_t *)device;
    pthread_mutex_lock(&adev->lock);
    if (adev->mixer) mixer_close(adev->mixer);
    adev->mixer = NULL;
    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_destroy(&adev->lock);
    free(adev);
    return 0;
}

static int adev_open(const struct hw_module_t *module, const char *name, struct hw_device_t **device) {
    if (!name || strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0) return -EINVAL;
    panther_audio_dev_t *adev = (panther_audio_dev_t *)calloc(1, sizeof(*adev));
    if (!adev) return -ENOMEM;

    pthread_mutex_init(&adev->lock, NULL);
    adev->card = -1;
    adev->master_volume = 1.0f;
    adev->voice_volume = 1.0f;
    adev->out_device = AUDIO_DEVICE_OUT_SPEAKER;
    adev->in_device = AUDIO_DEVICE_IN_BUILTIN_MIC;

    adev->hw_dev.common.tag = HARDWARE_DEVICE_TAG;
    adev->hw_dev.common.version = AUDIO_DEVICE_API_VERSION_2_0;
    adev->hw_dev.common.module = (struct hw_module_t *)module;
    adev->hw_dev.common.close = adev_close;

    adev->hw_dev.get_supported_devices = adev_get_supported_devices;
    adev->hw_dev.init_check = adev_init_check;
    adev->hw_dev.set_voice_volume = adev_set_voice_volume;
    adev->hw_dev.set_master_volume = adev_set_master_volume;
    adev->hw_dev.get_master_volume = adev_get_master_volume;
    adev->hw_dev.set_mode = adev_set_mode;
    adev->hw_dev.set_mic_mute = adev_set_mic_mute;
    adev->hw_dev.get_mic_mute = adev_get_mic_mute;
    adev->hw_dev.set_parameters = adev_set_parameters;
    adev->hw_dev.get_parameters = adev_get_parameters;
    adev->hw_dev.get_input_buffer_size = adev_get_input_buffer_size;
    adev->hw_dev.open_output_stream = adev_open_output_stream;
    adev->hw_dev.close_output_stream = adev_close_output_stream;
    adev->hw_dev.open_input_stream = adev_open_input_stream;
    adev->hw_dev.close_input_stream = adev_close_input_stream;
    adev->hw_dev.dump = adev_dump;
    adev->hw_dev.set_master_mute = adev_set_master_mute;
    adev->hw_dev.get_master_mute = adev_get_master_mute;

    *device = &adev->hw_dev.common;
    ALOGI("Pixel 7 (panther) AoC Primary Audio HAL opened");
    return 0;
}

static struct hw_module_methods_t audio_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "Pixel 7 AoC Audio HAL",
        .author = "IRMP",
        .methods = &audio_module_methods,
    },
};
