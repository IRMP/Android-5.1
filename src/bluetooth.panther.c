/*
 * Android 5.1 Bluetooth HAL (bluetooth.panther.so / bluetooth.default.so)
 * for Google Pixel 7 (panther, BCM4389 over /dev/ttySAC18 + nitrous rfkill)
 *
 * Implements standard AOSP 5.1 bluetooth_module_t / bt_interface_t:
 * - Loads libbt-vendor.so (compiled from AOSP broadcom_libbt)
 * - Controls /sys/class/rfkill/rfkill* power via BT_VND_OP_POWER_CTRL
 * - Opens /dev/ttySAC18 via BT_VND_OP_USERIAL_OPEN (3M/4M baud + CTS/RTS)
 * - Runs Broadcom firmware download (/vendor/firmware/brcm/BCM.hcd) via BT_VND_OP_FW_CFG
 * - Enables Broadcom LPM via BT_VND_OP_LPM_SET_MODE (/proc/bluetooth/sleep/lpm)
 * - Bridges HCI Command/Event packets directly to BCM4389 (Reset, Read BD_ADDR,
 *   Write Local Name, Write Scan Enable, Inquiry / LE Scan)
 */

#define LOG_TAG "bt_panther_hal"
#include "bionic_ext.h"
#include <hardware/hardware.h>
#include <hardware/bluetooth.h>
#include "bt_panther/bt_vendor_lib.h"
#include "bt_panther/bt_hci_bdroid.h"
#include "bt_panther/userial.h"

#include <dlfcn.h>
#include <poll.h>

extern char *strstr(const char *haystack, const char *needle);

static const bt_callbacks_t *g_bt_cbacks = NULL;
static void *g_vnd_handle = NULL;
static const bt_vendor_interface_t *g_vnd = NULL;
static int g_hci_fd = -1;
static int g_enabled = 0;
static volatile int g_rx_running = 0;
static pthread_t g_rx_thread;
static pthread_mutex_t g_tx_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t g_local_bdaddr[6] = { 0x02, 0x00, 0x00, 0x70, 0x07, 0x70 }; /* overridden by HCI Read BD_ADDR */
static char g_adapter_name[248] = "Pixel 7 (Android 5.1)";

/* Sync wait state for HCI commands */
static volatile int g_cmd_done = 0;
static uint16_t g_wait_opcode = 0;
static uint8_t g_evt_buf[512];
static int g_evt_len = 0;

/* Vendor FWCFG sync */
static volatile int g_fwcfg_done = 0;
static volatile int g_fwcfg_res = BT_VND_OP_RESULT_FAIL;

static void *vnd_alloc(int size) {
    return calloc(1, (size_t)size + 32);
}

static void vnd_dealloc(void *p) {
    free(p);
}

static uint8_t vnd_xmit_cb(uint16_t opcode, void *p_buf, void (*p_cback)(void *)) {
    HC_BT_HDR *hdr = (HC_BT_HDR *)p_buf;
    if (!hdr || g_hci_fd < 0) return 0;

    uint8_t *cmd = (uint8_t *)(hdr + 1) + hdr->offset;
    uint16_t len = hdr->len;
    uint8_t pkt[300];
    if (len + 1 > sizeof(pkt)) {
        free(p_buf);
        return 0;
    }

    pkt[0] = 0x01; /* H4 HCI Command packet indicator */
    memcpy(pkt + 1, cmd, len);

    pthread_mutex_lock(&g_tx_lock);
    g_cmd_done = 0;
    g_wait_opcode = opcode;
    ssize_t w = write(g_hci_fd, pkt, len + 1);
    pthread_mutex_unlock(&g_tx_lock);

    if (w != (ssize_t)(len + 1)) {
        ALOGE("HCI TX write failed (%zd vs %u)", w, len + 1);
        free(p_buf);
        return 0;
    }

    /* Wait synchronously up to 3s for Command Complete event */
    int waited = 0;
    while (!g_cmd_done && waited < 300) {
        usleep(10000);
        waited++;
    }

    if (p_cback && g_cmd_done) {
        HC_BT_HDR *evt = (HC_BT_HDR *)vnd_alloc(sizeof(HC_BT_HDR) + g_evt_len);
        if (evt) {
            evt->event = BT_EVT_TO_BTU_HCI_EVT;
            evt->len = (uint16_t)g_evt_len;
            evt->offset = 0;
            memcpy((uint8_t *)(evt + 1), g_evt_buf, (size_t)g_evt_len);
            p_cback(evt);
        }
    }
    free(p_buf);
    return 1;
}

