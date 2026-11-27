#!/bin/bash
set -e
TMP=/mnt/d/p7_live/stock/volantis/chk_libs
debugfs -R "dump /lib64/libhardware.so $TMP/libhardware.so" /mnt/d/p7_live/stock/volantis/system.raw.img >/dev/null
echo "=== libhardware.so ==="
aarch64-linux-gnu-nm -D "$TMP/libhardware.so" | grep " T "
echo "=== liblog.so ==="
aarch64-linux-gnu-nm -D "$TMP/liblog.so" | grep -E " __android_log_print| __android_log_buf_write"
echo "=== libcutils.so ==="
aarch64-linux-gnu-nm -D "$TMP/libcutils.so" | grep -E "ashmem_|native_handle_|property_"
echo "=== libc.so key funcs ==="
aarch64-linux-gnu-nm -D "$TMP/libc.so" | grep -wE "open|close|read|write|ioctl|mmap|munmap|malloc|calloc|free|memset|memcpy|strcmp|strncmp|snprintf|strerror|usleep|sleep|nanosleep|clock_gettime|poll|pthread_create|pthread_mutex_lock|pthread_mutex_unlock|pthread_join|__errno"
