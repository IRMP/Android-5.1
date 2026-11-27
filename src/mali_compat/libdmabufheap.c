#define LOG_TAG "libdmabufheap"
#include "bionic_ext.h"
#include <linux/ioctl.h>

struct dma_heap_allocation_data {
    uint64_t len;
    uint32_t fd;
    uint32_t fd_flags;
    uint64_t heap_flags;
};
#define DMA_HEAP_IOC_MAGIC 'H'
#define DMA_HEAP_IOCTL_ALLOC _IOWR(DMA_HEAP_IOC_MAGIC, 0x0, struct dma_heap_allocation_data)

typedef struct BufferAllocator {
    int magic;
} BufferAllocator;

static BufferAllocator g_alloc = { 0x444D4148 };

void *CreateDmabufHeapBufferAllocator(void) {
    return &g_alloc;
}

void FreeDmabufHeapBufferAllocator(void *allocator) {
    (void)allocator;
}

int DmabufHeapAlloc(void *allocator, const char *heap_name, size_t len,
                    unsigned int heap_flags, size_t legacy_align) {
    (void)allocator; (void)legacy_align;
    char path[128];
    const char *name = (heap_name && heap_name[0]) ? heap_name : "system";
    snprintf(path, sizeof(path), "/dev/dma_heap/%s", name);
    int heap_fd = open(path, O_RDONLY | O_CLOEXEC);
    if (heap_fd < 0 && strcmp(name, "system") != 0) {
        heap_fd = open("/dev/dma_heap/system", O_RDONLY | O_CLOEXEC);
    }
    if (heap_fd < 0) {
        ALOGE("Failed to open %s: %d", path, errno);
        return -errno;
    }
    struct dma_heap_allocation_data data;
    memset(&data, 0, sizeof(data));
    data.len = (uint64_t)((len + 4095u) & ~4095u);
    if (data.len == 0) data.len = 4096;
    data.fd_flags = O_RDWR | O_CLOEXEC;
    data.heap_flags = heap_flags;
    int rc = ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &data);
    int err = errno;
    close(heap_fd);
    if (rc < 0) {
        ALOGE("DMA_HEAP_IOCTL_ALLOC(%zu) failed: %d", len, err);
        return -err;
    }
    return (int)data.fd;
}
