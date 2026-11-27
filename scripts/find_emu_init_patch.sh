#!/bin/bash
set -e
INIT=/mnt/d/p7_live/stock/aosp51_emu/ramdisk_unp/init
python3 - <<'PY'
data = open('/mnt/d/p7_live/stock/aosp51_emu/ramdisk_unp/init', 'rb').read()
off = data.find(b'<6>init: loading selinux policy')
print('selinux string off:', hex(off))
PY
aarch64-linux-gnu-readelf -l "$INIT" | head -n 20
aarch64-linux-gnu-objdump -d "$INIT" | grep -B 5 -A 15 'adrp.*7b000\|adrp.*7a000\|adrp.*7c000\|adrp.*7d000' | grep -B 4 -A 12 'loading\|6>init' -C 5 2>/dev/null || true
# Better: search exact adrp+add for the string address
python3 - <<'PY'
import subprocess
data = open('/mnt/d/p7_live/stock/aosp51_emu/ramdisk_unp/init', 'rb').read()
str_off = data.find(b'<6>init: loading selinux policy')
vaddr = 0x400000 + str_off
page = vaddr & ~0xfff
lo = vaddr & 0xfff
print(f"vaddr={hex(vaddr)} page={hex(page)} lo={hex(lo)}")
out = subprocess.check_output(['aarch64-linux-gnu-objdump', '-d', '/mnt/d/p7_live/stock/aosp51_emu/ramdisk_unp/init'], text=True)
lines = out.splitlines()
for i, l in enumerate(lines):
    if hex(page)[2:] in l and 'adrp' in l:
        blk = "\n".join(lines[max(0,i-3):min(len(lines),i+12)])
        if hex(lo)[2:] in blk:
            print("=== MATCH ===")
            print(blk)
PY
