#!/bin/bash
set -e
UNP=/mnt/d/p7_live/stock/panther/unpacked
for p in boot init_boot vendor_boot vendor_kernel_boot; do
  echo "=== $p ==="
  ls -lh "$UNP/$p"
done

mkdir -p "$UNP/ramdisk_init" "$UNP/ramdisk_vendor" "$UNP/ramdisk_vkernel"
echo "=== File types of ramdisks ==="
file "$UNP/init_boot/ramdisk" "$UNP/vendor_boot/vendor_ramdisk"* "$UNP/vendor_kernel_boot/vendor_ramdisk"* 2>/dev/null || true

echo "=== vendor_boot bootconfig ==="
cat "$UNP/vendor_boot/bootconfig" 2>/dev/null || true

echo "=== Extract vendor_kernel_boot ramdisk list ==="
for r in "$UNP/vendor_kernel_boot/vendor_ramdisk"*; do
  [ -f "$r" ] || continue
  lz4 -dc "$r" 2>/dev/null | cpio -t 2>/dev/null | head -n 80 || \
  gzip -dc "$r" 2>/dev/null | cpio -t 2>/dev/null | head -n 80 || true
done

echo "=== Extract vendor_boot ramdisk list ==="
for r in "$UNP/vendor_boot/vendor_ramdisk"*; do
  [ -f "$r" ] || continue
  lz4 -dc "$r" 2>/dev/null | cpio -t 2>/dev/null | head -n 80 || \
  gzip -dc "$r" 2>/dev/null | cpio -t 2>/dev/null | head -n 80 || true
done

echo "=== Extract init_boot ramdisk list ==="
if [ -f "$UNP/init_boot/ramdisk" ]; then
  lz4 -dc "$UNP/init_boot/ramdisk" 2>/dev/null | cpio -t 2>/dev/null | head -n 80 || \
  gzip -dc "$UNP/init_boot/ramdisk" 2>/dev/null | cpio -t 2>/dev/null | head -n 80 || true
fi
