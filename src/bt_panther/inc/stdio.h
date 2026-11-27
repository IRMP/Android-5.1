#ifndef _BT_STDIO_H
#define _BT_STDIO_H
#include "bionic_ext.h"
extern int sprintf(char *str, const char *format, ...);
extern int asprintf(char **strp, const char *fmt, ...);
extern char *strtok(char *str, const char *delim);
#endif
