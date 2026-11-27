/*
 * panther_aocd - Lightweight AoC Firmware Loader & Monitor for Android 5.1 on Pixel 7
 * Triggers /sys/devices/platform/19000000.aoc/firmware and monitors /dev/aoc.
 */
#define LOG_TAG "panther_aocd"
#include "bionic_ext.h"
#include <poll.h>

extern void __libc_init(void *raw_args, void (*onexit)(void), int (*slingshot)(int, char**, char**), const void *structors);
static void (*const _init_arr[1])(void) __attribute__((used)) = {0};
__attribute__((used)) const struct { void *a, *b, *c, *d; } _bionic_structors = { (void*)_init_arr, (void*)_init_arr, (void*)_init_arr, 0 };
int main(int argc, char **argv);
void _start_aocd(void);
__asm__(".global _start_aocd\n_start_aocd:\n  mov x0, sp\n  mov x1, #0\n  adr x2, main\n  adr x3, _bionic_structors\n  b __libc_init\n");

static void trigger_aoc_fw(void) {
    static const char *paths[] = {
        "/sys/devices/platform/19000000.aoc/firmware",
        "/sys/bus/platform/devices/19000000.aoc/firmware",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        int fd = open(paths[i], O_WRONLY | O_CLOEXEC);
        if (fd >= 0) {
            write(fd, "aoc.bin", 7);
            close(fd);
            ALOGI("Triggered AoC firmware load via %s", paths[i]);
            property_set("vendor.aoc.firmware.version", "1");
            return;
        }
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    ALOGI("panther_aocd starting");
    for (int retry = 0; retry < 20; retry++) {
        int fd = open("/dev/aoc", O_RDWR | O_CLOEXEC);
        if (fd >= 0) {
            trigger_aoc_fw();
            property_set("vendor.aocd.ready", "1");
            for (;;) {
                struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
                poll(&pfd, 1, 5000);
            }
        }
        usleep(250000);
    }
    ALOGW("/dev/aoc never appeared; exiting cleanly");
    return 0;
}
