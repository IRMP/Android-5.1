#!/bin/bash
set -e
mkdir -p /mnt/d/p6p_live/tools/mkbootimg
cp -f /mnt/d/p7_live/tools/{busybox-aarch64,simg2img,mini_adbd,drm_test} /mnt/d/p6p_live/tools/
cp -f /mnt/d/p7_live/tools/mkbootimg/*.py /mnt/d/p6p_live/tools/mkbootimg/ 2>/dev/null || true

SYS=/mnt/d/p7_live/stock/aosp51_emu/system.raw.img
for d in / /etc /usr /usr/idc /usr/keylayout /lib64/hw; do
  echo "=== $d ==="
  debugfs -R "ls -l $d" "$SYS" 2>/dev/null || echo "(missing)"
done
echo "=== /etc/permissions ==="
debugfs -R "ls -l /etc/permissions" "$SYS" 2>/dev/null || true
