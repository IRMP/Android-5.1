#!/bin/bash
set -e
gcc -O2 -Wall -Wextra /mnt/d/p7_live/src/simg2img.c -o /mnt/d/p7_live/tools/simg2img
aarch64-linux-gnu-gcc -O2 -Wall -Wextra -static /mnt/d/p7_live/src/drm_test.c -I/usr/include -o /mnt/d/p7_live/tools/drm_test
aarch64-linux-gnu-gcc -O2 -Wall -Wextra -static -pthread /mnt/d/p7_live/src/mini_adbd.c -o /mnt/d/p7_live/tools/mini_adbd
aarch64-linux-gnu-strip /mnt/d/p7_live/tools/drm_test /mnt/d/p7_live/tools/mini_adbd
ls -lh /mnt/d/p7_live/tools/simg2img /mnt/d/p7_live/tools/drm_test /mnt/d/p7_live/tools/mini_adbd
