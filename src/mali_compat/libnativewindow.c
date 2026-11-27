#define LOG_TAG "libnativewindow"
#include "bionic_ext.h"
#include <system/window.h>
#include <system/graphics.h>
#include <cutils/native_handle.h>
#include <linux/ioctl.h>

struct dma_heap_allocation_data {
    uint64_t len;
    uint32_t fd;
    uint32_t fd_flags;
    uint64_t heap_flags;
};
#define DMA_HEAP_IOC_MAGIC 'H'
#define DMA_HEAP_IOCTL_ALLOC _IOWR(DMA_HEAP_IOC_MAGIC, 0x0, struct dma_heap_allocation_data)

#define PANTHER_GRALLOC_MAGIC 0x50374752
#define PANTHER_NUM_FDS  1
#define PANTHER_NUM_INTS 11

typedef struct panther_handle {
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
} panther_handle_t;

/* AHardwareBuffer_Desc matches Android 8+ NDK ABI */
typedef struct AHardwareBuffer_Desc {
    uint32_t width;
    uint32_t height;
    uint32_t layers;
    uint32_t format;
    uint64_t usage;
    uint32_t stride;
    uint32_t rfu0;
    uint64_t rfu1;
} AHardwareBuffer_Desc;

typedef struct PantherAHB {
    ANativeWindowBuffer_t nwbuf;
    volatile int refcount;
} PantherAHB;

static void ahb_inc_ref(struct android_native_base_t *base) {
    PantherAHB *p = (PantherAHB *)((char *)base - offsetof(ANativeWindowBuffer_t, common));
    __sync_add_and_fetch(&p->refcount, 1);
}

static void ahb_dec_ref(struct android_native_base_t *base) {
    PantherAHB *p = (PantherAHB *)((char *)base - offsetof(ANativeWindowBuffer_t, common));
    if (__sync_sub_and_fetch(&p->refcount, 1) <= 0) {
        if (p->nwbuf.handle) {
            native_handle_close((native_handle_t *)p->nwbuf.handle);
            native_handle_delete((native_handle_t *)p->nwbuf.handle);
        }
        free(p);
    }
}

/* ============ ANativeWindow C NDK API -> Android 5.1 ANativeWindow ops ============ */

void ANativeWindow_acquire(ANativeWindow *win) {
    if (win && win->common.incRef) win->common.incRef(&win->common);
}

void ANativeWindow_release(ANativeWindow *win) {
    if (win && win->common.decRef) win->common.decRef(&win->common);
}

int32_t ANativeWindow_getWidth(ANativeWindow *win) {
    int v = 0;
    if (!win || !win->query) return -EINVAL;
    return (win->query(win, NATIVE_WINDOW_WIDTH, &v) == 0) ? v : 0;
}

int32_t ANativeWindow_getHeight(ANativeWindow *win) {
    int v = 0;
    if (!win || !win->query) return -EINVAL;
    return (win->query(win, NATIVE_WINDOW_HEIGHT, &v) == 0) ? v : 0;
}

int32_t ANativeWindow_getFormat(ANativeWindow *win) {
    int v = 0;
    if (!win || !win->query) return -EINVAL;
    return (win->query(win, NATIVE_WINDOW_FORMAT, &v) == 0) ? v : 0;
}

int32_t ANativeWindow_setBuffersGeometry(ANativeWindow *win, int32_t w, int32_t h, int32_t fmt) {
    if (!win || !win->perform) return -EINVAL;
    return win->perform(win, NATIVE_WINDOW_SET_BUFFERS_GEOMETRY, w, h, fmt);
}

int ANativeWindow_setBuffersDimensions(ANativeWindow *win, int32_t w, int32_t h) {
    if (!win || !win->perform) return -EINVAL;
    return win->perform(win, NATIVE_WINDOW_SET_BUFFERS_DIMENSIONS, w, h);
}

int ANativeWindow_setBuffersFormat(ANativeWindow *win, int32_t fmt) {
    if (!win || !win->perform) return -EINVAL;
    return win->perform(win, NATIVE_WINDOW_SET_BUFFERS_FORMAT, fmt);
}

int ANativeWindow_setBuffersTransform(ANativeWindow *win, int32_t transform) {
    if (!win || !win->perform) return -EINVAL;
    return win->perform(win, NATIVE_WINDOW_SET_BUFFERS_TRANSFORM, transform);
}

int ANativeWindow_setSharedBufferMode(ANativeWindow *win, bool shared) {
    (void)win; (void)shared;
    return 0; /* Android 5.1 Surface does not support auto-refresh/shared buffer mode */
}

int ANativeWindow_setSwapInterval(ANativeWindow *win, int interval) {
    if (!win || !win->setSwapInterval) return -EINVAL;
    return win->setSwapInterval(win, interval);
}

