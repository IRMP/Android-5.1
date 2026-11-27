#!/bin/bash
set -e
ROOT=/mnt/d/p7_live
EMU=$ROOT/stock/aosp51_emu
STOCK=$ROOT/stock/panther
UNP=$STOCK/unpacked
MKBOOT=$ROOT/tools/mkbootimg/mkbootimg.py
A5=$ROOT/a5_rootfs
FS=$ROOT/final_initramfs
OUT=$ROOT/out/final

rm -rf "$A5" "$FS"
mkdir -p "$A5" "$FS"/{bin,sbin,etc,proc,sys,dev,tmp,mnt,data,newroot,lib/modules_live}
mkdir -p "$OUT"

# ==========================================
# PART 1: Build Stage 2 Android 5.1 rootfs
# ==========================================
cp -a "$EMU/ramdisk_unp/." "$A5/"
rm -f "$A5"/init.goldfish.rc "$A5"/init.ranchu.rc "$A5"/fstab.goldfish "$A5"/fstab.ranchu \
      "$A5"/ueventd.goldfish.rc "$A5"/ueventd.ranchu.rc "$A5"/init.zygote32.rc \
      "$A5"/sepolicy "$A5"/selinux_version

# Patch /init binary at 0x1778 (bl 0x41473c -> mov w0, #0) to bypass SELinux load
python3 - <<'PY'
p = '/mnt/d/p7_live/a5_rootfs/init'
b = bytearray(open(p, 'rb').read())
orig = bytes(b[0x1778:0x177c])
print('init[0x1778..0x177c] =', orig.hex())
assert orig == bytes.fromhex('f14b0094'), f"Unexpected bytes: {orig.hex()}"
b[0x1778:0x177c] = bytes.fromhex('00008052') # mov w0, #0
open(p, 'wb').write(b)
print('Patched init SELinux bypass OK')
PY

# Remove stock adbd; /sbin/boot_android5 copies busybox & mini_adbd from Stage 1 RAM into /newroot
rm -f "$A5/sbin/adbd"
mkdir -p "$A5/bin"

cat > "$A5/default.prop" <<'EOF'
ro.secure=0
ro.allow.mock.location=1
ro.debuggable=1
ro.adb.secure=0
ro.zygote=zygote64
ro.hardware=panther
ro.product.device=panther
ro.sf.lcd_density=420
ro.setupwizard.mode=DISABLED
ro.kernel.qemu=0
persist.sys.ui.hw=1
ro.opengles.version=196609
rild.libpath=/vendor/lib64/libpanther-ril.so
rild.libargs=-d /dev/umts_ipc0
wifi.interface=wlan0
persist.sys.usb.config=adb
persist.sys.dalvik.vm.lib.2=libart.so
dalvik.vm.dex2oat-Xms=64m
dalvik.vm.dex2oat-Xmx=512m
dalvik.vm.image-dex2oat-Xms=64m
dalvik.vm.image-dex2oat-Xmx=64m
ro.dalvik.vm.native.bridge=0
EOF

# Adapt init.rc cleanly using Python
python3 - <<'PY'
p = '/mnt/d/p7_live/a5_rootfs/init.rc'
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
        out.append('import /init.panther.rc')
    else:
        out.append(l)

open(p, 'w').write('\n'.join(out) + '\n')
print('Adapted init.rc OK')
PY

cat > "$A5/init.panther.rc" <<'EOF'
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

on fs
    setprop ro.crypto.state unencrypted
    mkdir /dev/usb-ffs 0770 shell shell
    mkdir /dev/usb-ffs/adb 0770 shell shell
    mount functionfs adb /dev/usb-ffs/adb
    mount tmpfs tmpfs /data mode=0771,uid=1000,gid=1000,size=1G
    mount tmpfs tmpfs /cache mode=0770,uid=1000,gid=2001,size=256M

