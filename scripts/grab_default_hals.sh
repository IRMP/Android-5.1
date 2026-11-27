#!/bin/bash
set -e
V=/mnt/d/p7_live/stock/volantis
OUT=/mnt/d/p7_live/out/hal
for f in audio.primary.default.so power.default.so vibrator.default.so gps.default.so; do
  debugfs -R "dump /lib64/hw/$f $OUT/$f" "$V/system.raw.img" >/dev/null
done
ls -lh "$OUT"
rm -f "$V/system.raw.img"
