#!/bin/bash
set -e
E=/mnt/d/p7_live/stock/aosp51_emu
HAL=/mnt/d/p6p_live/out/hal
OUT=/mnt/d/p6p_live/out/final
WORK=/mnt/d/p6p_live/work_sys
mkdir -p "$OUT" "$WORK"

SYS="$OUT/p6p_a5_system.ext4"
cp "$E/system.raw.img" "$SYS"

# Patch build.prop for Pixel 6 Pro (raven) + max-perf 8-core ART/Dalvik tuning
debugfs -R "dump /build.prop $WORK/build.prop" "$SYS" >/dev/null
grep -vE "^ro\.product\.(device|model|board)|^ro\.sf\.lcd_density|^ro\.setupwizard\.mode|^dalvik\.vm\.|^persist\.sys\.ui\.hw|^ro\.kernel\.qemu" "$WORK/build.prop" > "$WORK/build.prop.new"
cat >> "$WORK/build.prop.new" <<'EOF'
ro.product.model=Pixel 6 Pro (Android 5.1 Live)
ro.product.device=raven
ro.product.board=raven
ro.sf.lcd_density=560
ro.setupwizard.mode=DISABLED
ro.kernel.qemu=0
persist.sys.ui.hw=0
debug.egl.hw=0
debug.sf.nobootanimation=0
hwui.render_dirty_regions=false
dalvik.vm.dex2oat-threads=8
dalvik.vm.image-dex2oat-threads=8
dalvik.vm.dex2oat-Xms=64m
dalvik.vm.dex2oat-Xmx=512m
dalvik.vm.image-dex2oat-Xms=64m
dalvik.vm.image-dex2oat-Xmx=128m
dalvik.vm.heapstartsize=16m
dalvik.vm.heapgrowthlimit=256m
dalvik.vm.heapsize=512m
dalvik.vm.heaptargetutilization=0.75
dalvik.vm.heapminfree=4m
dalvik.vm.heapmaxfree=16m
EOF

printf "0 0 android\n" > "$WORK/egl.cfg"

# Generic touchscreen .idc configs so EventHub registers direct multitouch cleanly
cat > "$WORK/touch_generic.idc" <<'EOF'
touch.deviceType = touchScreen
touch.orientationAware = 1
keyboard.builtIn = 0
EOF

DFS="$WORK/patch.dfs"
> "$DFS"

del_dir() {
  local d="$1"
  local items
  items=$(debugfs -R "ls -p $d" "$SYS" 2>/dev/null | awk -F/ 'NF>=6 && $6!="." && $6!=".." {print $3 ":" $6}')
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

echo "=== Stripping ONLY incompatible telephony/emulator setup APKs (keeping full user apps) ==="
REMOVE_APPS="
/app/EmulatorSmokeTests
/app/Fallback
/app/OpenWnn
/app/SdkSetup
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

# Strip goldfish/ranchu HALs and broken emulation EGL
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

echo "rm /build.prop" >> "$DFS"
echo "write $WORK/build.prop.new /build.prop" >> "$DFS"
echo "set_inode_field /build.prop mode 0100644" >> "$DFS"
echo "set_inode_field /build.prop uid 0" >> "$DFS"
echo "set_inode_field /build.prop gid 0" >> "$DFS"

echo "rm /lib64/egl/egl.cfg" >> "$DFS"
echo "write $WORK/egl.cfg /lib64/egl/egl.cfg" >> "$DFS"
echo "set_inode_field /lib64/egl/egl.cfg mode 0100644" >> "$DFS"
echo "set_inode_field /lib64/egl/egl.cfg uid 0" >> "$DFS"
echo "set_inode_field /lib64/egl/egl.cfg gid 0" >> "$DFS"

for idc in ftm5_ts.idc sec_touchscreen.idc sec_tsp.idc google_touchscreen.idc; do
  echo "rm /usr/idc/$idc" >> "$DFS"
  echo "write $WORK/touch_generic.idc /usr/idc/$idc" >> "$DFS"
  echo "set_inode_field /usr/idc/$idc mode 0100644" >> "$DFS"
  echo "set_inode_field /usr/idc/$idc uid 0" >> "$DFS"
  echo "set_inode_field /usr/idc/$idc gid 0" >> "$DFS"
done

for f in gralloc.raven.so hwcomposer.raven.so lights.raven.so power.raven.so audio.primary.default.so vibrator.default.so gps.default.so; do
  echo "rm /lib64/hw/$f" >> "$DFS"
  echo "write $HAL/$f /lib64/hw/$f" >> "$DFS"
  echo "set_inode_field /lib64/hw/$f mode 0100644" >> "$DFS"
  echo "set_inode_field /lib64/hw/$f uid 0" >> "$DFS"
  echo "set_inode_field /lib64/hw/$f gid 0" >> "$DFS"
done

echo "=== Applying debugfs edits to p6p_a5_system.ext4 ==="
debugfs -w -f "$DFS" "$SYS" >/dev/null 2>&1 || debugfs -w -f "$DFS" "$SYS"

echo "=== Verify /lib64/hw, /lib64/egl, /usr/idc, /app ==="
debugfs -R "ls -l /lib64/hw" "$SYS"
debugfs -R "ls -l /lib64/egl" "$SYS"
debugfs -R "ls -l /usr/idc" "$SYS"
debugfs -R "ls -l /app" "$SYS"

echo "=== Shrink + 32M headroom ext4 ==="
e2fsck -fy "$SYS" || true
resize2fs -M "$SYS"
e2fsck -fy "$SYS" || true
CUR_KB=$(du -k "$SYS" | awk '{print $1}')
NEW_MB=$(( (CUR_KB / 1024) + 32 ))
resize2fs "$SYS" "${NEW_MB}M"
e2fsck -fy "$SYS"
ls -lh "$SYS"

echo "=== Compressing p6p_a5_system.ext4 with lz4 ==="
lz4 -l -9 -f "$SYS" "$OUT/p6p_a5_system.ext4.lz4"
rm -f "$SYS"
ls -lh "$OUT/p6p_a5_system.ext4.lz4"
