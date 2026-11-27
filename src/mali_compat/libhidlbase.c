#include "bionic_ext.h"

/* HIDL ABI structs matching Android 13 libhidlbase.so */
typedef struct hidl_string {
    const char *mBuffer;
    uint32_t mSize;
    uint8_t mOwnsBuffer;
    uint8_t pad[3];
} hidl_string_t;

typedef struct return_status {
    int32_t code;
    uint8_t pad[4];
    void *msg;
    uint64_t reserved[3];
} return_status_t;

extern void hidl_string_ctor_cstr(hidl_string_t *self, const char *s)
    __asm__("_ZN7android8hardware11hidl_stringC1EPKc");
void hidl_string_ctor_cstr(hidl_string_t *self, const char *s) {
    if (!s) s = "";
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (copy) memcpy(copy, s, len + 1);
    self->mBuffer = copy ? copy : "";
    self->mSize = (uint32_t)len;
    self->mOwnsBuffer = copy ? 1 : 0;
}

extern void hidl_string_ctor_copy(hidl_string_t *self, const hidl_string_t *other)
    __asm__("_ZN7android8hardware11hidl_stringC1ERKS1_");
void hidl_string_ctor_copy(hidl_string_t *self, const hidl_string_t *other) {
    if (!other || !other->mBuffer) {
        hidl_string_ctor_cstr(self, "");
        return;
    }
    uint32_t len = other->mSize;
    char *copy = (char *)malloc((size_t)len + 1);
    if (copy) {
        memcpy(copy, other->mBuffer, len);
        copy[len] = '\0';
    }
    self->mBuffer = copy ? copy : "";
    self->mSize = len;
    self->mOwnsBuffer = copy ? 1 : 0;
}

extern void hidl_string_dtor(hidl_string_t *self)
    __asm__("_ZN7android8hardware11hidl_stringD1Ev");
void hidl_string_dtor(hidl_string_t *self) {
    if (self && self->mOwnsBuffer && self->mBuffer) {
        free((void *)self->mBuffer);
    }
    if (self) memset(self, 0, sizeof(*self));
}

extern void return_status_dtor(return_status_t *self)
    __asm__("_ZN7android8hardware7details13return_statusD2Ev");
void return_status_dtor(return_status_t *self) {
    (void)self;
}
