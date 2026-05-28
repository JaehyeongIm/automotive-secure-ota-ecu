#include "stm32f4xx_hal.h"
#include <stdint.h>

/* 테스트에서 extern으로 접근해 제어/검증하는 전역 변수들 */
uint32_t         g_hal_tick          = 0;  /* HAL_GetTick() 반환값 */
int              g_nvic_reset_count  = 0;  /* NVIC_SystemReset() 호출 횟수 */

/* isotp.h에서 extern 선언된 전역 변수 — isotp.c를 컴파일하지 않으므로 여기서 정의 */
volatile uint8_t g_isotp_tx_fail_count = 0;

/* uds.c가 직접 호출하는 함수들의 스텁 구현 */
uint32_t HAL_GetTick(void)       { return g_hal_tick; }
void     HAL_Delay(uint32_t ms)  { (void)ms; }
void     NVIC_SystemReset(void)  { g_nvic_reset_count++; }
