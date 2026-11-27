#!/bin/bash
set -e
SRC=/mnt/d/p7_live/src
MALI_SRC=$SRC/mali_compat
LIBS=/mnt/d/p7_live/stock/aosp51_emu/chk_libs
INC_BIONIC=$SRC/bionic_inc
INC_AOSP=/mnt/d/p7_live/tools/aosp51/libhardware/include
INC_CORE=/mnt/d/p7_live/tools/aosp51/system_core/include
GCC_INC=$(aarch64-linux-gnu-gcc -print-file-name=include)
OUT=/mnt/d/p7_live/out/hal
SHIM_OUT=/mnt/d/p7_live/out/mali_compat
mkdir -p "$OUT" "$SHIM_OUT"

CFLAGS="-O2 -Wall -Wextra -fPIC -ffreestanding -nostdinc -isystem $GCC_INC -I$INC_BIONIC -I$INC_BIONIC/uapi -I$INC_AOSP -I$INC_CORE -I$SRC"

echo "=== 1. Compile updated DMA-BUF gralloc.panther.so ==="
aarch64-linux-gnu-gcc $CFLAGS -shared -nostdlib \
  "$SRC/gralloc.panther.c" \
  -Wl,-soname,gralloc.panther.so -Wl,--no-as-needed \
  -L"$LIBS" -lc -lcutils -llog \
  -o "$OUT/gralloc.panther.so"
aarch64-linux-gnu-strip "$OUT/gralloc.panther.so"

echo "=== 2. Compile libpanther_chk.so ==="
aarch64-linux-gnu-gcc $CFLAGS -shared -nostdlib \
  "$SRC/libpanther_chk.c" \
  -Wl,-soname,libpanther_chk.so -Wl,--no-as-needed \
  -L"$LIBS" -lc \
  -o "$SHIM_OUT/libpanther_chk.so"
aarch64-linux-gnu-strip "$SHIM_OUT/libpanther_chk.so"

echo "=== 3. Compile Mali-G710 C++ compat libraries ==="
build_shim() {
  local src="$1"
  local so="$2"
  shift 2
  aarch64-linux-gnu-gcc $CFLAGS -shared -nostdlib "$src" \
    -Wl,-soname,"$so" -Wl,--no-as-needed \
    -L"$LIBS" "$@" -lc -lcutils -llog \
    -o "$SHIM_OUT/$so"
  aarch64-linux-gnu-strip "$SHIM_OUT/$so"
}

build_shim "$MALI_SRC/libnativewindow.c" libnativewindow.so
build_shim "$MALI_SRC/libdmabufheap.c"   libdmabufheap.so
build_shim "$MALI_SRC/libbase.c"         libbase.so
build_shim "$MALI_SRC/libhidlbase.c"     libhidlbase.so
build_shim "$MALI_SRC/libgralloctypes.c" libgralloctypes.so
build_shim "$MALI_SRC/mapper4.c"         android.hardware.graphics.mapper@4.0.so

cp /mnt/d/p7_live/stock/panther/unpacked/v_inspect/libdrm.so "$SHIM_OUT/libdrm.so"

ls -lh "$SHIM_OUT" "$OUT/gralloc.panther.so"

echo "=== 4. Verify 100% symbol closure for libGLES_mali.so under AOSP51 + mali_compat ==="
python3 - <<'PY'
import subprocess

aosp_libs = [
    '/mnt/d/p7_live/stock/aosp51_emu/chk_libs/' + x
    for x in ['libc.so','libcutils.so','liblog.so','libutils.so','libhardware.so']
] + [
    '/mnt/d/p7_live/stock/panther/unpacked/v_inspect/emu_' + x
    for x in ['libm.so','libc++.so','libz.so']
]
shim_libs = [
    '/mnt/d/p7_live/out/mali_compat/' + x
    for x in ['libnativewindow.so','libdmabufheap.so','libbase.so','libhidlbase.so',
              'libgralloctypes.so','android.hardware.graphics.mapper@4.0.so','libdrm.so']
]
have = {'dlopen','dlsym','dlclose','dlerror','dladdr','dl_iterate_phdr'}
for p in aosp_libs + shim_libs:
    out = subprocess.check_output(['readelf','-Ws','--dyn-syms',p], text=True, stderr=subprocess.DEVNULL)
    for l in out.splitlines():
        c = l.split()
        if len(c) >= 8 and c[6] != 'UND':
            have.add(c[7].split('@@')[0])

mali = '/mnt/d/p7_live/stock/panther/unpacked/v_inspect/libGLES_mali.so'
und = set()
for l in subprocess.check_output(['readelf','-Ws','--dyn-syms',mali], text=True, stderr=subprocess.DEVNULL).splitlines():
    c = l.split()
    if len(c) >= 8 and c[6] == 'UND':
        und.add(c[7].split('@')[0])
miss = sorted(s for s in und if s and s not in have)
print(f"libGLES_mali.so total UND={len(und)}, UNRESOLVED={len(miss)}")
for s in miss:
    print("  UNRESOLVED:", s)

drm_und = set()
for l in subprocess.check_output(['readelf','-Ws','--dyn-syms','/mnt/d/p7_live/out/mali_compat/libdrm.so'], text=True, stderr=subprocess.DEVNULL).splitlines():
    c = l.split()
    if len(c) >= 8 and c[6] == 'UND':
        drm_und.add(c[7].split('@')[0])
drm_miss = sorted(s for s in drm_und if s and s not in have)
print(f"libdrm.so total UND={len(drm_und)}, UNRESOLVED={len(drm_miss)}")
for s in drm_miss:
    print("  DRM UNRESOLVED:", s)
PY
