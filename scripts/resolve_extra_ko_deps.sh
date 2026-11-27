#!/bin/bash
set -e
echo "=== 1. Compilers in WSL ==="
which aarch64-linux-gnu-gcc aarch64-linux-gnu-g++ clang clang++ lld ld.lld 2>&1 || true

echo "=== 2. Check missing .ko dependencies for all 6 subsystems ==="
python3 - <<'PY'
import os, subprocess

vk = '/mnt/d/p7_live/stock/panther/unpacked/vk_root/lib/modules'
dlkm = '/mnt/d/p7_live/stock/panther/unpacked/dlkm_root'
dlkm_all = '/mnt/d/p7_live/stock/panther/vendor_dlkm.raw.img'
stage1_list = set(open('/mnt/d/p7_live/step1_initramfs/lib/modules_live/modules.order').read().splitlines())

# Ensure all needed dlkm .ko files are extracted
targets = [
    'bcmdhd4389.ko',
    'nitrous.ko', 'exynos_tty.ko',
    'aoc_core.ko', 'aoc_channel_dev.ko', 'aoc_control_dev.ko', 'aoc_char_dev.ko',
    'aoc_alsa_dev_util.ko', 'aoc_alsa_dev.ko', 'aoc_usb_driver.ko',
    'snd-soc-wm-adsp.ko', 'snd-soc-cs35l41.ko', 'snd-soc-cs35l41-spi.ko',
    'snd-soc-cs35l45.ko', 'snd-soc-cs35l45-spi.ko',
    'cpif_page.ko', 'shm_ipc.ko', 'boot_device_spi.ko', 'cpif.ko', 'cp_thermal_zone.ko',
    'lwis.ko', 'stmvl53l1.ko',
    'gpu_cooling.ko', 'mali_pixel.ko', 'mali_kbase.ko', 'g2d.ko',
]

# Dump any missing target/dep from vendor_dlkm.raw.img
def find_ko(name):
    for d in [vk, dlkm, '/mnt/d/p7_live/stock/panther/unpacked/vdlkm_mod']:
        p = os.path.join(d, name)
        if os.path.isfile(p) and os.path.getsize(p) > 0:
            return p
    os.makedirs('/mnt/d/p7_live/stock/panther/unpacked/vdlkm_mod', exist_ok=True)
    out = os.path.join('/mnt/d/p7_live/stock/panther/unpacked/vdlkm_mod', name)
    subprocess.run(['debugfs', '-R', f'dump lib/modules/{name} {out}', dlkm_all], stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
    if os.path.isfile(out) and os.path.getsize(out) > 0:
        return out
    if os.path.exists(out): os.remove(out)
    return None

def get_deps(path):
    out = subprocess.check_output(['modinfo', '-F', 'depends', path], text=True).strip()
    if not out: return []
    return [d.strip() + '.ko' for d in out.split(',') if d.strip()]

order = []
visited = set(stage1_list)

def resolve(name):
    if name in visited: return
    visited.add(name)
    p = find_ko(name)
    if not p:
        print(f"WARN: module not found: {name}")
        return
    for dep in get_deps(p):
        resolve(dep)
    order.append((name, p, os.path.getsize(p)))

for t in targets:
    resolve(t)

total = 0
print(f"Need {len(order)} extra modules beyond Stage 1 ({len(stage1_list)} already in Stage 1):")
for name, p, sz in order:
    total += sz
    print(f"  {name:30s} {sz//1024:6d} KB  <- {p}")
print(f"Total uncompressed extra .ko size: {total/1024/1024:.2f} MB")
PY
