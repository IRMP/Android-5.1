#ifndef BT_VENDOR_LIB_H
#define BT_VENDOR_LIB_H
#include <stdint.h>
#include <stddef.h>

typedef enum {
    BT_VND_PWR_OFF = 0,
    BT_VND_PWR_ON,
} bt_vendor_power_state_t;

typedef enum {
    BT_VND_OP_POWER_CTRL = 0,
    BT_VND_OP_FW_CFG,
    BT_VND_OP_SCO_CFG,
    BT_VND_OP_USERIAL_OPEN,
    BT_VND_OP_USERIAL_CLOSE,
    BT_VND_OP_GET_LPM_IDLE_TIMEOUT,
    BT_VND_OP_LPM_SET_MODE,
    BT_VND_OP_LPM_WAKE_SET_STATE,
    BT_VND_OP_SET_AUDIO_STATE,
    BT_VND_OP_EPILOG,
    BT_VND_OP_A2DP_OFFLOAD_START,
    BT_VND_OP_A2DP_OFFLOAD_STOP,
} bt_vendor_opcode_t;

typedef enum {
    BT_VND_LPM_DISABLE = 0,
    BT_VND_LPM_ENABLE,
} bt_vendor_lpm_mode_t;

typedef enum {
    BT_VND_LPM_WAKE_ASSERT = 0,
    BT_VND_LPM_WAKE_DEASSERT,
} bt_vendor_lpm_wake_state_t;

typedef enum {
    BT_VND_OP_RESULT_SUCCESS = 0,
    BT_VND_OP_RESULT_FAIL,
} bt_vendor_op_result_t;

typedef struct {
    uint16_t handle;
    uint16_t peer_codec;
    uint16_t state;
} bt_vendor_op_audio_state_t;

#define SCO_CODEC_NONE 0x0000
#define SCO_CODEC_CVSD 0x0001
#define SCO_CODEC_MSBC 0x0002

typedef void (*cfg_result_cb)(bt_vendor_op_result_t result);
typedef void *(*malloc_cb)(int size);
typedef void (*mfree_cb)(void *p_buf);
typedef uint8_t (*cmd_xmit_cb)(uint16_t opcode, void *p_buf, void (*p_cback)(void *));
typedef void (*lpm_result_cb)(bt_vendor_op_result_t result);

typedef struct {
    size_t size;
    cfg_result_cb fwcfg_cb;
    cfg_result_cb scocfg_cb;
    lpm_result_cb lpm_cb;
    cfg_result_cb audio_state_cb;
    malloc_cb alloc;
    mfree_cb dealloc;
    cmd_xmit_cb xmit_cb;
    cfg_result_cb epilog_cb;
} bt_vendor_callbacks_t;

typedef struct {
    size_t size;
    int (*init)(const bt_vendor_callbacks_t *p_cb, unsigned char *local_bdaddr);
    int (*op)(bt_vendor_opcode_t opcode, void *param);
    void (*cleanup)(void);
} bt_vendor_interface_t;

#endif
