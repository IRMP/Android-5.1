#ifndef _BIONIC_EXT_H
#define _BIONIC_EXT_H

#include <stdbool.h>
#include "bionic_compat.h"

#ifndef EAGAIN
#define EAGAIN 11
#endif
#ifndef EPIPE
#define EPIPE 32
#endif
#ifndef ENOSYS
#define ENOSYS 38
#endif
#ifndef ENOENT
#define ENOENT 2
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 00004000
#endif
#ifndef O_CREAT
#define O_CREAT 00000100
#endif
#ifndef O_TRUNC
#define O_TRUNC 00001000
#endif

typedef struct __dirstream DIR;
struct dirent {
    uint64_t d_ino;
    int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[256];
};

typedef struct __sFILE FILE;
#define EOF (-1)

typedef union {
    char __size[48];
    long __align;
} pthread_cond_t;
#define PTHREAD_COND_INITIALIZER {{0}}

typedef unsigned int speed_t;
typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
#define NCCS 19
struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_line;
    cc_t c_cc[NCCS];
};

struct timeval {
    long tv_sec;
    long tv_usec;
};

static inline int ffs(int x) { return __builtin_ffs(x); }

extern void *realloc(void *ptr, size_t size);
extern size_t strlen(const char *s);
extern char *strcpy(char *dest, const char *src);
extern char *strncpy(char *dest, const char *src, size_t n);
extern char *strcat(char *dest, const char *src);
extern char *strncat(char *dest, const char *src, size_t n);
extern char *strchr(const char *s, int c);
extern char *strrchr(const char *s, int c);
extern char *strstr(const char *haystack, const char *needle);
extern char *strdup(const char *s);
extern int strcasecmp(const char *s1, const char *s2);
extern int strncasecmp(const char *s1, const char *s2, size_t n);
extern char *strtok_r(char *str, const char *delim, char **saveptr);
extern int atoi(const char *nptr);
extern long strtol(const char *nptr, char **endptr, int base);
extern unsigned long strtoul(const char *nptr, char **endptr, int base);
extern int vsnprintf(char *str, size_t size, const char *format, va_list ap);
extern int sscanf(const char *str, const char *format, ...);
extern void *memmove(void *dest, const void *src, size_t n);
extern int memcmp(const void *s1, const void *s2, size_t n);

extern FILE *fopen(const char *pathname, const char *mode);
extern int fclose(FILE *stream);
extern char *fgets(char *s, int size, FILE *stream);
extern size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
extern size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
extern int fseek(FILE *stream, long offset, int whence);
extern long ftell(FILE *stream);

extern DIR *opendir(const char *name);
extern struct dirent *readdir(DIR *dirp);
extern int closedir(DIR *dirp);

extern int access(const char *pathname, int mode);
extern int chmod(const char *pathname, mode_t mode);
extern int unlink(const char *pathname);
extern off_t lseek(int fd, off_t offset, int whence);
extern unsigned int sleep(unsigned int seconds);
extern int property_set(const char *key, const char *value);

extern int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr);
extern int pthread_mutex_destroy(pthread_mutex_t *mutex);
extern int pthread_cond_init(pthread_cond_t *cond, const void *attr);
extern int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
extern int pthread_cond_signal(pthread_cond_t *cond);
extern int pthread_cond_broadcast(pthread_cond_t *cond);

#endif
