#ifndef UDS_H
#define UDS_H

#include <stdint.h>

/* 펌웨어 다운로드 완료 후 IDLE 상태에서 재부팅을 대기하는 플래그 */
extern volatile uint8_t g_fw_pending;

void uds_init(void);
void uds_on_isotp_rx(const uint8_t *data, uint16_t len);
void uds_process(void);

#endif
