#!/bin/bash
set -e
UNP=/mnt/d/p7_live/stock/panther/unpacked
OUT=/mnt/d/p7_live/stock/panther

echo "=== Kernel version string ==="
strings "$UNP/boot/kernel" | grep -E "^Linux version [0-9]" | head -n 5

echo "=== Check mkbootimg args for boot, init_boot, vendor_boot, vendor_kernel_boot ==="
ls -la "$UNP/boot" "$UNP/init_boot" "$UNP/vendor_boot" "$UNP/vendor_kernel_boot"

echo "=== Full list of vendor_kernel_boot modules ==="
mkdir -p "$UNP/vk_root"
(cd "$UNP/vk_root" && lz4 -dc "$UNP/vendor_kernel_boot/vendor_ramdisk00" | cpio -idm 2>/dev/null)
find "$UNP/vk_root/lib/modules" -maxdepth 3 | sort

echo "=== Check modules.load in vendor_kernel_boot ==="
cat "$UNP/vk_root/lib/modules/modules.load"* 2>/dev/null || find "$UNP/vk_root" -name "modules*" -exec head -n 100 {} +

echo "=== Convert and inspect vendor_dlkm.img ==="
file "$OUT/vendor_dlkm.img"
/mnt/d/p7_live/tools/simg2img "$OUT/vendor_dlkm.img" "$OUT/vendor_dlkm.raw.img" || cp "$OUT/vendor_dlkm.img" "$OUT/vendor_dlkm.raw.img"
file "$OUT/vendor_dlkm.raw.img"
debugfs -R "ls -l /lib/modules" "$OUT/vendor_dlkm.raw.img" 2>/dev/null || debugfs -R "ls -l /" "$OUT/vendor_dlkm.raw.img" 2>/dev/null || true
