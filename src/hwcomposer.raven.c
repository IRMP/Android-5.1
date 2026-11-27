#define LOG_TAG "hwcomposer.raven"
#include "bionic_compat.h"
#include <arm_neon.h>
#include <hardware/gralloc.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <system/graphics.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

#define RAVEN_GRALLOC_MAGIC 0x5241564E

typedef struct raven_handle {
    native_handle_t base_nh;
    int fd;
    int magic;
    int flags;
    int size;
    int offset;
    int width;
    int height;
    int stride;
    int format;
    int pid;
    uint32_t base_lo;
    uint32_t base_hi;
} raven_handle_t;

typedef struct drm_buf {
    uint32_t fb_id;
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
    uint32_t *map;
} drm_buf_t;

typedef struct hwc_raven_ctx {
    hwc_composer_device_1_t device;
    const hwc_procs_t *procs;
    const gralloc_module_t *gralloc;

    int drm_fd;
    uint32_t conn_id;
    uint32_t crtc_id;
    drm_buf_t bufs[2];
    int front_buf;
    int use_page_flip;
    int crtc_set;

    struct drm_mode_modeinfo mode;
    uint32_t width;
    uint32_t height;
    uint32_t vrefresh;
    int64_t vsync_period_ns;
    int32_t xdpi;
    int32_t ydpi;

    volatile int vsync_enabled;
    volatile int running;
    pthread_t vsync_thread;
    pthread_mutex_t lock;
} hwc_raven_ctx_t;

static int xioctl(int fd, unsigned long req, void *arg) {
    int r;
    do { r = ioctl(fd, (int)req, arg); } while (r == -1 && errno == EINTR);
    return r;
}

static int alloc_dumb_buf(int drm_fd, uint32_t w, uint32_t h, drm_buf_t *b) {
    struct drm_mode_create_dumb dumb;
    memset(&dumb, 0, sizeof(dumb));
    dumb.width = w;
    dumb.height = h;
    dumb.bpp = 32;
    if (xioctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &dumb) < 0) return -1;

    struct drm_mode_fb_cmd fb_cmd;
    memset(&fb_cmd, 0, sizeof(fb_cmd));
    fb_cmd.width = w;
    fb_cmd.height = h;
    fb_cmd.pitch = dumb.pitch;
    fb_cmd.bpp = 32;
    fb_cmd.depth = 24;
    fb_cmd.handle = dumb.handle;
    if (xioctl(drm_fd, DRM_IOCTL_MODE_ADDFB, &fb_cmd) < 0) return -1;

    struct drm_mode_map_dumb map_req;
    memset(&map_req, 0, sizeof(map_req));
    map_req.handle = dumb.handle;
    if (xioctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_req) < 0) return -1;

    void *m = mmap(NULL, (size_t)dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, (off_t)map_req.offset);
    if (m == MAP_FAILED) return -1;
    memset(m, 0, (size_t)dumb.size);

    b->fb_id = fb_cmd.fb_id;
    b->handle = dumb.handle;
    b->pitch = dumb.pitch;
    b->size = dumb.size;
    b->map = (uint32_t *)m;
    return 0;
}

