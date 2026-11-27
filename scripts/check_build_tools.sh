#!/bin/bash
set -e
for t in make tar bzip2 flex bison nproc; do
  which "$t" >/dev/null 2>&1 && echo "OK: $t" || echo "MISSING: $t"
done
echo "=== Testing busybox tarball head ==="
curl -sIL https://busybox.net/downloads/busybox-1.36.1.tar.bz2 | head -n 10
