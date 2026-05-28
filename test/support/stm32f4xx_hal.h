#ifndef TEST_STM32F4XX_HAL_H
#define TEST_STM32F4XX_HAL_H

#include <stdint.h>

/* HAL 반환 타입 — ota_flash.h가 이 타입을 함수 반환형으로 사용 */
typedef enum {
    HAL_OK      = 0x00U,
    HAL_ERROR   = 0x01U,
    HAL_BUSY    = 0x02U,
    HAL_TIMEOUT = 0x03U
} HAL_StatusTypeDef;

/* IWDG 핸들 — ota_flash.c가 extern으로 참조 */
typedef struct {
    uint32_t dummy;
} IWDG_HandleTypeDef;

/* uds.c에서 직접 호출하는 HAL/CMSIS 함수 선언
   실제 구현은 hal_stubs.c에서 제공 */
uint32_t HAL_GetTick(void);
void     HAL_Delay(uint32_t Delay);
void     NVIC_SystemReset(void);

#endif /* TEST_STM32F4XX_HAL_H */
