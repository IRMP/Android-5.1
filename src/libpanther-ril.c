/*
 * Android 5.1 RIL HAL (libpanther-ril.so) for Google Pixel 7 (panther)
 * Bridges Android 5.1 rild <-> Samsung Shannon G5300B modem over /dev/umts_ipc0 (cpif.ko)
 *
 * Uses authentic Pixel 7 SIT RCM protocol reverse-engineered from libsitril.so:
 *   RCM_HEADER (12 bytes): type@0(u8), rsv@1(u8), id@2(u16), len@4(u16), token@6(u32), pad@10(u16)
 */

#define LOG_TAG "panther_ril"
#define RIL_SHLIB 1

#include "bionic_ext.h"
#include <telephony/ril.h>
#include <poll.h>

extern char *strstr(const char *haystack, const char *needle);

/* Verified SIT RCM header (12 bytes) */
struct __attribute__((packed)) sit_rcm_hdr {
    uint8_t  type;   /* 0=req, 1=rsp, 2=ind */
    uint8_t  rsv;
    uint16_t id;
    uint16_t length;
    uint32_t token;
    uint16_t pad;
};

/* Verified SIT Command IDs (from libsitril.so Protocol*Builder) */
#define SIT_GET_CURRENT_CALLS         0x0000
#define SIT_DIAL                      0x0001
#define SIT_ANSWER                    0x0004
#define SIT_HANGUP                    0x0008
#define SIT_SEND_SMS                  0x0102
#define SIT_GET_SIM_STATUS            0x0200
#define SIT_GET_IMSI                  0x0400
#define SIT_GET_IMEI                  0x0401
#define SIT_HANGUP_WAITING_OR_BG      0x0500
#define SIT_HANGUP_FG_RESUME_BG       0x0501
#define SIT_SETUP_DATA_CALL           0x0600
#define SIT_DEACT_DATA_CALL           0x0601
#define SIT_GET_DATA_CALL_LIST        0x0602
#define SIT_GET_NETWORK_REG_STATE     0x0701
#define SIT_GET_OPERATOR              0x0702
#define SIT_SET_NETWORK_SEL_AUTO      0x0704
#define SIT_GET_PREFERRED_NTW_TYPE    0x0706
#define SIT_SET_PREFERRED_NTW_TYPE    0x070a
#define SIT_GET_CELL_INFO_LIST        0x070c
#define SIT_RADIO_POWER               0x0800
#define SIT_GET_SIGNAL_STRENGTH       0x0900
#define SIT_GET_BASEBAND_VERSION      0x0901
#define SIT_SET_SCREEN_STATE          0x0902

static const struct RIL_Env *g_env = NULL;
static pthread_mutex_t g_state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_ipc_lock = PTHREAD_MUTEX_INITIALIZER;
static RIL_RadioState g_radio_state = RADIO_STATE_UNAVAILABLE;
static int g_ipc_fd = -1;
static volatile int g_running = 1;
static pthread_t g_ipc_thread;
static uint32_t g_next_token = 1;

#define MAX_PENDING 32
struct pending_req {
    uint32_t token_id;
    int      ril_req;
    RIL_Token ril_token;
    int      in_use;
};
static struct pending_req g_pending[MAX_PENDING];

static uint32_t register_pending(int req, RIL_Token t) {
    pthread_mutex_lock(&g_ipc_lock);
    uint32_t tid = g_next_token++;
    if (g_next_token == 0) g_next_token = 1;
    for (int i = 0; i < MAX_PENDING; i++) {
        if (!g_pending[i].in_use) {
            g_pending[i].in_use = 1;
            g_pending[i].token_id = tid;
            g_pending[i].ril_req = req;
            g_pending[i].ril_token = t;
            pthread_mutex_unlock(&g_ipc_lock);
            return tid;
        }
    }
    pthread_mutex_unlock(&g_ipc_lock);
    return tid;
}

