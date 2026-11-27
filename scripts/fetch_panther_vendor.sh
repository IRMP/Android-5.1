#!/bin/bash
set -e
STOCK=/mnt/d/p7_live/stock
OUT=$STOCK/panther
mkdir -p "$STOCK/tmp"

ZIP=$STOCK/panther-tq3a.230901.001.c2-factory-51bde7e8.zip
if [ ! -f "$ZIP" ]; then
  curl -L -C - -o "$ZIP" "https://dl.google.com/dl/android/aosp/panther-tq3a.230901.001.c2-factory-51bde7e8.zip"
fi

INNER_ZIP=$(unzip -l "$ZIP" | awk '/image-panther-.*\.zip$/ {print $4}' | head -n 1)
RADIO=$(unzip -l "$ZIP" | awk '/radio-panther-.*\.img$/ {print $4}' | head -n 1)
unzip -o -j "$ZIP" "$INNER_ZIP" "$RADIO" -d "$STOCK/tmp"
mv "$STOCK/tmp/$(basename "$RADIO")" "$OUT/radio.img"

IZIP="$STOCK/tmp/$(basename "$INNER_ZIP")"
unzip -o -j "$IZIP" vendor.img -d "$OUT"
rm -rf "$STOCK/tmp" "$ZIP"

/mnt/d/p7_live/tools/simg2img "$OUT/vendor.img" "$OUT/vendor.raw.img" || mv "$OUT/vendor.img" "$OUT/vendor.raw.img"
rm -f "$OUT/vendor.img"
file "$OUT/vendor.raw.img" "$OUT/radio.img"
ls -lh "$OUT"
du -sh /mnt/d/p7_live
