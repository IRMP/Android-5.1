#!/bin/bash
set -e
ROOT=/mnt/d/p6p_live
EMU=/mnt/d/p7_live/stock/aosp51_emu
STOCK=$ROOT/stock/raven
UNP=$STOCK/unpacked
TOOLS=$ROOT/tools
MKBOOT=$TOOLS/mkbootimg/mkbootimg.py
A5=$ROOT/a5_rootfs
FS=$ROOT/final_initramfs
OUT=$ROOT/out/final

rm -rf "$A5" "$FS"
mkdir -p "$A5" "$FS"/{bin,sbin,etc,proc,sys,dev,tmp,mnt,data,newroot,lib/modules_live,lib/firmware,vendor}
mkdir -p "$OUT"
ln -s /lib/firmware "$FS/vendor/firmware"

# =====================================================================
# PART 1: Collect curated GKI 5.10 modules + firmware for Pixel 6 Pro
# =====================================================================
echo "=== Collecting curated GS101 (raven) 5.10 modules ==="
MODOUT=$FS/lib/modules_live
> "$MODOUT/modules.order"

# 1) Core & SoC modules from vendor_boot vr1 in stock modules.load order
# Skip watchdogs, modem (cpif), AoC audio DSP, UWB, MFC, TPU, Mali GPU
while read -r mod; do
  [ -z "$mod" ] && continue
  case "$mod" in
    *wdt*|*softdog*|*watchdog*) continue ;;
    aoc_*|cpif*|dw3000*|mcps802154*|exynos_mfc*|smfc*|snd-soc-*|st33spi*|st54spi*|st21nfc*|bcm47765*|boot_device_spi*|g2d*|abrolhos*|mali_*) continue ;;
  esac
  if [ -f "$UNP/vendor_boot/vr1/lib/modules/$mod" ]; then
    cp "$UNP/vendor_boot/vr1/lib/modules/$mod" "$MODOUT/$mod"
    echo "$mod" >> "$MODOUT/modules.order"
  fi
done < "$UNP/vendor_boot/vr1/lib/modules/modules.load"

# 2) Display, touch, regulator & Wi-Fi modules from vendor_dlkm
VDLKM_MODS="
pinctrl-slg51000.ko
slg51000-core.ko
slg51000-regulator.ko
panel-samsung-s6e3hc3-c10.ko
panel-samsung-s6e3hc4.ko
panel-boe-nt37290.ko
touch_bus_negotiator.ko
touch_offload.ko
goog_touch_interface.ko
sec_touch.ko
ftm5.ko
bcmdhd4389.ko
"
for mod in $VDLKM_MODS; do
  if [ ! -f "$MODOUT/$mod" ]; then
    debugfs -R "dump /lib/modules/$mod $MODOUT/$mod" "$STOCK/vendor_dlkm.raw.img" >/dev/null 2>&1 || true
  fi
  if [ -s "$MODOUT/$mod" ]; then
    if ! grep -qx "$mod" "$MODOUT/modules.order"; then
      echo "$mod" >> "$MODOUT/modules.order"
    fi
  else
    rm -f "$MODOUT/$mod"
  fi
done

# Copy touch (s6sy79x.bin) & Wi-Fi (fw_bcmdhd.bin, bcmdhd.cal, bcmdhd_clm.blob) firmware
cp -f "$STOCK/firmware/s6sy79x.bin" "$FS/lib/firmware/"
cp -f "$STOCK/firmware/fw_bcmdhd.bin" "$STOCK/firmware/bcmdhd.cal" "$STOCK/firmware/bcmdhd_clm.blob" "$FS/lib/firmware/"
ls -lh "$FS/lib/firmware"
echo "Collected modules count: $(wc -l < "$MODOUT/modules.order")"

# =====================================================================
# PART 2: Build Stage 2 Android 5.1 rootfs (a5_rootfs.cpio.lz4)
# =====================================================================
cp -a "$EMU/ramdisk_unp/." "$A5/"
rm -f "$A5"/init.goldfish.rc "$A5"/init.ranchu.rc "$A5"/fstab.goldfish "$A5"/fstab.ranchu \
      "$A5"/ueventd.goldfish.rc "$A5"/ueventd.ranchu.rc "$A5"/init.zygote32.rc \
      "$A5"/sepolicy "$A5"/selinux_version "$A5"/sbin/adbd
mkdir -p "$A5/bin" "$A5/vendor" "$A5/lib/firmware"
ln -s /lib/firmware "$A5/vendor/firmware"

