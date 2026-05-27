#ifndef UDS_H
#define UDS_H

#include <stdint.h>

void uds_init(void);
void uds_on_isotp_rx(const uint8_t *data, uint16_t len);
void uds_process(void);
int  uds_ota_active(void);

#endif
