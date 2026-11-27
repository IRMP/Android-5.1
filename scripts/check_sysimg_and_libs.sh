#!/bin/bash
set -e
S=/mnt/d/p7_live/stock/volantis/system.raw.img
debugfs -R "ls -l /lib64" "$S" | grep -E "libETC1|libhardware|libpixelflinger|libui|libutils|libcutils|liblog|libstdc\+\+"
echo "=== Check Google Android 22 arm64 sysimg ==="
curl -sL "https://dl.google.com/android/repository/sys-img/android/sys-img2-1.xml" | grep -E "arm64-v8a-22|22.*arm64" -B 5 -A 15 | head -n 45 || true
