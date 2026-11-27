#!/bin/bash
set -e
E=/mnt/d/p7_live/stock/aosp51_emu
VOL=/mnt/d/p7_live/stock/volantis/a5_wifi_bt
HAL=/mnt/d/p7_live/out/hal
MALI=/mnt/d/p7_live/out/mali_compat
MALI_SO=/mnt/d/p7_live/stock/panther/unpacked/v_inspect/libGLES_mali.so
VSTAGE=/mnt/d/p7_live/out/vendor_stage
BIN=/mnt/d/p7_live/out/bin
OUT=/mnt/d/p7_live/out/final
WORK=/mnt/d/p7_live/work_sys
mkdir -p "$OUT" "$WORK"

SYS="$OUT/p7_a5_system.ext4"
cp "$E/system.raw.img" "$SYS"

# Give system image ample headroom for Mali-G710 (43MB) + Pixel 7 firmware/modules (43MB) before shrinking
truncate -s +140M "$SYS"
e2fsck -fy "$SYS" >/dev/null 2>&1 || true
resize2fs "$SYS" >/dev/null

# Extract build.prop to patch it cleanly
debugfs -R "dump /build.prop $WORK/build.prop" "$SYS" >/dev/null
cat "$WORK/build.prop" | grep -vE "^ro\.product\.(device|model|board)|^ro\.sf\.lcd_density|^ro\.setupwizard\.mode|^persist\.sys\.ui\.hw|^rild\.|^wifi\.interface|^ro\.opengles\.version" > "$WORK/build.prop.new"
cat >> "$WORK/build.prop.new" <<'EOF'
ro.product.model=Pixel 7 (Android 5.1)
ro.product.device=panther
ro.product.board=panther
ro.sf.lcd_density=420
ro.setupwizard.mode=DISABLED
ro.kernel.qemu=0
persist.sys.ui.hw=1
ro.opengles.version=196609
debug.sf.nobootanimation=0
rild.libpath=/vendor/lib64/libpanther-ril.so
rild.libargs=-d /dev/umts_ipc0
wifi.interface=wlan0
EOF

printf "0 0 mali\n0 1 android\n" > "$WORK/egl.cfg"

# Build debugfs batch script
DFS="$WORK/patch.dfs"
> "$DFS"

# Pre-create needed nested directories safely (debugfs mkdir errors harmlessly if exists)
for d in \
  /vendor/bin /vendor/lib /vendor/lib64 /vendor/lib/modules_extra \
  /vendor/firmware /vendor/firmware/brcm /vendor/etc /etc/wifi; do
  echo "mkdir $d" >> "$DFS"
done

# Helper to install a file via debugfs
put_file() {
  local src="$1"
  local dst="$2"
  local mode="${3:-0100644}"
  echo "rm $dst" >> "$DFS"
  echo "write $src $dst" >> "$DFS"
  echo "set_inode_field $dst mode $mode" >> "$DFS"
  echo "set_inode_field $dst uid 0" >> "$DFS"
  echo "set_inode_field $dst gid 0" >> "$DFS"
}

del_dir() {
  local d="$1"
  local items
  items=$(debugfs -R "ls -p $d" "$SYS" 2>/dev/null | awk -F/ 'NF>=6 && $6!="." && $6!=".." {print $2 ":" $6}')
  for item in $items; do
    local mode="${item%%:*}"
    local name="${item#*:}"
    local path="$d/$name"
    case "$mode" in
      04*) del_dir "$path" ;;
      *) echo "rm $path" >> "$DFS" ;;
    esac
  done
  echo "rmdir $d" >> "$DFS"
}

echo "=== Removing emulator-only bloat (keeping Dialer, Mms, TeleService, Camera, SoundRecorder, Browser) ==="
REMOVE_APPS="
/app/Email
/app/EmulatorSmokeTests
/app/Exchange2
/app/Fallback
/app/OpenWnn
/app/PrintSpooler
/app/Protips
/app/SdkSetup
/priv-app/ManagedProvisioning
"
for d in $REMOVE_APPS; do
  del_dir "$d"
done

# Remove goldfish/ranchu HALs & emulation EGL
for f in \
  /lib64/egl/libEGL_emulation.so \
  /lib64/egl/libGLESv1_CM_emulation.so \
  /lib64/egl/libGLESv2_emulation.so \
  /lib64/hw/audio.primary.goldfish.so \
  /lib64/hw/camera.goldfish.jpeg.so \
  /lib64/hw/camera.goldfish.so \
  /lib64/hw/camera.ranchu.jpeg.so \
  /lib64/hw/camera.ranchu.so \
  /lib64/hw/gps.goldfish.so \
  /lib64/hw/gps.ranchu.so \
  /lib64/hw/gralloc.goldfish.so \
  /lib64/hw/gralloc.ranchu.so \
  /lib64/hw/lights.goldfish.so \
  /lib64/hw/power.goldfish.so \
  /lib64/hw/sensors.goldfish.so \
  /lib64/hw/sensors.ranchu.so \
  /lib64/hw/vibrator.goldfish.so; do
  echo "rm $f" >> "$DFS"
