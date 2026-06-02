#include "unity.h"
#include "mock_isotp.h"
#include "mock_ota_flash.h"
#include "hal_stubs.h"
#include "uds.h"
#include "hmac_sha256.h"
#include "sha256.h"
#include "ota_psk.h"
#include <string.h>

/* ── 응답 캡처 ──────────────────────────────────────────────────────────────
   isotp_send()가 호출될 때마다 전송 데이터를 s_tx_buf에 복사한다.
   테스트에서 "ECU가 어떤 UDS 응답을 보냈는지" 검증하는 데 사용한다.    */
static uint8_t  s_tx_buf[64];
static uint16_t s_tx_len;

static void isotp_capture(const uint8_t *d, uint16_t len, int n)
{
    (void)n;
    memcpy(s_tx_buf, d, len);
    s_tx_len = len;
}

/* ── setUp / tearDown ───────────────────────────────────────────────────── */
void setUp(void)
{
    g_hal_tick         = 0;   /* seed 값을 결정론적으로 고정 */
    g_nvic_reset_count = 0;
    s_tx_len           = 0;
    memset(s_tx_buf, 0, sizeof(s_tx_buf));
    uds_init();
    /* 모든 isotp_send() 호출을 캡처로 연결 — mock 기대값 없이도 동작함 */
    isotp_send_Stub(isotp_capture);
}

void tearDown(void) {}

/* ── 헬퍼 함수 ──────────────────────────────────────────────────────────────
   UDS 메시지 수신 → 처리 한 사이클을 한 줄로 표현하기 위한 래퍼.
   실제 MCU에서 CAN 인터럽트(uds_on_isotp_rx) + 메인 루프(uds_process)에
   해당하는 두 단계를 순서대로 호출한다.                                    */
static void uds_send(const uint8_t *req, uint16_t len)
{
    uds_on_isotp_rx(req, len);
    uds_process();
}

/* ExtendedSession 진입 (0x10 0x02) */
static void do_extended_session(void)
{
    uint8_t req[] = {0x10, 0x02};
    uds_send(req, sizeof(req));
}

/* uds.c의 UNIT_TEST PSK와 동일: ASCII "OTA-DEV-PSK-DO-NOT-USE-IN-PROD!!" */
static const uint8_t s_test_psk[OTA_PSK_LEN] = {
    0x4F,0x54,0x41,0x2D,0x44,0x45,0x56,0x2D,0x50,0x53,0x4B,0x2D,0x44,0x4F,0x2D,0x4E,
    0x4F,0x54,0x2D,0x55,0x53,0x45,0x2D,0x49,0x4E,0x2D,0x50,0x52,0x4F,0x44,0x21,0x21
};

/* SecurityAccess Unlock:
   seed 요청 → 응답에서 seed 추출 → Key = HMAC-SHA256(PSK, Seed)[0:4] 전송  */
static void do_unlock(void)
{
    do_extended_session();

    uint8_t seed_req[] = {0x27, 0x01};
    uds_send(seed_req, sizeof(seed_req));

    /* s_tx_buf: [0x67, 0x01, seed_b3..seed_b0] — Seed가 HMAC 메시지(big-endian) */
    uint8_t seed_be[4] = { s_tx_buf[2], s_tx_buf[3], s_tx_buf[4], s_tx_buf[5] };
    uint8_t mac[32];
    hmac_sha256(s_test_psk, OTA_PSK_LEN, seed_be, 4, mac);

    uint8_t key_req[] = {0x27, 0x02, mac[0], mac[1], mac[2], mac[3]};
    uds_send(key_req, sizeof(key_req));
}

/* RequestDownload (Slot A 활성 → Slot B 대상):
   ota_flash mock에 기대값을 설정한 뒤 0x34를 전송한다.
   size=32768(0x8000) — Slot B 최대(256KB) 이내의 유효한 값              */
static void do_request_download(void)
{
    do_unlock();
    ota_get_active_slot_ExpectAndReturn(0);          /* Slot A 활성 */
    ota_flash_erase_slot_b_ExpectAndReturn(HAL_OK);  /* Slot B 소거 */

    uint8_t req[] = {0x34, 0x00, 0x44,
                     0x00, 0x00, 0x00, 0x00,   /* addr (ECU가 무시) */
                     0x00, 0x00, 0x80, 0x00};  /* size = 32768 bytes */
    uds_send(req, sizeof(req));
}

