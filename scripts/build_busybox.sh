#!/bin/bash
set -e
echo "=== Checking headers ==="
ls -d /usr/include/drm /usr/include/linux/usb/functionfs.h /usr/aarch64-linux-gnu/include/linux/usb/functionfs.h 2>&1 || true

cd /mnt/d/p7_live/tools/src
if [ ! -d busybox-1.36.1 ]; then
  tar -xjf busybox-1.36.1.tar.bz2
fi
cd busybox-1.36.1
if [ ! -f /mnt/d/p7_live/tools/busybox-aarch64 ]; then
  make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
  sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config
  # Disable tc/sha1/etc if kernel headers mismatch on 22.04 (CBQ removed in newer kernels, harmless on 5.15 headers)
  sed -i 's/CONFIG_TC=y/# CONFIG_TC is not set/' .config
  make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc)
  cp busybox /mnt/d/p7_live/tools/busybox-aarch64
  aarch64-linux-gnu-strip /mnt/d/p7_live/tools/busybox-aarch64
fi
file /mnt/d/p7_live/tools/busybox-aarch64
ls -lh /mnt/d/p7_live/tools/busybox-aarch64
