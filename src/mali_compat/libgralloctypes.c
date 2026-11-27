#include "bionic_ext.h"

/* Compact internal metadata blob passed between our IMapper@4.0 shim and gralloc4::decode* */
#define PANTHER_META_MAGIC 0x4D345037u /* "P74M" */

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

typedef struct hidl_vec_u8 {
    const uint8_t *mBuffer;
    uint32_t mSize;
    uint8_t mOwnsBuffer;
    uint8_t pad[3];
} hidl_vec_u8_t;

typedef struct std_vec_hdr {
    void *begin;
    void *end;
    void *cap;
} std_vec_hdr_t;

/* aidl::android::hardware::graphics::common::Rect = 16 bytes */
typedef struct aidl_rect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} aidl_rect_t;

/* Verified 88-byte aidl::android::hardware::graphics::common::PlaneLayout */
typedef struct aidl_plane_layout {
    std_vec_hdr_t components;        /* +0  (24B) */
    int64_t offsetInBytes;           /* +24 */
    int64_t sampleIncrementInBits;   /* +32 */
    int64_t strideInBytes;           /* +40 */
    int64_t widthInSamples;          /* +48 */
    int64_t heightInSamples;         /* +56 */
    int64_t totalSizeInBytes;        /* +64 */
    int64_t horizontalSubsampling;   /* +72 */
    int64_t verticalSubsampling;     /* +80 */
} aidl_plane_layout_t;

static const panther_gralloc4_meta_t *get_meta(const hidl_vec_u8_t *vec) {
    if (!vec || !vec->mBuffer || vec->mSize < sizeof(panther_gralloc4_meta_t)) return NULL;
    const panther_gralloc4_meta_t *m = (const panther_gralloc4_meta_t *)vec->mBuffer;
    return (m->magic == PANTHER_META_MAGIC) ? m : NULL;
}

extern int32_t dec_width(const hidl_vec_u8_t *vec, uint64_t *out)
    __asm__("_ZN7android8gralloc411decodeWidthERKNS_8hardware8hidl_vecIhEEPm");
int32_t dec_width(const hidl_vec_u8_t *vec, uint64_t *out) {
    const panther_gralloc4_meta_t *m = get_meta(vec);
    if (!m || !out) return -EINVAL;
    *out = m->width;
    return 0;
}

extern int32_t dec_height(const hidl_vec_u8_t *vec, uint64_t *out)
    __asm__("_ZN7android8gralloc412decodeHeightERKNS_8hardware8hidl_vecIhEEPm");
int32_t dec_height(const hidl_vec_u8_t *vec, uint64_t *out) {
    const panther_gralloc4_meta_t *m = get_meta(vec);
    if (!m || !out) return -EINVAL;
    *out = m->height;
    return 0;
}

extern int32_t dec_usage(const hidl_vec_u8_t *vec, uint64_t *out)
    __asm__("_ZN7android8gralloc411decodeUsageERKNS_8hardware8hidl_vecIhEEPm");
int32_t dec_usage(const hidl_vec_u8_t *vec, uint64_t *out) {
    const panther_gralloc4_meta_t *m = get_meta(vec);
    if (!m || !out) return -EINVAL;
    *out = m->usage;
    return 0;
}

extern int32_t dec_layer_count(const hidl_vec_u8_t *vec, uint64_t *out)
    __asm__("_ZN7android8gralloc416decodeLayerCountERKNS_8hardware8hidl_vecIhEEPm");
int32_t dec_layer_count(const hidl_vec_u8_t *vec, uint64_t *out) {
    const panther_gralloc4_meta_t *m = get_meta(vec);
    if (!m || !out) return -EINVAL;
    *out = m->layer_count;
    return 0;
}

extern int32_t dec_fourcc(const hidl_vec_u8_t *vec, uint32_t *out)
    __asm__("_ZN7android8gralloc423decodePixelFormatFourCCERKNS_8hardware8hidl_vecIhEEPj");
int32_t dec_fourcc(const hidl_vec_u8_t *vec, uint32_t *out) {
    const panther_gralloc4_meta_t *m = get_meta(vec);
    if (!m || !out) return -EINVAL;
    *out = m->fourcc;
    return 0;
}

extern int32_t dec_modifier(const hidl_vec_u8_t *vec, uint64_t *out)
    __asm__("_ZN7android8gralloc425decodePixelFormatModifierERKNS_8hardware8hidl_vecIhEEPm");
int32_t dec_modifier(const hidl_vec_u8_t *vec, uint64_t *out) {
    const panther_gralloc4_meta_t *m = get_meta(vec);
    if (!m || !out) return -EINVAL;
    *out = m->modifier;
    return 0;
}

extern int32_t dec_dataspace(const hidl_vec_u8_t *vec, int32_t *out)
    __asm__("_ZN7android8gralloc415decodeDataspaceERKNS_8hardware8hidl_vecIhEEPN4aidl7android8hardware8graphics6common9DataspaceE");
int32_t dec_dataspace(const hidl_vec_u8_t *vec, int32_t *out) {
    const panther_gralloc4_meta_t *m = get_meta(vec);
    if (!m || !out) return -EINVAL;
    *out = m->dataspace;
    return 0;
}

extern int32_t dec_crop(const hidl_vec_u8_t *vec, std_vec_hdr_t *out)
    __asm__("_ZN7android8gralloc410decodeCropERKNS_8hardware8hidl_vecIhEEPNSt3__16vectorIN4aidl7android8hardware8graphics6common4RectENS6_9allocatorISD_EEEE");
int32_t dec_crop(const hidl_vec_u8_t *vec, std_vec_hdr_t *out) {
    const panther_gralloc4_meta_t *m = get_meta(vec);
    if (!m || !out) return -EINVAL;
    aidl_rect_t *r = (aidl_rect_t *)calloc(1, sizeof(aidl_rect_t));
    if (!r) return -ENOMEM;
    r->left = 0;
    r->top = 0;
    r->right = m->right;
    r->bottom = m->bottom;
    out->begin = r;
    out->end = r + 1;
    out->cap = r + 1;
    return 0;
}

extern int32_t dec_plane_layouts(const hidl_vec_u8_t *vec, std_vec_hdr_t *out)
    __asm__("_ZN7android8gralloc418decodePlaneLayoutsERKNS_8hardware8hidl_vecIhEEPNSt3__16vectorIN4aidl7android8hardware8graphics6common11PlaneLayoutENS6_9allocatorISD_EEEE");
int32_t dec_plane_layouts(const hidl_vec_u8_t *vec, std_vec_hdr_t *out) {
    const panther_gralloc4_meta_t *m = get_meta(vec);
    if (!m || !out) return -EINVAL;
    aidl_plane_layout_t *pl = (aidl_plane_layout_t *)calloc(1, sizeof(aidl_plane_layout_t));
    if (!pl) return -ENOMEM;
    pl->offsetInBytes = 0;
    pl->sampleIncrementInBits = (m->fourcc == 0x36314752u /* RG16 */) ? 16 : 32;
    pl->strideInBytes = (int64_t)m->stride_bytes;
    pl->widthInSamples = (int64_t)m->width;
    pl->heightInSamples = (int64_t)m->height;
    pl->totalSizeInBytes = (int64_t)m->total_size;
    pl->horizontalSubsampling = 1;
    pl->verticalSubsampling = 1;
    out->begin = pl;
    out->end = pl + 1;
    out->cap = pl + 1;
    return 0;
}
