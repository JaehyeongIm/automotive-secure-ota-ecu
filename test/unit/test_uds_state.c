#include "unity.h"
#include "mock_isotp.h"
#include "mock_ota_flash.h"
#include "hal_stubs.h"
#include "uds.h"
#include "ota_meta.h"   /* uds.c가 ota_img_header_read를 호출 → ota_meta.c 링크용 */
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

    /* 10초(=SEC_LOCK_MS) 경과 → 잠금 해제. 단 그 사이 5s S3 타임아웃(FR-CAN-019)으로
       세션이 Default로 abort됐으므로, 잠금 해제 확인 전 Extended 세션 재진입이 필요하다.
       (잠금 카운터는 세션과 별개로 유지되어 S3로 우회되지 않는다.) */
    g_hal_tick = 10000;
    do_extended_session();                  /* S3로 끊긴 세션 재진입 */
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

/* ═══════════════════════════════════════════════════════════════════════════
   TC-UT-UDS-004~ : 음성 테스트 — 거부 분기 보강 (분기 커버리지)
   ═══════════════════════════════════════════════════════════════════════════ */

/* TransferData 정상 블록(seq 일치) → 0x76 + 블록 진행 (성공 경로) */
void test_transfer_data_correct_seq_returns_positive(void)
{
    do_request_download();
    ota_flash_write_IgnoreAndReturn(HAL_OK);
    uint8_t req[] = {0x36, 0x01, 0xAA, 0xBB};        /* seq=1 = 기대값 */
    uds_send(req, sizeof(req));
    TEST_ASSERT_EQUAL_UINT8(0x76, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x01, s_tx_buf[1]);
}

/* TransferData 중 flash write 실패 → NRC 0x72(transferDataSuspended/general) */
void test_transfer_data_write_fail_returns_nrc(void)
{
    do_request_download();
    ota_flash_write_IgnoreAndReturn(HAL_ERROR);
    uint8_t req[] = {0x36, 0x01, 0xAA, 0xBB};
    uds_send(req, sizeof(req));
    TEST_ASSERT_EQUAL_UINT8(0x7F, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x72, s_tx_buf[2]);
}

/* RequestDownload size=0 (endless-data 방어) → NRC 0x31(requestOutOfRange) */
void test_request_download_size_zero_returns_nrc(void)
{
    do_unlock();
    ota_get_active_slot_ExpectAndReturn(0);          /* size 검사 전 호출됨 */
    uint8_t req[] = {0x34, 0x00, 0x44, 0,0,0,0, 0,0,0,0};   /* size=0 */
    uds_send(req, sizeof(req));
    TEST_ASSERT_EQUAL_UINT8(0x7F, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x31, s_tx_buf[2]);
}

/* RequestDownload size > 슬롯 최대(256KB+) → NRC 0x31 (oversized, SR-ATK-007) */
void test_request_download_size_too_large_returns_nrc(void)
{
    do_unlock();
    ota_get_active_slot_ExpectAndReturn(0);
    uint8_t req[] = {0x34, 0x00, 0x44, 0,0,0,0, 0x00,0x05,0x00,0x00};  /* 0x50000 > 0x40000 */
    uds_send(req, sizeof(req));
    TEST_ASSERT_EQUAL_UINT8(0x7F, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x31, s_tx_buf[2]);
}

/* RequestDownload 잘못된 dataFormat/ALFID(req[2]≠0x44) → NRC 0x31 */
void test_request_download_bad_format_returns_nrc(void)
{
    do_unlock();
    uint8_t req[] = {0x34, 0x00, 0x55, 0,0,0,0, 0,0,0x80,0};   /* req[2]=0x55 */
    uds_send(req, sizeof(req));
    TEST_ASSERT_EQUAL_UINT8(0x7F, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x31, s_tx_buf[2]);
}

/* RequestDownload 슬롯 소거 실패 → NRC 0x72 */
void test_request_download_erase_fail_returns_nrc(void)
{
    do_unlock();
    ota_get_active_slot_ExpectAndReturn(0);          /* Slot A 활성 → B 대상 */
    ota_flash_erase_slot_b_ExpectAndReturn(HAL_ERROR);
    uint8_t req[] = {0x34, 0x00, 0x44, 0,0,0,0, 0,0,0x80,0};   /* size=0x8000 */
    uds_send(req, sizeof(req));
    TEST_ASSERT_EQUAL_UINT8(0x7F, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x72, s_tx_buf[2]);
}

