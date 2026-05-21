#pragma once
#include <stdint.h>

/* 1 = ON/OFF 제어,  2 = 비례(P) 제어 */
#define APP_VERSION 1

extern volatile uint8_t g_ota_active;     /* OTA 세션 중 모터 정지 */
extern volatile uint8_t g_obstacle_flag;  /* 0x200 수신 시 세트 (ISR에서 쓰임) */
extern volatile uint8_t g_driving_state;  /* 0=정지, 1=주행 (TIM2 ISR에서 읽음) */

void drive_init(void);
void drive_update(void);