# Patch AOSP 5.1 /init at 0x1778 (bl selinux_load_policy -> mov w0, #0)
python3 - <<'PY'
p = '/mnt/d/p6p_live/a5_rootfs/init'
b = bytearray(open(p, 'rb').read())
orig = bytes(b[0x1778:0x177c])
assert orig == bytes.fromhex('f14b0094'), f"Unexpected bytes: {orig.hex()}"
b[0x1778:0x177c] = bytes.fromhex('00008052')
open(p, 'wb').write(b)
print('Patched /init SELinux bypass OK')
PY

cat > "$A5/default.prop" <<'EOF'
ro.secure=0
ro.allow.mock.location=1
ro.debuggable=1
ro.adb.secure=0
ro.zygote=zygote64
ro.hardware=raven
ro.product.device=raven
ro.sf.lcd_density=560
ro.setupwizard.mode=DISABLED
ro.kernel.qemu=0
persist.sys.ui.hw=0
persist.sys.usb.config=adb
persist.sys.dalvik.vm.lib.2=libart.so
dalvik.vm.dex2oat-threads=8
dalvik.vm.image-dex2oat-threads=8
dalvik.vm.dex2oat-Xms=64m
dalvik.vm.dex2oat-Xmx=512m
dalvik.vm.image-dex2oat-Xms=64m
dalvik.vm.image-dex2oat-Xmx=128m
ro.dalvik.vm.native.bridge=0
EOF

python3 - <<'PY'
p = '/mnt/d/p6p_live/a5_rootfs/init.rc'
lines = open(p, 'r').read().splitlines()
out = []
skip_svc = False
for l in lines:
    s = l.strip()
    if s.startswith('service '):
        name = s.split()[1]
        skip_svc = name in ('healthd', 'ril-daemon', 'debuggerd', 'flash_recovery', 'adbd')
    if skip_svc:
        if s == '' or s.startswith('on '):
            skip_svc = False
        else:
            out.append('# ' + l)
            continue

    if any(k in s for k in [
        'write /sys/fs/selinux/',
        'setcon ',
        'restorecon',
        'mount rootfs rootfs / ro remount',
        '/sys/fs/cgroup/memory',
        '/dev/memcg',
        'seclabel ',
        'onrestart restart healthd',
        'on property:ro.kernel.qemu=1',
        'start adbd',
        'chmod 0444 /sys/fs/selinux/policy',
    ]):
        out.append('# ' + l)
    elif s == 'import /init.${ro.hardware}.rc':
        out.append('import /init.raven.rc')
    else:
        out.append(l)

open(p, 'w').write('\n'.join(out) + '\n')
print('Adapted init.rc for raven OK')
PY

cat > "$A5/init.raven.rc" <<'EOF'
on early-init
    mount debugfs debugfs /sys/kernel/debug
    mkdir /dev/binderfs 0755 root root
    mount binder binder /dev/binderfs
    chmod 0666 /dev/binderfs/binder

on init
    mkdir /mnt/media_rw/sdcard 0700 media_rw media_rw
    mkdir /storage/sdcard 0777 root root
    export EXTERNAL_STORAGE /storage/sdcard
    symlink /storage/sdcard /sdcard
    symlink /storage/sdcard /mnt/sdcard
    write /proc/sys/vm/swappiness 0

on fs
    setprop ro.crypto.state unencrypted
    mkdir /dev/usb-ffs 0770 shell shell
    mkdir /dev/usb-ffs/adb 0770 shell shell
    mount functionfs adb /dev/usb-ffs/adb
    mount tmpfs tmpfs /data mode=0771,uid=1000,gid=1000,size=4096M
    mount tmpfs tmpfs /cache mode=0770,uid=1000,gid=2001,size=512M

on post-fs-data
    setprop vold.post_fs_data_done 1

on boot
    setprop ro.hardware raven
    setprop ro.product.device raven
    setprop ro.sf.lcd_density 560
    setprop ro.setupwizard.mode DISABLED
    setprop debug.sf.nobootanimation 0
    setprop persist.sys.ui.hw 0
    setprop hwui.render_dirty_regions false
    setprop ro.kernel.qemu 0
    setprop sys.usb.controller 11110000.dwc3
    setprop sys.usb.state adb
    chmod 0666 /dev/dri/card0
    chmod 0666 /dev/ashmem
    chmod 0666 /dev/binder
    chmod 0666 /dev/input/event0
    chmod 0666 /dev/input/event1
    chmod 0666 /dev/input/event2
    chmod 0666 /dev/input/event3
    chmod 0666 /dev/input/event4
    chmod 0666 /dev/input/event5
    chmod 0666 /dev/input/event6
    chmod 0666 /dev/input/event7
    chmod 0666 /dev/input/event8
    chmod 0666 /dev/input/event9
    write /sys/devices/platform/exynos-drm/primary-panel/panel_idle "0"
    start tune_perf
    start mini_adbd
    start watchdog_pet
    class_start main
    class_start late_start

