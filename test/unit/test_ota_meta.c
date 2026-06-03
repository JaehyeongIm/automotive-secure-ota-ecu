#include "unity.h"
#include "ota_flash.h"
#include "ota_meta.h"
#include <string.h>

/* 테스트 전용 하니스(ota_flash.c #ifdef UNIT_TEST에 정의) */
void ota_test_init_meta(const OTA_Metadata_t *m);
void ota_test_get_meta(OTA_Metadata_t *m);

/* ── 헬퍼 ─────────────────────────────────────────────────────────────────── */
static OTA_Metadata_t make_meta(uint32_t active,
                                uint32_t a_st, uint32_t b_st,
                                uint32_t a_ver, uint32_t b_ver,
                                uint32_t boot_cnt)
{
    OTA_Metadata_t m = {0};
    m.magic          = METADATA_MAGIC;
    m.active_slot    = active;
    m.slot_a_status  = a_st;
    m.slot_b_status  = b_st;
    m.slot_a_version = a_ver;
    m.slot_b_version = b_ver;
    m.boot_count     = boot_cnt;
    return m;
}

void setUp(void)
{
    /* 빈 메타데이터(magic=0)로 초기화 */
    OTA_Metadata_t empty = {0};
    ota_test_init_meta(&empty);
}

void tearDown(void) {}

/* ═══════════════════════════════════════════════════════════════════════════
   TC-UT-META-001~003: ota_get_active_slot()
   ═══════════════════════════════════════════════════════════════════════════ */

/* TC-UT-META-001: magic 유효, active=0 → 0 반환 */
void test_get_active_slot_returns_0_when_active_is_0(void)
{
    OTA_Metadata_t m = make_meta(0, SLOT_CONFIRMED, SLOT_CONFIRMED, 1, 1, 0);
    ota_test_init_meta(&m);
    TEST_ASSERT_EQUAL_UINT8(0, ota_get_active_slot());
}

/* TC-UT-META-002: magic 유효, active=1 → 1 반환 */
void test_get_active_slot_returns_1_when_active_is_1(void)
{
    OTA_Metadata_t m = make_meta(1, SLOT_CONFIRMED, SLOT_CONFIRMED, 1, 2, 0);
    ota_test_init_meta(&m);
    TEST_ASSERT_EQUAL_UINT8(1, ota_get_active_slot());
}

/* TC-UT-META-003: magic 불일치 → 기본값 0 반환 */
void test_get_active_slot_returns_0_when_no_magic(void)
{
    /* setUp에서 magic=0으로 초기화되므로 그대로 사용 */
    TEST_ASSERT_EQUAL_UINT8(0, ota_get_active_slot());
}

/* ═══════════════════════════════════════════════════════════════════════════
   TC-UT-META-004~005: ota_meta_write_pending() 슬롯 상태 설정
   ═══════════════════════════════════════════════════════════════════════════ */

/* TC-UT-META-004: slot=1(Slot B) OTA → active=1, B=PENDING, A=CONFIRMED */
void test_write_pending_slot1_sets_b_pending_a_confirmed(void)
{
    OTA_Metadata_t cur = make_meta(0, SLOT_CONFIRMED, SLOT_CONFIRMED, 1, 1, 0);
    ota_test_init_meta(&cur);

    TEST_ASSERT_EQUAL_INT(HAL_OK, ota_meta_write_pending(1, 1024));

    OTA_Metadata_t result;
    ota_test_get_meta(&result);
    TEST_ASSERT_EQUAL_UINT32(METADATA_MAGIC,  result.magic);
    TEST_ASSERT_EQUAL_UINT32(1,               result.active_slot);
    TEST_ASSERT_EQUAL_UINT32(SLOT_UPDATED,    result.slot_b_status);
    TEST_ASSERT_EQUAL_UINT32(SLOT_CONFIRMED,  result.slot_a_status);
    TEST_ASSERT_EQUAL_UINT32(1024,            result.slot_b_size);
}

