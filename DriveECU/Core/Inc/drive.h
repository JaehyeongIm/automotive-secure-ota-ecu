#pragma once
#include <stdint.h>

/* v1=직진+정지(10cm)  v2=직진+정지(10cm)+자동후진복귀 */
#define APP_VERSION 2

extern volatile uint8_t  g_ota_active;      /* OTA 세션 중 모터 정지 */
extern volatile uint8_t  g_obstacle_flag;   /* CAN 0x200 data[0]: 장애물 플래그 */
extern volatile uint8_t  g_driving_state;   /* 0=정지, 1=주행 (TIM2 ISR에서 읽음) */
extern volatile uint8_t  g_button_pressed;  /* B1 버튼 EXTI에서 세트 */
extern volatile uint16_t g_distance_cm;     /* CAN 0x200 data[1:2]: 거리(cm) */

void drive_init(void);
void drive_update(void);
void drive_note_sensor_rx(void);   /* 0x200 수신 시각 기록(신선도 감시) */

/* 센서 프레임 신선도 (ISO 26262 안전상태 / AUTOSAR E2E 타임아웃 감시).
 * seen=0(미수신) 또는 age>timeout 이면 stale(0). unsigned 뺄셈 → tick wraparound 안전. */
static inline int drive_sensor_fresh(int seen, uint32_t now, uint32_t last_rx, uint32_t timeout_ms)
{
    if (!seen) return 0;
    return (uint32_t)(now - last_rx) <= timeout_ms;
}
