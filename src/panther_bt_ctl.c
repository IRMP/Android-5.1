/*
 * panther_bt_ctl - Standalone Pixel 7 Bluetooth Bringup & Inquiry CLI
 * Drives bluetooth.panther.so + libbt-vendor.so directly on BCM4389 (/dev/ttySAC18)
 */
#include "bionic_ext.h"
#include <hardware/hardware.h>
#include <hardware/bluetooth.h>
#include <dlfcn.h>

extern int printf(const char *fmt, ...);
extern void __libc_init(void *raw_args, void (*onexit)(void), int (*slingshot)(int, char**, char**), const void *structors);
static void (*const _init_arr[1])(void) __attribute__((used)) = {0};
__attribute__((used)) const struct { void *a, *b, *c, *d; } _bionic_structors = { (void*)_init_arr, (void*)_init_arr, (void*)_init_arr, 0 };
int main(int argc, char **argv);
void _start_bt_ctl(void);
__asm__(".global _start_bt_ctl\n_start_bt_ctl:\n  mov x0, sp\n  mov x1, #0\n  adr x2, main\n  adr x3, _bionic_structors\n  b __libc_init\n");

static volatile int g_state = 0;
static volatile int g_disc = 0;

static void on_state(bt_state_t state) {
    printf("[bt_ctl] adapter state -> %s (%d)\n", state == BT_STATE_ON ? "ON" : "OFF", state);
    g_state = (state == BT_STATE_ON) ? 1 : 2;
}

static void on_props(bt_status_t status, int num, bt_property_t *props) {
    (void)status;
    for (int i = 0; i < num; i++) {
        if (props[i].type == BT_PROPERTY_BDADDR && props[i].len == 6) {
            const uint8_t *a = (const uint8_t *)props[i].val;
            printf("[bt_ctl] BD_ADDR: %02x:%02x:%02x:%02x:%02x:%02x\n",
                   a[0], a[1], a[2], a[3], a[4], a[5]);
        } else if (props[i].type == BT_PROPERTY_BDNAME && props[i].val) {
            printf("[bt_ctl] Name: %s\n", (const char *)props[i].val);
        }
    }
}

static void on_found(int num, bt_property_t *props) {
    uint8_t addr[6] = {0};
    int rssi = 0;
    for (int i = 0; i < num; i++) {
        if (props[i].type == BT_PROPERTY_BDADDR && props[i].len == 6)
            memcpy(addr, props[i].val, 6);
        else if (props[i].type == BT_PROPERTY_REMOTE_RSSI && props[i].len == 4)
            rssi = *(int32_t *)props[i].val;
    }
    printf("[bt_ctl] FOUND %02x:%02x:%02x:%02x:%02x:%02x RSSI=%d\n",
           addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], rssi);
}

static void on_disc(bt_discovery_state_t s) {
    printf("[bt_ctl] discovery -> %s\n", s == BT_DISCOVERY_STARTED ? "STARTED" : "STOPPED");
    g_disc = (s == BT_DISCOVERY_STARTED) ? 1 : 2;
}

static bt_callbacks_t g_cb = {
    .size = sizeof(bt_callbacks_t),
    .adapter_state_changed_cb = on_state,
    .adapter_properties_cb = on_props,
    .device_found_cb = on_found,
    .discovery_state_changed_cb = on_disc,
};

int main(int argc, char **argv) {
    int do_scan = (argc > 1 && strcmp(argv[1], "scan") == 0);
    void *h = dlopen("/system/lib64/hw/bluetooth.default.so", RTLD_NOW);
    if (!h) h = dlopen("/system/lib64/hw/bluetooth.panther.so", RTLD_NOW);
    if (!h) {
        printf("[bt_ctl] Failed to load bluetooth HAL .so\n");
        return 1;
    }
    const hw_module_t *mod = (const hw_module_t *)dlsym(h, HAL_MODULE_INFO_SYM_AS_STR);
    if (!mod) {
        printf("[bt_ctl] Missing HMI symbol\n");
        return 2;
    }
    hw_device_t *dev = NULL;
    if (mod->methods->open(mod, BT_HARDWARE_MODULE_ID, &dev) != 0 || !dev) {
        printf("[bt_ctl] HAL open failed\n");
        return 3;
    }
    const bt_interface_t *iface = ((bluetooth_device_t *)dev)->get_bluetooth_interface();
    if (!iface || iface->init(&g_cb) != BT_STATUS_SUCCESS) {
        printf("[bt_ctl] iface->init failed\n");
        return 4;
    }
    if (iface->enable(false) != BT_STATUS_SUCCESS) {
        printf("[bt_ctl] enable failed (check /dev/ttySAC18 + /vendor/firmware/brcm/BCM.hcd)\n");
        return 5;
    }
    iface->get_adapter_properties();
    if (do_scan) {
        printf("[bt_ctl] Scanning BT Classic for 10s...\n");
        iface->start_discovery();
        for (int i = 0; i < 120 && g_disc != 2; i++) usleep(100000);
        iface->cancel_discovery();
    }
    iface->disable();
    iface->cleanup();
    dev->close(dev);
    return 0;
}