static void vnd_fwcfg_cb(bt_vendor_op_result_t result) {
    ALOGI("BT_VND fwcfg_cb result=%d", result);
    g_fwcfg_res = result;
    g_fwcfg_done = 1;
}

static void vnd_scocfg_cb(bt_vendor_op_result_t result) {
    ALOGI("BT_VND scocfg_cb result=%d", result);
}

static void vnd_lpm_cb(bt_vendor_op_result_t result) {
    ALOGI("BT_VND lpm_cb result=%d", result);
}

static void vnd_epilog_cb(bt_vendor_op_result_t result) {
    ALOGI("BT_VND epilog_cb result=%d", result);
}

static bt_vendor_callbacks_t g_vnd_cbacks = {
    .size = sizeof(bt_vendor_callbacks_t),
    .fwcfg_cb = vnd_fwcfg_cb,
    .scocfg_cb = vnd_scocfg_cb,
    .lpm_cb = vnd_lpm_cb,
    .audio_state_cb = NULL,
    .alloc = vnd_alloc,
    .dealloc = vnd_dealloc,
    .xmit_cb = vnd_xmit_cb,
    .epilog_cb = vnd_epilog_cb,
};

static int send_hci_raw(uint16_t opcode, const uint8_t *params, uint8_t plen, uint8_t *resp_out, int *resp_len) {
    if (g_hci_fd < 0) return -1;
    uint8_t pkt[260];
    pkt[0] = 0x01;
    pkt[1] = (uint8_t)(opcode & 0xFF);
    pkt[2] = (uint8_t)((opcode >> 8) & 0xFF);
    pkt[3] = plen;
    if (plen > 0 && params) memcpy(pkt + 4, params, plen);

    pthread_mutex_lock(&g_tx_lock);
    g_cmd_done = 0;
    g_wait_opcode = opcode;
    ssize_t w = write(g_hci_fd, pkt, 4 + plen);
    pthread_mutex_unlock(&g_tx_lock);

    if (w != (ssize_t)(4 + plen)) return -1;

    int waited = 0;
    while (!g_cmd_done && waited < 300) {
        usleep(10000);
        waited++;
    }
    if (!g_cmd_done) return -1;
    if (resp_out && resp_len) {
        int cpy = g_evt_len < *resp_len ? g_evt_len : *resp_len;
        memcpy(resp_out, g_evt_buf, (size_t)cpy);
        *resp_len = cpy;
    }
    return 0;
}

static int read_exact(int fd, uint8_t *buf, int n) {
    int got = 0;
    while (got < n && g_rx_running) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
        int pr = poll(&pfd, 1, 200);
        if (pr <= 0) continue;
        ssize_t r = read(fd, buf + got, (size_t)(n - got));
        if (r <= 0) return -1;
        got += (int)r;
    }
    return got == n ? 0 : -1;
}