service tune_perf /sbin/tune_perf
    class core
    user root
    group root
    oneshot

service mini_adbd /sbin/mini_adbd /sys/kernel/config/usb_gadget/g1/UDC 11110000.dwc3
    class core
    user root
    group root

service watchdog_pet /sbin/busybox watchdog -F -t 10 /dev/watchdog
    class core
    user root
    group root
EOF

(cd "$A5" && find . | sort | cpio -o -H newc --quiet) > "$FS/a5_rootfs.cpio"
lz4 -l -9 -f "$FS/a5_rootfs.cpio" "$FS/a5_rootfs.cpio.lz4"
rm -f "$FS/a5_rootfs.cpio"
ls -lh "$FS/a5_rootfs.cpio.lz4"

# =====================================================================
# PART 3: Build Stage 1 unified initramfs & helper scripts
# =====================================================================
cp "$TOOLS/busybox-aarch64" "$FS/bin/busybox"
cp "$TOOLS/mini_adbd" "$FS/sbin/mini_adbd"
cp "$TOOLS/drm_test" "$FS/sbin/drm_test"
chmod 0755 "$FS/bin/busybox" "$FS/sbin/mini_adbd" "$FS/sbin/drm_test"
ln -s busybox "$FS/bin/sh"

cat > "$FS/sbin/tune_perf" <<'EOF'
#!/bin/sh
for p in /sys/devices/system/cpu/cpufreq/policy*; do
  [ -w "$p/scaling_governor" ] && echo performance > "$p/scaling_governor" 2>/dev/null || true
  chmod 0666 "$p/scaling_governor" 2>/dev/null || true
