#!/bin/bash
set -e
R=/mnt/d/p6p_live/stock/raven
FW=/mnt/d/p6p_live/stock/raven/firmware
SYS=/mnt/d/p7_live/stock/aosp51_emu/system.raw.img
mkdir -p "$FW"

for f in s6sy79x.bin fw_bcmdhd.bin bcmdhd.cal bcmdhd_clm.blob bcmdhd.cal_ROW bcmdhd_clm.blob_ROW; do
  debugfs -R "dump /firmware/$f $FW/$f" "$R/vendor.raw.img" >/dev/null 2>&1 || true
done
ls -lh "$FW"

echo "=== Check wpa_supplicant & wifi bins in AOSP 5.1 ==="
debugfs -R "ls -l /bin" "$SYS" | grep -E "wpa|dhcp|ndc|netd|hostapd|iw" || true
debugfs -R "ls -l /etc/wifi" "$SYS" 2>/dev/null || echo "no /etc/wifi in aosp51_emu"
echo "=== Check mkbootimg.py vendor_boot args ==="
python3 /mnt/d/p6p_live/tools/mkbootimg/mkbootimg.py --help | grep -A 30 "vendor_boot" || true