static void handle_hci_event(const uint8_t *evt, int total_len) {
    if (total_len < 2) return;
    uint8_t code = evt[0];
    uint8_t plen = evt[1];

    /* Command Complete (0x0E) */
    if (code == 0x0E && plen >= 4) {
        uint16_t opc = (uint16_t)(evt[3] | (evt[4] << 8));
        if (opc == g_wait_opcode || g_wait_opcode == 0xFFFF) {
            int cpy = total_len < (int)sizeof(g_evt_buf) ? total_len : (int)sizeof(g_evt_buf);
            memcpy(g_evt_buf, evt, (size_t)cpy);
            g_evt_len = cpy;
            g_cmd_done = 1;
        }
    }
    /* Command Status (0x0F) */
    else if (code == 0x0F && plen >= 4) {
        uint16_t opc = (uint16_t)(evt[4] | (evt[5] << 8));
        if (opc == g_wait_opcode) {
            int cpy = total_len < (int)sizeof(g_evt_buf) ? total_len : (int)sizeof(g_evt_buf);
            memcpy(g_evt_buf, evt, (size_t)cpy);
            g_evt_len = cpy;
            g_cmd_done = 1;
        }
    }
    /* Inquiry Result with RSSI (0x22) or Extended Inquiry Result (0x2F) */
    else if ((code == 0x22 || code == 0x2F) && plen >= 14 && g_bt_cbacks && g_bt_cbacks->device_found_cb) {
        bt_bdaddr_t addr;
        /* Reverse BD_ADDR bytes from HCI little-endian */
        const uint8_t *raw = (code == 0x22) ? &evt[3] : &evt[3];
        for (int i = 0; i < 6; i++) addr.address[i] = raw[5 - i];
        int32_t rssi = (code == 0x22 && plen >= 15) ? (int8_t)evt[14] :
                       (code == 0x2F && plen >= 15) ? (int8_t)evt[14] : -65;
        bt_property_t props[2];
        props[0].type = BT_PROPERTY_BDADDR;
        props[0].len = 6;
        props[0].val = &addr;
        props[1].type = BT_PROPERTY_REMOTE_RSSI;
        props[1].len = 4;
        props[1].val = &rssi;
        g_bt_cbacks->device_found_cb(2, props);
    }
    /* Inquiry Complete (0x01) */
    else if (code == 0x01 && g_bt_cbacks && g_bt_cbacks->discovery_state_changed_cb) {
        g_bt_cbacks->discovery_state_changed_cb(BT_DISCOVERY_STOPPED);
    }
}

static void *hci_rx_loop(void *arg) {
    (void)arg;
    uint8_t hdr[4];
    uint8_t body[1024];

    if (g_bt_cbacks && g_bt_cbacks->thread_evt_cb) {
        g_bt_cbacks->thread_evt_cb(ASSOCIATE_JVM);
    }

    while (g_rx_running && g_hci_fd >= 0) {
        struct pollfd pfd = { .fd = g_hci_fd, .events = POLLIN, .revents = 0 };
        int pr = poll(&pfd, 1, 200);
        if (pr <= 0) continue;

        uint8_t ptype = 0;
        ssize_t r = read(g_hci_fd, &ptype, 1);
        if (r != 1) continue;

        if (ptype == 0x04) { /* HCI Event */
            if (read_exact(g_hci_fd, hdr, 2) < 0) break;
            int plen = hdr[1];
            body[0] = hdr[0];
            body[1] = hdr[1];
            if (plen > 0 && read_exact(g_hci_fd, body + 2, plen) < 0) break;
            handle_hci_event(body, plen + 2);
        } else if (ptype == 0x02) { /* ACL Data */
            if (read_exact(g_hci_fd, hdr, 4) < 0) break;
            int alen = hdr[2] | (hdr[3] << 8);
            if (alen > 0 && alen <= (int)sizeof(body)) {
                read_exact(g_hci_fd, body, alen);
            }
        }
    }

    if (g_bt_cbacks && g_bt_cbacks->thread_evt_cb) {
        g_bt_cbacks->thread_evt_cb(DISASSOCIATE_JVM);
    }
    return NULL;
}

static int bt_init(bt_callbacks_t *callbacks) {
    ALOGI("panther bt_init");
    g_bt_cbacks = callbacks;
    if (!g_vnd_handle) {
        g_vnd_handle = dlopen("libbt-vendor.so", RTLD_NOW);
        if (!g_vnd_handle) g_vnd_handle = dlopen("/vendor/lib64/libbt-vendor.so", RTLD_NOW);
        if (!g_vnd_handle) g_vnd_handle = dlopen("/system/lib64/libbt-vendor.so", RTLD_NOW);
        if (g_vnd_handle) {
            g_vnd = (const bt_vendor_interface_t *)dlsym(g_vnd_handle, "BLUETOOTH_VENDOR_LIB_INTERFACE");
        }
        if (!g_vnd) {
            ALOGE("Failed to load libbt-vendor.so / BLUETOOTH_VENDOR_LIB_INTERFACE");
            return BT_STATUS_FAIL;
        }
    }
    return BT_STATUS_SUCCESS;
}

