#!/bin/bash
set -e
ROOT=/mnt/d/p7_live
STOCK=$ROOT/stock/panther
UNP=$STOCK/unpacked
FS=$ROOT/step1_initramfs
OUT=$ROOT/out/step1
MKBOOT=$ROOT/tools/mkbootimg/mkbootimg.py

rm -rf "$FS" "$OUT"
mkdir -p "$FS"/{bin,sbin,etc,proc,sys,dev,tmp,mnt,data,lib/modules_live}
mkdir -p "$OUT"

# Core static binaries
cp "$ROOT/tools/busybox-aarch64" "$FS/bin/busybox"
cp "$ROOT/tools/mini_adbd" "$FS/sbin/mini_adbd"
cp "$ROOT/tools/drm_test" "$FS/sbin/drm_test"
chmod 0755 "$FS/bin/busybox" "$FS/sbin/mini_adbd" "$FS/sbin/drm_test"
ln -s busybox "$FS/bin/sh"

# Copy curated 5.10 modules from vendor_kernel_boot in modules.load order
# Skip heavy unused subsystems & watchdogs (s3c2410_wdt, softdog, hardlockup-watchdog, aoc, modem, uwb, mfc)
> "$FS/lib/modules_live/modules.order"
while read -r mod; do
  [ -z "$mod" ] && continue
  case "$mod" in
    *wdt*|*softdog*|*watchdog*) continue ;;
    aoc_*|cpif*|dw3000*|mcps802154*|exynos_mfc*|snd-soc-*|st33spi*|st54spi*|st21nfc*|bcm47765*|boot_device_spi*|g2d*|janeiro*) continue ;;
  esac
  if [ -f "$UNP/vk_root/lib/modules/$mod" ]; then
    cp "$UNP/vk_root/lib/modules/$mod" "$FS/lib/modules_live/$mod"
    echo "$mod" >> "$FS/lib/modules_live/modules.order"
  fi
done < "$UNP/vk_root/lib/modules/modules.load"

# Add touchscreen + regulator modules from vendor_dlkm.raw.img
VDLKM_MODS="
pinctrl-slg51000.ko
pinctrl-slg51002.ko
slg51000-core.ko
slg51002-core.ko
slg51000-regulator.ko
slg51002-regulator.ko
vh_mm.ko
goog_touch_interface.ko
focal_touch.ko
sec_touch.ko
syna_touch.ko
"
for mod in $VDLKM_MODS; do
  debugfs -R "dump /lib/modules/$mod $FS/lib/modules_live/$mod" "$STOCK/vendor_dlkm.raw.img" >/dev/null 2>&1 || true
  if [ -s "$FS/lib/modules_live/$mod" ]; then
    echo "$mod" >> "$FS/lib/modules_live/modules.order"
  else
    rm -f "$FS/lib/modules_live/$mod"
  fi
done

# Write /init script
cat > "$FS/init" <<'EOF'
#!/bin/sh
/bin/busybox --install -s /bin
export PATH=/sbin:/bin:/usr/sbin:/usr/bin

mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null || mdev -s
mkdir -p /dev/pts /dev/usb-ffs/adb /sys/kernel/config /tmp /mnt /data
mount -t devpts devpts /dev/pts
mount -t configfs none /sys/kernel/config
mount -t tmpfs -o size=75% tmpfs /tmp

exec >/tmp/init.log 2>&1
set -x

echo "=== Pixel 7 Live Step 1 init ==="
uname -a
cat /proc/cmdline

MODDIR=/lib/modules_live
if [ -f "$MODDIR/modules.order" ]; then
  while read -r mod; do
    [ -z "$mod" ] && continue
    insmod "$MODDIR/$mod" 2>/dev/null || echo "insmod skip/warn: $mod"
  done < "$MODDIR/modules.order"
fi

mdev -s

# Pet watchdog if present
if [ -c /dev/watchdog ]; then
  busybox watchdog -t 10 /dev/watchdog &
fi

