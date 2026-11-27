#!/bin/bash
set -e
OUT=/mnt/d/p7_live/stock/panther
UNP=$OUT/unpacked
mkdir -p "$UNP/vdlkm_mod"

echo "=== Extract touch + regulator + deps from vendor_dlkm.raw.img ==="
debugfs -R "cat /lib/modules/modules.dep" "$OUT/vendor_dlkm.raw.img" | grep -E "touch|slg5100|vh_mm|pixel_em" || true

echo "=== Extract touch + drm modules temporarily to check firmware strings ==="
for m in goog_touch_interface.ko focal_touch.ko sec_touch.ko syna_touch.ko; do
  debugfs -R "dump /lib/modules/$m $UNP/vdlkm_mod/$m" "$OUT/vendor_dlkm.raw.img"
  echo "--- $m firmware strings ---"
  strings "$UNP/vdlkm_mod/$m" | grep -E "\.bin|\.fw|ftm|focal|sec_ts|syna" | head -n 15 || true
done
for m in exynos-drm.ko panel-samsung-s6e3fc3-p10.ko panel-samsung-s6e3fc3.ko panel-boe-nt37290.ko; do
  echo "--- $m firmware strings ---"
  strings "$UNP/vk_root/lib/modules/$m" | grep -E "\.bin|\.fw|firmware=" | head -n 10 || true
done
