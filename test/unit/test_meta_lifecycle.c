#include "unity.h"
#include "ota_meta.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static OTA_Metadata_t mk(uint32_t active, uint32_t a_st, uint32_t b_st, uint32_t boot_count)
{
    OTA_Metadata_t m;
    memset(&m, 0, sizeof(m));
    m.magic         = METADATA_MAGIC;
    m.seq_counter   = 10;
    m.active_slot   = active;
    m.slot_a_status = a_st;
    m.slot_b_status = b_st;
    m.boot_count    = boot_count;
    return m;
}

/* UPDATED 슬롯 첫 부팅 → TRIAL, count=1, 기록 후 부팅 */
void test_plan_updated_to_trial(void)
{
    OTA_Metadata_t in = mk(1, SLOT_CONFIRMED, SLOT_UPDATED, 0), out;
    OTA_BootPlan_t p = ota_meta_plan_boot(&in, &out, 3);
    TEST_ASSERT_EQUAL_INT(1, p.write);
    TEST_ASSERT_EQUAL_INT(1, p.boot_slot);                     /* 새 슬롯 B 부팅 */
    TEST_ASSERT_EQUAL_HEX32(SLOT_TRIAL, out.slot_b_status);
    TEST_ASSERT_EQUAL_UINT32(1, out.boot_count);
    TEST_ASSERT_EQUAL_UINT32(11, out.seq_counter);
}

/* TRIAL 재시험 (count+1 ≤ 3) → count++ */
void test_plan_trial_retry(void)
{
    OTA_Metadata_t in = mk(1, SLOT_CONFIRMED, SLOT_TRIAL, 1), out;
    OTA_BootPlan_t p = ota_meta_plan_boot(&in, &out, 3);
    TEST_ASSERT_EQUAL_INT(1, p.write);
    TEST_ASSERT_EQUAL_INT(1, p.boot_slot);
    TEST_ASSERT_EQUAL_UINT32(2, out.boot_count);
    TEST_ASSERT_EQUAL_HEX32(SLOT_TRIAL, out.slot_b_status);
}

/* TRIAL 3-strike → INVALID + 반대 CONFIRMED로 롤백 */
void test_plan_trial_3strike_rollback(void)
{
    OTA_Metadata_t in = mk(1, SLOT_CONFIRMED, SLOT_TRIAL, 3), out;   /* 다음 4 > 3 */
    OTA_BootPlan_t p = ota_meta_plan_boot(&in, &out, 3);
    TEST_ASSERT_EQUAL_INT(1, p.write);
    TEST_ASSERT_EQUAL_INT(0, p.boot_slot);                     /* Slot A로 롤백 */
    TEST_ASSERT_EQUAL_HEX32(SLOT_INVALID, out.slot_b_status);
    TEST_ASSERT_EQUAL_UINT32(0, out.active_slot);              /* active → A */
}

/* TRIAL 3-strike, 폴백 CONFIRMED 없음 → safe state */
void test_plan_trial_3strike_no_fallback_safe(void)
{
    OTA_Metadata_t in = mk(1, SLOT_INVALID, SLOT_TRIAL, 3), out;
    OTA_BootPlan_t p = ota_meta_plan_boot(&in, &out, 3);
    TEST_ASSERT_EQUAL_INT(-1, p.boot_slot);                    /* safe state */
    TEST_ASSERT_EQUAL_HEX32(SLOT_INVALID, out.slot_b_status);
}

/* CONFIRMED → 부팅, 쓰기 없음 */
void test_plan_confirmed_boots_no_write(void)
{
    OTA_Metadata_t in = mk(0, SLOT_CONFIRMED, SLOT_INVALID, 0), out;
    OTA_BootPlan_t p = ota_meta_plan_boot(&in, &out, 3);
    TEST_ASSERT_EQUAL_INT(0, p.write);
    TEST_ASSERT_EQUAL_INT(0, p.boot_slot);
}

/* 메타 없음 → Slot A 기본 부팅 */
void test_plan_no_magic_boots_slot_a(void)
{
    OTA_Metadata_t in, out;
    memset(&in, 0, sizeof(in));        /* magic = 0 */
    OTA_BootPlan_t p = ota_meta_plan_boot(&in, &out, 3);
    TEST_ASSERT_EQUAL_INT(0, p.write);
    TEST_ASSERT_EQUAL_INT(0, p.boot_slot);
}

/* self-test 통과 → TRIAL 슬롯 CONFIRMED, count=0 */
void test_confirm_trial_to_confirmed(void)
{
    OTA_Metadata_t in = mk(1, SLOT_CONFIRMED, SLOT_TRIAL, 2), out;
    int w = ota_meta_plan_confirm(&in, 1, &out);
    TEST_ASSERT_EQUAL_INT(1, w);
    TEST_ASSERT_EQUAL_HEX32(SLOT_CONFIRMED, out.slot_b_status);
    TEST_ASSERT_EQUAL_UINT32(0, out.boot_count);
    TEST_ASSERT_EQUAL_UINT32(11, out.seq_counter);
}

/* 내 슬롯이 이미 CONFIRMED면 commit no-op */
void test_confirm_noop_when_not_trial(void)
{
    OTA_Metadata_t in = mk(1, SLOT_CONFIRMED, SLOT_CONFIRMED, 0), out;
    TEST_ASSERT_EQUAL_INT(0, ota_meta_plan_confirm(&in, 1, &out));
}
