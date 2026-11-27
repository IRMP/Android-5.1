#ifndef _BT_UTILS_LOG_H
#define _BT_UTILS_LOG_H
#include "bionic_ext.h"
#include <signal.h>
#include <time.h>
#ifndef LOG_TAG
#define LOG_TAG "bt_vendor"
#endif
#define ALOGV(...) __android_log_print(2, LOG_TAG, __VA_ARGS__)
#define ALOGD(...) __android_log_print(3, LOG_TAG, __VA_ARGS__)
extern int sprintf(char *str, const char *format, ...);
extern int asprintf(char **strp, const char *fmt, ...);
extern char *strtok(char *str, const char *delim);
#endif
