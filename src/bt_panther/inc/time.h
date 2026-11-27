#ifndef _BT_TIME_H
#define _BT_TIME_H
#include "bionic_ext.h"
#include <signal.h>

struct itimerspec {
    struct timespec it_interval;
    struct timespec it_value;
};

extern int timer_create(int clockid, struct sigevent *sevp, timer_t *timerid);
extern int timer_settime(timer_t timerid, int flags, const struct itimerspec *new_value, struct itimerspec *old_value);
extern int timer_delete(timer_t timerid);
#endif
