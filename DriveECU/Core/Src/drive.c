#include "drive.h"
#include "motor.h"
#include "main.h"
#include <stdio.h>

#define BASE_SPEED      720  /* 직진 속도 — 모터 안정 선형 구간 (0–999) */
#define SLOW_SPEED      400  /* 후진 속도 — v2 */
#define STOP_DIST_CM     10  /* 정지 거리 (cm) */
#define SLOW_DIST_CM     60  /* (미사용) */
#define FORWARD_MS     6000  /* 전진 시간 (ms) — 실측 후 캘리브레이션 필요 */
#define REVERSE_MS      600  /* 후진 복귀 시간 (ms) — v2 */
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
#define KICK_SPEED      999  /* 정지 마찰 극복 — 최대 출력 (820으로는 간헐적 출발 불발) */
#define KICK_MS         250  /* 킥 지속 — 차체가 실제로 구르기 시작할 시간 (40ms는 너무 짧음) */
#define LAUNCH_FLOOR    620  /* 킥 이후 유지 최저 출력 — 구름저항 위. 멈추면 ↑, 너무 튀면 ↓ */
#define HOLD_MS         150
#define RAMP_MS         500

/* 가감속 변화량 제한 — 거리 변화나 시작 직후의 급격한 요잉 완화 */
#define PWM_STEP_UP      90
#define PWM_STEP_DOWN   180

volatile uint8_t  g_ota_active     = 0;
volatile uint8_t  g_obstacle_flag  = 0;
volatile uint8_t  g_driving_state  = 0;
volatile uint8_t  g_button_pressed = 0;
volatile uint16_t g_distance_cm    = 999;

/* 센서(0x200) 신선도 감시 — 침묵 시 fail-safe 정지 (ISO 26262 안전상태). */
#define SENSOR_TIMEOUT_MS 150u          /* 센서 주기 ~50ms의 3배(E-Gas 3-miss); 실측 보정 대상 */
static volatile uint32_t s_sensor_rx_tick = 0;
static volatile uint8_t  s_sensor_seen    = 0;

typedef enum {
    DRIVE_IDLE,
    DRIVE_RUNNING,
    DRIVE_STOPPED,    /* v2 전용: 정지 후 후진 대기 */
    DRIVE_REVERSING,  /* v2 전용 */
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
        base_pwm = LAUNCH_FLOOR;
        trim_l = LAUNCH_TRIM_L;
        trim_r = LAUNCH_TRIM_R;
    } else if (elapsed_ms < KICK_MS + HOLD_MS + RAMP_MS) {
        uint32_t t = elapsed_ms - KICK_MS - HOLD_MS;
        base_pwm = (uint16_t)lerp_i32(LAUNCH_FLOOR, target, t, RAMP_MS);
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

/* 0x200 수신 시 호출(ISR) — 신선도 감시용 수신 시각·플래그 갱신. */
void drive_note_sensor_rx(void)
{
    s_sensor_rx_tick = HAL_GetTick();
    s_sensor_seen    = 1;
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

    /* 센서 신선도 감시 (ISO 26262 안전상태 전이, AUTOSAR E2E 타임아웃):
     * Sensor(0x200) 침묵 → 장애물 정보 신뢰 불가 → 장애물로 간주하고 정지(fail-safe). */
    if (!drive_sensor_fresh(s_sensor_seen, now, s_sensor_rx_tick, SENSOR_TIMEOUT_MS)) {
        if (s_state != DRIVE_IDLE || g_driving_state) {
            drive_force_stop();
            printf("[DRIVE] 센서 stale → fail-safe 정지\r\n");
        }
        g_driving_state = 0;
        s_state = DRIVE_IDLE;
        return;
    }

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
        /* 장애물 플래그(10cm 이내) 감지 시 정지 후 자동 후진 */
        if (g_obstacle_flag) {
            drive_force_stop();
            g_driving_state = 0;
            s_state    = DRIVE_STOPPED;
            s_state_ts = now;
            printf("[DRIVE] 장애물(10cm) 감지 → 정지 후 후진\r\n");
            break;
        }
        drive_set_fwd(BASE_SPEED, elapsed);
#endif
        g_driving_state = 1;
        break;
    }

    case DRIVE_STOPPED:
        /* 300ms 대기 후 후진 시작 (v2 전용) */
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
