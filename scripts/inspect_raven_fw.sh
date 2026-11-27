#!/bin/bash
set -e
R=/mnt/d/p6p_live/stock/raven
TMP=/mnt/d/p6p_live/work_inspect
mkdir -p "$TMP"

echo "=== vendor_dlkm modules.load ==="
debugfs -R "cat /lib/modules/modules.load" "$R/vendor_dlkm.raw.img" 2>/dev/null
echo "=== vendor_dlkm modules.dep (touch / panel / bcmdhd / usb) ==="
debugfs -R "cat /lib/modules/modules.dep" "$R/vendor_dlkm.raw.img" 2>/dev/null | grep -E "ftm5|sec_touch|bcmdhd|s6e3hc3|exynos-drm|dwc3|rndis|acme|devfreq"

echo "=== Firmware filenames inside ftm5.ko & sec_touch.ko & bcmdhd4389.ko ==="
for m in ftm5.ko sec_touch.ko bcmdhd4389.ko; do
  debugfs -R "dump /lib/modules/$m $TMP/$m" "$R/vendor_dlkm.raw.img" >/dev/null 2>&1 || true
  echo "--- $m firmware refs ---"
  strings "$TMP/$m" | grep -E "\.(bin|fw|cal|blob|mem|hex|ftb)" | sort -u | head -n 40
done

echo "=== vendor.raw.img /firmware/brcm & touch firmware check ==="
debugfs -R "ls -l /firmware/brcm" "$R/vendor.raw.img" 2>/dev/null || true
debugfs -R "ls -l /firmware" "$R/vendor.raw.img" 2>/dev/null | grep -iE "ftm|ftb|s6s|sec|touch|bcmdhd|4389|cy" || true
rm -rf "$TMP"
