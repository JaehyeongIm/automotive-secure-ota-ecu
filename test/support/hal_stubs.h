#ifndef HAL_STUBS_H
#define HAL_STUBS_H

#include <stdint.h>

/* 테스트에서 HAL 동작을 제어하기 위한 전역 변수 선언
   - setUp()에서 초기화, tearDown() 또는 검증 시 확인 */
extern uint32_t          g_hal_tick;          /* HAL_GetTick() 반환값 제어 */
extern int               g_nvic_reset_count;  /* NVIC_SystemReset() 호출 횟수 추적 */
extern volatile uint8_t  g_isotp_tx_fail_count;

#endif /* HAL_STUBS_H */
