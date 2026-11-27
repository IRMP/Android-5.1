#!/bin/bash
set -e
VDIR=/mnt/d/p7_live/stock/volantis
ZIP=$VDIR/volantis-lmy48t-factory-b925de62.zip

echo "=== Outer volantis zip ==="
unzip -l "$ZIP"
INNER=$(unzip -l "$ZIP" | awk '/image-volantis-.*\.zip$/ {print $4}' | head -n 1)
mkdir -p "$VDIR/tmp" "$VDIR/unpacked_boot" "$VDIR/boot_ramdisk"
unzip -o -j "$ZIP" "$INNER" -d "$VDIR/tmp"
IZIP="$VDIR/tmp/$(basename "$INNER")"
echo "=== Inner volantis zip ==="
unzip -l "$IZIP"
unzip -o -j "$IZIP" boot.img system.img -d "$VDIR"
unzip -o -j "$IZIP" vendor.img -d "$VDIR" 2>/dev/null || true
rm -rf "$VDIR/tmp" "$ZIP"

echo "=== Unpack volantis boot.img ==="
python3 /mnt/d/p7_live/tools/mkbootimg/unpack_bootimg.py --boot_img "$VDIR/boot.img" --out "$VDIR/unpacked_boot"
(cd "$VDIR/boot_ramdisk" && gzip -dc "$VDIR/unpacked_boot/ramdisk" | cpio -idm 2>/dev/null)
ls -la "$VDIR/boot_ramdisk"

echo "=== Convert volantis system.img ==="
/mnt/d/p7_live/tools/simg2img "$VDIR/system.img" "$VDIR/system.raw.img" || mv "$VDIR/system.img" "$VDIR/system.raw.img"
rm -f "$VDIR/system.img"
file "$VDIR/system.raw.img"
ls -lh "$VDIR"

echo "=== Inspect volantis system.raw.img top & hw/egl ==="
debugfs -R "ls -l /" "$VDIR/system.raw.img"
debugfs -R "ls -l /lib64/egl" "$VDIR/system.raw.img"
debugfs -R "ls -l /lib64/hw" "$VDIR/system.raw.img"
debugfs -R "ls -l /app" "$VDIR/system.raw.img"
debugfs -R "ls -l /priv-app" "$VDIR/system.raw.img"
