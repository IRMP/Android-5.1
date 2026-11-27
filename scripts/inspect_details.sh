#!/bin/bash
set -e
UNP=/mnt/d/p7_live/stock/panther/unpacked
OUT=/mnt/d/p7_live/stock/panther

echo "=== GKI kernel version ==="
lz4 -dc "$UNP/boot/kernel" | strings | grep -E "^Linux version [0-9]" | head -n 5

echo "=== UDC / USB / display / touch strings in dtb ==="
strings "$UNP/vendor_kernel_boot/dtb" | grep -E "dwc3|usb|panel|dsi|drm|touch|focal|sec_ts|syna|ftm5" | sort -u | head -n 60

echo "=== vendor_dlkm modules.load ==="
debugfs -R "cat /lib/modules/modules.load" "$OUT/vendor_dlkm.raw.img" 2>/dev/null || true

echo "=== vendor_boot init.recovery files ==="
mkdir -p "$UNP/vb_root"
(cd "$UNP/vb_root" && lz4 -dc "$UNP/vendor_boot/vendor_ramdisk00" | cpio -idm "*init*" "*default.prop*" "*prop.default*" 2>/dev/null || true)
cat "$UNP/vb_root/init.recovery.gs201.rc" "$UNP/vb_root/init.recovery.panther.rc" 2>/dev/null || true
