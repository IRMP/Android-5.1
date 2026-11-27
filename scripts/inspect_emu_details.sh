#!/bin/bash
set -e
E=/mnt/d/p7_live/stock/aosp51_emu
echo "=== egl.cfg ==="
debugfs -R "cat /lib64/egl/egl.cfg" "$E/system.raw.img" 2>&1 || true
echo "=== aosp51_emu ramdisk files ==="
ls -la "$E/ramdisk_unp"
echo "=== aosp51_emu init binary ==="
file "$E/ramdisk_unp/init"
strings "$E/ramdisk_unp/init" | grep -E "selinux|sepolicy|enforcing|permissive" | head -n 15
echo "=== Check /lib64/hw in aosp51_emu ==="
debugfs -R "ls -l /lib64/hw" "$E/system.raw.img"
echo "=== Check /bin in aosp51_emu ==="
debugfs -R "ls -l /bin" "$E/system.raw.img" | grep -E "surfaceflinger|servicemanager|app_process|mediaserver|drmserver|netd|vold|installd|bootanimation|rild" || true
echo "=== Check total size inside aosp51_emu system.raw.img ==="
debugfs -R "stat /" "$E/system.raw.img" | head -n 10
df -h /mnt/d/p7_live
