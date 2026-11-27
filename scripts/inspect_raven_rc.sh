#!/bin/bash
set -e
R=/mnt/d/p6p_live/stock/raven
U=$R/unpacked/vendor_boot
echo "=== vr0 init.recovery.raven.rc ==="
cat "$U/vr0/init.recovery.raven.rc" 2>/dev/null || true
echo "=== vendor.raw.img /etc/init/hw/ ==="
debugfs -R "ls -l /etc/init/hw" "$R/vendor.raw.img" 2>/dev/null || true
debugfs -R "cat /etc/init/hw/init.raven.rc" "$R/vendor.raw.img" 2>/dev/null | head -n 60 || true
echo "=== vendor.raw.img /etc/wifi ==="
debugfs -R "ls -l /etc/wifi" "$R/vendor.raw.img" 2>/dev/null || true
echo "=== sec_touch & ftm5 & panel & rndis in modules.dep ==="
debugfs -R "cat /lib/modules/modules.dep" "$R/vendor_dlkm.raw.img" 2>/dev/null | grep -E "sec_touch\.ko:|ftm5\.ko:|panel-samsung-s6e3hc3|panel-samsung-s6e3hc4|usb_f_rndis\.ko:|bcmdhd4389\.ko:"
