#ifndef _BT_SIGNAL_H
#define _BT_SIGNAL_H
#include <stdint.h>
typedef int sig_atomic_t;
typedef void *timer_t;
#define SIGTERM 15
#define SIGKILL 9
#define SIGUSR1 10
#define SIGALRM 14
#define SIGEV_SIGNAL 0
#define SIGEV_NONE   1
#define SIGEV_THREAD 2

union sigval {
    int sival_int;
    void *sival_ptr;
};

struct sigevent {
    union sigval sigev_value;
    int sigev_signo;
    int sigev_notify;
    void (*sigev_notify_function)(union sigval);
    void *sigev_notify_attributes;
    char __pad[32];
};

extern int kill(int pid, int sig);
#endif