done
for d in /sys/class/devfreq/*; do
  [ -w "$d/governor" ] && echo performance > "$d/governor" 2>/dev/null || true
  chmod 0666 "$d/governor" 2>/dev/null || true
done
for b in /sys/class/backlight/*; do
  chmod 0666 "$b/brightness" 2>/dev/null || true
done
echo 0 > /sys/devices/platform/exynos-drm/primary-panel/panel_idle 2>/dev/null || true
echo 0 > /proc/sys/vm/swappiness 2>/dev/null || true
EOF
chmod 0755 "$FS/sbin/tune_perf"

cat > "$FS/sbin/enable_usb_net" <<'EOF'
#!/bin/sh
set -e
G=/sys/kernel/config/usb_gadget/g1
UDC_VAL=$(cat "$G/UDC" 2>/dev/null || ls /sys/class/udc | head -n 1)
echo "" > "$G/UDC" 2>/dev/null || true
mkdir -p "$G/functions/rndis.usb0"
ln -sfn "$G/functions/rndis.usb0" "$G/configs/b.1/f2" 2>/dev/null || ln -s "$G/functions/rndis.usb0" "$G/configs/b.1/f2"
echo "adb,rndis" > "$G/configs/b.1/strings/0x409/configuration"
echo "$UDC_VAL" > "$G/UDC"
ifconfig rndis0 192.168.77.2 netmask 255.255.255.0 up 2>/dev/null || ip addr add 192.168.77.2/24 dev rndis0
ip link set rndis0 up 2>/dev/null || true
echo "RNDIS ready on rndis0 (192.168.77.2)"
EOF
chmod 0755 "$FS/sbin/enable_usb_net"

cat > "$FS/sbin/boot_android5" <<'EOF'
#!/bin/sh
set -e
SYS_LZ4=""
for cand in /tmp/a5_sys.lz4 /tmp/p6p_a5_system.ext4.lz4 /data/local/tmp/a5_sys.lz4 /data/local/tmp/p6p_a5_system.ext4.lz4; do
  if [ -f "$cand" ]; then
    SYS_LZ4="$cand"
    break
  fi
done
if [ -z "$SYS_LZ4" ]; then
  echo "ERROR: Push p6p_a5_system.ext4.lz4 to /tmp/a5_sys.lz4 first!"
  exit 1
fi

echo "Preparing /newroot (tmpfs 3072M on 12GB Pixel 6 Pro RAM)..."
mkdir -p /newroot
mount -t tmpfs -o size=3072M tmpfs /newroot
echo "Unpacking Android 5.1 Stage 2 rootfs..."
(cd /newroot && lz4 -dc /a5_rootfs.cpio.lz4 | cpio -idm)
cp /bin/busybox /newroot/sbin/busybox
cp /bin/busybox /newroot/bin/busybox
ln -sf busybox /newroot/bin/sh
cp /sbin/mini_adbd /newroot/sbin/mini_adbd
cp /sbin/tune_perf /newroot/sbin/tune_perf
cp /sbin/enable_usb_net /newroot/sbin/enable_usb_net
cp -a /lib/firmware/. /newroot/lib/firmware/ 2>/dev/null || true
chmod 0750 /newroot/init /newroot/sbin/* /newroot/bin/* 2>/dev/null || true
chmod 0644 /newroot/default.prop /newroot/*.rc 2>/dev/null || true

echo "Decompressing $SYS_LZ4 -> /newroot/sys.ext4..."
lz4 -dc "$SYS_LZ4" > /newroot/sys.ext4
rm -f "$SYS_LZ4"

mkdir -p /newroot/system /newroot/proc /newroot/sys /newroot/dev /newroot/data /newroot/cache
mount -t ext4 -o loop,rw /newroot/sys.ext4 /newroot/system
if [ ! -x /newroot/system/bin/surfaceflinger ]; then
  echo "ERROR: /newroot/system/bin/surfaceflinger missing!"
  exit 1
fi

echo "Android 5.1 staged in RAM. Triggering switch_root..."
touch /tmp/.go_android5
EOF
chmod 0755 "$FS/sbin/boot_android5"

cat > "$FS/init" <<'EOF'
#!/bin/sh
/bin/busybox --install -s /bin
export PATH=/sbin:/bin:/usr/sbin:/usr/bin

mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null || mdev -s
mkdir -p /dev/pts /dev/usb-ffs/adb /sys/kernel/config /tmp /mnt /data /dev/binderfs
mount -t devpts devpts /dev/pts
mount -t configfs none /sys/kernel/config
mount -t tmpfs -o size=2048M tmpfs /tmp

exec >/tmp/init.log 2>&1
set -x

echo "=== Pixel 6 Pro (raven) Android 5.1 Live Stage 1 init ==="
uname -a
cat /proc/cmdline

# Ensure kernel firmware loader finds /lib/firmware before loading touch & Wi-Fi drivers
echo -n "/lib/firmware" > /sys/module/firmware_class/parameters/path 2>/dev/null || true

MODDIR=/lib/modules_live
if [ -f "$MODDIR/modules.order" ]; then
  # Two passes cleanly resolve cross-directory vr1 <-> vendor_dlkm symbol dependencies
  for pass in 1 2; do
    while read -r mod; do
      [ -z "$mod" ] && continue
      insmod "$MODDIR/$mod" 2>/dev/null || true
    done < "$MODDIR/modules.order"
  done
fi
mdev -s

# Setup binderfs -> /dev/binder
mount -t binder binder /dev/binderfs 2>/dev/null || true
if [ ! -c /dev/binder ] && [ -c /dev/binderfs/binder ]; then
  touch /dev/binder
  mount -o bind /dev/binderfs/binder /dev/binder
fi
chmod 0666 /dev/binder /dev/ashmem 2>/dev/null || true

# Apply max-performance governors + disable LTPO panel idle drop + unlock backlight sysfs
/sbin/tune_perf

if [ -c /dev/watchdog ]; then
  busybox watchdog -t 10 /dev/watchdog &
fi

# Configure FunctionFS ADB gadget on GS101 DWC3
G=/sys/kernel/config/usb_gadget/g1
mkdir -p "$G"
echo 0x18d1 > "$G/idVendor"
echo 0x4ee7 > "$G/idProduct"
echo 0x0200 > "$G/bcdUSB"
mkdir -p "$G/strings/0x409"
echo "PIXEL6PROLIVE" > "$G/strings/0x409/serialnumber"
echo "Google" > "$G/strings/0x409/manufacturer"
echo "Pixel 6 Pro Live" > "$G/strings/0x409/product"
mkdir -p "$G/configs/b.1/strings/0x409"
echo "adb" > "$G/configs/b.1/strings/0x409/configuration"
echo 900 > "$G/configs/b.1/MaxPower"
mkdir -p "$G/functions/ffs.adb"
ln -s "$G/functions/ffs.adb" "$G/configs/b.1/f1"
mount -t functionfs adb /dev/usb-ffs/adb

UDC=""
for i in $(seq 1 50); do
  UDC=$(ls /sys/class/udc 2>/dev/null | head -n 1)
  [ -n "$UDC" ] && break
  sleep 0.2
done

if [ -n "$UDC" ]; then
  /sbin/mini_adbd "$G/UDC" "$UDC" >/tmp/adbd.log 2>&1 &
fi

for i in $(seq 1 50); do
  [ -c /dev/dri/card0 ] && break
  sleep 0.2
done
mdev -s
/sbin/tune_perf

if [ -c /dev/dri/card0 ]; then
  /sbin/drm_test /dev/dri/card0 >/tmp/drm.log 2>&1 &
fi

while [ ! -f /tmp/.go_android5 ]; do
  sleep 0.5
done

echo "=== Handing over to Android 5.1 Stage 2 ==="
killall drm_test 2>/dev/null || true
killall mini_adbd 2>/dev/null || true
killall watchdog 2>/dev/null || true
sleep 0.5
echo "" > "$G/UDC" 2>/dev/null || true
umount /dev/usb-ffs/adb 2>/dev/null || true

if [ ! -c /dev/binder ] && [ -c /dev/binderfs/binder ]; then
  touch /dev/binder
  mount -o bind /dev/binderfs/binder /dev/binder
fi

mount --move /proc /newroot/proc
mount --move /sys /newroot/sys
mount --move /dev /newroot/dev

exec busybox switch_root /newroot /init
EOF
chmod 0755 "$FS/init"
ln -s /init "$FS/sbin/init"

# Pack unified Stage 1 initramfs
(cd "$FS" && find . | sort | cpio -o -H newc --quiet) > "$OUT/ramdisk_final.cpio"
lz4 -l -9 -f "$OUT/ramdisk_final.cpio" "$OUT/p6p_live_ramdisk.lz4"
rm -f "$OUT/ramdisk_final.cpio"

cp "$UNP/boot/kernel" "$OUT/p6p_live_kernel"

# Output 1: boot v3 (kernel + unified ramdisk, ideal for one-command fastboot boot)
python3 "$MKBOOT" \
  --header_version 3 \
  --os_version 13.0.0 \
  --os_patch_level 2023-09 \
  --kernel "$OUT/p6p_live_kernel" \
  --ramdisk "$OUT/p6p_live_ramdisk.lz4" \
  --cmdline "androidboot.selinux=permissive enforcing=0 firmware_class.path=/lib/firmware" \
  --output "$OUT/p6p_live_boot_v3.img"

# Output 2: boot v4 with ramdisk (for strict v4 fastboot boot)
python3 "$MKBOOT" \
  --header_version 4 \
  --os_version 13.0.0 \
  --os_patch_level 2023-09 \
  --kernel "$OUT/p6p_live_kernel" \
  --ramdisk "$OUT/p6p_live_ramdisk.lz4" \
  --cmdline "androidboot.selinux=permissive enforcing=0 firmware_class.path=/lib/firmware" \
  --output "$OUT/p6p_live_boot_v4_ramdisk.img"

# Output 3 + 4: Native raven v4 split (boot_v4 kernel-only + vendor_boot_v4 with stock DTB + clean Stage 1 ramdisk)
python3 "$MKBOOT" \
  --header_version 4 \
  --kernel "$OUT/p6p_live_kernel" \
  --cmdline "" \
  --output "$OUT/p6p_live_boot_v4.img"

cp "$UNP/vendor_boot/bootconfig" "$OUT/vendor_bootconfig_permissive"
if ! grep -q "androidboot.selinux=permissive" "$OUT/vendor_bootconfig_permissive"; then
  printf "\nandroidboot.selinux=permissive\n" >> "$OUT/vendor_bootconfig_permissive"
fi

python3 "$MKBOOT" \
  --header_version 4 \
  --pagesize 0x00000800 \
  --base 0x00000000 \
  --kernel_offset 0x10008000 \
  --ramdisk_offset 0x11000000 \
  --tags_offset 0x10000100 \
  --dtb_offset 0x0000000011f00000 \
  --vendor_cmdline 'disable_dma32=on earlycon=exynos4210,0x10A00000 console=ttySAC0,115200 androidboot.console=ttySAC0 printk.devkmsg=on cma_sysfs.experimental=Y swiotlb=noforce at24.write_timeout=100 log_buf_len=1024K androidboot.selinux=permissive enforcing=0 firmware_class.path=/lib/firmware bootconfig' \
  --dtb "$UNP/vendor_boot/dtb" \
  --vendor_bootconfig "$OUT/vendor_bootconfig_permissive" \
  --ramdisk_type platform \
  --ramdisk_name '' \
  --vendor_ramdisk_fragment "$OUT/p6p_live_ramdisk.lz4" \
  --vendor_boot "$OUT/p6p_live_vendor_boot_v4.img"
rm -f "$OUT/vendor_bootconfig_permissive"

echo "=== Boot images built in $OUT ==="
ls -lh "$OUT"
