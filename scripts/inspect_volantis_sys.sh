#!/bin/bash
set -e
S=/mnt/d/p7_live/stock/volantis/system.raw.img
echo "=== /lib/egl ==="
debugfs -R "ls -l /lib/egl" "$S" 2>&1 || true
echo "=== /lib64 GLES/EGL/gralloc/surfaceflinger ==="
debugfs -R "ls -l /lib64" "$S" | grep -iE "gles|egl|gralloc|surface|gui|ui|hwui|skia|pixelflinger|binder|cutils|log|utils|c\.so|m\.so|dl\.so" || true
echo "=== /lib64/libGLES_android.so? ==="
debugfs -R "ls -l /lib/libGLES_android.so" "$S" 2>&1 || true
debugfs -R "ls -l /lib64/libGLES_android.so" "$S" 2>&1 || true
echo "=== /vendor symlink target ==="
debugfs -R "stat /vendor" "$S"
echo "=== /bin ==="
debugfs -R "ls -l /bin" "$S" | grep -E "surfaceflinger|servicemanager|app_process|mediaserver|drmserver|netd|vold|installd|bootanimation|sh|toolbox|logcat|dumpsys|service|am|pm" || true
