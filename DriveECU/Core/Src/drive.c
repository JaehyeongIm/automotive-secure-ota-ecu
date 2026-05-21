#include "drive.h"
#include "motor.h"
#include "main.h"

#define BASE_SPEED      600   /* 0..999 */
#define LINE_THRESHOLD  2048  /* 12-bit ADC 중간값, 실물 교정 필요 */
#define KP              200   /* v2 비례 게인: correction = error * KP / 4096 */

volatile uint8_t g_ota_active    = 0;
volatile uint8_t g_obstacle_flag = 0;
volatile uint8_t g_driving_state = 0;

extern ADC_HandleTypeDef hadc1;

static void read_sensors(uint16_t *l, uint16_t *c, uint16_t *r)
{
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    *l = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    *c = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    *r = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
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

    uint16_t l, c, r;
    read_sensors(&l, &c, &r);

    uint16_t lp, rp;

#if APP_VERSION == 1
    /* ON/OFF 제어: 감지된 센서 위치에 따라 방향 결정 */
    if (l > LINE_THRESHOLD) {
        lp = 0; rp = BASE_SPEED;           /* 좌측 감지 → 좌회전 */
    } else if (r > LINE_THRESHOLD) {
        lp = BASE_SPEED; rp = 0;           /* 우측 감지 → 우회전 */
    } else if (c > LINE_THRESHOLD) {
        lp = rp = BASE_SPEED;              /* 중앙 감지 → 직진 */
    } else {
        lp = rp = 0;                       /* 라인 없음 → 정지 */
    }
#else
    /* 비례 제어: 좌우 오차에 Kp 적용 */
    if (l < LINE_THRESHOLD && c < LINE_THRESHOLD && r < LINE_THRESHOLD) {
        lp = rp = 0;
    } else {
        int32_t error      = (int32_t)r - (int32_t)l;
        int32_t correction = (error * KP) / 4096;
        int32_t left_raw   = (int32_t)BASE_SPEED + correction;
        int32_t right_raw  = (int32_t)BASE_SPEED - correction;
        lp = (uint16_t)(left_raw  < 0 ? 0 : left_raw  > 999 ? 999 : left_raw);
        rp = (uint16_t)(right_raw < 0 ? 0 : right_raw > 999 ? 999 : right_raw);
    }
#endif

    motor_set(lp, rp);
    g_driving_state = (lp > 0 || rp > 0) ? 1 : 0;
}
