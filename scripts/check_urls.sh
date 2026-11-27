#!/bin/bash
set -e
echo "=== Check panther / volantis / hammerhead URLs ==="
curl -sL -H "Cookie: devsite_wall_acks=nexus-image-tos" "https://developers.google.com/android/images" \
  | grep -oE 'https://dl\.google\.com/dl/android/aosp/(panther|volantis|volantisg|hammerhead)[^"<> ]+\.zip' \
  | sort -u
echo "=== Check aarch64-linux-gnu-gcc & clang ==="
aarch64-linux-gnu-gcc --version | head -n 1
clang --version | head -n 1
echo "=== Check /mnt/d/p7_live ==="
ls -la /mnt/d/p7_live
