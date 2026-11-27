# Android 5.1 (Lollipop, arm64) In-RAM Live Port — Google Pixel 7 (`panther`)

An experimental engineering Proof-of-Concept (PoC) booting **Android 5.1.1 Lollipop (`arm64-v8a`)** completely in RAM (`tmpfs`) on the **Google Pixel 7 (`panther`, Tensor G2 / `gs201`)** using the stock **GKI `5.10.157`** Linux kernel, DRM/KMS dumb buffers, software OpenGL ES, and a two-stage `initramfs` -> `tmpfs` architecture.

> [!WARNING]
> **PoC Status:** Hardware acceleration (Mali-G710), modem/RIL, Wi-Fi, Bluetooth, camera, audio, and sensors are intentionally stubbed or disabled. Always keep a copy of your stock `init_boot.img` before testing.

---

## Architecture Overview

```
+-------------------------------------------------------------------+
| Stock Pixel 7 Bootloader (ABL)                                    |
| Loads GKI 5.10.157 kernel + Stage 1 initramfs (<= 8 MB)           |
+---------------------------------+---------------------------------+
                                  |
                                  v
+-------------------------------------------------------------------+
| Stage 1: BusyBox initramfs (in RAM)                               |
|  - Insmods 169 curated .ko modules (DRM, DSI panel, touch, dwc3)  |
|  - Excludes watchdogs (or pets /dev/watchdog via busybox)         |
|  - Mounts binderfs & bind-mounts /dev/binderfs/binder -> /dev/binder
|  - Brings up USB Gadget FunctionFS (11210000.dwc3) + mini_adbd    |
|  - Displays DRM/KMS pattern + touch circles via drm_test          |
+---------------------------------+---------------------------------+
                                  | adb push p7_a5_system.ext4.lz4
                                  | adb shell /sbin/boot_android5
                                  v
+-------------------------------------------------------------------+
| Stage 2: Android 5.1.1 arm64 in tmpfs (/newroot)                  |
|  - Unpacks a5_rootfs.cpio.lz4 (919 KB) + decompresses sys.ext4    |
|  - Mounts /system (ext4 loop rw), /data (1G tmpfs), /cache (256M) |
|  - Stops drm_test & Stage-1 mini_adbd, moves /proc /sys /dev      |
|  - exec switch_root /newroot /init (patched SELinux bypass)       |
|  - Starts ueventd, servicemanager, mini_adbd, surfaceflinger,     |
|    bootanim, zygote64 -> system_server -> SystemUI + Launcher2    |
+-------------------------------------------------------------------+
```

### Key Technical Solutions for Pixel 7 (`panther`)

1. **Stock Kernel & Curated Modules (No Kernel Recompile):**
   - Uses stock `boot.img` (`5.10.157-android13-4` from `panther-tq3a.230901.001.c2`).
   - Bundles **169 `.ko` modules** from `vendor_kernel_boot` and `vendor_dlkm` inside `/lib/modules_live` (`exynos-drm.ko`, `panel-samsung-s6e3fc3-p10.ko`, `panel-boe-nt37290.ko`, `dwc3-exynos-usb.ko`, `tcpci_max77759.ko`, `ufs-exynos-core.ko`, `goog_touch_interface.ko`, `focal_touch.ko`, `sec_touch.ko`, `syna_touch.ko`, `slg51000/51002`).
   - Isolates modules under `/lib/modules_live` so mismatched on-flash `vendor_kernel_boot` modules cannot interfere.
   - Excludes `s3c2410_wdt.ko`, `softdog.ko`, and `hardlockup-watchdog.ko` to prevent automatic watchdog reboots, and runs `busybox watchdog -F -t 10 /dev/watchdog` if `/dev/watchdog` is present.

2. **Two-Stage In-RAM Loading (`tmpfs`) & `init_boot` Size Fit:**
   - Pixel 7's `init_boot` partition is strictly `8.0 MB` (`8,388,608` bytes).
   - Stage 1 `p7_live_ramdisk.lz4` (including BusyBox, `mini_adbd`, `drm_test`, 169 `.ko` modules, and Stage 2 `a5_rootfs.cpio.lz4`) is kept at **`7.6 MB` (`7,903,761` bytes)** by sharing `busybox` and `mini_adbd` binaries across stages.
   - Android 5.1.1 `/system` (`p7_a5_system.ext4.lz4`, **`208 MB`** compressed / `448 MB` raw ext4) is pushed over ADB into `tmpfs` and loop-mounted in RAM before `switch_root`.

