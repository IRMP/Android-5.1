# Android 5.1 (Lollipop, arm64) In-RAM Live Port — Google Pixel 7 (`panther`)

An experimental engineering port booting **Android 5.1.1 Lollipop (`arm64-v8a`)** completely in RAM (`tmpfs`) on the **Google Pixel 7 (`panther`, Tensor G2 / `gs201`)** over stock **GKI `5.10.157`**, with full hardware subsystem driver integration across GPU, Audio, Wi-Fi, Bluetooth, Cellular Modem/RIL, and Camera.

> [!IMPORTANT]
> **Verification Scope:** All boot/system images (`7.6 MB` Stage 1 `init_boot`, `253 MB` LZ4 Stage 2 `/system`), kernel `.ko` dependency closure, extracted `/vendor/firmware` blobs, custom Bionic HAL `.so` modules, and `libGLES_mali.so` symbol resolution (`496/496` symbols = `0` unresolved) are built and verified offline. Live on-device execution on connected Pixel 7 hardware is pending device verification.

---

## Architecture & Subsystems

```
+-------------------------------------------------------------------+
| Stock Pixel 7 Bootloader (ABL)                                    |
| Loads GKI 5.10.157 kernel + Stage 1 initramfs (<= 8 MB)           |
+---------------------------------+---------------------------------+
                                  |
                                  v
+-------------------------------------------------------------------+
| Stage 1: BusyBox initramfs (7,913,472 bytes init_boot_v4)         |
|  - Insmods 169 curated .ko modules (DRM, DSI panel, touch, dwc3)  |
|  - Mounts binderfs & bind-mounts /dev/binderfs/binder -> /dev/binder
|  - Brings up USB Gadget FunctionFS (11210000.dwc3) + mini_adbd    |
|  - Displays DRM/KMS pattern + touch circles via drm_test          |
+---------------------------------+---------------------------------+
                                  | adb push p7_a5_system.ext4.lz4
                                  | adb shell /sbin/boot_android5
                                  v
+-------------------------------------------------------------------+
| Stage 2: Android 5.1.1 arm64 in tmpfs (/newroot)                  |
|  - Unpacks a5_rootfs.cpio.lz4 (920 KB) + decompresses 541M sys.ext4|
|  - Bind-mounts /vendor & loads 23 extra .ko modules + firmware:   |
|      mali_kbase + mali_pixel + g2d | bcmdhd4389 | aoc_* + cs35l41 |
|      cpif + boot_device_spi | lwis + stmvl53l1                    |
|  - Tears down Stage 1 services, switch_root -> /init (SELinux 0)  |
|  - Starts panther_aocd, cpboot-daemon (cbd), ril-daemon,          |
|    wpa_supplicant, mediaserver, surfaceflinger, zygote64, UI      |
+-------------------------------------------------------------------+
```

### Implemented Hardware Drivers & Subsystems

