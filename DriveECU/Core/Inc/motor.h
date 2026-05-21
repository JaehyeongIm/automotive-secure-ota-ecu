#pragma once
#include <stdint.h>

void motor_init(void);
void motor_set(uint16_t left, uint16_t right);  /* 0..999 */
void motor_stop(void);
