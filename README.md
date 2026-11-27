# Android 5.1 (Lollipop, arm64) In-RAM Live Port — Google Pixel 6 Pro (`raven`)

A high-performance, **full in-memory (`tmpfs`) port of Android 5.1.1 Lollipop (`arm64-v8a`)** for the **Google Pixel 6 Pro (`raven`, Tensor GS101)** built on top of the stock **GKI `5.10`** kernel, 120 Hz LTPO DRM/KMS double-buffered display output, ARM64 NEON SIMD blitting, real OLED backlight control, and full GS101 `performance` governor tuning — without touching internal userdata/system flash partitions.

> [!WARNING]
> **Status:** All binaries, HAL `.so` modules, boot images (`v3`/`v4`), and compressed `ext4` system images have been compiled, symbol-checked against Bionic, and verified offline (`unpack_bootimg`, `lz4 -t`, `e2fsck -fn`), awaiting on-device testing on a physical Pixel 6 Pro. Always back up your stock `boot.img` / `vendor_boot.img` before flashing or testing.

---

## Architecture Overview

```
+-----------------------------------------------------------------------+
| Stock Pixel 6 Pro Bootloader (ABL)                                    |
| Loads GKI 5.10 kernel + Stage 1 initramfs (v3 or v4 / vendor_boot)    |
+-----------------------------------+-----------------------------------+
                                    |
                                    v
+-----------------------------------------------------------------------+
| Stage 1: BusyBox initramfs (in RAM)                                   |
|  - Loads 184 curated GS101 .ko modules (two-pass insmod)              |
|  - Bundles touch & Wi-Fi firmware in /lib/firmware (s6sy79x.bin,      |
|    fw_bcmdhd.bin, bcmdhd.cal, bcmdhd_clm.blob)                        |
|  - Excludes watchdogs (*wdt*, softdog, hardlockup-watchdog)           |
|  - Runs tune_perf: performance CPU/devfreq governors, panel_idle=0    |
|  - Mounts binderfs & binds /dev/binderfs/binder -> /dev/binder        |
|  - Brings up USB FunctionFS (11110000.dwc3) + mini_adbd               |
|  - Displays DRM/KMS pattern + touch circles via drm_test              |
+-----------------------------------+-----------------------------------+
                                    | adb push p6p_a5_system.ext4.lz4
                                    | adb shell /sbin/boot_android5
                                    v
+-----------------------------------------------------------------------+
| Stage 2: Full Android 5.1.1 arm64 in tmpfs (/newroot, 3072 MB)        |
|  - Unpacks a5_rootfs.cpio.lz4 (919 KB) + decompresses sys.ext4        |
|  - Mounts /system (ext4 rw loop), /data (4096M tmpfs), /cache (512M)  |
|  - Copies /lib/firmware, shuts down Stage 1 daemons, moves /proc /sys /dev
|  - exec switch_root /newroot /init (SELinux bypass @ 0x1778)          |
|  - Starts ueventd, servicemanager, tune_perf, mini_adbd,              |
|    surfaceflinger (120Hz NEON DRM/KMS), zygote64 -> system_server     |
+-----------------------------------------------------------------------+
```

---

## Key Technical Highlights (`raven` / Tensor GS101)

### 1. Maximum-Performance Display, Backlight & Power HALs
All HALs are cross-compiled with `-O3 -ffreestanding -nostdinc -nostdlib` directly against Android 5.1 Bionic `libc.so`, `libcutils.so`, `liblog.so`, and `libhardware.so` (zero glibc dependencies):

- **[`src/hwcomposer.raven.c`](src/hwcomposer.raven.c)** (`hwcomposer.raven.so`, HWC 1.1):
  - Opens `/dev/dri/card0` and selects the highest-area mode with the highest `vrefresh` (**`1440x3120 @ 120 Hz`** LTPO OLED, overriding lower-refresh `PREFERRED` ties).
  - Allocates **double-buffered** 32-bit DRM dumb buffers (`bufs[2]`) and flips frames via `DRM_IOCTL_MODE_PAGE_FLIP` (automatic fallback to `SETCRTC` / `DIRTYFB`).
  - Uses **ARM64 NEON SIMD** (`vld4q_u8` / `vst4q_u8`) for 16-pixel vector `RGBA_8888` → `XRGB8888` swizzling with minimal CPU overhead.
  - Drives precision VSync notifications via `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ...)`.