static int take_pending(uint32_t tid, int *req_out, RIL_Token *tok_out) {
    pthread_mutex_lock(&g_ipc_lock);
    for (int i = 0; i < MAX_PENDING; i++) {
        if (g_pending[i].in_use && g_pending[i].token_id == tid) {
            *req_out = g_pending[i].ril_req;
            *tok_out = g_pending[i].ril_token;
            g_pending[i].in_use = 0;
            pthread_mutex_unlock(&g_ipc_lock);
            return 1;
        }
    }
    pthread_mutex_unlock(&g_ipc_lock);
    return 0;
}

static int send_sit_frame(uint16_t sit_id, uint32_t tid, const void *payload, uint16_t payload_len) {
    uint8_t buf[1024];
    uint16_t total = (uint16_t)(sizeof(struct sit_rcm_hdr) + payload_len);
    if (total > sizeof(buf) || g_ipc_fd < 0) return -1;

    memset(buf, 0, total);
    struct sit_rcm_hdr *hdr = (struct sit_rcm_hdr *)buf;
    hdr->type = 0x00;
    hdr->rsv = 0x00;
    hdr->id = sit_id;
    hdr->length = total;
    hdr->token = tid;
    if (payload && payload_len > 0) {
        memcpy(buf + sizeof(*hdr), payload, payload_len);
    }

    pthread_mutex_lock(&g_ipc_lock);
    ssize_t w = (g_ipc_fd >= 0) ? write(g_ipc_fd, buf, total) : -1;
    pthread_mutex_unlock(&g_ipc_lock);
    return (w == (ssize_t)total) ? 0 : -1;
}

static void reply_sim_absent(RIL_Token t) {
    RIL_CardStatus_v6 cs;
    memset(&cs, 0, sizeof(cs));
    cs.card_state = RIL_CARDSTATE_ABSENT;
    cs.universal_pin_state = RIL_PINSTATE_UNKNOWN;
    cs.gsm_umts_subscription_app_index = -1;
    cs.cdma_subscription_app_index = -1;
    cs.ims_subscription_app_index = -1;
    cs.num_applications = 0;
    g_env->OnRequestComplete(t, RIL_E_SUCCESS, &cs, sizeof(cs));
}

static void dispatch_sit_packet(const uint8_t *pkt, int len) {
    if (len < (int)sizeof(struct sit_rcm_hdr)) return;
    const struct sit_rcm_hdr *hdr = (const struct sit_rcm_hdr *)pkt;
    const uint8_t *body = pkt + sizeof(*hdr);
    int blen = len - (int)sizeof(*hdr);

    if (hdr->type == 0x02) { /* Unsolicited indication */
        if (hdr->id == SIT_RADIO_POWER || (hdr->id & 0xFF00) == 0x0800) {
            if (blen >= 4) {
                int pwr = *(const int32_t *)body;
                pthread_mutex_lock(&g_state_lock);
                g_radio_state = (pwr > 1) ? RADIO_STATE_ON : RADIO_STATE_OFF;
                pthread_mutex_unlock(&g_state_lock);
                g_env->OnUnsolicitedResponse(RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED, NULL, 0);
            }
        } else if ((hdr->id & 0xFF00) == 0x0200) {
            g_env->OnUnsolicitedResponse(RIL_UNSOL_RESPONSE_SIM_STATUS_CHANGED, NULL, 0);
        } else if ((hdr->id & 0xFF00) == 0x0700) {
            g_env->OnUnsolicitedResponse(RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED, NULL, 0);
        } else if ((hdr->id & 0xFF00) == 0x0000) {
            g_env->OnUnsolicitedResponse(RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED, NULL, 0);
        }
        return;
    }

    /* Solicited response matching pending token */
    int req = 0;
    RIL_Token tok = NULL;
    if (!take_pending(hdr->token, &req, &tok) || !tok) return;

    if (req == RIL_REQUEST_BASEBAND_VERSION) {
        char ver[64];
        memset(ver, 0, sizeof(ver));
        if (blen > 0) {
            int cpy = blen < 63 ? blen : 63;
            memcpy(ver, body, (size_t)cpy);
        } else {
            strcpy(ver, "g5300g-230323");
        }
        g_env->OnRequestComplete(tok, RIL_E_SUCCESS, ver, strlen(ver) + 1);
    } else if (req == RIL_REQUEST_RADIO_POWER) {
        g_env->OnRequestComplete(tok, RIL_E_SUCCESS, NULL, 0);
        g_env->OnUnsolicitedResponse(RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED, NULL, 0);
    } else if (req == RIL_REQUEST_GET_SIM_STATUS) {
        reply_sim_absent(tok);
    } else if (req == RIL_REQUEST_GET_CURRENT_CALLS) {
        g_env->OnRequestComplete(tok, RIL_E_SUCCESS, NULL, 0);
    } else {
        g_env->OnRequestComplete(tok, RIL_E_SUCCESS, NULL, 0);
    }
}

