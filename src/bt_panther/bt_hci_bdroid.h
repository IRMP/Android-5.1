#ifndef BT_HCI_BDROID_H
#define BT_HCI_BDROID_H
#include <stdint.h>

typedef struct {
    uint16_t event;
    uint16_t len;
    uint16_t offset;
    uint16_t layer_specific;
    uint8_t data[];
} HC_BT_HDR;

typedef void (*tINT_CMD_CBACK)(void *p_mem);

#define BT_HC_HDR_SIZE (sizeof(HC_BT_HDR))
#define BT_EVT_TO_BTU_HCI_EVT   0x1000
#define MSG_STACK_TO_HC_HCI_CMD 0x2000
#endif
