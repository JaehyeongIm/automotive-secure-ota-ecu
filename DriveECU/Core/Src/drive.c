#include "drive.h"
#include "motor.h"
#include "main.h"
#include <stdio.h>

#define BASE_SPEED    400   /* 직진 속도 (0–999) */
#define SLOW_SPEED    200   /* 감속 속도 — v2, v3 */
#define STOP_DIST_CM   10   /* 정지 거리 (cm) */
#define SLOW_DIST_CM   60   /* 감속 시작 거리 (cm) — v2, v3 */
#define FORWARD_MS   6000   /* 전진 시간 (ms) — 실측 후 캘리브레이션 필요 */
#define REVERSE_MS    600   /* 후진 복귀 시간 (ms) — v3 */

volatile uint8_t  g_ota_active     = 0;
volatile uint8_t  g_obstacle_flag  = 0;
volatile uint8_t  g_driving_state  = 0;
volatile uint8_t  g_button_pressed = 0;
volatile uint16_t g_distance_cm    = 999;

typedef enum {
    DRIVE_IDLE,
    DRIVE_RUNNING,
    DRIVE_STOPPED,    /* v3 전용: 정지 후 후진 대기 */
    DRIVE_REVERSING,  /* v3 전용 */
} DriveState;

static DriveState s_state    = DRIVE_IDLE;
static uint32_t   s_state_ts = 0;

void drive_init(void)
{
    motor_init();
}

void drive_update(void)
{
    if (g_ota_active) {
        motor_stop();
        g_driving_state = 0;
        s_state = DRIVE_IDLE;
        return;
    }

    uint32_t now = HAL_GetTick();

    switch (s_state) {

    case DRIVE_IDLE:
        if (g_button_pressed) {
            g_button_pressed = 0;
            s_state    = DRIVE_RUNNING;
            s_state_ts = now;
            printf("[DRIVE v%d] 출발\r\n", APP_VERSION);
        }
        break;

    case DRIVE_RUNNING:
        /* 1m 시간 완료 → 정상 정지 */
        if (now - s_state_ts >= FORWARD_MS) {
            motor_stop();
            g_driving_state = 0;
            s_state = DRIVE_IDLE;
            printf("[DRIVE] 1m 완료 → 정지\r\n");
            break;
        }

#if APP_VERSION == 1
        /* 장애물 플래그(10cm 이내) 감지 시 즉시 정지 */
        if (g_obstacle_flag) {
            motor_stop();
            g_driving_state = 0;
            s_state = DRIVE_IDLE;
            printf("[DRIVE] 장애물(10cm) 감지 → 정지\r\n");
            break;
        }
        motor_set(BASE_SPEED, BASE_SPEED);

#elif APP_VERSION == 2
        /* 10cm 이내: 정지 / 10~30cm: 거리 비례 감속 / 30cm 이상: 정속 */
        if (g_distance_cm <= STOP_DIST_CM) {
            motor_stop();
            g_driving_state = 0;
            s_state = DRIVE_IDLE;
            printf("[DRIVE] %ucm → 정지\r\n", g_distance_cm);
            break;
        } else if (g_distance_cm <= SLOW_DIST_CM) {
            uint16_t sp = SLOW_SPEED + (uint16_t)(
                (uint32_t)(BASE_SPEED - SLOW_SPEED)
                * (g_distance_cm - STOP_DIST_CM)
                / (SLOW_DIST_CM  - STOP_DIST_CM));
            motor_set(sp, sp);
        } else {
            motor_set(BASE_SPEED, BASE_SPEED);
        }

#elif APP_VERSION == 3
        /* v2 감속 로직 동일 + 정지 후 자동 후진 */
        if (g_distance_cm <= STOP_DIST_CM) {
            motor_stop();
            g_driving_state = 0;
            s_state    = DRIVE_STOPPED;
            s_state_ts = now;
            printf("[DRIVE] %ucm → 정지 후 후진\r\n", g_distance_cm);
            break;
        } else if (g_distance_cm <= SLOW_DIST_CM) {
            uint16_t sp = SLOW_SPEED + (uint16_t)(
                (uint32_t)(BASE_SPEED - SLOW_SPEED)
                * (g_distance_cm - STOP_DIST_CM)
                / (SLOW_DIST_CM  - STOP_DIST_CM));
            motor_set(sp, sp);
        } else {
            motor_set(BASE_SPEED, BASE_SPEED);
        }
#endif
        g_driving_state = 1;
        break;

    case DRIVE_STOPPED:
        /* 300ms 대기 후 후진 시작 (v3 전용) */
        g_driving_state = 0;
        if (now - s_state_ts >= 300) {
            motor_reverse(SLOW_SPEED, SLOW_SPEED);
            s_state    = DRIVE_REVERSING;
            s_state_ts = now;
            printf("[DRIVE] 후진 시작\r\n");
        }
        break;

    case DRIVE_REVERSING:
        g_driving_state = 0;
        if (now - s_state_ts >= REVERSE_MS) {
            motor_stop();
            s_state = DRIVE_IDLE;
            printf("[DRIVE] 후진 완료 → 대기\r\n");
        }
        break;
    }
}
