#!/bin/bash
set -e
S=/mnt/d/p7_live/stock/volantis/system.raw.img
TMP=/mnt/d/p7_live/stock/volantis/chk_libs
mkdir -p "$TMP"
for f in libEGL.so libsurfaceflinger.so hw/gralloc.default.so libbinder.so libcutils.so liblog.so libc.so libstdc++.so libui.so libutils.so libpixelflinger.so; do
  debugfs -R "dump /lib64/$f $TMP/$(basename $f)" "$S" >/dev/null
done
echo "=== libEGL.so egl/gles strings ==="
strings "$TMP/libEGL.so" | grep -E "egl\.cfg|libGLES_android|software|android" | head -n 30
echo "=== libsurfaceflinger.so GLES/hwc strings ==="
strings "$TMP/libsurfaceflinger.so" | grep -E "GLES|hwcomposer|gralloc|fb0|EGL" | head -n 30
echo "=== gralloc.default.so strings ==="
strings "$TMP/gralloc.default.so" | grep -E "ashmem|fb0|graphics|USAGE|error|failed" | head -n 30
echo "=== libc.so version/symbols sample ==="
aarch64-linux-gnu-nm -D "$TMP/libc.so" | grep -E " __errno$| memfd| mmap$| ioctl$| openat$| pthread_create$" || true
