#!/bin/bash
set -e
DIR=/mnt/d/p7_live/stock/volantis
OUT=/mnt/d/p7_live/stock/volantis/a5_wifi_bt
mkdir -p "$DIR/tmp_dl" "$OUT"

ZIP="$DIR/volantis-lmy48t-factory-b925de62.zip"
curl -L -C - -o "$ZIP" "https://dl.google.com/dl/android/aosp/volantis-lmy48t-factory-b925de62.zip"

IZIP_NAME=$(unzip -l "$ZIP" | awk '/image-volantis-.*\.zip$/ {print $4}' | head -n1)
unzip -o -j "$ZIP" "$IZIP_NAME" -d "$DIR/tmp_dl"
IZIP="$DIR/tmp_dl/$(basename "$IZIP_NAME")"
unzip -o -j "$IZIP" system.img -d "$DIR/tmp_dl"
rm -f "$ZIP" "$IZIP"

/mnt/d/p7_live/tools/simg2img "$DIR/tmp_dl/system.img" "$DIR/tmp_dl/system.raw.img"
rm -f "$DIR/tmp_dl/system.img"

SYS="$DIR/tmp_dl/system.raw.img"
echo "=== Volantis Bluetooth/WiFi inventory ==="
debugfs -R "ls -l bin" "$SYS" | grep -E "wpa|hostapd|dhcp" || true
debugfs -R "ls -l lib64/hw" "$SYS" | grep -iE "bt|blue|audio|power" || true
debugfs -R "ls -l lib64" "$SYS" | grep -iE "bt|blue|hardware_legacy|wpa|keystore" || true
debugfs -R "ls -l lib/hw" "$SYS" | grep -iE "bt|blue" || true
debugfs -R "ls -l app" "$SYS" | grep -iE "Blue|Nfc" || true

for p in \
  bin/wpa_supplicant \
  bin/hostapd \
  lib64/hw/bluetooth.default.so \
  lib64/hw/audio.a2dp.default.so \
  lib64/libbluetooth_jni.so \
  lib64/libbt-hci.so \
  lib64/libbt-utils.so \
  lib64/libbt-vendor.so \
  lib64/libhardware_legacy.so \
  lib/hw/bluetooth.default.so \
  lib/libbluetooth_jni.so \
  app/Bluetooth/Bluetooth.apk \
  app/Bluetooth/arm64/Bluetooth.odex \
  etc/permissions/android.hardware.wifi.xml \
  etc/permissions/android.hardware.wifi.direct.xml \
  etc/permissions/android.hardware.bluetooth.xml \
  etc/permissions/android.hardware.bluetooth_le.xml \
  etc/permissions/android.hardware.camera.xml \
  etc/permissions/android.hardware.camera.front.xml \
  etc/permissions/android.hardware.telephony.gsm.xml; do
    mkdir -p "$OUT/$(dirname "$p")"
    debugfs -R "dump $p $OUT/$p" "$SYS" 2>/dev/null || true
    [ -s "$OUT/$p" ] || rm -f "$OUT/$p"
done

rm -rf "$DIR/tmp_dl"
find "$OUT" -type f -exec ls -lh {} +
du -sh /mnt/d/p7_live
