#!/usr/bin/env bash
set -euo pipefail

VRAW="/mnt/d/p7_live/stock/panther/vendor.raw.img"
OUT_V="/mnt/d/p7_live/out/vendor_stage"
CHK="/mnt/d/p7_live/stock/aosp51_emu/chk_libs"
mkdir -p "$OUT_V/firmware/brcm" "$OUT_V/modules_extra" "$OUT_V/bin" "$OUT_V/etc/wifi" "$OUT_V/etc/permissions" "$OUT_V/lib64"

echo "=== 0. Check fstab.modem ==="
debugfs -R "cat etc/fstab.modem" "$VRAW" 2>/dev/null || true

echo "=== 1. Extract Pixel 7 firmware blobs from vendor.raw.img ==="
FW_LIST=(
    "aoc.bin"
    "mali_csffw.bin"
    "fw_bcmdhd.bin"
    "bcmdhd.cal"
    "bcmdhd_clm.blob"
    "fast_switch1.txt"
    "fast_switch2.txt"
    "fast_switch3.txt"
    "fast_switch4.txt"
    "filter_ie"
    "SensorHub.patch"
    "cs35l41-dsp1-spk-cali.bin"
    "cs35l41-dsp1-spk-cali.wmfw"
    "cs35l41-dsp1-spk-diag.bin"
    "cs35l41-dsp1-spk-diag.wmfw"
    "cs35l41-dsp1-spk-prot.bin"
    "cs35l41-dsp1-spk-prot.wmfw"
    "R-cs35l41-dsp1-spk-cali.bin"
    "R-cs35l41-dsp1-spk-diag.bin"
    "R-cs35l41-dsp1-spk-prot.bin"
    "brcm/BCM.hcd"
    "brcm/BTFW_B.hcd"
)
CMDS=""
for f in "${FW_LIST[@]}"; do
    CMDS+="dump firmware/$f $OUT_V/firmware/$f"$'\n'
done
printf "%s" "$CMDS" | debugfs "$VRAW" >/dev/null 2>&1
ls -lh "$OUT_V/firmware" | head -n 25

echo "=== 2. Copy all 23 extra kernel modules in dependency order ==="
python3 - << 'PY'
import os, shutil, subprocess
vk = '/mnt/d/p7_live/stock/panther/unpacked/vk_root/lib/modules'
dlkm = '/mnt/d/p7_live/stock/panther/unpacked/dlkm_root'
vdlkm = '/mnt/d/p7_live/stock/panther/unpacked/vdlkm_mod'
dlkm_all = '/mnt/d/p7_live/stock/panther/vendor_dlkm.raw.img'
stage1_list = set(open('/mnt/d/p7_live/step1_initramfs/lib/modules_live/modules.order').read().splitlines())
out_dir = '/mnt/d/p7_live/out/vendor_stage/modules_extra'

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

def find_ko(name):
    for d in (vk, dlkm, vdlkm):
        p = os.path.join(d, name)
        if os.path.isfile(p) and os.path.getsize(p) > 0:
            return p
    os.makedirs(vdlkm, exist_ok=True)
    out = os.path.join(vdlkm, name)
    subprocess.run(['debugfs', '-R', f'dump lib/modules/{name} {out}', dlkm_all], stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
    if os.path.isfile(out) and os.path.getsize(out) > 0:
        return out
    return None

def get_deps(path):
    out = subprocess.check_output(['modinfo', '-F', 'depends', path], text=True).strip()
    return [d.strip() + '.ko' for d in out.split(',') if d.strip()] if out else []

order = []
visited = set(stage1_list)
def resolve(name):
    if name in visited: return
    visited.add(name)
    p = find_ko(name)
    if not p:
        print("WARN missing ko:", name)
        return
    for dep in get_deps(p):
        resolve(dep)
    order.append((name, p))

for t in targets:
    resolve(t)

with open(os.path.join(out_dir, 'modules_extra.order'), 'w') as mf:
    for name, p in order:
        shutil.copy2(p, os.path.join(out_dir, name))
        mf.write(name + '\n')
print(f"Staged {len(order)} extra modules into {out_dir}")
PY

echo "=== 3. Compile panther_aocd & patch libhardware_legacy.so ==="
aarch64-linux-gnu-gcc -fPIE -pie -O2 -Wall -Wextra \
  -nostdlib -Wl,-dynamic-linker,/system/bin/linker64 \
  -Wl,-rpath-link,"$CHK" \
  -I/mnt/d/p7_live/src/bt_panther/inc \
  -I/mnt/d/p7_live/src/bionic_inc \
  -I/mnt/d/p7_live/src \
  /mnt/d/p7_live/src/panther_aocd.c \
  -Wl,--entry=_start_aocd -Wl,--allow-shlib-undefined \
  -L"$CHK" -lc -lcutils -llog -o "$OUT_V/bin/panther_aocd"
aarch64-linux-gnu-strip --strip-unneeded "$OUT_V/bin/panther_aocd"

cp /mnt/d/p7_live/stock/panther/unpacked/v_inspect/cbd "$OUT_V/bin/cbd"
cp /mnt/d/p7_live/stock/volantis/a5_wifi_bt/bin/wpa_supplicant "$OUT_V/bin/wpa_supplicant"
cp /mnt/d/p7_live/stock/volantis/a5_wifi_bt/bin/hostapd "$OUT_V/bin/hostapd"
cp /mnt/d/p7_live/stock/volantis/a5_wifi_bt/lib64/libhardware_legacy.so "$OUT_V/lib64/libhardware_legacy.so"

python3 - << 'PY'
# Verify exact prologue bytes at 0x3cb8 before patching wifi_change_fw_path -> return 0
p = "/mnt/d/p7_live/out/vendor_stage/lib64/libhardware_legacy.so"
with open(p, "r+b") as f:
    f.seek(0x3cb8)
    orig = f.read(8)
    assert orig == bytes.fromhex("fd7bbda9fd030091"), f"Unexpected bytes at 0x3cb8: {orig.hex()}"
    f.seek(0x3cb8)
    f.write(bytes.fromhex("00008052c0035fd6")) # mov w0, #0; ret
print("Patched libhardware_legacy.so wifi_change_fw_path @ 0x3cb8 -> return 0")
PY

cp /mnt/d/p7_live/stock/volantis/a5_wifi_bt/etc/permissions/*.xml "$OUT_V/etc/permissions/"
cp /mnt/d/p7_live/stock/panther/unpacked/v_inspect/cfg/mixer_paths.xml "$OUT_V/etc/mixer_paths.xml"
cp /mnt/d/p7_live/stock/panther/unpacked/v_inspect/cfg/audio_platform_configuration.xml "$OUT_V/etc/audio_platform_configuration.xml"

cat > "$OUT_V/etc/wifi/wpa_supplicant.conf" << 'EOF'
ctrl_interface=/data/misc/wifi/sockets
disable_scan_offload=1
driver_param=use_p2p_group_interface=1
update_config=1
device_name=Pixel_7_Android5
manufacturer=Google
model_name=Pixel 7
serial_number=panther
device_type=10-0050F204-5
config_methods=physical_display virtual_push_button
EOF

du -sh "$OUT_V"
