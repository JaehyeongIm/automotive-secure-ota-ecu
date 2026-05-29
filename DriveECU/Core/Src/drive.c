#include "drive.h"
#include "motor.h"
#include "main.h"
#include <stdio.h>

#define BASE_SPEED      720  /* 직진 속도 — 모터 안정 선형 구간 (0–999) */
#define SLOW_SPEED      400  /* 감속 속도 — v2, v3 */
#define STOP_DIST_CM     10  /* 정지 거리 (cm) */
#define SLOW_DIST_CM     60  /* 감속 시작 거리 (cm) — v2, v3 */
#define FORWARD_MS     6000  /* 전진 시간 (ms) — 실측 후 캘리브레이션 필요 */
#define REVERSE_MS      600  /* 후진 복귀 시간 (ms) — v3 */
#define PWM_MAX         999

/*
 * 직진 유지용 개방루프 보정:
 * - LAUNCH_TRIM_*: 출발 직후 차체가 비틀릴 때 보정
 * - CRUISE_TRIM_*: 정속 주행 중 한쪽으로 쏠릴 때 보정
 * +값은 해당 모터를 더 빠르게 만든다.
 */
#define LAUNCH_TRIM_L     0
#define LAUNCH_TRIM_R     0
#define CRUISE_TRIM_L     0
#define CRUISE_TRIM_R     0

/*
 * 출발 프로파일:
 * 1) 짧은 킥으로 정지 마찰 극복
 * 2) 낮은 속도 유지로 차체 자세 안정화
 * 3) 목표 속도까지 선형 램프업
 */
#define KICK_SPEED      820
#define KICK_MS          40
#define HOLD_MS         120
#define RAMP_MS         600

/* 가감속 변화량 제한 — 거리 변화나 시작 직후의 급격한 요잉 완화 */
#define PWM_STEP_UP      90
#define PWM_STEP_DOWN   180

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
static uint16_t   s_last_left_pwm  = 0;
static uint16_t   s_last_right_pwm = 0;

static uint16_t clamp_pwm(int32_t value)
{
    if (value < 0) {
        return 0;
    }
    if (value > PWM_MAX) {
        return PWM_MAX;
    }
    return (uint16_t)value;
}

static int32_t lerp_i32(int32_t from, int32_t to, uint32_t elapsed, uint32_t duration)
{
    if (duration == 0 || elapsed >= duration) {
        return to;
    }
    return from + ((to - from) * (int32_t)elapsed) / (int32_t)duration;
}

static uint16_t slew_limit(uint16_t current, uint16_t target)
{
    if (target > current) {
        uint16_t delta = (uint16_t)(target - current);
        if (delta > PWM_STEP_UP) {
            delta = PWM_STEP_UP;
        }
        return (uint16_t)(current + delta);
    }

    uint16_t delta = (uint16_t)(current - target);
    if (delta > PWM_STEP_DOWN) {
        delta = PWM_STEP_DOWN;
    }
    return (uint16_t)(current - delta);
}

static void drive_force_stop(void)
{
    s_last_left_pwm = 0;
    s_last_right_pwm = 0;
    motor_stop();
}

/* 킥 → 자세 안정화 → 램프업 → 정속 순으로 좌우 PWM을 독립 계산 */
static void drive_set_fwd(uint16_t target, uint32_t elapsed_ms)
{
    uint16_t base_pwm;
    int32_t trim_l;
    int32_t trim_r;

    if (elapsed_ms < KICK_MS) {
        base_pwm = KICK_SPEED;
        trim_l = LAUNCH_TRIM_L;
        trim_r = LAUNCH_TRIM_R;
    } else if (elapsed_ms < KICK_MS + HOLD_MS) {
        base_pwm = SLOW_SPEED;
        trim_l = LAUNCH_TRIM_L;
        trim_r = LAUNCH_TRIM_R;
    } else if (elapsed_ms < KICK_MS + HOLD_MS + RAMP_MS) {
        uint32_t t = elapsed_ms - KICK_MS - HOLD_MS;
        base_pwm = (uint16_t)lerp_i32(SLOW_SPEED, target, t, RAMP_MS);
        trim_l = lerp_i32(LAUNCH_TRIM_L, CRUISE_TRIM_L, t, RAMP_MS);
        trim_r = lerp_i32(LAUNCH_TRIM_R, CRUISE_TRIM_R, t, RAMP_MS);
    } else {
        base_pwm = target;
        trim_l = CRUISE_TRIM_L;
        trim_r = CRUISE_TRIM_R;
    }

    if (elapsed_ms < KICK_MS) {
        s_last_left_pwm = clamp_pwm((int32_t)base_pwm + trim_l);
        s_last_right_pwm = clamp_pwm((int32_t)base_pwm + trim_r);
    } else {
        uint16_t target_left = clamp_pwm((int32_t)base_pwm + trim_l);
        uint16_t target_right = clamp_pwm((int32_t)base_pwm + trim_r);

        s_last_left_pwm = slew_limit(s_last_left_pwm, target_left);
        s_last_right_pwm = slew_limit(s_last_right_pwm, target_right);
    }

    motor_set(s_last_left_pwm, s_last_right_pwm);
}

void drive_init(void)
{
    motor_init();
}

void drive_update(void)
{
    if (g_ota_active) {
        drive_force_stop();
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

    case DRIVE_RUNNING: {
        uint32_t elapsed = now - s_state_ts;

        /* 1m 시간 완료 → 정상 정지 */
        if (elapsed >= FORWARD_MS) {
            drive_force_stop();
            g_driving_state = 0;
            s_state = DRIVE_IDLE;
            printf("[DRIVE] 1m 완료 → 정지\r\n");
            break;
        }

#if APP_VERSION == 1
        /* 장애물 플래그(10cm 이내) 감지 시 즉시 정지 */
        if (g_obstacle_flag) {
            drive_force_stop();
            g_driving_state = 0;
            s_state = DRIVE_IDLE;
            printf("[DRIVE] 장애물(10cm) 감지 → 정지\r\n");
            break;
        }
        drive_set_fwd(BASE_SPEED, elapsed);

#elif APP_VERSION == 2
        /* 10cm 이내: 정지 / 10~60cm: 거리 비례 감속 / 60cm 이상: 정속 */
        if (g_distance_cm <= STOP_DIST_CM) {
            drive_force_stop();
            g_driving_state = 0;
            s_state = DRIVE_IDLE;
            printf("[DRIVE] %ucm → 정지\r\n", g_distance_cm);
            break;
        } else if (g_distance_cm <= SLOW_DIST_CM) {
            uint16_t sp = SLOW_SPEED + (uint16_t)(
                (uint32_t)(BASE_SPEED - SLOW_SPEED)
                * (g_distance_cm - STOP_DIST_CM)
                / (SLOW_DIST_CM  - STOP_DIST_CM));
            drive_set_fwd(sp, elapsed);
        } else {
            drive_set_fwd(BASE_SPEED, elapsed);
        }

#elif APP_VERSION == 3
        /* v2 감속 로직 동일 + 정지 후 자동 후진 */
        if (g_distance_cm <= STOP_DIST_CM) {
            drive_force_stop();
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
            drive_set_fwd(sp, elapsed);
        } else {
            drive_set_fwd(BASE_SPEED, elapsed);
        }
#endif
        g_driving_state = 1;
        break;
    }

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
            drive_force_stop();
            s_state = DRIVE_IDLE;
            printf("[DRIVE] 후진 완료 → 대기\r\n");
        }
        break;
    }
}
