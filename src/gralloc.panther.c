#define LOG_TAG "gralloc.panther"
#include "bionic_ext.h"
#include <hardware/gralloc.h>
#include <hardware/hardware.h>
#include <cutils/native_handle.h>
#include <system/graphics.h>
#include <linux/ioctl.h>

/* Linux UAPI <linux/dma-heap.h> */
struct dma_heap_allocation_data {
    uint64_t len;
    uint32_t fd;
    uint32_t fd_flags;
    uint64_t heap_flags;
};
#define DMA_HEAP_IOC_MAGIC 'H'
#define DMA_HEAP_IOCTL_ALLOC _IOWR(DMA_HEAP_IOC_MAGIC, 0x0, struct dma_heap_allocation_data)

#define PANTHER_GRALLOC_MAGIC 0x50374752 /* "P7GR" */
#define PANTHER_FLAG_DMABUF   0x00000001

typedef struct panther_handle {
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
} panther_handle_t;

#define PANTHER_NUM_FDS  1
#define PANTHER_NUM_INTS 11

static pthread_mutex_t g_gralloc_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_dma_heap_fd = -2; /* -2 = uninitialized, -1 = unavailable */

static int get_dma_heap_fd(void) {
    if (g_dma_heap_fd != -2) return g_dma_heap_fd;
    int fd = open("/dev/dma_heap/system", O_RDONLY | O_CLOEXEC);
    if (fd < 0) fd = open("/dev/dma_heap/system-uncached", O_RDONLY | O_CLOEXEC);
    g_dma_heap_fd = fd;
    if (fd >= 0) {
        ALOGI("Using real Linux DMA-BUF heap (/dev/dma_heap/system)");
    } else {
        ALOGW("DMA-BUF heap unavailable (%d), falling back to ashmem", errno);
    }
    return g_dma_heap_fd;
}

static inline void *hnd_get_base(const panther_handle_t *h) {
    uint64_t v = ((uint64_t)h->base_hi << 32) | (uint64_t)h->base_lo;
    return (void *)(uintptr_t)v;
}

static inline void hnd_set_base(panther_handle_t *h, void *p) {
    uint64_t v = (uint64_t)(uintptr_t)p;
    h->base_lo = (uint32_t)(v & 0xffffffffu);
    h->base_hi = (uint32_t)(v >> 32);
}

static int validate_handle(buffer_handle_t handle) {
    if (!handle) return -EINVAL;
    const panther_handle_t *h = (const panther_handle_t *)handle;
    if (h->base_nh.version != sizeof(native_handle_t) ||
        h->base_nh.numFds != PANTHER_NUM_FDS ||
        h->base_nh.numInts != PANTHER_NUM_INTS ||
        h->magic != PANTHER_GRALLOC_MAGIC) {
        ALOGE("Invalid gralloc handle %p", handle);
        return -EINVAL;
    }
    return 0;
}

static int gralloc_map(panther_handle_t *h) {
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
    panther_handle_t *h = (panther_handle_t *)handle;
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
    panther_handle_t *h = (panther_handle_t *)handle;
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
    panther_handle_t *ph = (panther_handle_t *)handle;
    pthread_mutex_lock(&g_gralloc_lock);
    int rc = gralloc_map(ph);
    if (rc == 0 && vaddr) *vaddr = hnd_get_base(ph);
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
    (void)module; (void)usage; (void)l; (void)t; (void)w; (void)h;
    if (validate_handle(handle) < 0 || !ycbcr) return -EINVAL;
    panther_handle_t *ph = (panther_handle_t *)handle;
    pthread_mutex_lock(&g_gralloc_lock);
    int rc = gralloc_map(ph);
    void *base = hnd_get_base(ph);
    pthread_mutex_unlock(&g_gralloc_lock);
    if (rc < 0 || !base) return -EINVAL;

    memset(ycbcr, 0, sizeof(*ycbcr));
    if (ph->format == HAL_PIXEL_FORMAT_YCrCb_420_SP) {
        ycbcr->y = base;
        ycbcr->cr = (uint8_t *)base + (size_t)ph->stride * (size_t)ph->height;
        ycbcr->cb = (uint8_t *)ycbcr->cr + 1;
        ycbcr->ystride = (size_t)ph->stride;
        ycbcr->cstride = (size_t)ph->stride;
        ycbcr->chroma_step = 2;
        return 0;
    }
    if (ph->format == HAL_PIXEL_FORMAT_YV12) {
        size_t ysize = (size_t)ph->stride * (size_t)ph->height;
        size_t cstride = ((size_t)(ph->stride / 2) + 15u) & ~15u;
        size_t csize = cstride * (size_t)(ph->height / 2);
        ycbcr->y = base;
        ycbcr->cr = (uint8_t *)base + ysize;
        ycbcr->cb = (uint8_t *)base + ysize + csize;
        ycbcr->ystride = (size_t)ph->stride;
        ycbcr->cstride = cstride;
        ycbcr->chroma_step = 1;
        return 0;
    }
    return -EINVAL;
}

