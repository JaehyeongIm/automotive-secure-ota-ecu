#ifndef ISOTP_H
#define ISOTP_H

#include <stdint.h>

#define ISOTP_RX_CAN_ID  0x7E1U   /* PC → SensorECU */
#define ISOTP_TX_CAN_ID  0x7E9U   /* SensorECU → PC */

typedef struct {
    uint8_t  kind;
    uint8_t  frame0;
    uint8_t  frame1;
    uint8_t  frame2;
    uint8_t  dlc;
    uint8_t  free_level;
    uint16_t total_len;
    uint16_t received;
    uint8_t  next_sn;
    uint32_t esr;
    uint32_t tsr;
    uint32_t error_code;
} ISOTP_TxFailDiag_t;

typedef struct {
    uint8_t  reason;
    uint8_t  got_sn;
    uint8_t  expected_sn;
    uint16_t total_len;
    uint16_t received;
} ISOTP_RxAbortDiag_t;

typedef void (*isotp_complete_cb_t)(const uint8_t *data, uint16_t len);

void isotp_init(isotp_complete_cb_t cb);
void isotp_can_rx(const uint8_t *frame, uint8_t dlc);
void isotp_send(const uint8_t *data, uint16_t len);
void isotp_diag_get_tx_fail(ISOTP_TxFailDiag_t *out);
void isotp_diag_get_rx_abort(ISOTP_RxAbortDiag_t *out);

extern volatile uint8_t g_isotp_tx_fail_count;
extern volatile uint8_t g_isotp_rx_abort_count;

#endif
