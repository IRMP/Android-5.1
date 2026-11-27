#!/bin/bash
set -e
EMU=/mnt/d/p7_live/stock/aosp51_emu
echo "=== aosp51_emu /bin ==="
debugfs -R "ls -l /bin" "$EMU/system.raw.img" | awk '{print $NF}' | sort
echo "=== aosp51_emu /etc ==="
debugfs -R "ls -l /etc" "$EMU/system.raw.img" | grep -iE "wifi|bluetooth|audio|mixer|media|gps|firmware" || true
echo "=== aosp51_emu /lib64/hw + /lib64/*bluetooth* ==="
debugfs -R "ls -l /lib64" "$EMU/system.raw.img" | grep -iE "bt|blue|wifi|wpa" || true

cd /mnt/d/p7_live/tools/aosp51
if [ ! -d hardware_ril ]; then
  git clone --depth=1 --branch lollipop-mr1-release https://android.googlesource.com/platform/hardware/ril hardware_ril
fi
if [ ! -d external_tinyalsa ]; then
  git clone --depth=1 --branch lollipop-mr1-release https://android.googlesource.com/platform/external/tinyalsa external_tinyalsa
fi
if [ ! -d broadcom_libbt ]; then
  git clone --depth=1 --branch lollipop-mr1-release https://android.googlesource.com/platform/hardware/broadcom/libbt broadcom_libbt
fi
ls -la hardware_ril/include/telephony external_tinyalsa/include/tinyalsa broadcom_libbt
