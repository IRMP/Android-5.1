# IRMP — Android 5.1 (Lollipop) In-RAM Live Ports

**IRMP (In-RAM Mobile Ports)** brings legacy Android 5.1 (Lollipop, `arm64-v8a`) to modern smartphones without modifying the device's userdata or system partitions on flash — booting completely into **RAM (`tmpfs`)** via a two-stage `initramfs` architecture on top of the stock GKI Linux kernel.

> [!NOTE]
> This is an experimental engineering Proof-of-Concept (PoC). Target devices run on their stock GKI 5.x kernels with minimal HAL shims, software OpenGL ES, and bypassed/permissive SELinux.

---

## Supported Devices (Branches)

| Device | Codename | SoC / Kernel | Branch | Status |
| :--- | :--- | :--- | :--- | :--- |
| **Google Pixel 7** | `panther` | Tensor G2 (`gs201`) / GKI `5.10.157` | [`Pixel-7`](https://github.com/IRMP/Android-5.1/tree/Pixel-7) | PoC (Stage 1 + Stage 2 + DRM/KMS HALs) |

Switch to your device branch for device-specific build scripts, kernel module lists, and fastboot/ADB boot instructions:

```bash
git clone -b Pixel-7 https://github.com/IRMP/Android-5.1.git
```

---

## Core Architecture

Modern Android devices (Android 13+, GKI 5.10+) enforce strict boot image size limits, modular vendor `.ko` loading, `binderfs`, `configfs` USB gadgets, and SELinux policy versions incompatible with Android 5.1. IRMP solves this cleanly in RAM:

### 1. Two-Stage In-RAM Boot (`initramfs` -> `tmpfs`)
- **Stage 1 (`initramfs`):** Minimal BusyBox + static `mini_adbd` + `drm_test` + curated device `.ko` modules (display, touch, USB Type-C/UDC, PMIC, clocks). Brings up USB ADB over FunctionFS and verifies `/dev/dri/card0` KMS output.
- **Stage 2 (`tmpfs` + `switch_root`):** Receives the compressed Android 5.1 `system.ext4.lz4` image over ADB (`adb push` into `tmpfs`), unpacks the patched Android 5.1 rootfs, mounts `/system`, `/data`, `/cache` in RAM, moves `/proc`, `/sys`, `/dev` (including `/dev/binderfs/binder` bind-mounted to `/dev/binder`), and executes `switch_root` into Android 5.1's `init`.

### 2. Userspace Adaptation (Android 5.1 on Linux 5.10+)
- **SELinux Bypass:** Android 5.1's `init` binary is patched to skip `selinux_load_policy()`, preventing kernel policy version mismatches (`policydb`) and permission denials on modern kernel nodes.
- **Binder (`binderfs`):** Since GKI 5.10+ kernels expose binder via `binderfs` instead of a legacy static `/dev/binder` node, Stage 1 mounts `binderfs` and bind-mounts `/dev/binderfs/binder` to `/dev/binder`.
- **USB ADB (`configfs` + FunctionFS):** Replaces legacy `/sys/class/android_usb` triggers with a standalone static [mini_adbd.c](src/mini_adbd.c) daemon speaking USB FunctionFS (`FS`/`HS`/`SS` descriptors) with `shell:` and `sync:` (`adb push`/`adb pull`).
- **64-bit Only (`zygote64`):** Uses Android 5.1.1 `arm64-v8a` (`zygote64` only) to avoid 32-bit `CONFIG_COMPAT` limitations on modern arm64 kernels.

### 3. Display & Graphics Without Proprietary GPU Drivers
Instead of reverse-engineering modern proprietary GPU blobs (e.g. Mali-G710), IRMP uses:
- **Linux DRM/KMS Dumb Buffers** (`/dev/dri/card0`)
- **Software OpenGL ES** (`libGLES_android.so` / `libpixelflinger.so` configured via `egl.cfg`)
- **Custom Bionic HAL Shims** (`gralloc.<device>.so` + `hwcomposer.<device>.so`) compiled directly against Android 5.1 Bionic `libc.so` / `libcutils.so` / `liblog.so` / `libhardware.so`.

---

## Shared Components (`main`)

| File | Purpose |
| :--- | :--- |
| [src/mini_adbd.c](src/mini_adbd.c) | Standalone static `aarch64` ADB daemon over Linux USB FunctionFS (`shell:` PTY + `sync:`) |
| [src/drm_test.c](src/drm_test.c) | Standalone static `aarch64` DRM/KMS dumb-buffer test pattern + `/dev/input/event*` touch visualizer |
| [src/simg2img.c](src/simg2img.c) | Lightweight Android sparse image to raw ext4 converter |
| [src/bionic_compat.h](src/bionic_compat.h) | Minimal Bionic ABI declarations for building Android 5.1 HAL `.so` modules with standard cross-GCC |
| [src/bionic_inc/](src/bionic_inc/) | Minimal header stubs avoiding glibc leakage during Bionic `-nostdlib` builds |

---

## General Requirements

- Linux / WSL2 host (`x86_64`) with `aarch64-linux-gnu-gcc`, `make`, `python3`, `git`, `curl`, `unzip`, `lz4`, `cpio`, `e2fsprogs` (`debugfs`, `e2fsck`, `resize2fs`).
- Unlocked bootloader + `adb` / `fastboot` (Android Platform-Tools).
- Device-specific scripts live in the corresponding device branch (e.g. [`Pixel-7`](https://github.com/IRMP/Android-5.1/tree/Pixel-7)).