static int drm_setup(hwc_raven_ctx_t *ctx) {
    ctx->drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (ctx->drm_fd < 0) {
        ALOGE("open /dev/dri/card0 failed: %d", errno);
        return -errno;
    }
    xioctl(ctx->drm_fd, DRM_IOCTL_SET_MASTER, NULL);

    struct drm_mode_card_res res;
    memset(&res, 0, sizeof(res));
    if (xioctl(ctx->drm_fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) return -1;

    uint32_t *crtcs = calloc(res.count_crtcs ? res.count_crtcs : 1, sizeof(uint32_t));
    uint32_t *conns = calloc(res.count_connectors ? res.count_connectors : 1, sizeof(uint32_t));
    uint32_t *encs  = calloc(res.count_encoders ? res.count_encoders : 1, sizeof(uint32_t));
    res.crtc_id_ptr = (uint64_t)(uintptr_t)crtcs;
    res.connector_id_ptr = (uint64_t)(uintptr_t)conns;
    res.encoder_id_ptr = (uint64_t)(uintptr_t)encs;
    if (xioctl(ctx->drm_fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
        free(crtcs); free(conns); free(encs);
        return -1;
    }

    for (uint32_t i = 0; i < res.count_connectors; i++) {
        struct drm_mode_get_connector conn;
        memset(&conn, 0, sizeof(conn));
        conn.connector_id = conns[i];
        if (xioctl(ctx->drm_fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0 || conn.count_modes == 0)
            continue;

        struct drm_mode_modeinfo *modes = calloc(conn.count_modes, sizeof(*modes));
        uint32_t *c_encs = calloc(conn.count_encoders ? conn.count_encoders : 1, sizeof(uint32_t));
        uint32_t *props = calloc(conn.count_props ? conn.count_props : 1, sizeof(uint32_t));
        uint64_t *propv = calloc(conn.count_props ? conn.count_props : 1, sizeof(uint64_t));
        conn.modes_ptr = (uint64_t)(uintptr_t)modes;
        conn.encoders_ptr = (uint64_t)(uintptr_t)c_encs;
        conn.props_ptr = (uint64_t)(uintptr_t)props;
        conn.prop_values_ptr = (uint64_t)(uintptr_t)propv;

        if (xioctl(ctx->drm_fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) == 0 && conn.connection == 1 && conn.count_modes > 0) {
            ctx->conn_id = conn.connector_id;
            /* Pick highest resolution and highest vrefresh (120Hz LTPO on Pixel 6 Pro) */
            ctx->mode = modes[0];
            uint64_t best_score = 0;
            for (uint32_t m = 0; m < conn.count_modes; m++) {
                uint64_t area = (uint64_t)modes[m].hdisplay * (uint64_t)modes[m].vdisplay;
                uint64_t pref = (modes[m].type & DRM_MODE_TYPE_PREFERRED) ? 1ULL : 0ULL;
                uint64_t score = (area << 20) + ((uint64_t)modes[m].vrefresh << 4) + pref;
                if (score > best_score) {
                    best_score = score;
                    ctx->mode = modes[m];
                }
            }
            if (conn.encoder_id) {
                struct drm_mode_get_encoder enc;
                memset(&enc, 0, sizeof(enc));
                enc.encoder_id = conn.encoder_id;
                if (xioctl(ctx->drm_fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0 && enc.crtc_id)
                    ctx->crtc_id = enc.crtc_id;
            }
            if (!ctx->crtc_id && conn.count_encoders > 0) {
                struct drm_mode_get_encoder enc;
                memset(&enc, 0, sizeof(enc));
                enc.encoder_id = c_encs[0];
                if (xioctl(ctx->drm_fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0) {
                    for (uint32_t c = 0; c < res.count_crtcs; c++) {
                        if (enc.possible_crtcs & (1u << c)) { ctx->crtc_id = crtcs[c]; break; }
                    }
                }
            }
            if (!ctx->crtc_id && res.count_crtcs > 0) ctx->crtc_id = crtcs[0];
            ctx->xdpi = (conn.mm_width > 0) ? (int32_t)(((int64_t)ctx->mode.hdisplay * 25400) / conn.mm_width) : 512000;
            ctx->ydpi = (conn.mm_height > 0) ? (int32_t)(((int64_t)ctx->mode.vdisplay * 25400) / conn.mm_height) : 512000;
        }
        free(modes); free(c_encs); free(props); free(propv);
        if (ctx->conn_id) break;
    }
    free(crtcs); free(conns); free(encs);

    if (!ctx->conn_id || !ctx->crtc_id) return -ENODEV;

    ctx->width = ctx->mode.hdisplay;
    ctx->height = ctx->mode.vdisplay;
    ctx->vrefresh = ctx->mode.vrefresh ? ctx->mode.vrefresh : 60;
    ctx->vsync_period_ns = 1000000000LL / (int64_t)ctx->vrefresh;

    if (alloc_dumb_buf(ctx->drm_fd, ctx->width, ctx->height, &ctx->bufs[0]) < 0) return -1;
    if (alloc_dumb_buf(ctx->drm_fd, ctx->width, ctx->height, &ctx->bufs[1]) < 0) {
        /* Fallback to single buffer if memory constrained */
        ctx->bufs[1] = ctx->bufs[0];
    }

    struct drm_mode_crtc crtc;
    memset(&crtc, 0, sizeof(crtc));
    crtc.set_connectors_ptr = (uint64_t)(uintptr_t)&ctx->conn_id;
    crtc.count_connectors = 1;
    crtc.crtc_id = ctx->crtc_id;
    crtc.fb_id = ctx->bufs[0].fb_id;
    crtc.mode_valid = 1;
    crtc.mode = ctx->mode;
    if (xioctl(ctx->drm_fd, DRM_IOCTL_MODE_SETCRTC, &crtc) == 0) {
        ctx->crtc_set = 1;
    }
    ctx->use_page_flip = (ctx->bufs[0].fb_id != ctx->bufs[1].fb_id);
    ALOGI("Raven DRM ready: %ux%u@%uHz pitch=%u fb0=%u fb1=%u",
          ctx->width, ctx->height, ctx->vrefresh, ctx->bufs[0].pitch,
          ctx->bufs[0].fb_id, ctx->bufs[1].fb_id);
    return 0;
}

static inline void neon_swizzle_rgba_to_xrgb(uint32_t *dst, const uint32_t *src, uint32_t count) {
    uint32_t x = 0;
    const uint8x16_t alpha_ff = vdupq_n_u8(0xFF);
    for (; x + 16 <= count; x += 16) {
        uint8x16x4_t px = vld4q_u8((const uint8_t *)(src + x));
        uint8x16x4_t out;
        out.val[0] = px.val[2];
        out.val[1] = px.val[1];
        out.val[2] = px.val[0];
        out.val[3] = alpha_ff;
        vst4q_u8((uint8_t *)(dst + x), out);
    }
    for (; x < count; x++) {
        uint32_t p = src[x];
        dst[x] = 0xFF000000u | (p & 0x0000FF00u) | ((p & 0x00FF0000u) >> 16) | ((p & 0x000000FFu) << 16);
    }
}

static void *vsync_loop(void *arg) {
    hwc_raven_ctx_t *ctx = (hwc_raven_ctx_t *)arg;
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    while (ctx->running) {
        next.tv_nsec += (long)ctx->vsync_period_ns;
        while (next.tv_nsec >= 1000000000L) {
            next.tv_sec += 1;
            next.tv_nsec -= 1000000000L;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        if (ctx->vsync_enabled && ctx->procs && ctx->procs->vsync) {
            int64_t ts = (int64_t)next.tv_sec * 1000000000LL + (int64_t)next.tv_nsec;
            ctx->procs->vsync(ctx->procs, 0, ts);
        }
    }
    return NULL;
}

static int hwc_prepare(struct hwc_composer_device_1 *dev, size_t numDisplays, hwc_display_contents_1_t **displays) {
    (void)dev;
    if (!displays || numDisplays == 0 || !displays[0]) return 0;
    hwc_display_contents_1_t *d = displays[0];
    for (size_t i = 0; i < d->numHwLayers; i++) {
        if (d->hwLayers[i].compositionType != HWC_FRAMEBUFFER_TARGET) {
            d->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
        }
    }
    return 0;
}

static int hwc_set(struct hwc_composer_device_1 *dev, size_t numDisplays, hwc_display_contents_1_t **displays) {
    hwc_raven_ctx_t *ctx = (hwc_raven_ctx_t *)dev;
    if (!displays || numDisplays == 0 || !displays[0]) return 0;
    hwc_display_contents_1_t *d = displays[0];

    hwc_layer_1_t *fb_layer = NULL;
    for (size_t i = 0; i < d->numHwLayers; i++) {
        if (d->hwLayers[i].compositionType == HWC_FRAMEBUFFER_TARGET) {
            fb_layer = &d->hwLayers[i];
        } else {
            if (d->hwLayers[i].acquireFenceFd >= 0) {
                close(d->hwLayers[i].acquireFenceFd);
                d->hwLayers[i].acquireFenceFd = -1;
            }
            d->hwLayers[i].releaseFenceFd = -1;
        }
    }

    if (fb_layer) {
        if (fb_layer->acquireFenceFd >= 0) {
            struct pollfd pfd = { .fd = fb_layer->acquireFenceFd, .events = POLLIN, .revents = 0 };
            poll(&pfd, 1, 1000);
            close(fb_layer->acquireFenceFd);
            fb_layer->acquireFenceFd = -1;
        }
        fb_layer->releaseFenceFd = -1;

        if (fb_layer->handle && ctx->gralloc && ctx->bufs[0].map) {
            void *vaddr = NULL;
            if (ctx->gralloc->lock(ctx->gralloc, fb_layer->handle, GRALLOC_USAGE_SW_READ_OFTEN,
                                   0, 0, (int)ctx->width, (int)ctx->height, &vaddr) == 0 && vaddr) {
                const raven_handle_t *rh = (const raven_handle_t *)fb_layer->handle;
                uint32_t src_stride = ctx->width;
                int fmt = HAL_PIXEL_FORMAT_RGBA_8888;
                if (rh->base_nh.version == sizeof(native_handle_t) && rh->magic == RAVEN_GRALLOC_MAGIC) {
                    src_stride = (uint32_t)rh->stride;
                    fmt = rh->format;
                }

                pthread_mutex_lock(&ctx->lock);
                int back = ctx->use_page_flip ? (ctx->front_buf ^ 1) : 0;
                drm_buf_t *db = &ctx->bufs[back];
                uint32_t dst_pitch_px = db->pitch / 4;
                const uint32_t *src = (const uint32_t *)vaddr;

                if (fmt == HAL_PIXEL_FORMAT_BGRA_8888) {
                    for (uint32_t y = 0; y < ctx->height; y++) {
                        memcpy(db->map + y * dst_pitch_px, src + y * src_stride, ctx->width * 4);
                    }
                } else {
                    for (uint32_t y = 0; y < ctx->height; y++) {
                        neon_swizzle_rgba_to_xrgb(db->map + y * dst_pitch_px, src + y * src_stride, ctx->width);
                    }
                }
                ctx->gralloc->unlock(ctx->gralloc, fb_layer->handle);

                if (!ctx->crtc_set) {
                    struct drm_mode_crtc crtc;
                    memset(&crtc, 0, sizeof(crtc));
                    crtc.set_connectors_ptr = (uint64_t)(uintptr_t)&ctx->conn_id;
                    crtc.count_connectors = 1;
                    crtc.crtc_id = ctx->crtc_id;
                    crtc.fb_id = db->fb_id;
                    crtc.mode_valid = 1;
                    crtc.mode = ctx->mode;
                    if (xioctl(ctx->drm_fd, DRM_IOCTL_MODE_SETCRTC, &crtc) == 0) ctx->crtc_set = 1;
                    ctx->front_buf = back;
                } else if (ctx->use_page_flip) {
                    struct drm_mode_crtc_page_flip pf;
                    memset(&pf, 0, sizeof(pf));
                    pf.crtc_id = ctx->crtc_id;
                    pf.fb_id = db->fb_id;
                    if (xioctl(ctx->drm_fd, DRM_IOCTL_MODE_PAGE_FLIP, &pf) == 0) {
                        ctx->front_buf = back;
                    } else {
                        /* Fallback to DIRTYFB / SETCRTC if driver rejects async page_flip */
                        ctx->use_page_flip = 0;
                        struct drm_mode_crtc crtc;
                        memset(&crtc, 0, sizeof(crtc));
                        crtc.set_connectors_ptr = (uint64_t)(uintptr_t)&ctx->conn_id;
                        crtc.count_connectors = 1;
                        crtc.crtc_id = ctx->crtc_id;
                        crtc.fb_id = db->fb_id;
                        crtc.mode_valid = 1;
                        crtc.mode = ctx->mode;
                        xioctl(ctx->drm_fd, DRM_IOCTL_MODE_SETCRTC, &crtc);
                    }
                } else {
                    struct drm_mode_fb_dirty_cmd dirty;
                    memset(&dirty, 0, sizeof(dirty));
                    dirty.fb_id = db->fb_id;
                    xioctl(ctx->drm_fd, DRM_IOCTL_MODE_DIRTYFB, &dirty);
                }
                pthread_mutex_unlock(&ctx->lock);
            }
        }
    }
    d->retireFenceFd = -1;
    return 0;
}

static int hwc_event_control(struct hwc_composer_device_1 *dev, int disp, int event, int enabled) {
    hwc_raven_ctx_t *ctx = (hwc_raven_ctx_t *)dev;
    if (disp != 0 || event != HWC_EVENT_VSYNC) return -EINVAL;
    ctx->vsync_enabled = enabled ? 1 : 0;
    return 0;
}

static int hwc_blank(struct hwc_composer_device_1 *dev, int disp, int blank) {
    (void)dev; (void)disp; (void)blank;
    return 0;
}

static int hwc_query(struct hwc_composer_device_1 *dev, int what, int *value) {
    hwc_raven_ctx_t *ctx = (hwc_raven_ctx_t *)dev;
    if (!value) return -EINVAL;
    switch (what) {
        case HWC_BACKGROUND_LAYER_SUPPORTED: *value = 0; return 0;
        case HWC_VSYNC_PERIOD: *value = (int)ctx->vsync_period_ns; return 0;
        case HWC_DISPLAY_TYPES_SUPPORTED: *value = HWC_DISPLAY_PRIMARY_BIT; return 0;
        default: return -EINVAL;
    }
}

static void hwc_register_procs(struct hwc_composer_device_1 *dev, hwc_procs_t const *procs) {
    hwc_raven_ctx_t *ctx = (hwc_raven_ctx_t *)dev;
    ctx->procs = procs;
}

static void hwc_dump(struct hwc_composer_device_1 *dev, char *buff, int buff_len) {
    hwc_raven_ctx_t *ctx = (hwc_raven_ctx_t *)dev;
    if (buff && buff_len > 0)
        snprintf(buff, (size_t)buff_len, "hwcomposer.raven NEON DRM/KMS %ux%u@%uHz\n",
                 ctx->width, ctx->height, ctx->vrefresh);
}

static int hwc_get_display_configs(struct hwc_composer_device_1 *dev, int disp, uint32_t *configs, size_t *numConfigs) {
    (void)dev;
    if (disp != 0 || !numConfigs) return -EINVAL;
    if (*numConfigs > 0 && configs) configs[0] = 0;
    *numConfigs = 1;
    return 0;
}

static int hwc_get_display_attributes(struct hwc_composer_device_1 *dev, int disp, uint32_t config,
                                      const uint32_t *attributes, int32_t *values) {
    hwc_raven_ctx_t *ctx = (hwc_raven_ctx_t *)dev;
    if (disp != 0 || config != 0 || !attributes || !values) return -EINVAL;
    for (int i = 0; attributes[i] != HWC_DISPLAY_NO_ATTRIBUTE; i++) {
        switch (attributes[i]) {
            case HWC_DISPLAY_VSYNC_PERIOD: values[i] = (int32_t)ctx->vsync_period_ns; break;
            case HWC_DISPLAY_WIDTH:  values[i] = (int32_t)ctx->width; break;
            case HWC_DISPLAY_HEIGHT: values[i] = (int32_t)ctx->height; break;
            case HWC_DISPLAY_DPI_X:  values[i] = ctx->xdpi; break;
            case HWC_DISPLAY_DPI_Y:  values[i] = ctx->ydpi; break;
            default: values[i] = 0; break;
        }
    }
    return 0;
}

static int hwc_close(struct hw_device_t *dev) {
    hwc_raven_ctx_t *ctx = (hwc_raven_ctx_t *)dev;
    ctx->running = 0;
    pthread_join(ctx->vsync_thread, NULL);
    if (ctx->bufs[0].map) munmap(ctx->bufs[0].map, (size_t)ctx->bufs[0].size);
    if (ctx->bufs[1].map && ctx->bufs[1].map != ctx->bufs[0].map) munmap(ctx->bufs[1].map, (size_t)ctx->bufs[1].size);
    if (ctx->drm_fd >= 0) close(ctx->drm_fd);
    free(ctx);
    return 0;
}

static int hwc_device_open(const struct hw_module_t *module, const char *name, struct hw_device_t **device) {
    if (strcmp(name, HWC_HARDWARE_COMPOSER) != 0) return -EINVAL;

    hwc_raven_ctx_t *ctx = (hwc_raven_ctx_t *)calloc(1, sizeof(hwc_raven_ctx_t));
    if (!ctx) return -ENOMEM;
    ctx->drm_fd = -1;

    const struct hw_module_t *gmod = NULL;
    if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &gmod) != 0 || !gmod) {
        free(ctx);
        return -EINVAL;
    }
    ctx->gralloc = (const gralloc_module_t *)gmod;

    if (drm_setup(ctx) < 0) {
        free(ctx);
        return -ENODEV;
    }

    ctx->device.common.tag = HARDWARE_DEVICE_TAG;
    ctx->device.common.version = HWC_DEVICE_API_VERSION_1_1;
    ctx->device.common.module = (struct hw_module_t *)module;
    ctx->device.common.close = hwc_close;
    ctx->device.prepare = hwc_prepare;
    ctx->device.set = hwc_set;
    ctx->device.eventControl = hwc_event_control;
    ctx->device.blank = hwc_blank;
    ctx->device.query = hwc_query;
    ctx->device.registerProcs = hwc_register_procs;
    ctx->device.dump = hwc_dump;
    ctx->device.getDisplayConfigs = hwc_get_display_configs;
    ctx->device.getDisplayAttributes = hwc_get_display_attributes;

    ctx->running = 1;
    pthread_create(&ctx->vsync_thread, NULL, vsync_loop, ctx);

    *device = &ctx->device.common;
    return 0;
}

static struct hw_module_methods_t hwc_module_methods = {
    .open = hwc_device_open,
};

hwc_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = HWC_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = HWC_HARDWARE_MODULE_ID,
        .name = "Pixel 6 Pro NEON DRM/KMS HWComposer",
        .author = "IRMP",
        .methods = &hwc_module_methods,
    },
};
