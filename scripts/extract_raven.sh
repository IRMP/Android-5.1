#!/bin/bash
set -e
ROOT=/mnt/d/p6p_live
STOCK=$ROOT/stock
OUT=$STOCK/raven
TOOLS=$ROOT/tools
UNPACK=$TOOLS/mkbootimg/unpack_bootimg.py
mkdir -p "$OUT" "$STOCK/tmp"

ZIP=$STOCK/raven-tq3a.230901.001.c2-factory-e7e2cf2e.zip
echo "=== Verifying zip integrity & listing ==="
unzip -l "$ZIP"

INNER_ZIP=$(unzip -l "$ZIP" | awk '/image-raven-.*\.zip$/ {print $4}' | head -n 1)
echo "=== Extracting inner zip: $INNER_ZIP ==="
unzip -o -j "$ZIP" "$INNER_ZIP" -d "$STOCK/tmp"

IZIP="$STOCK/tmp/$(basename "$INNER_ZIP")"
echo "=== Inner zip contents ==="
unzip -l "$IZIP"

for img in boot.img init_boot.img vendor_boot.img vendor_kernel_boot.img dtbo.img vbmeta.img vendor_dlkm.img super_empty.img vendor.img; do
  unzip -o -j "$IZIP" "$img" -d "$OUT" 2>/dev/null || echo "Not in inner zip: $img"
done

rm -rf "$STOCK/tmp"
ls -lh "$OUT"

if [ -f "$OUT/vendor_dlkm.img" ]; then
  echo "=== Converting vendor_dlkm.img ==="
  "$TOOLS/simg2img" "$OUT/vendor_dlkm.img" "$OUT/vendor_dlkm.raw.img" 2>/dev/null || cp "$OUT/vendor_dlkm.img" "$OUT/vendor_dlkm.raw.img"
  ls -lh "$OUT/vendor_dlkm.raw.img"
  debugfs -R "ls -l /lib/modules" "$OUT/vendor_dlkm.raw.img" 2>/dev/null || true
fi

echo "=== Unpacking raven boot images ==="
for p in boot init_boot vendor_boot vendor_kernel_boot; do
  if [ -f "$OUT/${p}.img" ]; then
    echo "--- $p (mkbootimg format args) ---"
    mkdir -p "$OUT/unpacked/$p"
    python3 "$UNPACK" --boot_img "$OUT/${p}.img" --out "$OUT/unpacked/$p" --format mkbootimg
    ls -lh "$OUT/unpacked/$p"
  fi
done