/* ═══════════════════════════════════════════════════════════════════════════
   TC-UT-UDS-001: DiagnosticSessionControl 정상 응답
   ═══════════════════════════════════════════════════════════════════════════
   SRS FR-CAN-009: Default/Extended Session 구분 지원
   0x10 0x02 → 긍정 응답 0x50 0x02 확인                                     */
void test_extended_session_returns_positive_response(void)
{
    do_extended_session();

    TEST_ASSERT_EQUAL_UINT8(0x50, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x02, s_tx_buf[1]);
    TEST_ASSERT_EQUAL_UINT16(2,   s_tx_len);
}

/* 0x10 0x01 → DefaultSession 복귀 응답 0x50 0x01                           */
void test_default_session_returns_positive_response(void)
{
    do_extended_session();   /* 먼저 Extended로 전환 */

    uint8_t req[] = {0x10, 0x01};
    uds_send(req, sizeof(req));

    TEST_ASSERT_EQUAL_UINT8(0x50, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x01, s_tx_buf[1]);
}

/* ═══════════════════════════════════════════════════════════════════════════
   TC-UT-UDS-001: SecurityAccess 정상 흐름
   ═══════════════════════════════════════════════════════════════════════════
   SRS FR-CAN-010: Seed 요청 시 0x67 0x01 + 4바이트 Seed 응답              */
void test_sa_seed_request_returns_seed(void)
{
    do_extended_session();

    uint8_t req[] = {0x27, 0x01};
    uds_send(req, sizeof(req));

    TEST_ASSERT_EQUAL_UINT8(0x67, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x01, s_tx_buf[1]);
    TEST_ASSERT_EQUAL_UINT16(6,   s_tx_len);  /* SID(1) + sub(1) + seed(4) */
}

/* Seed 요청 후 올바른 Key 전송 → 0x67 0x02 (Unlock 성공)
   Key = HMAC-SHA256(PSK, Seed)[0:4] — do_unlock()이 동적으로 계산         */
void test_sa_correct_key_returns_unlock_ok(void)
{
    do_unlock();

    TEST_ASSERT_EQUAL_UINT8(0x67, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x02, s_tx_buf[1]);
}

/* ═══════════════════════════════════════════════════════════════════════════
   TC-UT-UDS-001: RequestDownload 정상 응답
   ═══════════════════════════════════════════════════════════════════════════
   SRS FR-CAN-011: Unlock 후 0x34 → 0x74 긍정 응답
   uds_ota_active()로 DOWNLOADING 상태 진입도 동시에 확인                   */
