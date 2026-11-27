#ifndef _BT_CUTILS_PROPERTIES_H
#define _BT_CUTILS_PROPERTIES_H
#define PROPERTY_KEY_MAX   32
#define PROPERTY_VALUE_MAX 92
extern int property_get(const char *key, char *value, const char *default_value);
extern int property_set(const char *key, const char *value);
#endif