/* TC-UT-META-005: slot=0(Slot A) OTA → active=0, A=PENDING, B=CONFIRMED */
void test_write_pending_slot0_sets_a_pending_b_confirmed(void)
{
    OTA_Metadata_t cur = make_meta(1, SLOT_CONFIRMED, SLOT_CONFIRMED, 1, 1, 0);
    ota_test_init_meta(&cur);

    TEST_ASSERT_EQUAL_INT(HAL_OK, ota_meta_write_pending(0, 2048));

    OTA_Metadata_t result;
    ota_test_get_meta(&result);
    TEST_ASSERT_EQUAL_UINT32(0,               result.active_slot);
    TEST_ASSERT_EQUAL_UINT32(SLOT_UPDATED,    result.slot_a_status);
    TEST_ASSERT_EQUAL_UINT32(SLOT_CONFIRMED,  result.slot_b_status);
    TEST_ASSERT_EQUAL_UINT32(2048,            result.slot_a_size);
}

/* ═══════════════════════════════════════════════════════════════════════════
   TC-UT-META-006~007: 버전/부트카운트 이월
   ═══════════════════════════════════════════════════════════════════════════ */

/* TC-UT-META-006: slot=1 OTA → slot_b_version = 기존 + 1, slot_a_version 유지 */
void test_write_pending_slot1_increments_b_version(void)
{
    OTA_Metadata_t cur = make_meta(0, SLOT_CONFIRMED, SLOT_CONFIRMED, 3, 5, 0);
    ota_test_init_meta(&cur);

    ota_meta_write_pending(1, 512);

    OTA_Metadata_t result;
    ota_test_get_meta(&result);
    TEST_ASSERT_EQUAL_UINT32(6, result.slot_b_version);  /* 5 + 1 */
    TEST_ASSERT_EQUAL_UINT32(3, result.slot_a_version);  /* 유지 */
}

/* TC-UT-META-007: boot_count는 현재 값 그대로 이월 */
void test_write_pending_carries_over_boot_count(void)
{
    OTA_Metadata_t cur = make_meta(0, SLOT_CONFIRMED, SLOT_CONFIRMED, 1, 1, 42);
    ota_test_init_meta(&cur);

    ota_meta_write_pending(1, 512);

    OTA_Metadata_t result;
    ota_test_get_meta(&result);
    TEST_ASSERT_EQUAL_UINT32(42, result.boot_count);
}

/* ═══════════════════════════════════════════════════════════════════════════
   TC-UT-META-008: 메타데이터 없는 상태에서 첫 OTA
   ═══════════════════════════════════════════════════════════════════════════ */

/* TC-UT-META-008: magic 없는 초기 상태 → magic 기록, boot_count=0 */
void test_write_pending_from_no_magic_sets_magic(void)
{
    /* setUp에서 magic=0으로 초기화 */
    TEST_ASSERT_EQUAL_INT(HAL_OK, ota_meta_write_pending(1, 8192));

    OTA_Metadata_t result;
    ota_test_get_meta(&result);
    TEST_ASSERT_EQUAL_UINT32(METADATA_MAGIC, result.magic);
    TEST_ASSERT_EQUAL_UINT32(0,              result.boot_count);
}

/* ═══════════════════════════════════════════════════════════════════════════
   TC-UT-META-009: write_pending 후 get_active_slot 일관성
   ═══════════════════════════════════════════════════════════════════════════ */

/* TC-UT-META-009: slot=1 OTA 후 ota_get_active_slot() = 1 */
void test_get_active_slot_reflects_write_pending(void)
{
    OTA_Metadata_t cur = make_meta(0, SLOT_CONFIRMED, SLOT_CONFIRMED, 1, 1, 0);
    ota_test_init_meta(&cur);

    ota_meta_write_pending(1, 512);

    TEST_ASSERT_EQUAL_UINT8(1, ota_get_active_slot());
}

/* TC-UT-META-010: self-test 통과 → TRIAL 슬롯 self-confirm → CONFIRMED, boot_count=0 */
void test_self_confirm_trial_to_confirmed(void)
{
    OTA_Metadata_t cur = make_meta(1, SLOT_CONFIRMED, SLOT_TRIAL, 1, 2, 5);
    ota_test_init_meta(&cur);

    TEST_ASSERT_EQUAL_INT(HAL_OK, ota_meta_self_confirm(1));

    OTA_Metadata_t result;
    ota_test_get_meta(&result);
    TEST_ASSERT_EQUAL_UINT32(SLOT_CONFIRMED, result.slot_b_status);
    TEST_ASSERT_EQUAL_UINT32(0,              result.boot_count);
}