void test_request_download_returns_positive_response(void)
{
    do_request_download();

    TEST_ASSERT_EQUAL_UINT8(0x74, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_INT(1, uds_ota_active());
}

/* Slot B 활성일 때 Slot A를 대상으로 선택하는지 확인                       */
void test_request_download_targets_slot_a_when_b_active(void)
{
    do_unlock();
    ota_get_active_slot_ExpectAndReturn(1);          /* Slot B 활성 */
    ota_flash_erase_slot_a_ExpectAndReturn(HAL_OK);  /* Slot A 소거 기대 */

    uint8_t req[] = {0x34, 0x00, 0x44,
                     0x00, 0x00, 0x00, 0x00,
                     0x00, 0x00, 0x80, 0x00};
    uds_send(req, sizeof(req));

    TEST_ASSERT_EQUAL_UINT8(0x74, s_tx_buf[0]);
}

/* ═══════════════════════════════════════════════════════════════════════════
   TC-UT-UDS-002: 잘못된 세션/상태에서 SID 시도 → NRC 0x22
   ═══════════════════════════════════════════════════════════════════════════
   NRC 응답 구조: [0x7F, SID, NRC_code]
   SRS FR-CAN-016: 조건 불충족 시 NRC 0x22(conditionsNotCorrect)          */

/* Default 세션에서 SecurityAccess → NRC 0x22                               */
void test_sa_in_default_session_returns_nrc_conditions_not_correct(void)
{
    uint8_t req[] = {0x27, 0x01};
    uds_send(req, sizeof(req));

    TEST_ASSERT_EQUAL_UINT8(0x7F, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x27, s_tx_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x22, s_tx_buf[2]);
}

/* Extended 세션이지만 Unlock 전에 RequestDownload → NRC 0x22              */
void test_request_download_without_unlock_returns_nrc_conditions_not_correct(void)
{
    do_extended_session();

    uint8_t req[] = {0x34, 0x00, 0x44,
                     0x00, 0x00, 0x00, 0x00,
                     0x00, 0x00, 0x80, 0x00};
    uds_send(req, sizeof(req));

    TEST_ASSERT_EQUAL_UINT8(0x7F, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x34, s_tx_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x22, s_tx_buf[2]);
}

/* Seed 요청 없이 Key 전송(잘못된 순서) → NRC 0x24(requestSequenceError)  */
void test_sa_key_without_seed_returns_nrc_sequence_error(void)
{
    do_extended_session();

    uint8_t req[] = {0x27, 0x02, 0x00, 0x00, 0x00, 0x00};
    uds_send(req, sizeof(req));

    TEST_ASSERT_EQUAL_UINT8(0x7F, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x27, s_tx_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x24, s_tx_buf[2]);
}

/* 잘못된 Key → NRC 0x35(invalidKey), 상태는 Extended로 복귀               */
void test_sa_wrong_key_returns_nrc_invalid_key(void)
{
    do_extended_session();

    uint8_t seed_req[] = {0x27, 0x01};
    uds_send(seed_req, sizeof(seed_req));

    /* 의도적으로 틀린 key 전송 */
    uint8_t key_req[] = {0x27, 0x02, 0x00, 0x00, 0x00, 0x00};
    uds_send(key_req, sizeof(key_req));

    TEST_ASSERT_EQUAL_UINT8(0x7F, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x27, s_tx_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x35, s_tx_buf[2]);
}

/* 연속 3회 잘못된 Key → NRC 0x36(exceededNumberOfAttempts) + 10초 잠금,
   잠금 중 추가 요청 → NRC 0x37(requiredTimeDelayNotExpired),
   10초 경과 후 잠금 해제 (SRS FR-CAN-010, SR-ATK-006)                     */
void test_sa_lockout_after_three_wrong_keys(void)
{
    do_extended_session();

    for (int i = 0; i < 3; ++i) {
        uint8_t seed_req[] = {0x27, 0x01};
        uds_send(seed_req, sizeof(seed_req));            /* SEED_SENT */
        uint8_t bad_key[] = {0x27, 0x02, 0x00, 0x00, 0x00, 0x00};
        uds_send(bad_key, sizeof(bad_key));              /* wrong Key */
    }
    /* 3회째 실패 → NRC 0x36 */
    TEST_ASSERT_EQUAL_UINT8(0x7F, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x27, s_tx_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x36, s_tx_buf[2]);

    /* 잠금 상태: seed 요청조차 NRC 0x37 */
    uint8_t seed_req[] = {0x27, 0x01};
    uds_send(seed_req, sizeof(seed_req));
    TEST_ASSERT_EQUAL_UINT8(0x37, s_tx_buf[2]);

    /* 10초(=SEC_LOCK_MS) 경과 → 잠금 해제, seed 요청 정상 */
    g_hal_tick = 10000;
    uds_send(seed_req, sizeof(seed_req));
    TEST_ASSERT_EQUAL_UINT8(0x67, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x01, s_tx_buf[1]);
}

/* ═══════════════════════════════════════════════════════════════════════════
   TC-UT-UDS-003: TransferData 블록 시퀀스 오류 → NRC 0x73
   ═══════════════════════════════════════════════════════════════════════════
   SRS FR-CAN-012: Sequence Number 불일치 시 NRC 0x73(wrongBlockSequenceCounter)
   RequestDownload 직후 g_block_seq=1, seq=2로 전송하면 즉시 NRC 반환     */
void test_transfer_data_wrong_seq_returns_nrc_wrong_block_sequence(void)
{
    do_request_download();

    /* seq=2로 전송 (기대값은 1) */
    uint8_t req[] = {0x36, 0x02, 0xAA, 0xBB};
    uds_send(req, sizeof(req));

    TEST_ASSERT_EQUAL_UINT8(0x7F, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x36, s_tx_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x73, s_tx_buf[2]);
}