| Subsystem | Kernel / Firmware | Android 5.1 HAL / Userspace Implementation |
|---|---|---|
| **HW Graphics (`Mali-G710`)** | `mali_kbase.ko`, `mali_pixel.ko`, `gpu_cooling.ko`, `g2d.ko`, `mali_csffw.bin` | Stock Android 13 `libGLES_mali.so` (`43 MB` Valhall CSF) + `libdrm.so` + [`src/gralloc.panther.c`](src/gralloc.panther.c) (`/dev/dma_heap/system` DMA-BUF allocator) + [`src/mali_compat/`](src/mali_compat/) Android 13->5.1 compat shim suite (`496/496` symbols resolved) + `egl.cfg` (`0 0 mali`, fallback `0 1 android`) + `persist.sys.ui.hw=1`. |
| **Display & Touch** | `exynos-drm.ko`, `panel-*.ko`, `goog_touch_interface.ko`, `focal/sec/syna_touch.ko` | [`src/hwcomposer.panther.c`](src/hwcomposer.panther.c) (`HWC_DEVICE_API_VERSION_1_1` over `/dev/dri/card0` dumb buffers + 60 Hz vsync + XRGB8888 scanout). |
| **Audio (`AoC` + `CS35L41`)** | `aoc_core.ko`, `aoc_alsa_dev*.ko`, `snd-soc-cs35l41*.ko`, `aoc.bin`, `cs35l41-*.wmfw/.bin` | [`src/panther_aocd.c`](src/panther_aocd.c) (`/dev/aoc` + `/sys/devices/platform/19000000.aoc/firmware`) + [`src/audio.primary.panther.c`](src/audio.primary.panther.c) (`AUDIO_HARDWARE_MODULE_ID_PRIMARY` over `libtinyalsa` PCM devices `1`/`5`/`8` + CS35L41 mixer routing) + `audio.a2dp.default.so`. |
| **Wi-Fi (`BCM4389`)** | `bcmdhd4389.ko` + `fw_bcmdhd.bin`, `bcmdhd.cal`, `bcmdhd_clm.blob` | AOSP 5.1.1 `volantis` `wpa_supplicant` + `hostapd` + binary-patched `libhardware_legacy.so` (`0x3cb8`: `wifi_change_fw_path -> return 0` for `bcmdhd4389`) + `wpa_supplicant.conf` + `android.hardware.wifi*.xml`. |
| **Bluetooth (`BCM4389`)** | `nitrous.ko`, `exynos_tty.ko`, `brcm/BCM.hcd` (`/dev/ttySAC18`) | Compiled AOSP 5.1 `libbt-vendor.so` from `broadcom_libbt` ([`src/bt_panther/`](src/bt_panther/)) + [`src/bluetooth.panther.c`](src/bluetooth.panther.c) (`bluetooth.default.so` 64-bit HCI HAL) + [`src/panther_bt_ctl.c`](src/panther_bt_ctl.c) CLI + `android.hardware.bluetooth*.xml`. *(Note: `aosp51_emu` `arm64` lacks `Bluetooth.apk`, so BT hardware control is exposed through `bluetooth.default.so` + `/system/bin/panther_bt_ctl`.)* |
| **Cellular Modem (`G5300B`)** | `cpif.ko`, `cpif_page.ko`, `boot_device_spi.ko`, `modem_a/b` mounted at `/mnt/vendor/modem_img` | Stock `/vendor/bin/cbd` running under `LD_PRELOAD=/system/lib64/libpanther_chk.so` (`__write_chk`/`__fread_chk` shims) + [`src/libpanther-ril.c`](src/libpanther-ril.c) (`libpanther-ril.so`, RIL v11 over `/dev/umts_ipc0` using reverse-engineered 12-byte SIT `RCM_HEADER` & `0x0000..0x0902` SIT command IDs) + `rild` + AOSP `Dialer`, `TeleService`, `Telecom`, `TelephonyProvider`, `Mms`. |
| **Camera (`LWIS`)** | `lwis.ko`, `stmvl53l1.ko` | [`src/camera.panther.c`](src/camera.panther.c) (`CAMERA_MODULE_API_VERSION_1_0` probing `/dev/lwis-*` via `LWIS_GET_DEVICE_INFO` `0xc6c84c01` & toggling `LWIS_DEVICE_ENABLE`/`DISABLE` `0x4c05`/`0x4c06` + NV21/JPEG preview & capture callbacks for `mediaserver`/`LegacyCamera`). *(Full 3A/ISP sensor frame streaming remains bounded without the 169 MB Android 13 `com.google.pixel.camera.hal` APEX/EdgeTPU userspace stack.)* |

---

## Repository Layout