- **[`src/gralloc.raven.c`](src/gralloc.raven.c)** (`gralloc.raven.so`, Gralloc 0.2):
  - Ashmem-backed buffers aligned to **16-pixel / 64-byte boundaries** for aligned NEON blits.
- **[`src/lights.raven.c`](src/lights.raven.c)** (`lights.raven.so`):
  - Controls the physical OLED brightness by discovering `/sys/class/backlight/*/brightness` and scaling 8-bit luma to hardware `max_brightness` (`0666` unlocked by `tune_perf` for `system_server`).
- **[`src/power.raven.c`](src/power.raven.c)** (`power.raven.so`, Power HAL 0.3):
  - Pins all GS101 CPU clusters (`/sys/devices/system/cpu/cpufreq/policy*/scaling_governor`) and memory/bus `/sys/class/devfreq/*/governor` to **`performance`** while interactive, dropping to `schedutil` when the screen turns off.

### 2. Pixel 6 Pro Stock Kernel, Modules & Firmware (`raven-tq3a.230901.001.c2`)
- **Partition difference vs Pixel 7:** Pixel 6 Pro uses **Boot Header v4** (`boot.img` kernel-only + `vendor_boot.img` containing `dtb`, `bootconfig`, `vendor_ramdisk00`, and `dlkm` `vendor_ramdisk01`) plus `vendor_dlkm.img` — there is no `init_boot` or `vendor_kernel_boot` partition.
- **184 curated GKI `5.10` `.ko` modules** loaded in two passes from `/lib/modules_live`:
  - **Display:** `exynos-drm.ko`, `panel-samsung-drv.ko`, `panel-samsung-s6e3hc3*.ko`, `panel-samsung-s6e3hc4.ko`, `pinctrl-slg51000.ko`, `slg51000-core.ko`, `slg51000-regulator.ko`.
  - **Touchscreen:** `sec_touch.ko` (Samsung `s6sy79x` controller) + `ftm5.ko`, `goog_touch_interface.ko`, `touch_bus_negotiator.ko`, `touch_offload.ko`, paired with IDC profiles (`/usr/idc/{sec_touchscreen,sec_tsp,ftm5_ts,google_touchscreen}.idc`).
  - **USB & Networking:** `dwc3-exynos-usb.ko` (UDC `11110000.dwc3` verified from GS101 `dtb` and `init.recovery.raven.rc`), `usb_f_rndis.ko`, `pcie-exynos-core.ko`, `pcie-exynos-gs101-rc-cal.ko`, `bcmdhd4389.ko`.
  - **Performance & Storage:** `exynos-acme.ko`, `exynos_devfreq.ko`, `memlat-devfreq.ko`, `governor_memlat.ko`, `ufs-exynos-core.ko`, `gs_thermal.ko`.
  - **Watchdog safety:** `s3c2410_wdt.ko`, `softdog.ko`, and `hardlockup-watchdog.ko` are excluded; `/dev/watchdog` (if present) is kept alive by `busybox watchdog`.
- **Firmware bundled in RAM** (`/lib/firmware` + `/vendor/firmware` symlink, pointed via `/sys/module/firmware_class/parameters/path` and `firmware_class.path=/lib/firmware`): `s6sy79x.bin`, `fw_bcmdhd.bin`, `bcmdhd.cal`, `bcmdhd_clm.blob`.

