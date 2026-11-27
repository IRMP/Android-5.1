#!/bin/bash
set -e
E=/mnt/d/p7_live/stock/aosp51_emu
HAL=/mnt/d/p7_live/out/hal
OUT=/mnt/d/p7_live/out/final
WORK=/mnt/d/p7_live/work_sys
mkdir -p "$OUT" "$WORK"

SYS="$OUT/p7_a5_system.ext4"
cp "$E/system.raw.img" "$SYS"

# Extract build.prop to patch it cleanly
debugfs -R "dump /build.prop $WORK/build.prop" "$SYS" >/dev/null
cat "$WORK/build.prop" | grep -vE "^ro\.product\.(device|model|board)|^ro\.sf\.lcd_density|^ro\.setupwizard\.mode" > "$WORK/build.prop.new"
cat >> "$WORK/build.prop.new" <<'EOF'
ro.product.model=Pixel 7 (Android 5.1 PoC)
ro.product.device=panther
ro.product.board=panther
ro.sf.lcd_density=420
ro.setupwizard.mode=DISABLED
ro.kernel.qemu=0
persist.sys.ui.hw=0
debug.sf.nobootanimation=0
EOF

printf "0 0 android\n" > "$WORK/egl.cfg"

# Build debugfs batch script to remove bloat & install our HALs
DFS="$WORK/patch.dfs"
> "$DFS"

# Helper to recursively delete a directory in debugfs
del_dir() {
  local d="$1"
  local items
  items=$(debugfs -R "ls -p $d" "$SYS" 2>/dev/null | awk -F/ 'NF>=6 && $6!="." && $6!=".." {print $2 ":" $6}')
  for item in $items; do
    local mode="${item%%:*}"
    local name="${item#*:}"
    local path="$d/$name"
    # mode starting with 04 is directory
    case "$mode" in
      04*) del_dir "$path" ;;
      *) echo "rm $path" >> "$DFS" ;;
    esac
  done
  echo "rmdir $d" >> "$DFS"
}

echo "=== Scanning bloat dirs in system.ext4 ==="
REMOVE_APPS="
/app/Browser
/app/Calendar
/app/Email
/app/EmulatorSmokeTests
/app/Exchange2
/app/Fallback
/app/Gallery
/app/LegacyCamera
/app/LiveWallpapersPicker
/app/Music
/app/OpenWnn
/app/PicoTts
/app/PrintSpooler
/app/Protips
/app/QuickSearchBox
/app/SdkSetup
/app/SoundRecorder
/app/SpeechRecorder
/app/webview
/priv-app/Contacts
/priv-app/Dialer
/priv-app/ManagedProvisioning
/priv-app/Mms
/priv-app/MmsService
/priv-app/TeleService
/priv-app/Telecom
/priv-app/TelephonyProvider
"
for d in $REMOVE_APPS; do
  del_dir "$d"
done

# Remove emulator / goldfish / ranchu HALs & emulation EGL
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

# Install our HALs, egl.cfg, and patched build.prop
echo "rm /build.prop" >> "$DFS"
echo "write $WORK/build.prop.new /build.prop" >> "$DFS"
echo "set_inode_field /build.prop mode 0100644" >> "$DFS"
echo "set_inode_field /build.prop uid 0" >> "$DFS"
echo "set_inode_field /build.prop gid 0" >> "$DFS"

echo "write $WORK/egl.cfg /lib64/egl/egl.cfg" >> "$DFS"
echo "set_inode_field /lib64/egl/egl.cfg mode 0100644" >> "$DFS"
echo "set_inode_field /lib64/egl/egl.cfg uid 0" >> "$DFS"
echo "set_inode_field /lib64/egl/egl.cfg gid 0" >> "$DFS"

for f in gralloc.panther.so hwcomposer.panther.so audio.primary.default.so power.default.so vibrator.default.so gps.default.so; do
  echo "rm /lib64/hw/$f" >> "$DFS"
  echo "write $HAL/$f /lib64/hw/$f" >> "$DFS"
  echo "set_inode_field /lib64/hw/$f mode 0100644" >> "$DFS"
  echo "set_inode_field /lib64/hw/$f uid 0" >> "$DFS"
  echo "set_inode_field /lib64/hw/$f gid 0" >> "$DFS"
done

echo "=== Applying debugfs edits ==="
debugfs -w -f "$DFS" "$SYS" >/dev/null 2>&1 || debugfs -w -f "$DFS" "$SYS"

echo "=== Verify /lib64/hw and /lib64/egl ==="
debugfs -R "ls -l /lib64/hw" "$SYS"
debugfs -R "ls -l /lib64/egl" "$SYS"

echo "=== Shrink ext4 with e2fsck & resize2fs ==="
e2fsck -fy "$SYS" || true
resize2fs -M "$SYS"
e2fsck -fy "$SYS" || true
ls -lh "$SYS"

echo "=== Compressing p7_a5_system.ext4 with lz4 ==="
lz4 -l -9 -f "$SYS" "$OUT/p7_a5_system.ext4.lz4"
rm -f "$SYS"
ls -lh "$OUT/p7_a5_system.ext4.lz4"
