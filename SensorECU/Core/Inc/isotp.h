#ifndef ISOTP_H
#define ISOTP_H

#include <stdint.h>

#define ISOTP_RX_CAN_ID  0x7E1U   /* PC → SensorECU */
#define ISOTP_TX_CAN_ID  0x7E9U   /* SensorECU → PC */

typedef void (*isotp_complete_cb_t)(const uint8_t *data, uint16_t len);

void isotp_init(isotp_complete_cb_t cb);
void isotp_can_rx(const uint8_t *frame, uint8_t dlc);
void isotp_send(const uint8_t *data, uint16_t len);

#endif
