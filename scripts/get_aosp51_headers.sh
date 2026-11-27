#!/bin/bash
set -e
echo "=== Check squashfs in kernel ==="
lz4 -dc /mnt/d/p7_live/stock/panther/unpacked/boot/kernel | strings | grep -i "squashfs" | head -n 20 || true

echo "=== Clone AOSP 5.1 libhardware & system/core headers ==="
mkdir -p /mnt/d/p7_live/tools/aosp51
cd /mnt/d/p7_live/tools/aosp51
if [ ! -d libhardware ]; then
  git clone --depth=1 --branch lollipop-mr1-release https://android.googlesource.com/platform/hardware/libhardware
fi
if [ ! -d system_core ]; then
  git clone --depth=1 --branch lollipop-mr1-release --filter=blob:none --sparse https://android.googlesource.com/platform/system/core
  (cd system_core && git sparse-checkout set include)
fi
ls -la libhardware/include/hardware system_core/include/system system_core/include/cutils
