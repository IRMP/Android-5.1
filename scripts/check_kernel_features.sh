#!/bin/bash
set -e
K=/mnt/d/p7_live/stock/panther/unpacked/boot/kernel
echo "=== Check kernel features in GKI 5.10 ==="
lz4 -dc "$K" | strings | grep -E "ashmem|binder|binderfs|squashfs|erofs|memfd|compat|exynos-drm" | sort -u | head -n 60
