#!/bin/bash
set -e
R=/mnt/d/p6p_live/stock/raven
U=$R/unpacked/vendor_boot
mkdir -p "$U/vr0" "$U/vr1"

echo "=== bootconfig ==="
cat "$U/bootconfig"
echo ""

echo "=== Unpack vendor_ramdisk00 ==="
file "$U/vendor_ramdisk00" "$U/vendor_ramdisk01"
(cd "$U/vr0" && (lz4 -dc "$U/vendor_ramdisk00" 2>/dev/null || gzip -dc "$U/vendor_ramdisk00" 2>/dev/null || cat "$U/vendor_ramdisk00") | cpio -idm 2>/dev/null)
ls -la "$U/vr0"
ls -la "$U/vr0/lib/modules" 2>/dev/null || true

echo "=== Unpack vendor_ramdisk01 ==="
(cd "$U/vr1" && (lz4 -dc "$U/vendor_ramdisk01" 2>/dev/null || gzip -dc "$U/vendor_ramdisk01" 2>/dev/null || cat "$U/vendor_ramdisk01") | cpio -idm 2>/dev/null)
ls -la "$U/vr1" "$U/vr1/lib/modules" 2>/dev/null || true
if [ -f "$U/vr1/lib/modules/modules.load" ]; then
  echo "=== vr1 modules.load ==="
  cat "$U/vr1/lib/modules/modules.load"
fi

echo "=== DTB UDC / touch / display strings ==="
strings "$U/dtb" | grep -E "dwc3|backlight|s6e3|sec_touch|ftm5|goodix|goog_touch|panel" | sort -u | head -n 60

echo "=== vendor.img conversion & firmware check ==="
/mnt/d/p6p_live/tools/simg2img "$R/vendor.img" "$R/vendor.raw.img" 2>/dev/null || cp "$R/vendor.img" "$R/vendor.raw.img"
debugfs -R "ls -l /firmware" "$R/vendor.raw.img" 2>/dev/null || true
debugfs -R "ls -l /etc/vintf" "$R/vendor.raw.img" 2>/dev/null || true
debugfs -R "ls -l /lib/modules" "$R/vendor_dlkm.raw.img" 2>/dev/null | grep -E "touch|ftm|sec|bcmdhd|panel|exynos|drm|usb|rndis|udc|dwc3|devfreq|acme|wdt|dog" || true
