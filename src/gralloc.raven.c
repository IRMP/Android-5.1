#define LOG_TAG "gralloc.raven"
#include "bionic_compat.h"
#include <hardware/gralloc.h>
#include <hardware/hardware.h>
#include <cutils/native_handle.h>
#include <system/graphics.h>

#define RAVEN_GRALLOC_MAGIC 0x5241564E /* "RAVN" */

typedef struct raven_handle {
    native_handle_t base_nh; /* version, numFds=1, numInts=11 */
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

#define RAVEN_NUM_FDS  1
#define RAVEN_NUM_INTS 11

static pthread_mutex_t g_gralloc_lock = PTHREAD_MUTEX_INITIALIZER;

static inline void *hnd_get_base(const raven_handle_t *h) {
    uint64_t v = ((uint64_t)h->base_hi << 32) | (uint64_t)h->base_lo;
    return (void *)(uintptr_t)v;
}

static inline void hnd_set_base(raven_handle_t *h, void *p) {
    uint64_t v = (uint64_t)(uintptr_t)p;
    h->base_lo = (uint32_t)(v & 0xffffffffu);
    h->base_hi = (uint32_t)(v >> 32);
}

static int validate_handle(buffer_handle_t handle) {
    if (!handle) return -EINVAL;
    const raven_handle_t *h = (const raven_handle_t *)handle;
    if (h->base_nh.version != sizeof(native_handle_t) ||
        h->base_nh.numFds != RAVEN_NUM_FDS ||
        h->base_nh.numInts != RAVEN_NUM_INTS ||
        h->magic != RAVEN_GRALLOC_MAGIC) {
        return -EINVAL;
    }
    return 0;
}

static int gralloc_map(raven_handle_t *h) {
    if (h->pid == getpid() && hnd_get_base(h) != NULL) return 0;
    void *addr = mmap(NULL, (size_t)h->size, PROT_READ | PROT_WRITE, MAP_SHARED, h->fd, 0);
    if (addr == MAP_FAILED) {
        ALOGE("mmap fd=%d size=%d failed: %d", h->fd, h->size, errno);
        return -errno;
    }
    hnd_set_base(h, addr);
    h->pid = getpid();
    return 0;
}

static int gralloc_register_buffer(struct gralloc_module_t const *module, buffer_handle_t handle) {
    (void)module;
    if (validate_handle(handle) < 0) return -EINVAL;
    raven_handle_t *h = (raven_handle_t *)handle;
    pthread_mutex_lock(&g_gralloc_lock);
    if (h->pid != getpid()) {
        hnd_set_base(h, NULL);
        h->pid = 0;
    }
    int rc = gralloc_map(h);
    pthread_mutex_unlock(&g_gralloc_lock);
    return rc;
}

static int gralloc_unregister_buffer(struct gralloc_module_t const *module, buffer_handle_t handle) {
    (void)module;
    if (validate_handle(handle) < 0) return -EINVAL;
    raven_handle_t *h = (raven_handle_t *)handle;
    pthread_mutex_lock(&g_gralloc_lock);
    void *base = hnd_get_base(h);
    if (h->pid == getpid() && base != NULL) {
        munmap(base, (size_t)h->size);
        hnd_set_base(h, NULL);
        h->pid = 0;
    }
    pthread_mutex_unlock(&g_gralloc_lock);
    return 0;
}

static int gralloc_lock(struct gralloc_module_t const *module,
                        buffer_handle_t handle, int usage,
                        int l, int t, int w, int h, void **vaddr) {
    (void)module; (void)usage; (void)l; (void)t; (void)w; (void)h;
    if (validate_handle(handle) < 0) return -EINVAL;
    raven_handle_t *rh = (raven_handle_t *)handle;
    pthread_mutex_lock(&g_gralloc_lock);
    int rc = gralloc_map(rh);
    if (rc == 0 && vaddr) *vaddr = hnd_get_base(rh);
    pthread_mutex_unlock(&g_gralloc_lock);
    return rc;
}

static int gralloc_unlock(struct gralloc_module_t const *module, buffer_handle_t handle) {
    (void)module;
    if (validate_handle(handle) < 0) return -EINVAL;
    return 0;
}

static int gralloc_lock_ycbcr(struct gralloc_module_t const *module,
                              buffer_handle_t handle, int usage,
                              int l, int t, int w, int h,
                              struct android_ycbcr *ycbcr) {
    (void)module; (void)usage; (void)l; (void)t; (void)w; (void)h; (void)ycbcr;
    if (validate_handle(handle) < 0) return -EINVAL;
    return -EINVAL;
}

static int gralloc_alloc(struct alloc_device_t *dev,
                         int w, int h, int format, int usage,
                         buffer_handle_t *handle, int *stride) {
    (void)dev; (void)usage;
    if (!handle || !stride || w <= 0 || h <= 0) return -EINVAL;

    int bpp = 4;
    switch (format) {
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
        case HAL_PIXEL_FORMAT_BGRA_8888:
            bpp = 4; break;
        case HAL_PIXEL_FORMAT_RGB_888:
            bpp = 3; break;
        case HAL_PIXEL_FORMAT_RGB_565:
        case HAL_PIXEL_FORMAT_YV12:
            bpp = 2; break;
        default:
            bpp = 4; break;
    }

    /* 16-pixel (64-byte) alignment for NEON vld4q_u8/vst4q_u8 SIMD blits */
    int align_w = (w + 15) & ~15;
    size_t sz = (size_t)align_w * (size_t)h * (size_t)bpp;
    sz = (sz + 4095u) & ~4095u;
    if (sz == 0) sz = 4096;

    int fd = ashmem_create_region("gralloc-raven", sz);
    if (fd < 0) {
        ALOGE("ashmem_create_region(%zu) failed", sz);
        return -ENOMEM;
    }

    raven_handle_t *rh = (raven_handle_t *)native_handle_create(RAVEN_NUM_FDS, RAVEN_NUM_INTS);
    if (!rh) {
        close(fd);
        return -ENOMEM;
    }
    rh->fd = fd;
    rh->magic = RAVEN_GRALLOC_MAGIC;
    rh->flags = 0;
    rh->size = (int)sz;
    rh->offset = 0;
    rh->width = w;
    rh->height = h;
    rh->stride = align_w;
    rh->format = format;
    rh->pid = getpid();
    rh->base_lo = 0;
    rh->base_hi = 0;

    if (gralloc_map(rh) < 0) {
        native_handle_close(&rh->base_nh);
        native_handle_delete(&rh->base_nh);
        return -ENOMEM;
    }
    memset(hnd_get_base(rh), 0, sz);

    *handle = &rh->base_nh;
    *stride = align_w;
    return 0;
}

static int gralloc_free(struct alloc_device_t *dev, buffer_handle_t handle) {
    (void)dev;
    if (validate_handle(handle) < 0) return -EINVAL;
    raven_handle_t *rh = (raven_handle_t *)handle;
    void *base = hnd_get_base(rh);
    if (rh->pid == getpid() && base != NULL) {
        munmap(base, (size_t)rh->size);
    }
    native_handle_close(&rh->base_nh);
    native_handle_delete(&rh->base_nh);
    return 0;
}

static int raven_gralloc_close(struct hw_device_t *dev) {
    free(dev);
    return 0;
}

static int gralloc_device_open(const struct hw_module_t *module, const char *name, struct hw_device_t **device) {
    if (strcmp(name, GRALLOC_HARDWARE_GPU0) == 0) {
        alloc_device_t *dev = (alloc_device_t *)calloc(1, sizeof(alloc_device_t));
        if (!dev) return -ENOMEM;
        dev->common.tag = HARDWARE_DEVICE_TAG;
        dev->common.version = GRALLOC_DEVICE_API_VERSION_0_1;
        dev->common.module = (struct hw_module_t *)module;
        dev->common.close = raven_gralloc_close;
        dev->alloc = gralloc_alloc;
        dev->free = gralloc_free;
        *device = &dev->common;
        ALOGI("Opened gralloc gpu0 (raven 64B-aligned)");
        return 0;
    }
    return -EINVAL;
}

static struct hw_module_methods_t gralloc_module_methods = {
    .open = gralloc_device_open,
};

struct private_module_t {
    gralloc_module_t base;
};

struct private_module_t HAL_MODULE_INFO_SYM = {
    .base = {
        .common = {
            .tag = HARDWARE_MODULE_TAG,
            .module_api_version = GRALLOC_MODULE_API_VERSION_0_2,
            .hal_api_version = HARDWARE_HAL_API_VERSION,
            .id = GRALLOC_HARDWARE_MODULE_ID,
            .name = "Pixel 6 Pro Ashmem Gralloc",
            .author = "IRMP",
            .methods = &gralloc_module_methods,
        },
        .registerBuffer = gralloc_register_buffer,
        .unregisterBuffer = gralloc_unregister_buffer,
        .lock = gralloc_lock,
        .unlock = gralloc_unlock,
        .lock_ycbcr = gralloc_lock_ycbcr,
    },
};