/* DOWNLOADING 아닌데 TransferData → NRC 0x22 */
void test_transfer_data_not_downloading_returns_nrc(void)
{
    uint8_t req[] = {0x36, 0x01, 0xAA, 0xBB};
    uds_send(req, sizeof(req));
    TEST_ASSERT_EQUAL_UINT8(0x7F, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x22, s_tx_buf[2]);
}

/* DOWNLOADING 아닌데 TransferExit → NRC 0x22 */
void test_transfer_exit_not_downloading_returns_nrc(void)
{
    uint8_t req[] = {0x37};
    uds_send(req, sizeof(req));
    TEST_ASSERT_EQUAL_UINT8(0x7F, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x22, s_tx_buf[2]);
}

/* 미지원 SID → NRC 0x11(serviceNotSupported) */
void test_unknown_sid_returns_nrc_service_not_supported(void)
{
    uint8_t req[] = {0x99};
    uds_send(req, sizeof(req));
    TEST_ASSERT_EQUAL_UINT8(0x7F, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x99, s_tx_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x11, s_tx_buf[2]);
}

/* DiagnosticSessionControl 미지원 subfunction → NRC 0x12 */
void test_session_unknown_subfunction_returns_nrc(void)
{
    uint8_t req[] = {0x10, 0x03};
    uds_send(req, sizeof(req));
    TEST_ASSERT_EQUAL_UINT8(0x7F, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x12, s_tx_buf[2]);
}

/* SecurityAccess 미지원 subfunction → NRC 0x12 */
void test_sa_unknown_subfunction_returns_nrc(void)
{
    do_extended_session();
    uint8_t req[] = {0x27, 0x03};
    uds_send(req, sizeof(req));
    TEST_ASSERT_EQUAL_UINT8(0x7F, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x12, s_tx_buf[2]);
}

/* 길이 부족 메시지 → NRC 0x13(incorrectMessageLength): 0x34 / 0x36 */
void test_request_download_too_short_returns_nrc_length(void)
{
    do_unlock();
    uint8_t req[] = {0x34, 0x00, 0x44};              /* len 3 < 11 */
    uds_send(req, sizeof(req));
    TEST_ASSERT_EQUAL_UINT8(0x7F, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x13, s_tx_buf[2]);
}

void test_transfer_data_too_short_returns_nrc_length(void)
{
    do_request_download();
    uint8_t req[] = {0x36};                          /* len 1 < 2 */
    uds_send(req, sizeof(req));
    TEST_ASSERT_EQUAL_UINT8(0x7F, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x13, s_tx_buf[2]);
}

/* F-003 회귀: 한 블록 데이터가 광고 maxBlockLen(256) 초과 → NRC 0x31 (padded[260] 스택오버플로 방지).
   퍼징(FH-3)이 261B 블록으로 발견한 CWE-787을 차단. */
void test_transfer_data_oversized_block_returns_nrc(void)
{
    do_request_download();
    uint8_t req[2 + 300];                            /* chunk_len = 300 > 256 */
    req[0] = 0x36; req[1] = 0x01;
    memset(req + 2, 0xAB, 300);
    uds_send(req, sizeof(req));
    TEST_ASSERT_EQUAL_UINT8(0x7F, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x36, s_tx_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x31, s_tx_buf[2]);
}

/* ═══════════════════════════════════════════════════════════════════════════
   TC-UT-UDS-005: endless-data 방어 — 누적 수신 상한·완료 검증
   ═══════════════════════════════════════════════════════════════════════════
   SRS FR-CAN-012(누적 > image_size → NRC 0x31 + 세션 종료, SR-ATK-007)
       FR-CAN-013(누적 != image_size → NRC 0x24)                            */

