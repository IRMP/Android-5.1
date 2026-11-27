#define LOG_TAG "mapper@4.0-panther"
#include "bionic_ext.h"
#include <cutils/native_handle.h>
#include <system/graphics.h>

#define PANTHER_GRALLOC_MAGIC 0x50374752 /* "P7GR" */
#define PANTHER_META_MAGIC    0x4D345037u

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

typedef struct panther_gralloc4_meta {
    uint32_t magic;
    uint32_t fourcc;
    uint64_t width;
    uint64_t height;
    uint64_t stride_bytes;
    uint64_t total_size;
    uint64_t usage;
    uint64_t layer_count;
    uint64_t modifier;
    int32_t dataspace;
    int32_t right;
    int32_t bottom;
} panther_gralloc4_meta_t;

typedef struct hidl_string {
    const char *mBuffer;
    uint32_t mSize;
    uint8_t mOwnsBuffer;
    uint8_t pad[3];
} hidl_string_t;

typedef struct metadata_type {
    hidl_string_t name;
    int64_t value;
} metadata_type_t;

typedef struct hidl_vec_u8 {
    const uint8_t *mBuffer;
    uint32_t mSize;
    uint8_t mOwnsBuffer;
    uint8_t pad[3];
} hidl_vec_u8_t;

typedef struct return_status {
    int32_t code;
    uint8_t pad[4];
    void *msg;
    uint64_t reserved[3];
} return_status_t;

/* std::__1::function<void(Error, hidl_vec<uint8_t> const&)> layout (48B, __f_ at +32) */
typedef struct std_func {
    uint8_t buf[32];
    void **f_ptr;
} std_func_t;

typedef void (*cb_invoke_fn)(void *obj, int32_t *err, const hidl_vec_u8_t *vec);

typedef struct MapperVtable {
    void *slots[32];
} MapperVtable;

typedef struct PantherMapper {
    MapperVtable *vptr;
    void *refbase_vptr;
    int32_t refs;
} PantherMapper;

static uint32_t hal_fmt_to_drm_fourcc(int fmt, int *bpp_out) {
    /* DRM_FORMAT_* little-endian FourCC codes expected by Mali Valhall */
    switch (fmt) {
        case HAL_PIXEL_FORMAT_BGRA_8888:
            *bpp_out = 4; return 0x34325241u; /* AR24 = ARGB8888 */
        case HAL_PIXEL_FORMAT_RGB_565:
            *bpp_out = 2; return 0x36314752u; /* RG16 = RGB565 */
        case HAL_PIXEL_FORMAT_RGBX_8888:
            *bpp_out = 4; return 0x34324258u; /* XB24 = XBGR8888 */
        case HAL_PIXEL_FORMAT_RGB_888:
            *bpp_out = 3; return 0x34324752u; /* RG24 = RGB888 */
        case HAL_PIXEL_FORMAT_RGBA_8888:
        default:
            *bpp_out = 4; return 0x34324241u; /* AB24 = ABGR8888 */
    }
}

static void mapper_noop_ret(void *self, return_status_t *ret) {
    (void)self;
    if (ret) memset(ret, 0, sizeof(*ret));
}

/* Slot 23 (byte offset 184) = IMapper::get(void* buffer, MetadataType const&, std::function) */
static void mapper_get(void *self, return_status_t *ret, const native_handle_t *buf,
                       const metadata_type_t *meta_type, const std_func_t *cb) {
    (void)self; (void)meta_type;
    if (ret) memset(ret, 0, sizeof(*ret));

    panther_gralloc4_meta_t meta;
    memset(&meta, 0, sizeof(meta));
    meta.magic = PANTHER_META_MAGIC;
    meta.layer_count = 1;
    meta.modifier = 0;

    int bpp = 4;
    if (buf && buf->version == sizeof(native_handle_t) &&
        buf->numInts >= 9 && ((const panther_handle_t *)buf)->magic == PANTHER_GRALLOC_MAGIC) {
        const panther_handle_t *ph = (const panther_handle_t *)buf;
        meta.fourcc = hal_fmt_to_drm_fourcc(ph->format, &bpp);
        meta.width = (uint64_t)ph->width;
        meta.height = (uint64_t)ph->height;
        meta.stride_bytes = (uint64_t)ph->stride * (uint64_t)bpp;
        meta.total_size = (uint64_t)ph->size;
        meta.usage = 0x333; /* CPU_READ|CPU_WRITE|GPU_TEXTURE|GPU_RENDER_TARGET|COMPOSER */
        meta.right = ph->width;
        meta.bottom = ph->height;
    } else {
        meta.fourcc = 0x34324241u;
        meta.width = 1080;
        meta.height = 2400;
        meta.stride_bytes = 1088 * 4;
        meta.total_size = 1088 * 2400 * 4;
        meta.usage = 0x333;
        meta.right = 1080;
        meta.bottom = 2400;
    }

    hidl_vec_u8_t vec;
    vec.mBuffer = (const uint8_t *)&meta;
    vec.mSize = sizeof(meta);
    vec.mOwnsBuffer = 0;

    if (cb && cb->f_ptr) {
        void **vtbl = *(void ***)cb->f_ptr;
        cb_invoke_fn inv = (cb_invoke_fn)vtbl[6]; /* libc++ __func::operator() */
        int32_t err = 0; /* Error::NONE */
        inv(cb->f_ptr, &err, &vec);
    }
}

static MapperVtable g_mapper_vtable;
static PantherMapper g_mapper = { &g_mapper_vtable, NULL, 1000 };
static int g_vtable_inited = 0;

/* sp<IMapper> IMapper::getService(std::string const&, bool) */
extern void mapper_get_service(PantherMapper **out_sp, const void *svc_name, bool getStub)
    __asm__("_ZN7android8hardware8graphics6mapper4V4_07IMapper10getServiceERKNSt3__112basic_stringIcNS5_11char_traitsIcEENS5_9allocatorIcEEEEb");
void mapper_get_service(PantherMapper **out_sp, const void *svc_name, bool getStub) {
    (void)svc_name; (void)getStub;
    if (!g_vtable_inited) {
        for (int i = 0; i < 32; i++) g_mapper_vtable.slots[i] = (void *)mapper_noop_ret;
        g_mapper_vtable.slots[23] = (void *)mapper_get;
        g_vtable_inited = 1;
    }
    if (out_sp) *out_sp = &g_mapper;
}

/* android::RefBase::decStrong stub if needed via HIDL */
extern void refbase_dec_strong(void *self, const void *id) __asm__("_ZNK7android7RefBase9decStrongEPKv");
void refbase_dec_strong(void *self, const void *id) { (void)self; (void)id; }