```text
├── README.md
├── .gitignore
├── .github/workflows/ci.yml
├── src/
│   ├── mini_adbd.c                    # Static aarch64 FunctionFS ADB daemon
│   ├── drm_test.c                     # Stage 1 DRM/KMS pattern & touch tester
│   ├── simg2img.c                     # Android sparse-to-raw image converter
│   ├── gralloc.panther.c              # DMA-BUF (/dev/dma_heap/system) + ashmem gralloc HAL
│   ├── hwcomposer.panther.c           # DRM/KMS HWC 1.1 display composer
│   ├── audio.primary.panther.c        # AoC TinyALSA + CS35L41 Primary Audio HAL
│   ├── camera.panther.c               # LWIS (/dev/lwis-*) Camera HAL v1
│   ├── bluetooth.panther.c            # 64-bit Bluedroid HCI HAL over libbt-vendor.so
│   ├── panther_bt_ctl.c               # Native BT power/firmware/inquiry CLI tool
│   ├── panther_aocd.c                 # AoC firmware loader daemon (/dev/aoc)
│   ├── libpanther-ril.c               # Shannon G5300B SIT RIL v11 (/dev/umts_ipc0)
│   ├── libpanther_chk.c               # Bionic fortify compat (__write_chk, __fread_chk)
│   ├── bt_panther/                    # Broadcom libbt-vendor headers & Pixel 7 config
│   ├── mali_compat/                   # Android 13 -> 5.1 compat libs for libGLES_mali.so
│   ├── bionic_compat.h & bionic_ext.h # Minimal Bionic ABI declarations
│   └── bionic_inc/                    # Header stubs for -nostdlib Bionic builds
└── scripts/
    ├── setup_tools.sh                 # Clone AOSP mkbootimg tools
    ├── download_step1.sh              # Download panther factory zip & BusyBox source
    ├── build_busybox.sh               # Build static aarch64 BusyBox 1.36.1
    ├── compile_c_tools.sh             # Compile simg2img, drm_test, mini_adbd
    ├── extract_panther.sh             # Extract boot/vendor_boot/vendor_kernel_boot/vendor_dlkm
    ├── build_step1.sh                 # Assemble Stage 1 test initramfs & boot images
    ├── get_aosp51_headers.sh          # Sparse-clone AOSP 5.1 headers & tinyalsa/ril/libbt
    ├── download_volantis.sh           # Download Nexus 9 5.1.1 image (Wi-Fi/A2DP/permissions)
    ├── extract_volantis.sh            # Extract volantis images
    ├── finish_volantis.sh             # Strip Nexus 9 boot header & convert system.img
    ├── grab_default_hals.sh           # Extract default power/vibrator/gps HALs
    ├── fetch_panther_vendor.sh        # Extract Pixel 7 vendor.img & radio.img
    ├── fetch_volantis_wifi_bt.sh      # Extract wpa_supplicant, hostapd, permissions XMLs
    ├── prep_subsystem_sources.sh      # Clone AOSP 5.1 tinyalsa, broadcom/libbt, hardware/ril
    ├── resolve_extra_ko_deps.sh       # Resolve 23 extra kernel modules & dependency graph
    ├── build_mali_compat.sh           # Build Mali-G710 compat libs & DMA-BUF gralloc
    ├── build_bt_panther.sh            # Build libbt-vendor.so, bluetooth.panther.so, panther_bt_ctl
    ├── build_ril_cam.sh               # Build libpanther-ril.so, camera.panther.so & check symbols
    ├── stage_panther_vendor.sh        # Extract firmware/modules, build panther_aocd, patch libhardware_legacy
    ├── get_emu_a51.sh                 # Download AOSP 5.1 arm64 base system image
    ├── build_hal_and_check_emu.sh     # Compile core HALs against AOSP 5.1 Bionic
    ├── build_step2_system.sh          # Assemble, shrink & LZ4-compress p7_a5_system.ext4.lz4
    └── build_step2_3_boot.sh          # Assemble Stage 2 rootfs + final boot/init_boot images
```

---

## Building from Scratch (WSL2 / Ubuntu 22.04)

> [!IMPORTANT]
> Scripts target `/mnt/d/p7_live` (`D:\p7_live` on Windows) so host `C:` storage is not consumed. Peak workspace footprint is `~4.1 GB`.

### Prerequisites
```bash
sudo apt update
sudo apt install -y gcc aarch64-linux-gnu-gcc make bzip2 flex bison python3 \
  git curl wget lz4 cpio unzip e2fsprogs file
```