done

put_file "$WORK/build.prop.new" "/build.prop" 0100644
put_file "$WORK/egl.cfg" "/lib64/egl/egl.cfg" 0100644

# 1. HW Acceleration: Mali-G710 Valhall blob + compat shims
put_file "$MALI_SO" "/lib64/egl/libGLES_mali.so" 0100644
for s in "$MALI"/*.so; do
  bn=$(basename "$s")
  put_file "$s" "/lib64/$bn" 0100644
done

# 2. Custom Panther HALs (Graphics, Audio, BT, Camera, RIL, Misc)
for f in gralloc.panther.so hwcomposer.panther.so audio.primary.panther.so camera.panther.so bluetooth.panther.so power.default.so vibrator.default.so gps.default.so; do
  put_file "$HAL/$f" "/lib64/hw/$f" 0100644
done
# Aliases expected by AOSP 5.1 hw_get_module default lookup
put_file "$HAL/audio.primary.panther.so" "/lib64/hw/audio.primary.default.so" 0100644
put_file "$HAL/camera.panther.so"        "/lib64/hw/camera.default.so"        0100644
put_file "$HAL/bluetooth.panther.so"     "/lib64/hw/bluetooth.default.so"     0100644
put_file "$VOL/lib64/hw/audio.a2dp.default.so" "/lib64/hw/audio.a2dp.default.so" 0100644

# 3. Bluetooth vendor lib & RIL lib + test CLI
put_file "$HAL/libbt-vendor.so"  "/lib64/libbt-vendor.so"        0100644
put_file "$HAL/libbt-vendor.so"  "/vendor/lib64/libbt-vendor.so" 0100644
put_file "$HAL/libpanther-ril.so" "/lib64/libpanther-ril.so"     0100644
put_file "$HAL/libpanther-ril.so" "/vendor/lib64/libpanther-ril.so" 0100644
put_file "$BIN/panther_bt_ctl"   "/bin/panther_bt_ctl"           0100755

# 4. Wi-Fi (patched volantis libhardware_legacy.so, wpa_supplicant, hostapd, config)
put_file "$VSTAGE/lib64/libhardware_legacy.so" "/lib64/libhardware_legacy.so" 0100644
put_file "$VSTAGE/bin/wpa_supplicant"          "/bin/wpa_supplicant"          0100755
put_file "$VSTAGE/bin/hostapd"                 "/bin/hostapd"                 0100755
put_file "$VSTAGE/etc/wifi/wpa_supplicant.conf" "/etc/wifi/wpa_supplicant.conf" 0100644

# 5. AoC & Modem daemons + XML hardware permissions & Audio configs
put_file "$VSTAGE/bin/panther_aocd"            "/vendor/bin/panther_aocd"     0100755
put_file "$VSTAGE/bin/cbd"                     "/vendor/bin/cbd"              0100755
put_file "$VSTAGE/etc/mixer_paths.xml"         "/etc/mixer_paths.xml"         0100644
put_file "$VSTAGE/etc/mixer_paths.xml"         "/vendor/etc/mixer_paths.xml"  0100644
put_file "$VSTAGE/etc/audio_platform_configuration.xml" "/vendor/etc/audio_platform_configuration.xml" 0100644

for x in "$VSTAGE"/etc/permissions/*.xml; do
  bn=$(basename "$x")
  put_file "$x" "/etc/permissions/$bn" 0100644
done

# 6. Pixel 7 vendor firmware (/vendor/firmware) + 23 extra kernel modules (/vendor/lib/modules_extra)
for f in "$VSTAGE"/firmware/*; do
  [ -f "$f" ] || continue
  bn=$(basename "$f")
  put_file "$f" "/vendor/firmware/$bn" 0100644
done
for f in "$VSTAGE"/firmware/brcm/*; do
  [ -f "$f" ] || continue
  bn=$(basename "$f")
  put_file "$f" "/vendor/firmware/brcm/$bn" 0100644
done
for f in "$VSTAGE"/modules_extra/*; do
  [ -f "$f" ] || continue
  bn=$(basename "$f")
  put_file "$f" "/vendor/lib/modules_extra/$bn" 0100644
done

echo "=== Applying debugfs edits ==="
debugfs -w -f "$DFS" "$SYS" >/dev/null 2>&1 || debugfs -w -f "$DFS" "$SYS"

echo "=== Verify /lib64/hw, /lib64/egl, /vendor/firmware, /vendor/lib/modules_extra ==="
debugfs -R "ls -l /lib64/hw" "$SYS"
debugfs -R "ls -l /lib64/egl" "$SYS"
debugfs -R "ls -l /vendor/bin" "$SYS"
debugfs -R "ls /vendor/firmware" "$SYS"
debugfs -R "ls /vendor/lib/modules_extra" "$SYS"

echo "=== Shrink ext4 with e2fsck & resize2fs ==="
e2fsck -fy "$SYS" >/dev/null || true
resize2fs -M "$SYS"

echo "=== Compress with lz4 ==="
lz4 -l -9 -f "$SYS" "$SYS.lz4"
ls -lh "$SYS" "$SYS.lz4"
