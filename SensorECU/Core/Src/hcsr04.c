#include "hcsr04.h"
#include "main.h"

extern TIM_HandleTypeDef htim2;

/* Returns distance in cm. Returns 0 on timeout (no object or out of range). */
uint16_t hcsr04_measure_cm(void)
{
    /* 10μs TRIG pulse */
    HAL_GPIO_WritePin(HCSR_TRIG_GPIO_Port, HCSR_TRIG_Pin, GPIO_PIN_SET);
    __HAL_TIM_SET_COUNTER(&htim2, 0);
    while (__HAL_TIM_GET_COUNTER(&htim2) < 10);
    HAL_GPIO_WritePin(HCSR_TRIG_GPIO_Port, HCSR_TRIG_Pin, GPIO_PIN_RESET);

    /* Wait for ECHO HIGH (timeout 30ms) */
    __HAL_TIM_SET_COUNTER(&htim2, 0);
    while (HAL_GPIO_ReadPin(HCSR_ECHO_GPIO_Port, HCSR_ECHO_Pin) == GPIO_PIN_RESET) {
        if (__HAL_TIM_GET_COUNTER(&htim2) > 30000) return 0;
    }

    /* Measure ECHO HIGH duration (timeout 30ms) */
    __HAL_TIM_SET_COUNTER(&htim2, 0);
    while (HAL_GPIO_ReadPin(HCSR_ECHO_GPIO_Port, HCSR_ECHO_Pin) == GPIO_PIN_SET) {
        if (__HAL_TIM_GET_COUNTER(&htim2) > 30000) return 0;
    }

    /* distance(cm) = pulse_us / 58  (sound speed 340m/s, round trip) */
    return (uint16_t)(__HAL_TIM_GET_COUNTER(&htim2) / 58);
}