### Build Sequence
1. **Tools, Stage 1 Kernel & Base Images:**
   ```bash
   mkdir -p /mnt/d/p7_live
   git clone -b Pixel-7 https://github.com/IRMP/Android-5.1.git /mnt/d/p7_live/repo_git
   cp -r /mnt/d/p7_live/repo_git/{src,scripts} /mnt/d/p7_live/
   bash /mnt/d/p7_live/scripts/setup_tools.sh
   bash /mnt/d/p7_live/scripts/download_step1.sh
   bash /mnt/d/p7_live/scripts/build_busybox.sh
   bash /mnt/d/p7_live/scripts/compile_c_tools.sh
   bash /mnt/d/p7_live/scripts/extract_panther.sh
   bash /mnt/d/p7_live/scripts/inspect_modules.sh
   bash /mnt/d/p7_live/scripts/build_step1.sh
   ```
2. **AOSP 5.1 Base, Vendor Extraction & All 6 Subsystems:**
   ```bash
   bash /mnt/d/p7_live/scripts/get_aosp51_headers.sh
   bash /mnt/d/p7_live/scripts/prep_subsystem_sources.sh
   bash /mnt/d/p7_live/scripts/get_emu_a51.sh
   bash /mnt/d/p7_live/scripts/download_volantis.sh
   bash /mnt/d/p7_live/scripts/extract_volantis.sh || true
   bash /mnt/d/p7_live/scripts/finish_volantis.sh
   bash /mnt/d/p7_live/scripts/grab_default_hals.sh
   bash /mnt/d/p7_live/scripts/fetch_volantis_wifi_bt.sh
   bash /mnt/d/p7_live/scripts/fetch_panther_vendor.sh
   bash /mnt/d/p7_live/scripts/build_hal_and_check_emu.sh
   bash /mnt/d/p7_live/scripts/build_mali_compat.sh
   bash /mnt/d/p7_live/scripts/build_bt_panther.sh
   bash /mnt/d/p7_live/scripts/build_ril_cam.sh
   bash /mnt/d/p7_live/scripts/stage_panther_vendor.sh
   bash /mnt/d/p7_live/scripts/build_step2_system.sh
   bash /mnt/d/p7_live/scripts/build_step2_3_boot.sh
   ```

Final outputs in `/mnt/d/p7_live/out/final/`:
- `p7_live_init_boot_v4.img` (`7,913,472` bytes / `7.6 MB`) — Stage 1 `init_boot` (fits in stock `8 MB` partition)
- `p7_live_boot_v4.img` (`24 MB`) — Stock GKI `5.10.157` kernel image
- `p7_live_boot_v3.img` (`32 MB`) — Combined Boot v3 test image (`fastboot boot`)
- `p7_live_vendor_boot_v4.img` (`21 MB`) — `vendor_boot` v4 with permissive cmdline
- `p7_a5_system.ext4.lz4` (`253 MB` compressed / `541 MB` raw ext4) — Android 5.1.1 `/system` + `/vendor/firmware` + `/vendor/lib/modules_extra`

---

## Booting & Verifying on Pixel 7

```powershell
# 1. Boot Stage 1 into RAM
fastboot boot D:\p7_live\out\final\p7_live_boot_v3.img
# Or if ABL ignores fastboot-boot ramdisk:
# fastboot flash init_boot D:\p7_live\out\final\p7_live_init_boot_v4.img
# fastboot boot D:\p7_live\out\final\p7_live_boot_v4.img

# 2. Push Stage 2 Android 5.1 system (with Mali-G710, firmware, and 23 extra modules) & switch_root
adb push D:\p7_live\out\final\p7_a5_system.ext4.lz4 /tmp/a5_sys.lz4
adb shell /sbin/boot_android5

# 3. Verify loaded kernel modules, services, GPU/Audio/Wi-Fi/BT/Modem/Camera
adb wait-for-device
adb shell "lsmod; ls /dev/mali* /dev/aoc /dev/umts_* /dev/lwis-* /dev/ttySAC18"
adb shell "netcfg; /system/bin/panther_bt_ctl; ps; logcat -d -v time | tail -n 80"
```
