#!/bin/bash
set -e
echo "=== Check p7_step1_ramdisk.lz4 permissions & symlinks ==="
lz4 -dc /mnt/d/p7_live/out/step1/p7_step1_ramdisk.lz4 | cpio -tv | head -n 30

mkdir -p /mnt/d/p7_live/stock/volantis
cd /mnt/d/p7_live/stock/volantis
if [ ! -f volantis-lmy48t-factory-b925de62.zip ]; then
  curl -L -C - -o volantis-lmy48t-factory-b925de62.zip \
    "https://dl.google.com/dl/android/aosp/volantis-lmy48t-factory-b925de62.zip"
fi
ls -lh volantis-lmy48t-factory-b925de62.zip
