#!/bin/bash
set -e
EDIR=/mnt/d/p7_live/stock/aosp51_emu
mkdir -p "$EDIR"
cd "$EDIR"
if [ ! -f arm64-v8a-22_r02.zip ]; then
  curl -L -C - -o arm64-v8a-22_r02.zip "https://dl.google.com/android/repository/sys-img/android/arm64-v8a-22_r02.zip"
fi
unzip -l arm64-v8a-22_r02.zip
unzip -o -j arm64-v8a-22_r02.zip "arm64-v8a/system.img" "arm64-v8a/ramdisk.img" -d "$EDIR" 2>/dev/null || \
  unzip -o -j arm64-v8a-22_r02.zip "*system.img" "*ramdisk.img" -d "$EDIR"
rm -f arm64-v8a-22_r02.zip
file "$EDIR/system.img"
/mnt/d/p7_live/tools/simg2img "$EDIR/system.img" "$EDIR/system.raw.img" || mv "$EDIR/system.img" "$EDIR/system.raw.img"
rm -f "$EDIR/system.img"
debugfs -R "ls -l /lib64/egl" "$EDIR/system.raw.img"
debugfs -R "ls -l /lib64/hw" "$EDIR/system.raw.img"
debugfs -R "ls -l /app" "$EDIR/system.raw.img"
