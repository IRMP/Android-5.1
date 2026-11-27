#ifndef ANDROID_SYSTEM_CAMERA_VENDOR_TAGS_H
#define ANDROID_SYSTEM_CAMERA_VENDOR_TAGS_H
typedef struct vendor_tag_ops {
    int (*get_tag_count)(const struct vendor_tag_ops *v);
    void (*get_all_tags)(const struct vendor_tag_ops *v, uint32_t *tag_array);
    const char *(*get_section_name)(const struct vendor_tag_ops *v, uint32_t tag);
    const char *(*get_tag_name)(const struct vendor_tag_ops *v, uint32_t tag);
    int (*get_tag_type)(const struct vendor_tag_ops *v, uint32_t tag);
    void *reserved[8];
} vendor_tag_ops_t;
#endif
