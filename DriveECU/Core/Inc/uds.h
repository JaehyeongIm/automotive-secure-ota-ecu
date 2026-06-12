#ifndef UDS_H
#define UDS_H

#include <stdint.h>

void uds_init(void);
void uds_on_isotp_rx(const uint8_t *data, uint16_t len);
void uds_process(void);

/* SecurityAccess seed 유도(순수): SHA-256(UID ‖ mid ‖ ctr)[0:4]. 'mid'는 freshness
 * 중간 워드(현행=SysTick, 재부팅 시 리셋 → replay 창). 호스트 단위테스트가 replay
 * 재현성/수정 효과를 결정론적으로 실증하도록 노출(SR-ATK-005). */
uint32_t sec_derive_seed(const uint8_t uid[12], uint32_t mid, uint32_t ctr);

#endif