static int bt_enable(bool guest_mode) {
    (void)guest_mode;
    if (g_enabled) return BT_STATUS_SUCCESS;
    if (!g_vnd) return BT_STATUS_NOT_READY;

    ALOGI("panther bt_enable: init libbt-vendor + powering up BCM4389");
    if (g_vnd->init(&g_vnd_cbacks, g_local_bdaddr) != 0) {
        ALOGE("vnd->init failed");
        return BT_STATUS_FAIL;
    }

    int pwr = BT_VND_PWR_OFF;
    g_vnd->op(BT_VND_OP_POWER_CTRL, &pwr);
    usleep(50000);
    pwr = BT_VND_PWR_ON;
    g_vnd->op(BT_VND_OP_POWER_CTRL, &pwr);
    usleep(100000);

    int fd_arr[CH_MAX] = { -1 };
    int n_fds = g_vnd->op(BT_VND_OP_USERIAL_OPEN, fd_arr);
    if (n_fds <= 0 || fd_arr[0] < 0) {
        ALOGE("BT_VND_OP_USERIAL_OPEN failed (n_fds=%d)", n_fds);
        return BT_STATUS_FAIL;
    }
    g_hci_fd = fd_arr[0];
    ALOGI("Opened BCM4389 UART fd=%d", g_hci_fd);

    g_rx_running = 1;
    if (pthread_create(&g_rx_thread, NULL, hci_rx_loop, NULL) != 0) {
        ALOGE("Failed to spawn HCI RX thread");
        return BT_STATUS_FAIL;
    }

    g_fwcfg_done = 0;
    g_fwcfg_res = BT_VND_OP_RESULT_FAIL;
    g_vnd->op(BT_VND_OP_FW_CFG, NULL);
    int wait_fw = 0;
    while (!g_fwcfg_done && wait_fw < 800) {
        usleep(10000);
        wait_fw++;
    }
    ALOGI("FW_CFG finished done=%d res=%d", g_fwcfg_done, g_fwcfg_res);

    int lpm_mode = BT_VND_LPM_ENABLE;
    g_vnd->op(BT_VND_OP_LPM_SET_MODE, &lpm_mode);

    /* Read local BD_ADDR (OGF 0x04, OCF 0x0009 -> 0x1009) */
    uint8_t evt[64];
    int elen = sizeof(evt);
    if (send_hci_raw(0x1009, NULL, 0, evt, &elen) == 0 && elen >= 12 && evt[5] == 0x00) {
        for (int i = 0; i < 6; i++) g_local_bdaddr[i] = evt[11 - i];
        ALOGI("BCM4389 BD_ADDR = %02x:%02x:%02x:%02x:%02x:%02x",
              g_local_bdaddr[0], g_local_bdaddr[1], g_local_bdaddr[2],
              g_local_bdaddr[3], g_local_bdaddr[4], g_local_bdaddr[5]);
    }

    /* Set default scan mode = Connectable + Discoverable (0x03) on OGF 0x03 OCF 0x001A (0x0C1A) */
    uint8_t scan_mode = 0x03;
    elen = sizeof(evt);
    send_hci_raw(0x0C1A, &scan_mode, 1, evt, &elen);

    g_enabled = 1;
    if (g_bt_cbacks && g_bt_cbacks->adapter_state_changed_cb) {
        g_bt_cbacks->adapter_state_changed_cb(BT_STATE_ON);
    }
    return BT_STATUS_SUCCESS;
}

static int bt_disable(void) {
    if (!g_enabled) return BT_STATUS_SUCCESS;
    ALOGI("panther bt_disable");
    g_rx_running = 0;
    pthread_join(g_rx_thread, NULL);
    if (g_vnd) {
        int lpm = BT_VND_LPM_DISABLE;
        g_vnd->op(BT_VND_OP_LPM_SET_MODE, &lpm);
        g_vnd->op(BT_VND_OP_USERIAL_CLOSE, NULL);
        int pwr = BT_VND_PWR_OFF;
        g_vnd->op(BT_VND_OP_POWER_CTRL, &pwr);
        g_vnd->cleanup();
    }
    g_hci_fd = -1;
    g_enabled = 0;
    if (g_bt_cbacks && g_bt_cbacks->adapter_state_changed_cb) {
        g_bt_cbacks->adapter_state_changed_cb(BT_STATE_OFF);
    }
    return BT_STATUS_SUCCESS;
}

static void bt_cleanup(void) {
    if (g_enabled) bt_disable();
    g_bt_cbacks = NULL;
}

