#!/usr/bin/env bash
set -euo pipefail

CC="aarch64-linux-gnu-gcc"
CHK="/mnt/d/p7_live/stock/aosp51_emu/chk_libs"
OUT_HAL="/mnt/d/p7_live/out/hal"
mkdir -p "$OUT_HAL"

echo "=== 1. Compiling libpanther-ril.so ==="
$CC -shared -fPIC -nostdlib -O2 -Wall -Wextra \
  -I/mnt/d/p7_live/src/bt_panther/inc \
  -I/mnt/d/p7_live/src/bionic_inc \
  -I/mnt/d/p7_live/src \
  -I/mnt/d/p7_live/tools/aosp51/hardware_ril/include \
  -I/mnt/d/p7_live/tools/aosp51/system_core/include \
  /mnt/d/p7_live/src/libpanther-ril.c \
  -Wl,-soname,libpanther-ril.so -Wl,--allow-shlib-undefined \
  -L"$CHK" -lc -lcutils -llog \
  -o "$OUT_HAL/libpanther-ril.so"
aarch64-linux-gnu-strip --strip-unneeded "$OUT_HAL/libpanther-ril.so"

echo "=== 2. Compiling camera.panther.so ==="
$CC -shared -fPIC -nostdlib -O2 -Wall -Wextra \
  -I/mnt/d/p7_live/src/bt_panther/inc \
  -I/mnt/d/p7_live/src/bionic_inc \
  -I/mnt/d/p7_live/src \
  -I/mnt/d/p7_live/tools/aosp51/libhardware/include \
  -I/mnt/d/p7_live/tools/aosp51/system_core/include \
  /mnt/d/p7_live/src/camera.panther.c \
  -Wl,-soname,camera.panther.so -Wl,--allow-shlib-undefined \
  -L"$CHK" -lc -lcutils -llog \
  -o "$OUT_HAL/camera.panther.so"
aarch64-linux-gnu-strip --strip-unneeded "$OUT_HAL/camera.panther.so"

echo "=== 3. Verify symbol closure across ALL built HALs & binaries ==="
for f in \
  "$OUT_HAL/audio.primary.panther.so" \
  "$OUT_HAL/gralloc.panther.so" \
  "$OUT_HAL/hwcomposer.panther.so" \
  "$OUT_HAL/libbt-vendor.so" \
  "$OUT_HAL/bluetooth.panther.so" \
  "$OUT_HAL/libpanther-ril.so" \
  "$OUT_HAL/camera.panther.so" \
  "/mnt/d/p7_live/out/bin/panther_bt_ctl"
do
    python3 - "$f" "$CHK" << 'PY'
import sys, subprocess, os
so, chk = sys.argv[1], sys.argv[2]
avail = set()
for fn in os.listdir(chk):
    if fn.endswith(".so"):
        out = subprocess.check_output(["aarch64-linux-gnu-nm", "-D", "--defined-only", os.path.join(chk, fn)], text=True)
        for l in out.splitlines():
            p = l.split()
            if len(p) >= 3: avail.add(p[2].split("@@")[0])
# Also tinyalsa/binder if audio uses them
for extra_lib in ["/mnt/d/p7_live/stock/aosp51_emu/chk_libs/libtinyalsa.so"]:
    if os.path.exists(extra_lib):
        out = subprocess.check_output(["aarch64-linux-gnu-nm", "-D", "--defined-only", extra_lib], text=True)
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
ls -lh "$OUT_HAL"
