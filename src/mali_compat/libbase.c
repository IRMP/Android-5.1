#include "bionic_ext.h"

/* Minimal android::base::CachedProperty C++ ABI shim */
struct CachedPropertyLayout {
    const char *prop_name;
    void *serial;
    char value[92];
};

extern void CachedProperty_ctor(struct CachedPropertyLayout *self, const char *name)
    __asm__("_ZN7android4base14CachedPropertyC1EPKc");
void CachedProperty_ctor(struct CachedPropertyLayout *self, const char *name) {
    memset(self, 0, sizeof(*self));
    self->prop_name = name;
}

extern const char *CachedProperty_Get(struct CachedPropertyLayout *self, uint8_t *changed)
    __asm__("_ZN7android4base14CachedProperty3GetEPb");
const char *CachedProperty_Get(struct CachedPropertyLayout *self, uint8_t *changed) {
    if (changed) *changed = 0;
    self->value[0] = '\0';
    if (self->prop_name) {
        property_get(self->prop_name, self->value, "");
    }
    return self->value;
}
