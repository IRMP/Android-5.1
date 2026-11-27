#ifndef _BIONIC_COMPAT_H
#define _BIONIC_COMPAT_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <sys/types.h>

#define O_RDONLY    00000000
#define O_WRONLY    00000001
#define O_RDWR      00000002
#define O_CLOEXEC   02000000

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define MAP_SHARED  0x01
#define MAP_FAILED  ((void *)-1)

#define MS_SYNC     4
#define CLOCK_MONOTONIC 1
#define TIMER_ABSTIME   1

#define EINVAL 22
#define ENOMEM 12
#define EINTR  4
#define ENODEV 19

#define POLLIN 0x0001

#define ANDROID_LOG_INFO  4
#define ANDROID_LOG_WARN  5
#define ANDROID_LOG_ERROR 6

struct timespec {
    long tv_sec;
    long tv_nsec;
};

struct pollfd {
    int fd;
    short events;
    short revents;
};

struct dirent {
    uint64_t d_ino;
    int64_t  d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[256];
};
typedef void DIR;

typedef long pthread_t;
typedef struct {
    int volatile value;
    char __reserved[36];
} pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER {0}

/* Bionic libc.so */
extern volatile int *__errno(void);
#define errno (*__errno())
extern int open(const char *pathname, int flags, ...);
extern int close(int fd);
extern ssize_t read(int fd, void *buf, size_t count);
extern ssize_t write(int fd, const void *buf, size_t count);
extern int ioctl(int fd, int request, ...);
extern void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
extern int munmap(void *addr, size_t length);
extern int poll(struct pollfd *fds, unsigned long nfds, int timeout);
extern pid_t getpid(void);
extern int clock_gettime(int clk_id, struct timespec *tp);
extern int clock_nanosleep(int clk_id, int flags, const struct timespec *req, struct timespec *rem);
extern int nanosleep(const struct timespec *req, struct timespec *rem);
extern void *malloc(size_t size);
extern void *calloc(size_t nmemb, size_t size);
extern void free(void *ptr);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);
extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, size_t n);
extern int snprintf(char *str, size_t size, const char *format, ...);
extern long strtol(const char *nptr, char **endptr, int base);
extern DIR *opendir(const char *name);
extern struct dirent *readdir(DIR *dirp);
extern int closedir(DIR *dirp);
extern int pthread_create(pthread_t *thread, const void *attr, void *(*start_routine)(void *), void *arg);
extern int pthread_join(pthread_t thread, void **retval);
extern int pthread_mutex_lock(pthread_mutex_t *mutex);
extern int pthread_mutex_unlock(pthread_mutex_t *mutex);

/* liblog.so */
extern int __android_log_print(int prio, const char *tag, const char *fmt, ...);
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* libcutils.so */
extern int ashmem_create_region(const char *name, size_t size);

#endif
