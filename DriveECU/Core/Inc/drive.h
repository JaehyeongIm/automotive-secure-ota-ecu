#pragma once
#include <stdint.h>

/* v1=직진+정지(10cm)  v2=감속(30cm)+정지(10cm)  v3=v2+자동후진복귀 */
#define APP_VERSION 2

extern volatile uint8_t  g_ota_active;      /* OTA 세션 중 모터 정지 */
extern volatile uint8_t  g_obstacle_flag;   /* CAN 0x200 data[0]: 장애물 플래그 */
extern volatile uint8_t  g_driving_state;   /* 0=정지, 1=주행 (TIM2 ISR에서 읽음) */
extern volatile uint8_t  g_button_pressed;  /* B1 버튼 EXTI에서 세트 */
extern volatile uint16_t g_distance_cm;     /* CAN 0x200 data[1:2]: 거리(cm) */

void drive_init(void);
void drive_update(void);
