#pragma once
#include <stdint.h>

void motor_init(void);
void motor_set(uint16_t left, uint16_t right);     /* 0..999, 방향은 현재 설정 그대로 */
void motor_stop(void);                              /* PWM=0 + 방향 핀 전진으로 복귀 */
void motor_reverse(uint16_t left, uint16_t right); /* 방향 핀 후진으로 설정 후 PWM */
