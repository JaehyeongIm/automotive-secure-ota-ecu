#include "drive.h"
#include "motor.h"
#include "main.h"

#define BASE_SPEED  600  /* 0..999 */
#define KP          80   /* v2 비례 게인: correction = error * KP */

volatile uint8_t g_ota_active    = 0;
volatile uint8_t g_obstacle_flag = 0;
volatile uint8_t g_driving_state = 0;

/* DO 출력: 라인 감지 시 LOW(RESET), 미감지 시 HIGH(SET) */
static void read_sensors(uint8_t *s1, uint8_t *s2, uint8_t *s3, uint8_t *s4)
{
    *s4 = (HAL_GPIO_ReadPin(IR_S1_GPIO_Port, IR_S1_Pin) == GPIO_PIN_RESET) ? 1 : 0;
    *s3 = (HAL_GPIO_ReadPin(IR_S2_GPIO_Port, IR_S2_Pin) == GPIO_PIN_RESET) ? 1 : 0;
    *s2 = (HAL_GPIO_ReadPin(IR_S3_GPIO_Port, IR_S3_Pin) == GPIO_PIN_RESET) ? 1 : 0;
    *s1 = (HAL_GPIO_ReadPin(IR_S4_GPIO_Port, IR_S4_Pin) == GPIO_PIN_RESET) ? 1 : 0;
}

void drive_init(void)
{
    motor_init();
}

void drive_update(void)
{
    if (g_ota_active || g_obstacle_flag) {
        motor_stop();
        g_driving_state = 0;
        return;
    }

    uint8_t s1, s2, s3, s4;
    read_sensors(&s1, &s2, &s3, &s4);

    uint16_t lp, rp;

#if APP_VERSION == 1
    /* ON/OFF 제어
     * 우선순위: 외측 센서(S1/S4) > 내측 센서(S2/S3) > 라인 없음
     * 직진: S2+S3 동시 감지, 또는 전체 감지(교차로) */
    if (!s1 && !s2 && !s3 && !s4) {
        lp = rp = 0;                             /* 라인 없음 → 정지 */
    } else if (s1 && s2 && s3 && s4) {
        lp = rp = BASE_SPEED;                    /* 전체 감지(교차로) → 직진 */
    } else if (s1) {
        lp = 0;           rp = BASE_SPEED;       /* 맨 좌측 → 급좌회전 */
    } else if (s4) {
        lp = BASE_SPEED;  rp = 0;               /* 맨 우측 → 급우회전 */
    } else if (s2 && !s3) {
        lp = BASE_SPEED / 2; rp = BASE_SPEED;   /* 좌중 → 완좌회전 */
    } else if (s3 && !s2) {
        lp = BASE_SPEED; rp = BASE_SPEED / 2;   /* 우중 → 완우회전 */
    } else {
        lp = rp = BASE_SPEED;                    /* S2+S3 → 직진 */
    }
#else
    /* 비례 제어 (App v2)
     * 가중치: S1=-3, S2=-1, S3=+1, S4=+3
     * 양수 error → 오른쪽 라인 → 우회전(좌모터 증속, 우모터 감속) */
    if (!s1 && !s2 && !s3 && !s4) {
        lp = rp = 0;
    } else {
        int32_t error      = -3*(int32_t)s1 - 1*(int32_t)s2
                            + 1*(int32_t)s3 + 3*(int32_t)s4;
        int32_t correction = error * KP;
        int32_t left_raw   = (int32_t)BASE_SPEED + correction;
        int32_t right_raw  = (int32_t)BASE_SPEED - correction;
        lp = (uint16_t)(left_raw  < 0 ? 0 : left_raw  > 999 ? 999 : left_raw);
        rp = (uint16_t)(right_raw < 0 ? 0 : right_raw > 999 ? 999 : right_raw);
    }
#endif

    motor_set(lp, rp);
    g_driving_state = (lp > 0 || rp > 0) ? 1 : 0;
}
