#include "isotp.h"
#include "main.h"
#include <string.h>

#define BUF_SIZE 512U
#define TX_KIND_NONE   0U
#define TX_KIND_FC     1U
#define TX_KIND_UDS_SF 2U

#define RX_ABORT_NONE          0U
#define RX_ABORT_CF_INACTIVE   1U
#define RX_ABORT_SN_MISMATCH   2U

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
volatile uint8_t           g_isotp_tx_fail_count = 0;
volatile uint8_t           g_isotp_rx_abort_count = 0;
static volatile ISOTP_TxFailDiag_t s_last_tx_fail;
static volatile ISOTP_RxAbortDiag_t s_last_rx_abort;

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
    if (HAL_CAN_AddTxMessage(&hcan1, &hdr, data, &mb) != HAL_OK) {
        s_last_tx_fail.kind      = ((data[0] & 0xF0U) == 0x30U) ? TX_KIND_FC : TX_KIND_UDS_SF;
        s_last_tx_fail.frame0    = data[0];
        s_last_tx_fail.frame1    = data[1];
        s_last_tx_fail.frame2    = data[2];
        s_last_tx_fail.dlc       = dlc;
        s_last_tx_fail.free_level = (uint8_t)HAL_CAN_GetTxMailboxesFreeLevel(&hcan1);
        s_last_tx_fail.total_len = s_ctx.total_len;
        s_last_tx_fail.received  = s_ctx.received;
        s_last_tx_fail.next_sn   = s_ctx.next_sn;
        s_last_tx_fail.esr       = CAN1->ESR;
        s_last_tx_fail.tsr       = CAN1->TSR;
        s_last_tx_fail.error_code = hcan1.ErrorCode;
        g_isotp_tx_fail_count++;
    }
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
        if (!s_ctx.active) {
            s_last_rx_abort.reason   = RX_ABORT_CF_INACTIVE;
            s_last_rx_abort.got_sn   = frame[0] & 0x0F;
            s_last_rx_abort.expected_sn = s_ctx.next_sn;
            s_last_rx_abort.total_len = s_ctx.total_len;
            s_last_rx_abort.received  = s_ctx.received;
            g_isotp_rx_abort_count++;
            return;
        }
        uint8_t sn = frame[0] & 0x0F;
        if (sn != s_ctx.next_sn) {
            s_last_rx_abort.reason      = RX_ABORT_SN_MISMATCH;
            s_last_rx_abort.got_sn      = sn;
            s_last_rx_abort.expected_sn = s_ctx.next_sn;
            s_last_rx_abort.total_len   = s_ctx.total_len;
            s_last_rx_abort.received    = s_ctx.received;
            g_isotp_rx_abort_count++;
            s_ctx.active = 0;
            return;
        }
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

void isotp_diag_get_tx_fail(ISOTP_TxFailDiag_t *out)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    memcpy(out, (const void *)&s_last_tx_fail, sizeof(*out));
    if (!primask) {
        __enable_irq();
    }
}

void isotp_diag_get_rx_abort(ISOTP_RxAbortDiag_t *out)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    memcpy(out, (const void *)&s_last_rx_abort, sizeof(*out));
    if (!primask) {
        __enable_irq();
    }
}
