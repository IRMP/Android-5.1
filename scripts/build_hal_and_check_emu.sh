#!/bin/bash
set -e
SRC=/mnt/d/p7_live/src
LIBS=/mnt/d/p7_live/stock/volantis/chk_libs
INC_BIONIC=$SRC/bionic_inc
INC_AOSP=/mnt/d/p7_live/tools/aosp51/libhardware/include
INC_CORE=/mnt/d/p7_live/tools/aosp51/system_core/include
GCC_INC=$(aarch64-linux-gnu-gcc -print-file-name=include)
OUT=/mnt/d/p7_live/out/hal
mkdir -p "$OUT" "$INC_BIONIC/uapi"

ln -sfn /usr/include/drm "$INC_BIONIC/uapi/drm"
ln -sfn /usr/aarch64-linux-gnu/include/linux "$INC_BIONIC/uapi/linux"
ln -sfn /usr/aarch64-linux-gnu/include/asm "$INC_BIONIC/uapi/asm"
ln -sfn /usr/aarch64-linux-gnu/include/asm-generic "$INC_BIONIC/uapi/asm-generic"

CFLAGS="-O2 -Wall -Wextra -fPIC -ffreestanding -nostdinc -isystem $GCC_INC -I$INC_BIONIC -I$INC_BIONIC/uapi -I$INC_AOSP -I$INC_CORE -I$SRC"

aarch64-linux-gnu-gcc $CFLAGS -shared -nostdlib \
  "$SRC/gralloc.panther.c" \
  -Wl,-soname,gralloc.panther.so -Wl,--no-as-needed \
  -L"$LIBS" -lc -lcutils -llog \
  -o "$OUT/gralloc.panther.so"

aarch64-linux-gnu-gcc $CFLAGS -shared -nostdlib \
  "$SRC/hwcomposer.panther.c" \
  -Wl,-soname,hwcomposer.panther.so -Wl,--no-as-needed \
  -L"$LIBS" -lc -lcutils -llog -lhardware \
  -o "$OUT/hwcomposer.panther.so"

aarch64-linux-gnu-strip "$OUT/gralloc.panther.so" "$OUT/hwcomposer.panther.so"
ls -lh "$OUT"
echo "=== gralloc.panther.so NEEDED & undef ==="
aarch64-linux-gnu-readelf -d "$OUT/gralloc.panther.so" | grep NEEDED
aarch64-linux-gnu-nm -D -u "$OUT/gralloc.panther.so"
echo "=== hwcomposer.panther.so NEEDED & undef ==="
aarch64-linux-gnu-readelf -d "$OUT/hwcomposer.panther.so" | grep NEEDED
aarch64-linux-gnu-nm -D -u "$OUT/hwcomposer.panther.so"

echo "=== Check aosp51_emu ramdisk & priv-app ==="
E=/mnt/d/p7_live/stock/aosp51_emu
mkdir -p "$E/ramdisk_unp"
(cd "$E/ramdisk_unp" && gzip -dc "$E/ramdisk.img" | cpio -idm 2>/dev/null)
cat "$E/ramdisk_unp/default.prop"
debugfs -R "ls -l /priv-app" "$E/system.raw.img"
debugfs -R "cat /build.prop" "$E/system.raw.img" | head -n 35
