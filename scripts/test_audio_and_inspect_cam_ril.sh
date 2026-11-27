#!/bin/bash
set -e
SRC=/mnt/d/p7_live/src
LIBS=/mnt/d/p7_live/stock/aosp51_emu/chk_libs
INC_BIONIC=$SRC/bionic_inc
INC_AOSP=/mnt/d/p7_live/tools/aosp51/libhardware/include
INC_CORE=/mnt/d/p7_live/tools/aosp51/system_core/include
INC_ALSA=/mnt/d/p7_live/tools/aosp51/external_tinyalsa/include
GCC_INC=$(aarch64-linux-gnu-gcc -print-file-name=include)
OUT=/mnt/d/p7_live/out/hal
mkdir -p "$OUT"

CFLAGS="-O2 -Wall -Wextra -fPIC -ffreestanding -nostdinc -isystem $GCC_INC -I$INC_BIONIC -I$INC_BIONIC/uapi -I$INC_AOSP -I$INC_CORE -I$INC_ALSA -I$SRC"

echo "=== 1. Compile audio.primary.panther.so ==="
aarch64-linux-gnu-gcc $CFLAGS -shared -nostdlib \
  "$SRC/audio.primary.panther.c" \
  -Wl,-soname,audio.primary.panther.so -Wl,--no-as-needed \
  -L"$LIBS" -lc -lcutils -llog -ltinyalsa \
  -o "$OUT/audio.primary.panther.so"
aarch64-linux-gnu-strip "$OUT/audio.primary.panther.so"
ls -lh "$OUT/audio.primary.panther.so"
aarch64-linux-gnu-readelf -d "$OUT/audio.primary.panther.so" | grep NEEDED
aarch64-linux-gnu-nm -D -u "$OUT/audio.primary.panther.so"

echo "=== 2. Inspect libsitril.so ProcessModemData & SendData (RCM/SIT frame header) ==="
TMP=/mnt/d/p7_live/stock/panther/unpacked/v_inspect
aarch64-linux-gnu-objdump -d "$TMP/libsitril.so" --start-address=0x146af0 --stop-address=0x146bd0

echo "=== 3. Inspect cam.apex libgooglecamerahal.so & liblyric_hwl.so ==="
V=/mnt/d/p7_live/stock/panther/vendor.raw.img
mkdir -p "$TMP/cam_chk"
debugfs -R "dump apex/com.google.pixel.camera.hal.apex $TMP/cam_chk/cam.apex" "$V" >/dev/null
unzip -o -j "$TMP/cam_chk/cam.apex" apex_payload.img -d "$TMP/cam_chk" >/dev/null
debugfs -R "dump lib64/libgooglecamerahal.so $TMP/cam_chk/libgooglecamerahal.so" "$TMP/cam_chk/apex_payload.img" >/dev/null
debugfs -R "dump lib64/liblyric_hwl.so $TMP/cam_chk/liblyric_hwl.so" "$TMP/cam_chk/apex_payload.img" >/dev/null
echo "--- DT_NEEDED libgooglecamerahal.so ---"
readelf -d "$TMP/cam_chk/libgooglecamerahal.so" | grep NEEDED || true
echo "--- DT_NEEDED liblyric_hwl.so ---"
readelf -d "$TMP/cam_chk/liblyric_hwl.so" | grep NEEDED || true
echo "--- exported Create* symbols in libgooglecamerahal.so ---"
readelf -Ws "$TMP/cam_chk/libgooglecamerahal.so" | grep -iE "CreateCamera|GoogleCameraHal|Hwl" | head -n 20 || true
rm -rf "$TMP/cam_chk"
