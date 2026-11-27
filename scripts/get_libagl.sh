#!/bin/bash
set -e
TMP=/mnt/d/p7_live/stock/volantis/chk_libs
echo "=== libEGL.so loader strings ==="
strings "$TMP/libEGL.so" | grep -E "egl|GLES|vendor|system" | grep -v "^_Z" | head -n 40

cd /mnt/d/p7_live/tools/aosp51
if [ ! -d frameworks_native ]; then
  git clone --depth=1 --branch lollipop-mr1-release --filter=blob:none --sparse https://android.googlesource.com/platform/frameworks/native frameworks_native
  (cd frameworks_native && git sparse-checkout set opengl/libagl opengl/include include)
fi
ls -la frameworks_native/opengl/libagl
cat frameworks_native/opengl/libagl/Android.mk
