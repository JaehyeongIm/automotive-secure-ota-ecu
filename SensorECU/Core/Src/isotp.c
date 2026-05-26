#include "isotp.h"
#include "main.h"
#include <string.h>

#define BUF_SIZE 512U

extern CAN_HandleTypeDef hcan1;

typedef struct {
    uint8_t  buf[BUF_SIZE];
    uint16_t total_len;
    uint16_t received;
    uint8_t  next_sn;
    uint8_t  active;
} Ctx_t;

static Ctx_t               s_ctx;
static isotp_complete_cb_t s_cb;

void isotp_init(isotp_complete_cb_t cb)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_cb = cb;
}

static void send_can(uint8_t *data, uint8_t dlc)
{
    CAN_TxHeaderTypeDef hdr = {0};
    uint32_t mb;
    hdr.StdId = ISOTP_TX_CAN_ID;
    hdr.IDE   = CAN_ID_STD;
    hdr.RTR   = CAN_RTR_DATA;
    hdr.DLC   = dlc;
    HAL_CAN_AddTxMessage(&hcan1, &hdr, data, &mb);
}

void isotp_can_rx(const uint8_t *frame, uint8_t dlc)
{
    (void)dlc;
    uint8_t pci = frame[0] & 0xF0;

    if (pci == 0x00) {                          /* Single Frame */
        uint8_t len = frame[0] & 0x0F;
        if (len == 0 || len > 7) return;
        memcpy(s_ctx.buf, &frame[1], len);
        s_ctx.active = 0;
        if (s_cb) s_cb(s_ctx.buf, len);

    } else if (pci == 0x10) {                   /* First Frame */
        uint16_t len = ((uint16_t)(frame[0] & 0x0F) << 8) | frame[1];
        if (len > BUF_SIZE) return;
        s_ctx.total_len = len;
        s_ctx.received  = 6;
        s_ctx.next_sn   = 1;
        s_ctx.active    = 1;
        memcpy(s_ctx.buf, &frame[2], 6);
        uint8_t fc[8] = {0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        send_can(fc, 8);

    } else if (pci == 0x20) {                   /* Consecutive Frame */
        if (!s_ctx.active) return;
        uint8_t sn = frame[0] & 0x0F;
        if (sn != s_ctx.next_sn) { s_ctx.active = 0; return; }
        s_ctx.next_sn = (s_ctx.next_sn + 1) & 0x0F;
        uint16_t remaining = s_ctx.total_len - s_ctx.received;
        uint8_t  n = (remaining > 7) ? 7 : (uint8_t)remaining;
        memcpy(&s_ctx.buf[s_ctx.received], &frame[1], n);
        s_ctx.received += n;
        if (s_ctx.received >= s_ctx.total_len) {
            s_ctx.active = 0;
            if (s_cb) s_cb(s_ctx.buf, s_ctx.total_len);
        }
    }
}

void isotp_send(const uint8_t *data, uint16_t len)
{
    if (len > 7) return;    /* All ECU responses fit in a Single Frame */
    uint8_t frame[8] = {0};
    frame[0] = (uint8_t)len;
    memcpy(&frame[1], data, len);
    send_can(frame, 8);
}