3. **Userspace Adaptation (`aosp51_emu` `LMY48X` `arm64-v8a` -> Linux `5.10.157`):**
   - **64-bit Only (`zygote64`):** Uses `sdk_phone_arm64` (`ro.zygote=zygote64`) without Google Play Services / `SetupWizard` bloat, keeping AOSP `Launcher2`, `SystemUI`, and `Settings`.
   - **SELinux Bypass:** Patches Android 5.1 `/init` at offset `0x1778` (`f14b0094` -> `00008052`, `mov w0, #0`) to skip `selinux_load_policy()`, avoiding kernel `policydb` version incompatibilities and denials while passing `androidboot.selinux=permissive enforcing=0`.
   - **`binderfs` Compatibility:** Mounts `binderfs` and bind-mounts `/dev/binderfs/binder` to `/dev/binder` (`0666`) so Android 5.1 `servicemanager` and `libbinder.so` work out of the box.
   - **`ashmem`:** Stock Pixel 7 GKI `5.10.157` includes `ashmem` natively (`/dev/ashmem`), allowing Bionic `ashmem_create_region()` to function directly.
   - **`init.rc` & `init.panther.rc`:** Disables `ril-daemon`, `healthd`, `debuggerd` (32-bit), and `flash_recovery`; removes `mount rootfs ro remount` and `memory` cgroups; sets `ro.crypto.state=unencrypted` and `vold.post_fs_data_done=1` so `class main` (`zygote`) starts cleanly.

4. **DRM/KMS Graphics & Software OpenGL ES:**
   - [`src/gralloc.panther.c`](src/gralloc.panther.c) -> `gralloc.panther.so`: Ashmem-backed `GRALLOC_MODULE_API_VERSION_0_2` allocator supporting cross-process `registerBuffer`/`lock`/`unlock`.
   - [`src/hwcomposer.panther.c`](src/hwcomposer.panther.c) -> `hwcomposer.panther.so`: `HWC_DEVICE_API_VERSION_1_1` composer opening `/dev/dri/card0`, creating a 32-bit DRM dumb buffer, running a 60 Hz `vsync` thread, and blitting `HWC_FRAMEBUFFER_TARGET` (`RGBA_8888` -> `XRGB8888` swizzle) via `SETCRTC` / `DIRTYFB`.
   - Compiled with `-nostdlib -ffreestanding` directly against Android 5.1 Bionic `libc.so`, `libcutils.so`, `liblog.so`, and `libhardware.so` (zero glibc dependencies).
   - Configured with `libGLES_android.so` + `/system/lib64/egl/egl.cfg` (`0 0 android`).

---

## Repository Layout

```text
├── README.md
├── .gitignore
├── src/
│   ├── mini_adbd.c              # Static aarch64 FunctionFS ADB daemon (shell: + sync:)
│   ├── drm_test.c               # Stage 1 DRM/KMS test pattern & /dev/input/event* touch test
│   ├── simg2img.c               # Android sparse-to-raw image converter
│   ├── gralloc.panther.c        # Android 5.1 ashmem gralloc HAL (gralloc.panther.so)
│   ├── hwcomposer.panther.c     # Android 5.1 DRM/KMS HWC 1.1 HAL (hwcomposer.panther.so)
│   ├── bionic_compat.h          # Minimal Bionic ABI declarations for cross-GCC
│   └── bionic_inc/              # Minimal header stubs for -nostdinc Bionic HAL builds
└── scripts/
    ├── setup_tools.sh           # Clone AOSP mkbootimg tools
    ├── download_step1.sh        # Download panther-tq3a factory zip & BusyBox source
    ├── build_busybox.sh         # Build static aarch64 BusyBox 1.36.1
    ├── compile_c_tools.sh       # Compile simg2img, drm_test, mini_adbd
    ├── extract_panther.sh       # Extract boot, init_boot, vendor_boot, vendor_kernel_boot, vendor_dlkm
    ├── build_step1.sh           # Assemble Stage 1 test initramfs & boot images
    ├── get_emu_a51.sh           # Download AOSP 5.1 arm64 system image (for libGLES_android & base system)
    ├── download_volantis.sh     # Download Nexus 9 5.1.1 factory image (for default stub HALs & Bionic libs)
    ├── extract_volantis.sh      # Extract volantis boot & system
    ├── finish_volantis.sh       # Strip Nexus 9 boot header & convert system.img
    ├── grab_default_hals.sh     # Extract audio/power/vibrator/gps default stub HALs
    ├── get_aosp51_headers.sh    # Sparse-clone AOSP 5.1 libhardware & system/core headers
    ├── build_hal_and_check_emu.sh # Compile gralloc.panther.so & hwcomposer.panther.so
    ├── build_step2_system.sh    # Patch, strip, shrink, and LZ4-compress p7_a5_system.ext4.lz4
    ├── build_step2_3_boot.sh    # Assemble Stage 2 rootfs + unified Stage 1/2 Pixel 7 boot images
    └── inspect_*.sh / check_*.sh # Helper verification & inspection scripts
```

---

## Building from Scratch (WSL2 / Ubuntu 22.04)

> [!IMPORTANT]
> The build scripts expect workspace root `/mnt/d/p7_live` (`D:\p7_live` on Windows) so host `C:` storage is not consumed. Adjust `ROOT` in the scripts if building in another path.

### Prerequisites
```bash
sudo apt update
sudo apt install -y gcc aarch64-linux-gnu-gcc make bzip2 flex bison python3 \
  git curl wget lz4 cpio unzip e2fsprogs
```