# Setup USB Gadget FunctionFS for ADB
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
echo "UDC=$UDC"

if [ -n "$UDC" ]; then
  /sbin/mini_adbd "$G/UDC" "$UDC" >/tmp/adbd.log 2>&1 &
fi

for i in $(seq 1 50); do
  [ -c /dev/dri/card0 ] && break
  sleep 0.2
done
mdev -s
ls -la /dev/dri /dev/input || true

if [ -c /dev/dri/card0 ]; then
  /sbin/drm_test /dev/dri/card0 >/tmp/drm.log 2>&1 &
else
  echo "ERROR: /dev/dri/card0 not found"
  dmesg | tail -n 50 > /tmp/dmesg_drm_err.txt
fi

while true; do
  sleep 60
done
EOF
chmod 0755 "$FS/init"
ln -s /init "$FS/sbin/init"

# Pack cpio + legacy lz4 (02 21 4c 18) matching Pixel 7 kernel/init_boot
echo "=== Packing initramfs ==="
(cd "$FS" && find . | sort | cpio -o -H newc --quiet) > "$OUT/ramdisk_step1.cpio"
lz4 -l -9 -f "$OUT/ramdisk_step1.cpio" "$OUT/p7_step1_ramdisk.lz4"
rm -f "$OUT/ramdisk_step1.cpio"
ls -lh "$OUT/p7_step1_ramdisk.lz4"

# Export raw kernel
cp "$UNP/boot/kernel" "$OUT/p7_step1_kernel"

# 1) Boot v3 (kernel + ramdisk + cmdline) for fastboot boot testing
python3 "$MKBOOT" \
  --header_version 3 \
  --os_version 13.0.0 \
  --os_patch_level 2023-09 \
  --kernel "$OUT/p7_step1_kernel" \
  --ramdisk "$OUT/p7_step1_ramdisk.lz4" \
  --cmdline "androidboot.selinux=permissive enforcing=0" \
  --output "$OUT/p7_step1_boot_v3.img"

# 2) Boot v4 (kernel only, matching stock boot.img)
python3 "$MKBOOT" \
  --header_version 4 \
  --kernel "$OUT/p7_step1_kernel" \
  --cmdline "" \
  --output "$OUT/p7_step1_boot_v4.img"

# 3) Init_boot v4 (ramdisk only, <= 8 MB, matching stock init_boot.img)
python3 "$MKBOOT" \
  --header_version 4 \
  --ramdisk "$OUT/p7_step1_ramdisk.lz4" \
  --cmdline "" \
  --output "$OUT/p7_step1_init_boot_v4.img"

# 4) Vendor_boot v4 with androidboot.selinux=permissive
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
  --vendor_cmdline 'swiotlb=noforce dyndbg="func alloc_contig_dump_pages +p" earlycon=exynos4210,0x10A00000 console=ttySAC0,115200 androidboot.console=ttySAC0 printk.devkmsg=on cma_sysfs.experimental=Y cgroup_disable=memory rcupdate.rcu_expedited=1 rcu_nocbs=all stack_depot_disable=off page_pinner=on swiotlb=1024 disable_dma32=on at24.write_timeout=100 log_buf_len=1024K androidboot.selinux=permissive enforcing=0 bootconfig' \
  --vendor_bootconfig "$OUT/vendor_bootconfig_permissive" \
  --ramdisk_type 1 \
  --ramdisk_name '' \
  --vendor_ramdisk_fragment "$UNP/vendor_boot/vendor_ramdisk00" \
  --vendor_boot "$OUT/p7_step1_vendor_boot_v4.img"
rm -f "$OUT/vendor_bootconfig_permissive"

# Remove large factory zip to save disk space (keep extracted stock partition images)
rm -f "$ROOT/stock/panther-tq3a.230901.001.c2-factory-51bde7e8.zip"

echo "=== Step 1 artifacts ==="
ls -lh "$OUT"
echo "=== Disk usage of /mnt/d/p7_live ==="
du -sh "$ROOT"/* "$ROOT"
