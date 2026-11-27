#!/bin/bash
set -e
ROOT=/mnt/d/p6p_live
OUT=$ROOT/out/final
UNPACK=$ROOT/tools/mkbootimg/unpack_bootimg.py
VWORK=$ROOT/work_verify

rm -rf "$VWORK"
mkdir -p "$VWORK"

echo "=== 1. Verify boot & vendor_boot images via unpack_bootimg.py ==="
for img in p6p_live_boot_v3.img p6p_live_boot_v4_ramdisk.img p6p_live_boot_v4.img p6p_live_vendor_boot_v4.img; do
  echo "--- $img ---"
  mkdir -p "$VWORK/$img"
  python3 "$UNPACK" --boot_img "$OUT/$img" --out "$VWORK/$img" --format mkbootimg
done

echo "=== 2. Verify LZ4 streams & decompressed ext4 integrity ==="
lz4 -t "$OUT/p6p_live_ramdisk.lz4"
lz4 -t "$OUT/p6p_a5_system.ext4.lz4"
lz4 -dc "$OUT/p6p_a5_system.ext4.lz4" > "$VWORK/sys_check.ext4"
e2fsck -fn "$VWORK/sys_check.ext4"

echo "=== 3. Verify stripped vs kept apps & HALs in p6p_a5_system.ext4 ==="
debugfs -R "ls -l /app" "$VWORK/sys_check.ext4"
debugfs -R "ls -l /priv-app" "$VWORK/sys_check.ext4"
debugfs -R "ls -l /lib64/hw" "$VWORK/sys_check.ext4"
debugfs -R "cat /build.prop" "$VWORK/sys_check.ext4" | tail -n 25

echo "=== 4. Clean large factory zip & unneeded intermediates on D:\p6p_live ==="
rm -rf "$VWORK" "$ROOT/work_sys" "$ROOT/stock/raven/vendor.img" "$ROOT/stock/raven/vendor.raw.img" \
       "$ROOT/stock/raven-tq3a.230901.001.c2-factory-e7e2cf2e.zip"

echo "=== 5. Final artifact sizes & disk usage ==="
ls -lh "$OUT"
ls -lh "$ROOT/out/hal"
du -sh "$ROOT"/* "$ROOT"