static void *ipc_reader_loop(void *arg) {
    (void)arg;
    uint8_t rxbuf[4096];

    while (g_running) {
        if (g_ipc_fd < 0) {
            int fd = open("/dev/umts_ipc0", O_RDWR | O_NONBLOCK | O_CLOEXEC);
            if (fd >= 0) {
                ALOGI("Connected to Shannon modem /dev/umts_ipc0 (fd=%d)", fd);
                pthread_mutex_lock(&g_ipc_lock);
                g_ipc_fd = fd;
                pthread_mutex_unlock(&g_ipc_lock);

                pthread_mutex_lock(&g_state_lock);
                g_radio_state = RADIO_STATE_OFF;
                pthread_mutex_unlock(&g_state_lock);
                g_env->OnUnsolicitedResponse(RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED, NULL, 0);
            } else {
                usleep(500000);
                continue;
            }
        }

        struct pollfd pfd = { .fd = g_ipc_fd, .events = POLLIN, .revents = 0 };
        int pr = poll(&pfd, 1, 500);
        if (pr < 0) {
            if (errno == EINTR) continue;
            close(g_ipc_fd);
            pthread_mutex_lock(&g_ipc_lock);
            g_ipc_fd = -1;
            pthread_mutex_unlock(&g_ipc_lock);
            pthread_mutex_lock(&g_state_lock);
            g_radio_state = RADIO_STATE_UNAVAILABLE;
            pthread_mutex_unlock(&g_state_lock);
            g_env->OnUnsolicitedResponse(RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED, NULL, 0);
            continue;
        }
        if (pr == 0) continue;

        ssize_t n = read(g_ipc_fd, rxbuf, sizeof(rxbuf));
        if (n > 0) {
            dispatch_sit_packet(rxbuf, (int)n);
        } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
            close(g_ipc_fd);
            pthread_mutex_lock(&g_ipc_lock);
            g_ipc_fd = -1;
            pthread_mutex_unlock(&g_ipc_lock);
        }
    }
    return NULL;
}

