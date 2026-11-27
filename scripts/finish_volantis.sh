#!/bin/bash
set -e
VDIR=/mnt/d/p7_live/stock/volantis

echo "=== Strip vendor header from volantis boot.img ==="
python3 - <<'PY'
data = open('/mnt/d/p7_live/stock/volantis/boot.img', 'rb').read()
off = data.find(b'ANDROID!')
print('Found ANDROID! at offset:', hex(off))
assert off >= 0
open('/mnt/d/p7_live/stock/volantis/boot_stripped.img', 'wb').write(data[off:])
PY

python3 /mnt/d/p7_live/tools/mkbootimg/unpack_bootimg.py --boot_img "$VDIR/boot_stripped.img" --out "$VDIR/unpacked_boot"
file "$VDIR/unpacked_boot/ramdisk"
(cd "$VDIR/boot_ramdisk" && gzip -dc "$VDIR/unpacked_boot/ramdisk" | cpio -idm 2>/dev/null)
ls -la "$VDIR/boot_ramdisk"

echo "=== Convert volantis system.img ==="
/mnt/d/p7_live/tools/simg2img "$VDIR/system.img" "$VDIR/system.raw.img" || mv "$VDIR/system.img" "$VDIR/system.raw.img"
rm -f "$VDIR/system.img" "$VDIR/vendor.img" "$VDIR/boot_stripped.img"
file "$VDIR/system.raw.img"

echo "=== Inspect volantis system.raw.img ==="
debugfs -R "ls -l /" "$VDIR/system.raw.img"
debugfs -R "ls -l /lib64/egl" "$VDIR/system.raw.img"
debugfs -R "ls -l /lib64/hw" "$VDIR/system.raw.img"
debugfs -R "ls -l /app" "$VDIR/system.raw.img"
debugfs -R "ls -l /priv-app" "$VDIR/system.raw.img"
