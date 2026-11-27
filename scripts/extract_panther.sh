#!/bin/bash
set -e
STOCK=/mnt/d/p7_live/stock
OUT=$STOCK/panther
mkdir -p "$OUT"

ZIP=$STOCK/panther-tq3a.230901.001.c2-factory-51bde7e8.zip
echo "=== Outer zip contents ==="
unzip -l "$ZIP"

INNER_ZIP=$(unzip -l "$ZIP" | awk '/image-panther-.*\.zip$/ {print $4}' | head -n 1)
echo "=== Inner zip: $INNER_ZIP ==="
mkdir -p "$STOCK/tmp"
unzip -o -j "$ZIP" "$INNER_ZIP" -d "$STOCK/tmp"

IZIP="$STOCK/tmp/$(basename "$INNER_ZIP")"
echo "=== Inner zip contents ==="
unzip -l "$IZIP"

# Extract boot-related images + vendor_dlkm if present
for img in boot.img init_boot.img vendor_boot.img vendor_kernel_boot.img dtbo.img vbmeta.img vendor_dlkm.img super_empty.img; do
  unzip -o -j "$IZIP" "$img" -d "$OUT" 2>/dev/null || echo "Skip missing: $img"
done

rm -rf "$STOCK/tmp"
ls -lh "$OUT"

echo "=== Unpacking boot images ==="
UNPACK=/mnt/d/p7_live/tools/mkbootimg/unpack_bootimg.py
for p in boot init_boot vendor_boot vendor_kernel_boot; do
  if [ -f "$OUT/${p}.img" ]; then
    echo "--- $p ---"
    mkdir -p "$OUT/unpacked/$p"
    python3 "$UNPACK" --boot_img "$OUT/${p}.img" --out "$OUT/unpacked/$p"
  fi
done