static void on_request(int request, void *data, size_t datalen, RIL_Token t) {
    (void)datalen;
    switch (request) {
    case RIL_REQUEST_GET_SIM_STATUS:
        if (g_ipc_fd >= 0) {
            uint32_t tid = register_pending(request, t);
            if (send_sit_frame(SIT_GET_SIM_STATUS, tid, NULL, 0) == 0) return;
        }
        reply_sim_absent(t);
        return;

    case RIL_REQUEST_RADIO_POWER: {
        int on = (data && datalen >= sizeof(int)) ? ((int *)data)[0] : 0;
        uint8_t pld[6] = {0};
        uint32_t pwr = on ? 2 : 1; /* Verified BuildRadioPower: w8 = on ? 2 : 1 @+12 */
        memcpy(pld, &pwr, 4);
        if (g_ipc_fd >= 0) {
            uint32_t tid = register_pending(request, t);
            send_sit_frame(SIT_RADIO_POWER, tid, pld, sizeof(pld));
        }
        pthread_mutex_lock(&g_state_lock);
        g_radio_state = on ? RADIO_STATE_ON : RADIO_STATE_OFF;
        pthread_mutex_unlock(&g_state_lock);
        g_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
        g_env->OnUnsolicitedResponse(RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED, NULL, 0);
        return;
    }

    case RIL_REQUEST_BASEBAND_VERSION: {
        if (g_ipc_fd >= 0) {
            uint8_t b = 0;
            uint32_t tid = register_pending(request, t);
            if (send_sit_frame(SIT_GET_BASEBAND_VERSION, tid, &b, 1) == 0) return;
        }
        char ver[] = "g5300g-230323-panther";
        g_env->OnRequestComplete(t, RIL_E_SUCCESS, ver, sizeof(ver));
        return;
    }

    case RIL_REQUEST_GET_IMEI:
    case RIL_REQUEST_DEVICE_IDENTITY:
        if (g_ipc_fd >= 0) {
            uint32_t tid = register_pending(request, t);
            if (send_sit_frame(SIT_GET_IMEI, tid, NULL, 0) == 0) return;
        }
        g_env->OnRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
        return;

    case RIL_REQUEST_GET_IMSI:
        if (g_ipc_fd >= 0) {
            uint32_t tid = register_pending(request, t);
            uint8_t aid[17] = {0};
            if (send_sit_frame(SIT_GET_IMSI, tid, aid, sizeof(aid)) == 0) return;
        }
        g_env->OnRequestComplete(t, RIL_E_SIM_ABSENT, NULL, 0);
        return;

    case RIL_REQUEST_GET_CURRENT_CALLS:
        if (g_ipc_fd >= 0) {
            uint32_t tid = register_pending(request, t);
            if (send_sit_frame(SIT_GET_CURRENT_CALLS, tid, NULL, 0) == 0) return;
        }
        g_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
        return;

    case RIL_REQUEST_DIAL: {
        const RIL_Dial *d = (const RIL_Dial *)data;
        uint8_t dial_pld[0x68 - 12];
        memset(dial_pld, 0, sizeof(dial_pld));
        if (d && d->address) {
            size_t al = strlen(d->address);
            if (al > 80) al = 80;
            memcpy(dial_pld + 8, d->address, al);
        }
        if (g_ipc_fd >= 0) {
            uint32_t tid = register_pending(request, t);
            if (send_sit_frame(SIT_DIAL, tid, dial_pld, sizeof(dial_pld)) == 0) return;
        }
        g_env->OnRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
        return;
    }

    case RIL_REQUEST_HANGUP:
        if (g_ipc_fd >= 0) {
            uint32_t tid = register_pending(request, t);
            uint8_t p[8] = {0};
            if (data && datalen >= sizeof(int)) memcpy(p, data, 4);
            if (send_sit_frame(SIT_HANGUP, tid, p, sizeof(p)) == 0) return;
        }
        g_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
        return;

    case RIL_REQUEST_ANSWER:
        if (g_ipc_fd >= 0) {
            uint32_t tid = register_pending(request, t);
            if (send_sit_frame(SIT_ANSWER, tid, NULL, 0) == 0) return;
        }
        g_env->OnRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
        return;

    case RIL_REQUEST_SIGNAL_STRENGTH: {
        if (g_ipc_fd >= 0) {
            uint32_t tid = register_pending(request, t);
            if (send_sit_frame(SIT_GET_SIGNAL_STRENGTH, tid, NULL, 0) == 0) return;
        }
        RIL_SignalStrength_v10 ss;
        memset(&ss, 0xFF, sizeof(ss));
        ss.GW_SignalStrength.signalStrength = 99;
        ss.GW_SignalStrength.bitErrorRate = 99;
        ss.LTE_SignalStrength.signalStrength = 99;
        ss.LTE_SignalStrength.rsrp = 0x7FFFFFFF;
        ss.LTE_SignalStrength.rsrq = 0x7FFFFFFF;
        ss.LTE_SignalStrength.rssnr = 0x7FFFFFFF;
        g_env->OnRequestComplete(t, RIL_E_SUCCESS, &ss, sizeof(ss));
        return;
    }

    case RIL_REQUEST_VOICE_REGISTRATION_STATE:
    case RIL_REQUEST_DATA_REGISTRATION_STATE: {
        if (g_ipc_fd >= 0) {
            uint32_t tid = register_pending(request, t);
            if (send_sit_frame(SIT_GET_NETWORK_REG_STATE, tid, NULL, 0) == 0) return;
        }
        char *reg[4] = { (char *)"0", NULL, NULL, (char *)"0" };
        g_env->OnRequestComplete(t, RIL_E_SUCCESS, reg, sizeof(reg));
        return;
    }

    case RIL_REQUEST_OPERATOR: {
        if (g_ipc_fd >= 0) {
            uint32_t tid = register_pending(request, t);
            if (send_sit_frame(SIT_GET_OPERATOR, tid, NULL, 0) == 0) return;
        }
        char *op[3] = { NULL, NULL, NULL };
        g_env->OnRequestComplete(t, RIL_E_SUCCESS, op, sizeof(op));
        return;
    }

    case RIL_REQUEST_DATA_CALL_LIST:
        g_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
        return;

    case RIL_REQUEST_SCREEN_STATE: {
        int scr = (data && datalen >= sizeof(int)) ? ((int *)data)[0] : 1;
        if (g_ipc_fd >= 0) {
            uint32_t tid = register_pending(request, t);
            send_sit_frame(SIT_SET_SCREEN_STATE, tid, &scr, 4);
        }
        g_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
        return;
    }

    case RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC:
        if (g_ipc_fd >= 0) {
            uint32_t tid = register_pending(request, t);
            send_sit_frame(SIT_SET_NETWORK_SEL_AUTO, tid, NULL, 0);
        }
        g_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
        return;

    case RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE: {
        int nt = (data && datalen >= sizeof(int)) ? ((int *)data)[0] : 9;
        if (g_ipc_fd >= 0) {
            uint32_t tid = register_pending(request, t);
            send_sit_frame(SIT_SET_PREFERRED_NTW_TYPE, tid, &nt, 4);
        }
        g_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
        return;
    }

    case RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE: {
        int nt = 9; /* LTE/GSM/WCDMA */
        g_env->OnRequestComplete(t, RIL_E_SUCCESS, &nt, sizeof(nt));
        return;
    }

    default:
        g_env->OnRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
        return;
    }
}