on post-fs-data
    mkdir /data/misc 0771 system misc
    mkdir /data/misc/wifi 0770 wifi wifi
    mkdir /data/misc/wifi/sockets 0770 wifi wifi
    mkdir /data/misc/dhcp 0770 dhcp dhcp
    mkdir /data/vendor 0771 root system
    mkdir /data/vendor/log 0775 root radio
    mkdir /data/vendor/log/cbd 0775 root radio
    mkdir /mnt/vendor 0755 root system
    mkdir /mnt/vendor/modem_img 0755 root system
    mkdir /mnt/vendor/efs 0771 root radio
    copy /system/etc/wifi/wpa_supplicant.conf /data/misc/wifi/wpa_supplicant.conf
    chmod 0660 /data/misc/wifi/wpa_supplicant.conf
    chown wifi wifi /data/misc/wifi/wpa_supplicant.conf
    setprop vold.post_fs_data_done 1

on boot
    setprop ro.hardware panther
    setprop ro.product.device panther
    setprop ro.sf.lcd_density 420
    setprop ro.setupwizard.mode DISABLED
    setprop debug.sf.nobootanimation 0
    setprop persist.sys.ui.hw 1
    setprop ro.opengles.version 196609
    setprop hwui.render_dirty_regions false
    setprop ro.kernel.qemu 0
    setprop wifi.interface wlan0
    setprop rild.libpath /vendor/lib64/libpanther-ril.so
    setprop rild.libargs "-d /dev/umts_ipc0"
    setprop sys.usb.controller 11210000.dwc3
    setprop sys.usb.state adb
    chmod 0666 /dev/dri/card0
    chmod 0666 /dev/mali0
    chmod 0666 /dev/dma_heap/system
    chmod 0666 /dev/ashmem
    chmod 0666 /dev/binder
    chmod 0666 /dev/aoc
    chmod 0666 /dev/acd-audio
    chmod 0666 /dev/snd/controlC0
    chmod 0666 /dev/snd/pcmC0D1p
    chmod 0666 /dev/snd/pcmC0D5p
    chmod 0666 /dev/snd/pcmC0D8c
    chmod 0666 /dev/ttySAC18
    chmod 0666 /proc/bluetooth/sleep/lpm
    chmod 0666 /proc/bluetooth/sleep/btwrite
    chmod 0666 /sys/class/rfkill/rfkill0/state
    chmod 0666 /sys/class/rfkill/rfkill1/state
    chmod 0666 /dev/umts_ipc0
    chmod 0666 /dev/umts_ipc1
    chmod 0666 /dev/umts_boot0
    chmod 0666 /dev/input/event0
    chmod 0666 /dev/input/event1
    chmod 0666 /dev/input/event2
    chmod 0666 /dev/input/event3
    chmod 0666 /dev/input/event4
    chmod 0666 /dev/input/event5
    start mini_adbd
    start watchdog_pet
    start panther_aocd
    start cpboot-daemon
    class_start main
    class_start late_start

service mini_adbd /sbin/mini_adbd /sys/kernel/config/usb_gadget/g1/UDC 11210000.dwc3
    class core
    user root
    group root

service watchdog_pet /sbin/busybox watchdog -F -t 10 /dev/watchdog
    class core
    user root
    group root

service panther_aocd /system/vendor/bin/panther_aocd
    class core
    user root
    group root system audio

service cpboot-daemon /system/vendor/bin/cbd -d -t s5100 -P by-name/modem -s 2
    setenv LD_PRELOAD /system/lib64/libpanther_chk.so
    class main
    user root
    group radio system cache inet misc audio log
    oneshot

service ril-daemon /system/bin/rild -l /vendor/lib64/libpanther-ril.so -- -d /dev/umts_ipc0
    class main
    socket rild stream 660 root radio
    socket rild-debug stream 660 radio system
    user root
    group radio cache inet misc audio log

service wpa_supplicant /system/bin/wpa_supplicant -iwlan0 -Dnl80211 -c/data/misc/wifi/wpa_supplicant.conf -O/data/misc/wifi/sockets -g@android:wpa_wlan0
    class main
    socket wpa_wlan0 dgram 660 wifi wifi
    disabled
    oneshot

service dhcpcd_wlan0 /system/bin/dhcpcd -aABDKL
    class main
    disabled
    oneshot

service iprenew_wlan0 /system/bin/dhcpcd -n
    class main
    disabled
    oneshot
EOF