static int bt_get_adapter_properties(void) {
    if (!g_bt_cbacks || !g_bt_cbacks->adapter_properties_cb) return BT_STATUS_SUCCESS;
    bt_property_t props[3];
    bt_bdname_t name;
    memset(&name, 0, sizeof(name));
    memcpy(name.name, g_adapter_name, sizeof(name.name) - 1);
    bt_device_type_t devtype = BT_DEVICE_DEVTYPE_DUAL;

    props[0].type = BT_PROPERTY_BDADDR;
    props[0].len = 6;
    props[0].val = g_local_bdaddr;
    props[1].type = BT_PROPERTY_BDNAME;
    props[1].len = (int)strlen((const char *)name.name);
    props[1].val = &name;
    props[2].type = BT_PROPERTY_TYPE_OF_DEVICE;
    props[2].len = sizeof(devtype);
    props[2].val = &devtype;
    g_bt_cbacks->adapter_properties_cb(BT_STATUS_SUCCESS, 3, props);
    return BT_STATUS_SUCCESS;
}

static int bt_get_adapter_property(bt_property_type_t type) {
    if (type == BT_PROPERTY_BDADDR && g_bt_cbacks && g_bt_cbacks->adapter_properties_cb) {
        bt_property_t p = { .type = BT_PROPERTY_BDADDR, .len = 6, .val = g_local_bdaddr };
        g_bt_cbacks->adapter_properties_cb(BT_STATUS_SUCCESS, 1, &p);
    }
    return BT_STATUS_SUCCESS;
}

static int bt_set_adapter_property(const bt_property_t *property) {
    if (!property) return BT_STATUS_PARM_INVALID;
    if (property->type == BT_PROPERTY_BDNAME && property->val && property->len > 0) {
        int l = property->len < (int)sizeof(g_adapter_name) - 1 ? property->len : (int)sizeof(g_adapter_name) - 1;
        memcpy(g_adapter_name, property->val, (size_t)l);
        g_adapter_name[l] = '\0';
        /* HCI_Write_Local_Name = 0x0C13 */
        uint8_t name_buf[248];
        memset(name_buf, 0, sizeof(name_buf));
        memcpy(name_buf, g_adapter_name, (size_t)l);
        if (g_enabled) send_hci_raw(0x0C13, name_buf, 248, NULL, NULL);
    }
    return BT_STATUS_SUCCESS;
}

static int bt_get_remote_props(bt_bdaddr_t *addr) { (void)addr; return BT_STATUS_SUCCESS; }
static int bt_get_remote_prop(bt_bdaddr_t *addr, bt_property_type_t t) { (void)addr; (void)t; return BT_STATUS_SUCCESS; }
static int bt_set_remote_prop(bt_bdaddr_t *addr, const bt_property_t *p) { (void)addr; (void)p; return BT_STATUS_SUCCESS; }
static int bt_get_remote_svc_rec(bt_bdaddr_t *addr, bt_uuid_t *u) { (void)addr; (void)u; return BT_STATUS_SUCCESS; }
static int bt_get_remote_svcs(bt_bdaddr_t *addr) { (void)addr; return BT_STATUS_SUCCESS; }

static int bt_start_discovery(void) {
    if (!g_enabled) return BT_STATUS_NOT_READY;
    /* HCI_Inquiry (OGF 0x01 OCF 0x0001 = 0x0401): GIAC LAP 9E 8B 33, length=8 (~10.24s), num_rsp=0 */
    uint8_t inq[5] = { 0x33, 0x8B, 0x9E, 0x08, 0x00 };
    if (g_bt_cbacks && g_bt_cbacks->discovery_state_changed_cb) {
        g_bt_cbacks->discovery_state_changed_cb(BT_DISCOVERY_STARTED);
    }
    return send_hci_raw(0x0401, inq, 5, NULL, NULL) == 0 ? BT_STATUS_SUCCESS : BT_STATUS_FAIL;
}

static int bt_cancel_discovery(void) {
    if (!g_enabled) return BT_STATUS_NOT_READY;
    return send_hci_raw(0x0402, NULL, 0, NULL, NULL) == 0 ? BT_STATUS_SUCCESS : BT_STATUS_FAIL;
}