### Build Sequence
1. **Clone repo & prepare tools:**
   ```bash
   mkdir -p /mnt/d/p7_live
   git clone -b Pixel-7 https://github.com/IRMP/Android-5.1.git /mnt/d/p7_live/repo_git
   cp -r /mnt/d/p7_live/repo_git/{src,scripts} /mnt/d/p7_live/
   bash /mnt/d/p7_live/scripts/setup_tools.sh
   bash /mnt/d/p7_live/scripts/download_step1.sh
   bash /mnt/d/p7_live/scripts/build_busybox.sh
   bash /mnt/d/p7_live/scripts/compile_c_tools.sh
   ```
2. **Extract Pixel 7 (`panther`) kernel & modules + build Stage 1:**
   ```bash
   bash /mnt/d/p7_live/scripts/extract_panther.sh
   bash /mnt/d/p7_live/scripts/inspect_modules.sh
   bash /mnt/d/p7_live/scripts/build_step1.sh
   ```
3. **Prepare AOSP 5.1.1 arm64 base, compile DRM/KMS HALs & build final Stage 1+2 images:**
   ```bash
   bash /mnt/d/p7_live/scripts/get_aosp51_headers.sh
   bash /mnt/d/p7_live/scripts/download_volantis.sh
   bash /mnt/d/p7_live/scripts/extract_volantis.sh || true
   bash /mnt/d/p7_live/scripts/finish_volantis.sh
   bash /mnt/d/p7_live/scripts/inspect_volantis_hal.sh
   bash /mnt/d/p7_live/scripts/check_hal_syms.sh
   bash /mnt/d/p7_live/scripts/grab_default_hals.sh
   bash /mnt/d/p7_live/scripts/get_emu_a51.sh
   bash /mnt/d/p7_live/scripts/build_hal_and_check_emu.sh
   bash /mnt/d/p7_live/scripts/build_step2_system.sh
   bash /mnt/d/p7_live/scripts/build_step2_3_boot.sh
   ```

Final outputs appear in `/mnt/d/p7_live/out/final/`:
- `p7_live_boot_v3.img` (`~32 MB`) — Boot Header v3 (`kernel` + unified `ramdisk` + cmdline)
- `p7_live_init_boot_v4.img` (`~7.6 MB`) — Boot Header v4 `init_boot` (fits in stock `8 MB` partition)
- `p7_live_boot_v4.img` (`~24 MB`) — Boot Header v4 `kernel` only
- `p7_live_vendor_boot_v4.img` (`~21 MB`) — `vendor_boot` v4 with permissive cmdline/bootconfig
- `p7_a5_system.ext4.lz4` (`~208 MB`) — Compressed Android 5.1.1 arm64 `/system`

---

## Booting & Testing on Pixel 7

### 1. Boot Stage 1 into RAM
Reboot your unlocked Pixel 7 into bootloader (`adb reboot bootloader`):

```powershell
# Try pure in-RAM fastboot boot first:
fastboot boot D:\p7_live\out\final\p7_live_boot_v3.img
```

If Tensor G2's ABL ignores the ramdisk in `fastboot boot` and loads `init_boot` from flash:
```powershell
fastboot flash init_boot D:\p7_live\out\final\p7_live_init_boot_v4.img
fastboot boot D:\p7_live\out\final\p7_live_boot_v4.img
```

> **Restore stock `init_boot` at any time:**
> ```powershell
> fastboot flash init_boot D:\p7_live\stock\panther\init_boot.img
> ```

### 2. Verify Stage 1 (Display + Touch + ADB)
Once booted into Stage 1, the screen should display SMPTE color bars with `PIXEL 7 LIVE POC` and draw red circles on touch:
```powershell
adb devices
adb shell "cat /tmp/init.log; cat /tmp/drm.log"
```

### 3. Push Android 5.1 `/system` to `tmpfs` & Trigger Stage 2
```powershell
adb push D:\p7_live\out\final\p7_a5_system.ext4.lz4 /tmp/a5_sys.lz4
adb shell /sbin/boot_android5
```
`boot_android5` unpacks Android 5.1 rootfs into `/newroot`, decompresses `/tmp/a5_sys.lz4`, loop-mounts `/newroot/system`, tears down Stage 1 `drm_test` & `mini_adbd`, and triggers `exec switch_root /newroot /init`.

### 4. Inspect Stage 2 Android 5.1
```powershell
adb wait-for-device
adb shell "ps; service list | head -n 30; logcat -d -v time | tail -n 80"
```

---

## Current Limitations
- **On-device verification pending:** All images, binaries, and HAL `.so` modules have been built and verified offline (`unpack_bootimg`, `readelf`, `e2fsck`), awaiting live hardware test on a connected Pixel 7.
- **Software rendering only:** `SurfaceFlinger` and UI run via `libGLES_android.so` + `libpixelflinger.so` + DRM/KMS dumb buffers; no Vulkan/GLES2+ hardware acceleration.
- **Ephemeral `/data`:** `/data` and `/cache` live in `tmpfs` and reset on reboot.
- **Stubbed subsystems:** No cellular/RIL, Wi-Fi, Bluetooth, audio hardware, camera, or sensors.
