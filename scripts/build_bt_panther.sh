#!/usr/bin/env bash
set -euo pipefail

CC="aarch64-linux-gnu-gcc"
CHK="/mnt/d/p7_live/stock/aosp51_emu/chk_libs"
BT_DIR="/mnt/d/p7_live/tools/aosp51/broadcom_libbt"
OUT_HAL="/mnt/d/p7_live/out/hal"
OUT_BIN="/mnt/d/p7_live/out/bin"
mkdir -p "$OUT_HAL" "$OUT_BIN" "$CHK"

if [ ! -f "$CHK/libdl.so" ]; then
    debugfs -R "dump lib64/libdl.so $CHK/libdl.so" /mnt/d/p7_live/stock/aosp51_emu/system.raw.img 2>/dev/null || true
fi

# 1. Build libbt-vendor.so
echo "=== 1. Compiling libbt-vendor.so ==="
$CC -shared -fPIC -nostdlib -O2 -Wall -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare \
  -include /mnt/d/p7_live/src/bt_panther/vnd_buildcfg.h \
  -I/mnt/d/p7_live/src/bt_panther \
  -I/mnt/d/p7_live/src/bt_panther/inc \
  -I/mnt/d/p7_live/src/bionic_inc \
  -I/mnt/d/p7_live/src \
  -I"$BT_DIR/include" \
  "$BT_DIR/src/bt_vendor_brcm.c" \
  "$BT_DIR/src/hardware.c" \
  "$BT_DIR/src/upio.c" \
  "$BT_DIR/src/userial_vendor.c" \
  "$BT_DIR/src/conf.c" \
  -Wl,-soname,libbt-vendor.so -Wl,--allow-shlib-undefined \
  -L"$CHK" -lc -lcutils -llog \
  -o "$OUT_HAL/libbt-vendor.so"
aarch64-linux-gnu-strip --strip-unneeded "$OUT_HAL/libbt-vendor.so"

# 2. Build bluetooth.panther.so
echo "=== 2. Compiling bluetooth.panther.so ==="
$CC -shared -fPIC -nostdlib -O2 -Wall -Wextra \
  -I/mnt/d/p7_live/src/bt_panther/inc \
  -I/mnt/d/p7_live/src/bionic_inc \
  -I/mnt/d/p7_live/src \
  -I/mnt/d/p7_live/src/bt_panther \
  -I/mnt/d/p7_live/tools/aosp51/libhardware/include \
  -I/mnt/d/p7_live/tools/aosp51/system_core/include \
  /mnt/d/p7_live/src/bluetooth.panther.c \
  -Wl,-soname,bluetooth.default.so -Wl,--allow-shlib-undefined \
  -L"$CHK" -lc -lcutils -llog -ldl \
  -o "$OUT_HAL/bluetooth.panther.so"
aarch64-linux-gnu-strip --strip-unneeded "$OUT_HAL/bluetooth.panther.so"

# 3. Compile panther_bt_ctl (using Android linker64 + bionic libc)
echo "=== 3. Compiling panther_bt_ctl ==="
$CC -fPIE -pie -O2 -Wall -Wextra \
  -nostdlib -Wl,-dynamic-linker,/system/bin/linker64 \
  -Wl,-rpath-link,"$CHK" \
  -I/mnt/d/p7_live/src/bt_panther/inc \
  -I/mnt/d/p7_live/src/bionic_inc \
  -I/mnt/d/p7_live/src \
  -I/mnt/d/p7_live/tools/aosp51/libhardware/include \
  -I/mnt/d/p7_live/tools/aosp51/system_core/include \
  /mnt/d/p7_live/src/panther_bt_ctl.c \
  -Wl,--entry=_start_bt_ctl -Wl,--allow-shlib-undefined \
  -L"$CHK" -lc -ldl -o "$OUT_BIN/panther_bt_ctl"
aarch64-linux-gnu-strip --strip-unneeded "$OUT_BIN/panther_bt_ctl"

# Check undefined symbols against AOSP51 libc/libcutils/liblog/libdl
echo "=== 4. Symbol closure check ==="
for s in "$OUT_HAL/libbt-vendor.so" "$OUT_HAL/bluetooth.panther.so"; do
    python3 - "$s" "$CHK" << 'PY'
import sys, subprocess, os
so, chk = sys.argv[1], sys.argv[2]
avail = set()
for f in os.listdir(chk):
    if f.endswith(".so"):
        out = subprocess.check_output(["aarch64-linux-gnu-nm", "-D", "--defined-only", os.path.join(chk, f)], text=True)
        for l in out.splitlines():
            p = l.split()
            if len(p) >= 3: avail.add(p[2].split("@@")[0])
req = set()
out = subprocess.check_output(["aarch64-linux-gnu-nm", "-D", "--undefined-only", so], text=True)
for l in out.splitlines():
    p = l.split()
    if len(p) >= 2: req.add(p[-1].split("@@")[0])
miss = sorted(req - avail)
print(f"{os.path.basename(so)}: UND={len(req)}, MISSING={len(miss)} -> {miss}")
PY
done
ls -lh "$OUT_HAL/libbt-vendor.so" "$OUT_HAL/bluetooth.panther.so"
