#!/bin/bash
set -e
UNP=/mnt/d/p7_live/stock/panther/unpacked
echo "=== First 16 bytes of init_boot/ramdisk and boot/kernel ==="
xxd -l 16 "$UNP/init_boot/ramdisk" 2>/dev/null || od -tx1 -N 16 "$UNP/init_boot/ramdisk"
od -tx1 -N 16 "$UNP/boot/kernel"

echo "=== Largest modules in vk_root ==="
du -sh "$UNP/vk_root/lib/modules/"*.ko | sort -hr | head -n 25
echo "Total vk_root modules:"
du -sh "$UNP/vk_root/lib/modules"