int ANativeWindow_setUsage(ANativeWindow *win, uint64_t usage) {
    if (!win || !win->perform) return -EINVAL;
    return win->perform(win, NATIVE_WINDOW_SET_USAGE, (int)(usage & 0xFFFFFFFFu));
}

int ANativeWindow_query(const ANativeWindow *win, int what, int *value) {
    if (!win || !win->query || !value) return -EINVAL;
    /* Map Android 8+ query enums cleanly if different from 5.1 */
    if (what == 13 /* MIRROR_TRANSFORM */ || what == 16 /* BUFFER_AGE */) {
        *value = 0;
        return 0;
    }
    return win->query(win, what, value);
}

int ANativeWindow_queryf(const ANativeWindow *win, int what, float *value) {
    (void)win; (void)what;
    if (value) *value = 60.0f;
    return 0;
}

int ANativeWindow_dequeueBuffer(ANativeWindow *win, ANativeWindowBuffer_t **buffer, int *fenceFd) {
    if (!win || !win->dequeueBuffer) return -EINVAL;
    return win->dequeueBuffer(win, buffer, fenceFd);
}

int ANativeWindow_queueBuffer(ANativeWindow *win, ANativeWindowBuffer_t *buffer, int fenceFd) {
    if (!win || !win->queueBuffer) return -EINVAL;
    return win->queueBuffer(win, buffer, fenceFd);
}

int ANativeWindow_cancelBuffer(ANativeWindow *win, ANativeWindowBuffer_t *buffer, int fenceFd) {
    if (!win || !win->cancelBuffer) return -EINVAL;
    return win->cancelBuffer(win, buffer, fenceFd);
}

/* ============ AHardwareBuffer API (binary layout compatible with ANativeWindowBuffer) ============ */

void AHardwareBuffer_acquire(void *buffer) {
    ANativeWindowBuffer_t *nw = (ANativeWindowBuffer_t *)buffer;
    if (nw && nw->common.incRef) nw->common.incRef(&nw->common);
}

void AHardwareBuffer_release(void *buffer) {
    ANativeWindowBuffer_t *nw = (ANativeWindowBuffer_t *)buffer;
    if (nw && nw->common.decRef) nw->common.decRef(&nw->common);
}

const native_handle_t *AHardwareBuffer_getNativeHandle(const void *buffer) {
    const ANativeWindowBuffer_t *nw = (const ANativeWindowBuffer_t *)buffer;
    return nw ? nw->handle : NULL;
}

void AHardwareBuffer_describe(const void *buffer, AHardwareBuffer_Desc *outDesc) {
    const ANativeWindowBuffer_t *nw = (const ANativeWindowBuffer_t *)buffer;
    if (!nw || !outDesc) return;
    memset(outDesc, 0, sizeof(*outDesc));
    outDesc->width = (uint32_t)nw->width;
    outDesc->height = (uint32_t)nw->height;
    outDesc->layers = 1;
    outDesc->format = (uint32_t)nw->format;
    outDesc->usage = (uint64_t)(uint32_t)nw->usage;
    outDesc->stride = (uint32_t)nw->stride;
}

int AHardwareBuffer_getId(const void *buffer, uint64_t *outId) {
    const ANativeWindowBuffer_t *nw = (const ANativeWindowBuffer_t *)buffer;
    if (!nw || !outId) return -EINVAL;
    *outId = (uint64_t)(uintptr_t)(nw->handle ? nw->handle : (const void *)nw);
    return 0;
}

int AHardwareBuffer_allocate(const AHardwareBuffer_Desc *desc, void **outBuffer) {
    if (!desc || !outBuffer || desc->width == 0 || desc->height == 0) return -EINVAL;
    uint32_t bpp = (desc->format == HAL_PIXEL_FORMAT_RGB_565) ? 2 : 4;
    uint32_t align_w = (desc->width + 63u) & ~63u;
    size_t sz = (size_t)align_w * (size_t)desc->height * (size_t)bpp;
    sz = (sz + 4095u) & ~4095u;
    if (sz == 0) sz = 4096;

    int fd = -1;
    int heap_fd = open("/dev/dma_heap/system", O_RDONLY | O_CLOEXEC);
    if (heap_fd >= 0) {
        struct dma_heap_allocation_data req;
        memset(&req, 0, sizeof(req));
        req.len = sz;
        req.fd_flags = O_RDWR | O_CLOEXEC;
        if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &req) == 0) fd = (int)req.fd;
        close(heap_fd);
    }
    if (fd < 0) fd = ashmem_create_region("ahb-buf", sz);
    if (fd < 0) return -ENOMEM;

    panther_handle_t *ph = (panther_handle_t *)native_handle_create(PANTHER_NUM_FDS, PANTHER_NUM_INTS);
    PantherAHB *ahb = (PantherAHB *)calloc(1, sizeof(*ahb));
    if (!ph || !ahb) {
        if (ph) native_handle_delete(&ph->base_nh);
        close(fd);
        free(ahb);
        return -ENOMEM;
    }
    ph->fd = fd;
    ph->magic = PANTHER_GRALLOC_MAGIC;
    ph->flags = 1;
    ph->size = (int)sz;
    ph->width = (int)desc->width;
    ph->height = (int)desc->height;
    ph->stride = (int)align_w;
    ph->format = (int)desc->format;

    ahb->refcount = 1;
    ahb->nwbuf.common.magic = ANDROID_NATIVE_BUFFER_MAGIC;
    ahb->nwbuf.common.version = sizeof(ANativeWindowBuffer_t);
    ahb->nwbuf.common.incRef = ahb_inc_ref;
    ahb->nwbuf.common.decRef = ahb_dec_ref;
    ahb->nwbuf.width = (int)desc->width;
    ahb->nwbuf.height = (int)desc->height;
    ahb->nwbuf.stride = (int)align_w;
    ahb->nwbuf.format = (int)desc->format;
    ahb->nwbuf.usage = (int)(desc->usage & 0xFFFFFFFFu);
    ahb->nwbuf.handle = &ph->base_nh;
    *outBuffer = &ahb->nwbuf;
    return 0;
}

