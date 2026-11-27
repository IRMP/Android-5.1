#ifndef _BT_DLFCN_H
#define _BT_DLFCN_H
#define RTLD_LAZY   0x0001
#define RTLD_NOW    0x0000
#define RTLD_GLOBAL 0x0100
extern void *dlopen(const char *filename, int flags);
extern void *dlsym(void *handle, const char *symbol);
extern int dlclose(void *handle);
extern char *dlerror(void);
#endif
