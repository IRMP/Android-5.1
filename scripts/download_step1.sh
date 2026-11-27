#!/bin/bash
set -e
mkdir -p /mnt/d/p7_live/stock /mnt/d/p7_live/tools/src
cd /mnt/d/p7_live/tools/src
if [ ! -f busybox-1.36.1.tar.bz2 ]; then
  curl -L -C - -o busybox-1.36.1.tar.bz2 https://busybox.net/downloads/busybox-1.36.1.tar.bz2
fi
cd /mnt/d/p7_live/stock
if [ ! -f panther-tq3a.230901.001.c2-factory-51bde7e8.zip ]; then
  curl -L -C - -o panther-tq3a.230901.001.c2-factory-51bde7e8.zip \
    "https://dl.google.com/dl/android/aosp/panther-tq3a.230901.001.c2-factory-51bde7e8.zip"
fi
echo "=== Downloaded files ==="
ls -lh /mnt/d/p7_live/tools/src/busybox-1.36.1.tar.bz2 /mnt/d/p7_live/stock/panther-tq3a.230901.001.c2-factory-51bde7e8.zip