(cd "$A5" && find . | sort | cpio -o -H newc --quiet) > "$FS/a5_rootfs.cpio"
lz4 -l -9 -f "$FS/a5_rootfs.cpio" "$FS/a5_rootfs.cpio.lz4"
rm -f "$FS/a5_rootfs.cpio"
ls -lh "$FS/a5_rootfs.cpio.lz4"

# ==========================================
# PART 2: Build Stage 1 unified initramfs
# ==========================================
cp "$ROOT/tools/busybox-aarch64" "$FS/bin/busybox"
cp "$ROOT/tools/mini_adbd" "$FS/sbin/mini_adbd"
cp "$ROOT/tools/drm_test" "$FS/sbin/drm_test"
chmod 0755 "$FS/bin/busybox" "$FS/sbin/mini_adbd" "$FS/sbin/drm_test"
ln -s busybox "$FS/bin/sh"

# Reuse curated 5.10 modules from step1_initramfs
cp -a "$ROOT/step1_initramfs/lib/modules_live/." "$FS/lib/modules_live/"

cat > "$FS/sbin/boot_android5" <<'EOF'
#!/bin/sh
set -e
SYS_LZ4=""
for cand in /tmp/a5_sys.lz4 /tmp/p7_a5_system.ext4.lz4 /data/local/tmp/a5_sys.lz4 /data/local/tmp/p7_a5_system.ext4.lz4; do
  if [ -f "$cand" ]; then
    SYS_LZ4="$cand"
    break
  fi
done
if [ -z "$SYS_LZ4" ]; then
  echo "ERROR: Push p7_a5_system.ext4.lz4 to /tmp/a5_sys.lz4 first!"
  exit 1
fi

echo "Preparing /newroot (tmpfs 1536M)..."
mkdir -p /newroot
mount -t tmpfs -o size=1536M tmpfs /newroot
echo "Unpacking Android 5.1 rootfs..."
(cd /newroot && lz4 -dc /a5_rootfs.cpio.lz4 | cpio -idm)
cp /bin/busybox /newroot/sbin/busybox
cp /bin/busybox /newroot/bin/busybox
ln -sf busybox /newroot/bin/sh
cp /sbin/mini_adbd /newroot/sbin/mini_adbd
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

# Bind-mount /vendor so kernel request_firmware() finds /vendor/firmware/* during extra module insmod
mkdir -p /vendor
mount -o bind /newroot/system/vendor /vendor 2>/dev/null || true

