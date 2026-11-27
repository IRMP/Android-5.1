#!/bin/bash
set -e
mkdir -p /mnt/d/p6p_live/{stock,src,scripts,out/final,out/hal,tools}
cd /mnt/d/p6p_live/stock
URL="https://dl.google.com/dl/android/aosp/raven-tq3a.230901.001.c2-factory-e7e2cf2e.zip"
curl -sIL "$URL" | grep -i content-length
if [ ! -f raven-tq3a.230901.001.c2-factory-e7e2cf2e.zip ]; then
  curl -L -C - -o raven-tq3a.230901.001.c2-factory-e7e2cf2e.zip "$URL"
fi
ls -lh raven-tq3a.230901.001.c2-factory-e7e2cf2e.zip
