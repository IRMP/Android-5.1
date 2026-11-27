#!/bin/bash
set -e
UNP=/mnt/d/p7_live/stock/panther/unpacked
OUT=/mnt/d/p7_live/stock/panther

echo "=== vendor_boot firmware & modules ==="
ls -la "$UNP/vb_root/lib/firmware" 2>/dev/null || true
debugfs -R "ls -l /" "$OUT/vendor_dlkm.raw.img" 2>/dev/null || true
debugfs -R "cat /lib/modules/modules.dep" "$OUT/vendor_dlkm.raw.img" 2>/dev/null | grep -E "touch|drm|panel|dwc3|usb|focal|sec_|syna|mali" || true

echo "=== vendor_kernel_boot modules.dep (display/usb/touch/wdt/ufs) ==="
grep -E "drm|panel|dsim|dwc3|usb|tcpci|max77759|touch|ufs|wdt|softdog|s2mpg12|clk_exynos|pinctrl|i2c" "$UNP/vk_root/lib/modules/modules.dep" || true

echo "=== Check mkbootimg args from unpack_bootimg ==="
python3 /mnt/d/p7_live/tools/mkbootimg/unpack_bootimg.py --boot_img "$OUT/boot.img" --out /tmp/chk_b --format mkbootimg
echo ""
python3 /mnt/d/p7_live/tools/mkbootimg/unpack_bootimg.py --boot_img "$OUT/init_boot.img" --out /tmp/chk_ib --format mkbootimg
echo ""
python3 /mnt/d/p7_live/tools/mkbootimg/unpack_bootimg.py --boot_img "$OUT/vendor_boot.img" --out /tmp/chk_vb --format mkbootimg
echo ""
python3 /mnt/d/p7_live/tools/mkbootimg/unpack_bootimg.py --boot_img "$OUT/vendor_kernel_boot.img" --out /tmp/chk_vkb --format mkbootimg
echo ""
rm -rf /tmp/chk_b /tmp/chk_ib /tmp/chk_vb /tmp/chk_vkb