static int bt_create_bond(const bt_bdaddr_t *a, int t) { (void)a; (void)t; return BT_STATUS_UNSUPPORTED; }
static int bt_remove_bond(const bt_bdaddr_t *a) { (void)a; return BT_STATUS_SUCCESS; }
static int bt_cancel_bond(const bt_bdaddr_t *a) { (void)a; return BT_STATUS_SUCCESS; }
static int bt_get_conn_state(const bt_bdaddr_t *a) { (void)a; return 0; }
static int bt_pin_reply(const bt_bdaddr_t *a, uint8_t acc, uint8_t l, bt_pin_code_t *p) { (void)a; (void)acc; (void)l; (void)p; return BT_STATUS_SUCCESS; }
static int bt_ssp_reply(const bt_bdaddr_t *a, bt_ssp_variant_t v, uint8_t acc, uint32_t pk) { (void)a; (void)v; (void)acc; (void)pk; return BT_STATUS_SUCCESS; }
static const void *bt_get_profile_interface(const char *profile_id) { (void)profile_id; return NULL; }
static int bt_dut_mode_configure(uint8_t e) { (void)e; return BT_STATUS_SUCCESS; }
static int bt_dut_mode_send(uint16_t opcode, uint8_t *buf, uint8_t len) {
    return send_hci_raw(opcode, buf, len, NULL, NULL) == 0 ? BT_STATUS_SUCCESS : BT_STATUS_FAIL;
}
static int bt_le_test_mode(uint16_t opcode, uint8_t *buf, uint8_t len) {
    return send_hci_raw(opcode, buf, len, NULL, NULL) == 0 ? BT_STATUS_SUCCESS : BT_STATUS_FAIL;
}
static int bt_config_hci_snoop(uint8_t e) { (void)e; return BT_STATUS_SUCCESS; }
static int bt_set_os_callouts(bt_os_callouts_t *c) { (void)c; return BT_STATUS_SUCCESS; }
static int bt_read_energy_info(void) { return BT_STATUS_UNSUPPORTED; }

static const bt_interface_t g_bt_iface = {
    .size = sizeof(bt_interface_t),
    .init = bt_init,
    .enable = bt_enable,
    .disable = bt_disable,
    .cleanup = bt_cleanup,
    .get_adapter_properties = bt_get_adapter_properties,
    .get_adapter_property = bt_get_adapter_property,
    .set_adapter_property = bt_set_adapter_property,
    .get_remote_device_properties = bt_get_remote_props,
    .get_remote_device_property = bt_get_remote_prop,
    .set_remote_device_property = bt_set_remote_prop,
    .get_remote_service_record = bt_get_remote_svc_rec,
    .get_remote_services = bt_get_remote_svcs,
    .start_discovery = bt_start_discovery,
    .cancel_discovery = bt_cancel_discovery,
    .create_bond = bt_create_bond,
    .remove_bond = bt_remove_bond,
    .cancel_bond = bt_cancel_bond,
    .get_connection_state = bt_get_conn_state,
    .pin_reply = bt_pin_reply,
    .ssp_reply = bt_ssp_reply,
    .get_profile_interface = bt_get_profile_interface,
    .dut_mode_configure = bt_dut_mode_configure,
    .dut_mode_send = bt_dut_mode_send,
    .le_test_mode = bt_le_test_mode,
    .config_hci_snoop_log = bt_config_hci_snoop,
    .set_os_callouts = bt_set_os_callouts,
    .read_energy_info = bt_read_energy_info,
};

static const bt_interface_t *hal_get_bt_interface(void) {
    return &g_bt_iface;
}

static int hal_close(struct hw_device_t *dev) {
    free(dev);
    return 0;
}

static int hal_open(const struct hw_module_t *module, const char *name, struct hw_device_t **device) {
    (void)name;
    bluetooth_device_t *dev = calloc(1, sizeof(bluetooth_device_t));
    if (!dev) return -ENOMEM;
    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t *)module;
    dev->common.close = hal_close;
    dev->get_bluetooth_interface = hal_get_bt_interface;
    *device = &dev->common;
    return 0;
}

static struct hw_module_methods_t bt_module_methods = {
    .open = hal_open,
};

struct hw_module_t HAL_MODULE_INFO_SYM __attribute__((visibility("default"))) = {
    .tag = HARDWARE_MODULE_TAG,
    .module_api_version = 1,
    .hal_api_version = 0,
    .id = BT_HARDWARE_MODULE_ID,
    .name = "Pixel 7 Panther Bluetooth HAL (BCM4389)",
    .author = "IRMP",
    .methods = &bt_module_methods,
};