### 3. Full In-RAM Android 5.1.1 System (`p6p_a5_system.ext4.lz4`)
Thanks to Pixel 6 Pro's **12 GB LPDDR5 RAM**, we keep the full desktop/media AOSP 5.1.1 app suite instead of stripping user applications:
- **RAM layout:** `/tmp` `2048 MB` (Stage 1) → `/newroot` `3072 MB` (Stage 2) + `/data` `4096 MB` + `/cache` `512 MB`, `vm.swappiness=0`.
- **Kept apps:** `Browser`, `webview`, `Gallery`, `Music`, `Calendar`, `DeskClock`, `Calculator`, `DocumentsUI`, `DownloadProviderUi`, `HTMLViewer`, `LatinIME`, `PackageInstaller`, `QuickSearchBox`, `SoundRecorder`, `Email`, `Exchange2`, `LiveWallpapersPicker`, `Launcher2`, `SystemUI`, `Settings`, `Contacts`.
- **Stripped incompatible APKs/HALs:** Only broken telephony/emulator setup packages (`TeleService`, `Telecom`, `TelephonyProvider`, `Dialer`, `Mms`, `MmsService`, `ManagedProvisioning`, `SdkSetup`, `EmulatorSmokeTests`, `Fallback`, `OpenWnn`) and `goldfish`/`ranchu` HALs.
- **ART/Display tuning (`build.prop` & `default.prop`):** `ro.sf.lcd_density=560`, `dalvik.vm.dex2oat-threads=8`, `dalvik.vm.image-dex2oat-threads=8`, `dalvik.vm.dex2oat-Xmx=512m`, `dalvik.vm.heapgrowthlimit=256m`, `dalvik.vm.heapsize=512m`, `panel_idle=0`.

---

## Repository Layout

```text
├── README.md
├── .gitignore
├── .github/workflows/ci.yml       # Syntax + Bionic/tool cross-compile CI
├── src/
│   ├── gralloc.raven.c            # 64B-aligned ashmem gralloc HAL
│   ├── hwcomposer.raven.c         # 120Hz LTPO DRM/KMS + NEON HWC 1.1 HAL
│   ├── lights.raven.c             # Real OLED backlight sysfs HAL
│   ├── power.raven.c              # GS101 performance governor Power HAL
│   ├── mini_adbd.c                # Static aarch64 FunctionFS ADB daemon
│   ├── drm_test.c                 # Stage 1 DRM/KMS pattern & touch tester
│   ├── simg2img.c                 # Android sparse-to-raw converter
│   ├── bionic_compat.h            # Bionic ABI declarations for cross-GCC
│   └── bionic_inc/                # Minimal headers for -nostdinc builds
└── scripts/
    ├── list_raven.sh              # Locate raven factory image URL
    ├── download_raven.sh          # Download raven-tq3a.230901.001.c2 factory zip
    ├── extract_raven.sh           # Extract boot, vendor_boot, vendor_dlkm, vendor.img
    ├── inspect_raven_details.sh   # Inspect vendor_ramdisk00/01, dtb, vendor/firmware
    ├── inspect_raven_fw.sh        # Inspect modules.dep & firmware references
    ├── inspect_raven_rc.sh        # Inspect init.raven.rc & UDC controller
    ├── prep_raven_fw.sh           # Dump s6sy79x + bcmdhd4389 firmware blobs
    ├── compile_p6p_hal.sh         # Compile all raven HALs & verify Bionic symbols
    ├── build_p6p_system.sh        # Patch, clean, tune & compress p6p_a5_system.ext4.lz4
    ├── build_p6p_boot.sh          # Assemble Stage 1+2 ramdisk & v3/v4 boot images
    ├── inspect_emu_extra.sh       # Copy shared tools & inspect base AOSP system
    └── verify_and_clean.sh        # Run unpack_bootimg, lz4 -t, e2fsck -fn & clean zips
```

---

## Building from Scratch (WSL2 / Ubuntu 22.04)

