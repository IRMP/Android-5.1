#ifndef _VND_BUILDCFG_H
#define _VND_BUILDCFG_H
#define BLUETOOTH_UART_DEVICE_PORT "/dev/ttySAC18"
#define FW_PATCHFILE_LOCATION "/vendor/firmware/brcm/"
#define FW_PATCHFILE_NAME "BCM.hcd"
#define FW_PATCH_SETTLEMENT_DELAY_MS 100
#define UART_TARGET_BAUD_RATE 4000000
#define USERIAL_VENDOR_SET_BAUD_DELAY_US 200000
#define LPM_SLEEP_MODE 1
#define LPM_IDLE_TIMEOUT_MULTIPLE 10
#define BT_WAKE_VIA_PROC TRUE
#define VENDOR_LPM_PROC_NODE "/proc/bluetooth/sleep/lpm"
#define VENDOR_BTWRITE_PROC_NODE "/proc/bluetooth/sleep/btwrite"
#define SCO_CFG_INCLUDED TRUE
#define SCO_USE_I2S_INTERFACE FALSE
#define USE_CONTROLLER_BDADDR TRUE
#define VENDOR_LIB_CONF_FILE "/vendor/etc/bluetooth/bt_vendor.conf"
#define BTVND_DBG FALSE
#define BTHW_DBG FALSE
#define VNDUSERIAL_DBG FALSE
#define UPIO_DBG FALSE
#endif