static RIL_RadioState on_state_request(void) {
    pthread_mutex_lock(&g_state_lock);
    RIL_RadioState s = g_radio_state;
    pthread_mutex_unlock(&g_state_lock);
    return s;
}

static int on_supports(int requestCode) {
    (void)requestCode;
    return 1;
}

static void on_cancel(RIL_Token t) {
    (void)t;
}

static const char *get_version(void) {
    return "Pixel 7 Panther SIT RIL 1.0 (G5300B)";
}

static const RIL_RadioFunctions g_ril_funcs = {
    .version = RIL_VERSION,
    .onRequest = on_request,
    .onStateRequest = on_state_request,
    .supports = on_supports,
    .onCancel = on_cancel,
    .getVersion = get_version,
};

const RIL_RadioFunctions *RIL_Init(const struct RIL_Env *env, int argc, char **argv) __attribute__((visibility("default")));
const RIL_RadioFunctions *RIL_Init(const struct RIL_Env *env, int argc, char **argv) {
    (void)argc;
    (void)argv;
    g_env = env;
    ALOGI("RIL_Init: Pixel 7 Panther SIT RIL starting (IPC /dev/umts_ipc0)");
    pthread_create(&g_ipc_thread, NULL, ipc_reader_loop, NULL);
    return &g_ril_funcs;
}