EXTRA_MODDIR="/vendor/lib/modules_extra"
if [ -f "$EXTRA_MODDIR/modules_extra.order" ]; then
  echo "Loading Pixel 7 extra hardware modules (Wi-Fi, AoC Audio, Mali-G710, Modem CPIF, Camera LWIS)..."
  while read -r emod; do
    [ -z "$emod" ] && continue
    case "$emod" in
      bcmdhd4389.ko)
        insmod "$EXTRA_MODDIR/$emod" \
          firmware_path=/vendor/firmware/fw_bcmdhd.bin \
          nvram_path=/vendor/firmware/bcmdhd.cal \
          clm_path=/vendor/firmware/bcmdhd_clm.blob 2>/dev/null || true
        ;;
      *)
        insmod "$EXTRA_MODDIR/$emod" 2>/dev/null || true
        ;;
    esac
  done < "$EXTRA_MODDIR/modules_extra.order"
  mdev -s 2>/dev/null || true
  chmod 0666 /dev/mali* /dev/dma_heap/* /dev/aoc* /dev/acd-* /dev/snd/* /dev/ttySAC* /dev/umts_* /dev/lwis-* /dev/video* 2>/dev/null || true
fi

# Mount stock modem firmware partition read-only for cbd if present
mkdir -p /newroot/mnt/vendor/modem_img /newroot/mnt/vendor/efs
SLOT=$(cat /proc/cmdline | tr ' ' '\n' | sed -n 's/^androidboot.slot_suffix=//p' | head -n 1)
for mp in "/dev/block/platform/14700000.ufs/by-name/modem${SLOT}" "/dev/block/platform/14700000.ufs/by-name/modem_a" "/dev/block/platform/14700000.ufs/by-name/modem_b"; do
  if [ -b "$mp" ]; then
    mount -t erofs -o ro "$mp" /newroot/mnt/vendor/modem_img 2>/dev/null || \
    mount -t ext4 -o ro "$mp" /newroot/mnt/vendor/modem_img 2>/dev/null || true
    break
  fi
done

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
mount -t tmpfs -o size=1536M tmpfs /tmp

exec >/tmp/init.log 2>&1
set -x

echo "=== Pixel 7 Live Stage 1 init ==="
uname -a

MODDIR=/lib/modules_live
if [ -f "$MODDIR/modules.order" ]; then
  while read -r mod; do
    [ -z "$mod" ] && continue
    insmod "$MODDIR/$mod" 2>/dev/null || true
  done < "$MODDIR/modules.order"
fi
mdev -s

# Setup binderfs -> /dev/binder if kernel didn't create /dev/binder directly
mount -t binder binder /dev/binderfs 2>/dev/null || true
if [ ! -c /dev/binder ] && [ -c /dev/binderfs/binder ]; then
  touch /dev/binder
  mount -o bind /dev/binderfs/binder /dev/binder
fi
chmod 0666 /dev/binder /dev/ashmem 2>/dev/null || true

if [ -c /dev/watchdog ]; then
  busybox watchdog -t 10 /dev/watchdog &
fi

G=/sys/kernel/config/usb_gadget/g1
mkdir -p "$G"
echo 0x18d1 > "$G/idVendor"
echo 0x4ee7 > "$G/idProduct"
echo 0x0200 > "$G/bcdUSB"
mkdir -p "$G/strings/0x409"
echo "PIXEL7LIVE" > "$G/strings/0x409/serialnumber"
echo "Google" > "$G/strings/0x409/manufacturer"
echo "Pixel 7 Live" > "$G/strings/0x409/product"
mkdir -p "$G/configs/b.1/strings/0x409"
echo "adb" > "$G/configs/b.1/strings/0x409/configuration"
echo 500 > "$G/configs/b.1/MaxPower"
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

if [ -c /dev/dri/card0 ]; then
  /sbin/drm_test /dev/dri/card0 >/tmp/drm.log 2>&1 &
fi

# Wait for Stage 2 trigger (/sbin/boot_android5)
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

# Ensure /dev/binder is ready inside /dev before moving /dev to /newroot/dev
if [ ! -c /dev/binder ] && [ -c /dev/binderfs/binder ]; then
  touch /dev/binder
  mount -o bind /dev/binderfs/binder /dev/binder
fi

umount /vendor 2>/dev/null || true

mount --move /proc /newroot/proc
mount --move /sys /newroot/sys
mount --move /dev /newroot/dev

exec busybox switch_root /newroot /init
EOF
chmod 0755 "$FS/init"
ln -s /init "$FS/sbin/init"

# Pack Stage 1 initramfs
(cd "$FS" && find . | sort | cpio -o -H newc --quiet) > "$OUT/ramdisk_final.cpio"
lz4 -l -9 -f "$OUT/ramdisk_final.cpio" "$OUT/p7_live_ramdisk.lz4"
rm -f "$OUT/ramdisk_final.cpio"

cp "$UNP/boot/kernel" "$OUT/p7_live_kernel"

python3 "$MKBOOT" \
  --header_version 3 \
  --os_version 13.0.0 \
  --os_patch_level 2023-09 \
  --kernel "$OUT/p7_live_kernel" \
  --ramdisk "$OUT/p7_live_ramdisk.lz4" \
  --cmdline "androidboot.selinux=permissive enforcing=0" \
  --output "$OUT/p7_live_boot_v3.img"

python3 "$MKBOOT" \
  --header_version 4 \
  --kernel "$OUT/p7_live_kernel" \
  --cmdline "" \
  --output "$OUT/p7_live_boot_v4.img"

python3 "$MKBOOT" \
  --header_version 4 \
  --ramdisk "$OUT/p7_live_ramdisk.lz4" \
  --cmdline "" \
  --output "$OUT/p7_live_init_boot_v4.img"

cp "$ROOT/out/step1/p7_step1_vendor_boot_v4.img" "$OUT/p7_live_vendor_boot_v4.img"

echo "=== Final artifacts in $OUT ==="
ls -lh "$OUT"
echo "=== Total disk usage of $ROOT ==="
du -sh "$ROOT"/* "$ROOT"
