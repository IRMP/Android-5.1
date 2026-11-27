#!/bin/bash
set -e
echo "=== Check sizes ==="
curl -sIL "https://dl.google.com/dl/android/aosp/panther-tq3a.230901.001.c2-factory-51bde7e8.zip" | grep -i content-length
curl -sIL "https://dl.google.com/dl/android/aosp/volantis-lmy48t-factory-b925de62.zip" | grep -i content-length

echo "=== Clone mkbootimg ==="
if [ ! -d /mnt/d/p7_live/tools/mkbootimg ]; then
  git clone --depth=1 https://android.googlesource.com/platform/system/tools/mkbootimg /mnt/d/p7_live/tools/mkbootimg
fi
ls -la /mnt/d/p7_live/tools/mkbootimg