/* 누적 수신이 선언 size 초과 → NRC 0x31 + 세션 종료(이후 TransferData는 NRC 0x22) */
void test_transfer_data_exceeds_declared_size_returns_nrc_and_aborts(void)
{
    do_unlock();
    ota_get_active_slot_ExpectAndReturn(0);          /* Slot A 활성 → B 대상 */
    ota_flash_erase_slot_b_ExpectAndReturn(HAL_OK);
    uint8_t dl[] = {0x34, 0x00, 0x44, 0,0,0,0, 0,0,0,4};   /* size = 4 bytes */
    uds_send(dl, sizeof(dl));
    TEST_ASSERT_EQUAL_UINT8(0x74, s_tx_buf[0]);

    /* seq=1, chunk_len=8 > size 4 → 쓰기 전에 거부 (flash write 미호출) */
    uint8_t blk[] = {0x36, 0x01, 1,2,3,4,5,6,7,8};
    uds_send(blk, sizeof(blk));
    TEST_ASSERT_EQUAL_UINT8(0x7F, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x36, s_tx_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x31, s_tx_buf[2]);

    /* 세션 종료 확인: DOWNLOADING 아님 → 추가 TransferData는 NRC 0x22 */
    TEST_ASSERT_EQUAL_INT(0, uds_ota_active());
    uint8_t blk2[] = {0x36, 0x01, 0xAA};
    uds_send(blk2, sizeof(blk2));
    TEST_ASSERT_EQUAL_UINT8(0x22, s_tx_buf[2]);
}

/* RequestTransferExit 시 누적 수신 != 선언 size → NRC 0x24 (불완전 전송 거부) */
void test_transfer_exit_incomplete_returns_nrc_sequence_error(void)
{
    do_unlock();
    ota_get_active_slot_ExpectAndReturn(0);
    ota_flash_erase_slot_b_ExpectAndReturn(HAL_OK);
    uint8_t dl[] = {0x34, 0x00, 0x44, 0,0,0,0, 0,0,0,8};   /* size = 8 bytes */
    uds_send(dl, sizeof(dl));

    /* 4바이트만 전송(size 8 미만) → 정상 0x76 */
    ota_flash_write_IgnoreAndReturn(HAL_OK);
    uint8_t blk[] = {0x36, 0x01, 1,2,3,4};                 /* chunk_len = 4 */
    uds_send(blk, sizeof(blk));
    TEST_ASSERT_EQUAL_UINT8(0x76, s_tx_buf[0]);

    /* 0x37 → written(4) != size(8) → NRC 0x24 (헤더 deref 전에 거부) */
    uint8_t exit_req[] = {0x37};
    uds_send(exit_req, sizeof(exit_req));
    TEST_ASSERT_EQUAL_UINT8(0x7F, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x37, s_tx_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x24, s_tx_buf[2]);
}

/* ═══════════════════════════════════════════════════════════════════════════
   TC-UT-UDS-006: S3 세션 타임아웃 (FR-CAN-019 / FR-BL-012 / NFR-REL-003)
   ═══════════════════════════════════════════════════════════════════════════
   비-Default 세션에서 마지막 요청 후 5000ms 무요청이면 세션 abort → Default 복귀.   */

/* 5s 초과 무요청 → 세션 abort. 이후 TransferData는 DOWNLOADING 아님 → NRC 0x22 */
void test_s3_timeout_aborts_session(void)
{
    do_request_download();                       /* DOWNLOADING 진입(tick=0) */
    TEST_ASSERT_EQUAL_INT(1, uds_ota_active());

    g_hal_tick = 5001;                           /* S3_TIMEOUT_MS(5000) + 1 */
    uds_process();                               /* 보류 메시지 없이 S3 검사 발화 */
    TEST_ASSERT_EQUAL_INT(0, uds_ota_active());  /* 세션 abort → DOWNLOADING 아님 */

    uint8_t blk[] = {0x36, 0x01, 0xAA};
    uds_send(blk, sizeof(blk));
    TEST_ASSERT_EQUAL_UINT8(0x7F, s_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x22, s_tx_buf[2]);  /* conditionsNotCorrect */
}

/* 타임아웃 전(5s 이내)엔 abort 안 함 — 정상 전송 회귀 방지 */
void test_s3_no_abort_within_timeout(void)
{
    do_request_download();
    g_hal_tick = 4999;                           /* S3_TIMEOUT_MS 미만 */
    uds_process();
    TEST_ASSERT_EQUAL_INT(1, uds_ota_active());  /* 세션 유지 */
}