/* ============ Bionic N/R + libdrm + libc++17 compat symbols (resolved via DT_NEEDED[0]) ============ */

ssize_t __write_chk(int fd, const void *buf, size_t count, size_t buf_size) {
    (void)buf_size;
    return write(fd, buf, count);
}

int __poll_chk(struct pollfd *fds, unsigned long nfds, int timeout, size_t fds_size) {
    (void)fds_size;
    return poll(fds, nfds, timeout);
}

char *__gnu_strerror_r(int errnum, char *buf, size_t buflen) {
    char *s = strerror(errnum);
    if (buf && buflen > 0) {
        strncpy(buf, s ? s : "Unknown error", buflen - 1);
        buf[buflen - 1] = '\0';
        return buf;
    }
    return s;
}

extern int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void));
int __register_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void), void *dso_handle) {
    (void)dso_handle;
    return pthread_atfork(prepare, parent, child);
}

void _Unwind_Resume(void *exc) { (void)exc; }
FILE *open_memstream(char **ptr, size_t *sizeloc) { (void)ptr; (void)sizeloc; return NULL; }

uint64_t atrace_get_enabled_tags(void) { return 0; }
void atrace_begin_body(const char *name) { (void)name; }
void atrace_end_body(void) {}
void atrace_async_begin_body(const char *name, int32_t cookie) { (void)name; (void)cookie; }
void atrace_async_end_body(const char *name, int32_t cookie) { (void)name; (void)cookie; }
void atrace_int64_body(const char *name, int64_t val) { (void)name; (void)val; }

/* std::bad_optional_access & std::bad_any_cast vtables/dtor */
static const char *bad_what_opt(const void *self) { (void)self; return "bad optional access"; }
static const char *bad_what_any(const void *self) { (void)self; return "bad any_cast"; }
void bad_optional_access_dtor(void *self) __asm__("_ZNSt19bad_optional_accessD1Ev");
void bad_optional_access_dtor(void *self) { (void)self; }

void *const bad_opt_vtable[5] __asm__("_ZTVSt19bad_optional_access") = {
    NULL, NULL, (void *)bad_optional_access_dtor, (void *)bad_optional_access_dtor, (void *)bad_what_opt
};
void *const bad_any_vtable[5] __asm__("_ZTVSt12bad_any_cast") = {
    NULL, NULL, (void *)bad_optional_access_dtor, (void *)bad_optional_access_dtor, (void *)bad_what_any
};

/* libc++ std::__1::__shared_mutex_base backed by a pthread mutex (safe for 64-byte opaque object) */
struct shared_mutex_storage {
    pthread_mutex_t mu;
    char pad[64 - sizeof(pthread_mutex_t)];
};

extern void shared_mutex_ctor(struct shared_mutex_storage *s) __asm__("_ZNSt3__119__shared_mutex_baseC1Ev");
void shared_mutex_ctor(struct shared_mutex_storage *s) {
    memset(s, 0, sizeof(*s));
    pthread_mutex_init(&s->mu, NULL);
}

extern int pthread_mutex_trylock(pthread_mutex_t *m);
extern bool shared_mutex_try_lock_shared(struct shared_mutex_storage *s) __asm__("_ZNSt3__119__shared_mutex_base15try_lock_sharedEv");
bool shared_mutex_try_lock_shared(struct shared_mutex_storage *s) {
    return pthread_mutex_trylock(&s->mu) == 0;
}

extern void shared_mutex_unlock_shared(struct shared_mutex_storage *s) __asm__("_ZNSt3__119__shared_mutex_base13unlock_sharedEv");
void shared_mutex_unlock_shared(struct shared_mutex_storage *s) {
    pthread_mutex_unlock(&s->mu);
}
