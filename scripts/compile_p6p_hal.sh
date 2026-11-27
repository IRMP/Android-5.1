#!/bin/bash
set -e
P7_SRC=/mnt/d/p7_live/src
SRC=/mnt/d/p6p_live/src
LIBS=/mnt/d/p7_live/stock/volantis/chk_libs
INC_BIONIC=$SRC/bionic_inc
INC_AOSP=/mnt/d/p7_live/tools/aosp51/libhardware/include
INC_CORE=/mnt/d/p7_live/tools/aosp51/system_core/include
GCC_INC=$(aarch64-linux-gnu-gcc -print-file-name=include)
OUT=/mnt/d/p6p_live/out/hal

mkdir -p "$OUT" "$INC_BIONIC/sys" "$INC_BIONIC/uapi"
cp -f "$P7_SRC/bionic_inc/"*.h "$INC_BIONIC/"
cp -f "$P7_SRC/bionic_inc/sys/"*.h "$INC_BIONIC/sys/"
cp -f "$P7_SRC/mini_adbd.c" "$P7_SRC/drm_test.c" "$P7_SRC/simg2img.c" "$SRC/"
cp -f /mnt/d/p7_live/out/hal/{audio.primary.default.so,power.default.so,vibrator.default.so,gps.default.so} "$OUT/"

ln -sfn /usr/include/drm "$INC_BIONIC/uapi/drm"
ln -sfn /usr/aarch64-linux-gnu/include/linux "$INC_BIONIC/uapi/linux"
ln -sfn /usr/aarch64-linux-gnu/include/asm "$INC_BIONIC/uapi/asm"
ln -sfn /usr/aarch64-linux-gnu/include/asm-generic "$INC_BIONIC/uapi/asm-generic"

CFLAGS="-O3 -Wall -Wextra -fPIC -ffreestanding -nostdinc -isystem $GCC_INC -I$INC_BIONIC -I$INC_BIONIC/uapi -I$INC_AOSP -I$INC_CORE -I$SRC"

aarch64-linux-gnu-gcc $CFLAGS -shared -nostdlib \
  "$SRC/gralloc.raven.c" \
  -Wl,-soname,gralloc.raven.so -Wl,--no-as-needed \
  -L"$LIBS" -lc -lcutils -llog \
  -o "$OUT/gralloc.raven.so"

aarch64-linux-gnu-gcc $CFLAGS -shared -nostdlib \
  "$SRC/hwcomposer.raven.c" \
  -Wl,-soname,hwcomposer.raven.so -Wl,--no-as-needed \
  -L"$LIBS" -lc -lcutils -llog -lhardware \
  -o "$OUT/hwcomposer.raven.so"

aarch64-linux-gnu-gcc $CFLAGS -shared -nostdlib \
  "$SRC/lights.raven.c" \
  -Wl,-soname,lights.raven.so -Wl,--no-as-needed \
  -L"$LIBS" -lc -lcutils -llog \
  -o "$OUT/lights.raven.so"

aarch64-linux-gnu-gcc $CFLAGS -shared -nostdlib \
  "$SRC/power.raven.c" \
  -Wl,-soname,power.raven.so -Wl,--no-as-needed \
  -L"$LIBS" -lc -lcutils -llog \
  -o "$OUT/power.raven.so"

aarch64-linux-gnu-strip "$OUT/gralloc.raven.so" "$OUT/hwcomposer.raven.so" "$OUT/lights.raven.so" "$OUT/power.raven.so"
ls -lh "$OUT"

for s in gralloc.raven.so hwcomposer.raven.so lights.raven.so power.raven.so; do
  echo "=== $s NEEDED & undef ==="
  aarch64-linux-gnu-readelf -d "$OUT/$s" | grep NEEDED
  aarch64-linux-gnu-nm -D -u "$OUT/$s"
done

echo "=== Verify all undef symbols exist in volantis chk_libs ==="
for s in gralloc.raven.so hwcomposer.raven.so lights.raven.so power.raven.so; do
  for sym in $(aarch64-linux-gnu-nm -D -u "$OUT/$s" | awk '{print $2}' | sed 's/@.*//'); do
    found=$(aarch64-linux-gnu-nm -D "$LIBS/libc.so" "$LIBS/libcutils.so" "$LIBS/liblog.so" "$LIBS/libhardware.so" 2>/dev/null | grep -E " [TWV] ${sym}$" || true)
    if [ -z "$found" ]; then
      echo "MISSING_SYM in $s: $sym"
    fi
  done
done
echo "=== Symbol check finished ==="