static int gralloc_alloc(struct alloc_device_t *dev,
                         int w, int h, int format, int usage,
                         buffer_handle_t *handle, int *stride) {
    (void)dev;
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
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
            bpp = 2; break;
        default:
            bpp = 4; break;
    }

    /* 64-pixel alignment keeps Mali-G710 AFBC / linear stride requirements happy */
    int align_w = (w + 63) & ~63;
    size_t sz = (size_t)align_w * (size_t)h * (size_t)bpp;
    sz = (sz + 4095u) & ~4095u;
    if (sz == 0) sz = 4096;

    int fd = -1;
    int flags = 0;
    pthread_mutex_lock(&g_gralloc_lock);
    int heap_fd = get_dma_heap_fd();
    pthread_mutex_unlock(&g_gralloc_lock);

    if (heap_fd >= 0) {
        struct dma_heap_allocation_data alloc_req;
        memset(&alloc_req, 0, sizeof(alloc_req));
        alloc_req.len = (uint64_t)sz;
        alloc_req.fd_flags = O_RDWR | O_CLOEXEC;
        if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc_req) == 0) {
            fd = (int)alloc_req.fd;
            flags |= PANTHER_FLAG_DMABUF;
        }
    }
    if (fd < 0) {
        fd = ashmem_create_region("gralloc-buf", sz);
    }
    if (fd < 0) {
        ALOGE("buffer alloc(%zu) failed", sz);
        return -ENOMEM;
    }

    panther_handle_t *ph = (panther_handle_t *)native_handle_create(PANTHER_NUM_FDS, PANTHER_NUM_INTS);
    if (!ph) {
        close(fd);
        return -ENOMEM;
    }
    ph->fd = fd;
    ph->magic = PANTHER_GRALLOC_MAGIC;
    ph->flags = flags | ((usage & 0xFFFF) << 4);
    ph->size = (int)sz;
    ph->offset = 0;
    ph->width = w;
    ph->height = h;
    ph->stride = align_w;
    ph->format = format;
    ph->pid = getpid();
    ph->base_lo = 0;
    ph->base_hi = 0;

    if (gralloc_map(ph) < 0) {
        native_handle_close(&ph->base_nh);
        native_handle_delete(&ph->base_nh);
        return -ENOMEM;
    }
    memset(hnd_get_base(ph), 0, sz);

    *handle = &ph->base_nh;
    *stride = align_w;
    return 0;
}

static int gralloc_free(struct alloc_device_t *dev, buffer_handle_t handle) {
    (void)dev;
    if (validate_handle(handle) < 0) return -EINVAL;
    panther_handle_t *ph = (panther_handle_t *)handle;
    void *base = hnd_get_base(ph);
    if (ph->pid == getpid() && base != NULL) {
        munmap(base, (size_t)ph->size);
    }
    native_handle_close(&ph->base_nh);
    native_handle_delete(&ph->base_nh);
    return 0;
}

static int panther_gralloc_close(struct hw_device_t *dev) {
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
        dev->common.close = panther_gralloc_close;
        dev->alloc = gralloc_alloc;
        dev->free = gralloc_free;
        *device = &dev->common;
        ALOGI("Opened gralloc gpu0 (panther DMA-BUF)");
        return 0;
    }
    ALOGE("Unsupported gralloc device: %s", name ? name : "(null)");
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
            .name = "Pixel 7 DMA-BUF Gralloc",
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