> [!IMPORTANT]
> The Pixel 6 Pro scripts live under `/mnt/d/p6p_live` (`D:\p6p_live`) and reuse shared prerequisites (`busybox-aarch64`, `simg2img`, `mini_adbd`, `drm_test`, `mkbootimg`, `aosp51` headers, `volantis` Bionic libs, and `aosp51_emu/system.raw.img`) built in `/mnt/d/p7_live` from the [`Pixel-7`](https://github.com/IRMP/Android-5.1/tree/Pixel-7) branch.

### Prerequisites
```bash
sudo apt update
sudo apt install -y gcc aarch64-linux-gnu-gcc make bzip2 flex bison python3 \
  git curl wget lz4 cpio unzip e2fsprogs
```

### Build Sequence
1. **Prepare shared toolchain & AOSP 5.1.1 base** (if not already present in `/mnt/d/p7_live`):
   Follow step 1 & step 3 of the [`Pixel-7`](https://github.com/IRMP/Android-5.1/tree/Pixel-7) branch to populate `/mnt/d/p7_live/{tools,stock/aosp51_emu,stock/volantis,out/hal}`.
2. **Clone `pixel-6-pro` and build Raven HALs + images:**
   ```bash
   mkdir -p /mnt/d/p6p_live
   git clone -b pixel-6-pro https://github.com/IRMP/Android-5.1.git /mnt/d/p6p_live/repo_git
   cp -r /mnt/d/p6p_live/repo_git/{src,scripts} /mnt/d/p6p_live/

   bash /mnt/d/p6p_live/scripts/inspect_emu_extra.sh
   bash /mnt/d/p6p_live/scripts/compile_p6p_hal.sh
   bash /mnt/d/p6p_live/scripts/download_raven.sh
   bash /mnt/d/p6p_live/scripts/extract_raven.sh
   bash /mnt/d/p6p_live/scripts/prep_raven_fw.sh
   bash /mnt/d/p6p_live/scripts/build_p6p_system.sh
   bash /mnt/d/p6p_live/scripts/build_p6p_boot.sh
   bash /mnt/d/p6p_live/scripts/verify_and_clean.sh
   ```

---

## Booting & Testing on Pixel 6 Pro (`raven`)

### 1. Boot Stage 1 into RAM
Reboot your unlocked Pixel 6 Pro to the bootloader (`adb reboot bootloader`):

```powershell
# Option A: Pure in-RAM boot (v3 unified boot image)
fastboot boot D:\p6p_live\out\final\p6p_live_boot_v3.img

# Option B: If bootloader enforces Header v4 ramdisk format
fastboot boot D:\p6p_live\out\final\p6p_live_boot_v4_ramdisk.img

# Option C: Native raven v4 split (if stock vendor_boot conflicts)
fastboot flash vendor_boot D:\p6p_live\out\final\p6p_live_vendor_boot_v4.img
fastboot boot D:\p6p_live\out\final\p6p_live_boot_v4.img
```

> **Restore stock `vendor_boot` (if Option C was used):**
> ```powershell
> fastboot flash vendor_boot D:\p6p_live\stock\raven\vendor_boot.img
> ```

### 2. Verify Stage 1 (Display + Touch + ADB)
```powershell
adb devices
adb shell "cat /tmp/init.log; cat /tmp/drm.log"
```

### 3. Push Android 5.1 `/system` to `tmpfs` & Trigger Stage 2
```powershell
adb push D:\p6p_live\out\final\p6p_a5_system.ext4.lz4 /tmp/a5_sys.lz4
adb shell /sbin/boot_android5
```

### 4. Inspect Stage 2 & Optional USB RNDIS Networking
```powershell
adb wait-for-device
adb shell "ps; service list | head -n 30; logcat -d -v time | tail -n 80"

# Enable USB RNDIS network interface (192.168.77.2) alongside ADB:
adb shell /sbin/enable_usb_net
```

---

## Known Limitations
- **Pending live hardware test:** Built and verified offline; real hardware boot logs on `raven` may require minor tweaks to `modules.order` or `init.raven.rc`.
- **CPU-based GLES rendering:** Mali-G78 proprietary blobs are ABI-incompatible with Android 5.1 Bionic; graphics use `libGLES_android.so` + NEON-accelerated `hwcomposer.raven.so`.
- **Wi-Fi userspace:** `bcmdhd4389.ko` and its firmware blobs are bundled, but `aosp51_emu` lacks a `wpa_supplicant` binary (`dhcpcd`/`ndc`/`netd` are present). Use `/sbin/enable_usb_net` (USB RNDIS) for immediate host-bridged networking.
- **Stubbed hardware:** Cellular/RIL (`cpif`), AoC audio DSP, cameras (`lwis`), Bluetooth, sensors, and fingerprint are disabled or stubbed.
- **Volatile storage:** `/data` (`4096 MB`) and `/cache` (`512 MB`) reside in `tmpfs` and reset on reboot.
