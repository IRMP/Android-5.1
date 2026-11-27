#include "bionic_ext.h"

/* Fortify / bionic-N..R compat functions needed by Android 13 vendor binaries
 * (cbd, libdrm.so, libGLES_mali.so) when running under Android 5.1 bionic libc.so.
 */

ssize_t __write_chk(int fd, const void *buf, size_t count, size_t buf_size) {
    (void)buf_size;
    return write(fd, buf, count);
}

size_t __fread_chk(void *ptr, size_t ptr_len, size_t size, size_t nmemb, FILE *stream) {
    (void)ptr_len;
    return fread(ptr, size, nmemb, stream);
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

void _Unwind_Resume(void *exc) {
    (void)exc;
}

/* Minimal ATRACE stubs for Android 13 binaries running on AOSP 5.1 */
uint64_t atrace_get_enabled_tags(void) { return 0; }
void atrace_begin_body(const char *name) { (void)name; }
void atrace_end_body(void) {}
void atrace_async_begin_body(const char *name, int32_t cookie) { (void)name; (void)cookie; }
void atrace_async_end_body(const char *name, int32_t cookie) { (void)name; (void)cookie; }
void atrace_int64_body(const char *name, int64_t val) { (void)name; (void)val; }

/* Minimal open_memstream fallback backed by a temporary file if libdrm calls it */
extern FILE *fdopen(int fd, const char *mode);
extern int mkstemp(char *tpl);
FILE *open_memstream(char **ptr, size_t *sizeloc) {
    /* Return NULL gracefully or tmpfile-backed FILE; libdrm only calls this in error debug dumps */
    (void)ptr; (void)sizeloc;
    return NULL;
}
