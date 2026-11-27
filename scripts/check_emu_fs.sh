#!/bin/bash
set -e
E=/mnt/d/p7_live/stock/aosp51_emu
echo "=== Check aosp51_emu system.raw.img free space / sizes ==="
dumpe2fs -h "$E/system.raw.img" | grep -E "Block count|Free blocks|Block size"
echo "=== Check /bin/sh & /lib64/egl in aosp51_emu ==="
debugfs -R "ls -l /bin/sh" "$E/system.raw.img"
debugfs -R "ls -l /lib64/egl" "$E/system.raw.img"
debugfs -R "ls -l /etc/permissions" "$E/system.raw.img" | head -n 25
debugfs -R "ls -l /framework" "$E/system.raw.img" | head -n 30
debugfs -R "ls -l /usr/idc" "$E/system.raw.img" 2>&1 || true
