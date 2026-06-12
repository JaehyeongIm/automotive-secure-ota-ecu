/* SR-ATK-005 (replay) — SecurityAccess seed freshness 실증/검증.
 *
 * seed = sec_derive_seed(UID, mid, ctr)  [SHA-256(UID‖mid‖ctr)[0:4]]
 *
 *   현행 구조:  mid = SysTick(재부팅 시 0부터),  ctr = RAM(재부팅 시 1부터)  → 둘 다 리셋.
 *   → 재부팅 후 같은 타이밍이면 같은 (mid,ctr) → 같은 seed → 캡처한 Key가 다시 맞음.
 *
 * 이 파일은 그 재현성을 결정론적으로 실증(①②)하고, 영속 단조 epoch로 바꾸면 닫힘(③)을
 * 같은 순수 함수로 검증한다. (퍼징 SC3 음성통제와 같은 "취약→수정 대조" 방식)            */
#include "unity.h"
#include "mock_isotp.h"
#include "mock_ota_flash.h"
#include "hal_stubs.h"
#include "uds.h"
#include "ota_meta.h"
#include "hmac_sha256.h"
#include "sha256.h"
#include "ota_psk.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* 보드별 고정 device UID 모델 (재부팅해도 동일) */
static const uint8_t s_uid[12] = {
    0x11,0x22,0x33,0x44, 0x55,0x66,0x77,0x88, 0x99,0xAA,0xBB,0xCC
};

/* SecurityAccess PSK (uds.c UNIT_TEST와 동일한 테스트 키) */
static const uint8_t s_psk[OTA_PSK_LEN] = {
    0x4F,0x54,0x41,0x2D,0x44,0x45,0x56,0x2D,0x50,0x53,0x4B,0x2D,0x44,0x4F,0x2D,0x4E,
    0x4F,0x54,0x2D,0x55,0x53,0x45,0x2D,0x49,0x4E,0x2D,0x50,0x52,0x4F,0x44,0x21,0x21
};

/* 테스터/ECU가 똑같이 계산하는 Key = HMAC-SHA256(PSK, Seed_be)[0:4]. */
static uint32_t compute_key(uint32_t seed)
{
    uint8_t seed_be[4] = {
        (uint8_t)(seed >> 24), (uint8_t)(seed >> 16),
        (uint8_t)(seed >> 8),  (uint8_t)(seed)
    };
    uint8_t mac[32];
    hmac_sha256(s_psk, OTA_PSK_LEN, seed_be, 4, mac);
    return ((uint32_t)mac[0] << 24) | ((uint32_t)mac[1] << 16) |
           ((uint32_t)mac[2] << 8) | mac[3];
}

/* ── 현행 구조 모델 ──────────────────────────────────────────────────────────
   부팅 직후 첫 RequestSeed: mid = 그 시점 SysTick(tick), ctr = 1(재부팅마다 1부터). */
static uint32_t boot_first_seed_CURRENT(uint32_t tick_at_request)
{
    return sec_derive_seed(s_uid, /*mid=tick*/ tick_at_request, /*ctr*/ 1u);
}

/* ★ 실증 ① — 현행: 재부팅 후 같은 타이밍이면 seed가 *재현*된다(replay 창 실재). */
void test_current_structure_reproduces_seed_across_reboot(void)
{
    uint32_t boot1 = boot_first_seed_CURRENT(/*tick*/ 100u);  /* 1차 부팅, 첫 요청 */
    /* ── 전원 리셋: SysTick→0부터, RAM ctr→0부터 다시 ── */
    uint32_t boot2 = boot_first_seed_CURRENT(/*tick*/ 100u);  /* 2차 부팅, 같은 타이밍 */

    /* UID 고정·tick 동일·ctr=1 동일 → seed *동일* → 재현 가능 */
    TEST_ASSERT_EQUAL_HEX32(boot1, boot2);
}

/* ★ 실증 ② — seed가 재현되면 PSK 없이 *캡처한 Key*로 인증된다(replay 성공). */
void test_captured_key_replays_when_seed_reproduced(void)
{
    /* 1차 부팅: 공격자가 (seed, key) 캡처 — Key는 정상 테스터가 PSK로 만든 값 */
    uint32_t seed1        = boot_first_seed_CURRENT(100u);
    uint32_t captured_key = compute_key(seed1);

    /* 전원 리셋 후 2차 부팅: ECU가 같은 seed 재생성 */
    uint32_t seed2        = boot_first_seed_CURRENT(100u);
    uint32_t expected_key = compute_key(seed2);   /* ECU가 자기 PSK로 계산 */

    /* 캡처한 Key == 2차 부팅 기대 Key → PSK 몰라도 unlock (replay 성공) */
    TEST_ASSERT_EQUAL_HEX32(captured_key, expected_key);
}

/* 음성 대조 — 같은 부팅 안에서는 ctr이 증가하므로 seed가 안 겹친다(부팅 내 replay 차단). */
void test_within_one_boot_seed_does_not_repeat(void)
{
    uint32_t s_req1 = sec_derive_seed(s_uid, 100u, 1u);
    uint32_t s_req2 = sec_derive_seed(s_uid, 100u, 2u);   /* 같은 부팅, ctr++ */
    TEST_ASSERT_NOT_EQUAL(s_req1, s_req2);
}

/* ── 수정 구조 모델 ──────────────────────────────────────────────────────────
   mid = 영속 boot_epoch(재부팅마다 +1, 절대 리셋·감소 안 함). 첫 요청 ctr=1.       */
static uint32_t boot_first_seed_FIXED(uint32_t boot_epoch)
{
    return sec_derive_seed(s_uid, /*mid=epoch*/ boot_epoch, /*ctr*/ 1u);
}

/* ★ 실증 ③ — 수정: 영속 epoch면 재부팅 후 seed가 *달라진다*(replay 창 닫힘). */
void test_boot_epoch_makes_seed_unique_across_reboot(void)
{
    uint32_t boot1 = boot_first_seed_FIXED(/*epoch*/ 5u);  /* 1차 부팅 */
    /* ── 전원 리셋: epoch는 영속 → 6으로 증가(리셋 안 됨) ── */
    uint32_t boot2 = boot_first_seed_FIXED(/*epoch*/ 6u);  /* 2차 부팅 */

    TEST_ASSERT_NOT_EQUAL(boot1, boot2);   /* seed 달라짐 → 재현 불가 */
}

/* ★ 실증 ④ — 수정 구조에선 캡처한 Key가 다음 부팅에 안 맞는다(replay 실패). */
void test_captured_key_fails_to_replay_with_boot_epoch(void)
{
    uint32_t seed1        = boot_first_seed_FIXED(5u);     /* 1차 부팅, 캡처 */
    uint32_t captured_key = compute_key(seed1);

    uint32_t seed2        = boot_first_seed_FIXED(6u);     /* epoch 증가 → 다른 seed */
    uint32_t expected_key = compute_key(seed2);           /* ECU 기대 Key */

    TEST_ASSERT_NOT_EQUAL(captured_key, expected_key);    /* 캡처 Key ≠ 기대 → unlock 실패 */
}
