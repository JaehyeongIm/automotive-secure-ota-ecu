#include "unity.h"
#include "bootloader.h"

void setUp(void)    {}
void tearDown(void) {}

/* OTA_Metadata_t 생성 헬퍼 — magic 고정, active/status만 변경 */
static OTA_Metadata_t make_meta(uint32_t active,
                                uint32_t a_status,
                                uint32_t b_status)
{
    OTA_Metadata_t m = {0};
    m.magic         = METADATA_MAGIC;
    m.active_slot   = active;
    m.slot_a_status = a_status;
    m.slot_b_status = b_status;
    return m;
}

/* ═══════════════════════════════════════════════════════════════════════════
   TC-UT-BL-001: active=0, Slot A CONFIRMED → SLOT_A_ADDR
   ═══════════════════════════════════════════════════════════════════════════
   SRS FR-BL-001: 활성 슬롯 우선 선택                                       */
void test_active0_slot_a_confirmed_selects_slot_a(void)
{
    OTA_Metadata_t m = make_meta(0, SLOT_CONFIRMED, 0xFF);
    TEST_ASSERT_EQUAL_HEX32(SLOT_A_ADDR, bootloader_select_boot_addr(&m));
}

/* TC-UT-BL-002: active=0, Slot A PENDING → SLOT_A_ADDR
   SRS FR-BL-001: PENDING 슬롯도 부팅 대상                                  */
void test_active0_slot_a_pending_selects_slot_a(void)
{
    OTA_Metadata_t m = make_meta(0, SLOT_UPDATED, 0xFF);
    TEST_ASSERT_EQUAL_HEX32(SLOT_A_ADDR, bootloader_select_boot_addr(&m));
}

/* TC-UT-BL-003: active=0, Slot A 무효, Slot B CONFIRMED → SLOT_B_ADDR (폴백)
   SRS FR-BL-002: 활성 슬롯 무효 시 반대 슬롯 폴백                          */
void test_active0_slot_a_invalid_slot_b_confirmed_selects_slot_b(void)
{
    OTA_Metadata_t m = make_meta(0, 0xFF, SLOT_CONFIRMED);
    TEST_ASSERT_EQUAL_HEX32(SLOT_B_ADDR, bootloader_select_boot_addr(&m));
}

/* TC-UT-BL-004: active=1, Slot B CONFIRMED → SLOT_B_ADDR
   SRS FR-BL-001: 활성 슬롯 우선 선택                                       */
void test_active1_slot_b_confirmed_selects_slot_b(void)
{
    OTA_Metadata_t m = make_meta(1, 0xFF, SLOT_CONFIRMED);
    TEST_ASSERT_EQUAL_HEX32(SLOT_B_ADDR, bootloader_select_boot_addr(&m));
}

/* TC-UT-BL-005: active=1, Slot B PENDING → SLOT_B_ADDR
   SRS FR-BL-001: PENDING 슬롯도 부팅 대상                                  */
void test_active1_slot_b_pending_selects_slot_b(void)
{
    OTA_Metadata_t m = make_meta(1, 0xFF, SLOT_UPDATED);
    TEST_ASSERT_EQUAL_HEX32(SLOT_B_ADDR, bootloader_select_boot_addr(&m));
}

/* TC-UT-BL-006: active=1, Slot B 무효, Slot A CONFIRMED → SLOT_A_ADDR (폴백)
   SRS FR-BL-002: 활성 슬롯 무효 시 반대 슬롯 폴백                          */
void test_active1_slot_b_invalid_slot_a_confirmed_selects_slot_a(void)
{
    OTA_Metadata_t m = make_meta(1, SLOT_CONFIRMED, 0xFF);
    TEST_ASSERT_EQUAL_HEX32(SLOT_A_ADDR, bootloader_select_boot_addr(&m));
}

/* TC-UT-BL-007: 두 슬롯 모두 무효 → 0 (safe_state 진입 신호)
   SRS FR-BL-003: 유효 슬롯 없으면 safe_state                               */
void test_active0_both_invalid_returns_zero(void)
{
    OTA_Metadata_t m = make_meta(0, 0xFF, 0xFF);
    TEST_ASSERT_EQUAL_HEX32(0, bootloader_select_boot_addr(&m));
}

/* TC-UT-BL-008: magic 불일치 → SLOT_A_ADDR (기본값)
   SRS FR-BL-004: 메타데이터 없으면 Slot A 기본 부팅                        */
void test_no_magic_defaults_to_slot_a(void)
{
    OTA_Metadata_t m = {0};  /* magic = 0, METADATA_MAGIC과 불일치 */
    TEST_ASSERT_EQUAL_HEX32(SLOT_A_ADDR, bootloader_select_boot_addr(&m));
}

/* ═══════════════════════════════════════════════════════════════════════════
   bootloader_verify_decision — ECDSA 게이팅 fail-closed (FR-AB-003, CWE-636)
   ═══════════════════════════════════════════════════════════════════════════ */
#define SLOT_MAX_A 0x30000UL

/* TC-UT-BL-009: 메타 유효 + 정상 size → 검증 필수 */
void test_verify_decision_normal_size_required(void)
{
    OTA_Metadata_t m = make_meta(0, SLOT_CONFIRMED, 0xFF);
    TEST_ASSERT_EQUAL_INT(BL_VERIFY_REQUIRED, bootloader_verify_decision(&m, 0x8000, SLOT_MAX_A));
}

/* TC-UT-BL-010: 메타 유효 + size==0 → 거부 (서명검증 우회 차단 — 핵심 공격) */
void test_verify_decision_size_zero_refuse(void)
{
    OTA_Metadata_t m = make_meta(0, SLOT_CONFIRMED, 0xFF);
    TEST_ASSERT_EQUAL_INT(BL_VERIFY_REFUSE, bootloader_verify_decision(&m, 0, SLOT_MAX_A));
}

/* TC-UT-BL-011: 메타 유효 + size==0xFFFFFFFF(erased/구 메타) → 거부 */
void test_verify_decision_size_erased_refuse(void)
{
    OTA_Metadata_t m = make_meta(0, SLOT_CONFIRMED, 0xFF);
    TEST_ASSERT_EQUAL_INT(BL_VERIFY_REFUSE, bootloader_verify_decision(&m, 0xFFFFFFFFUL, SLOT_MAX_A));
}

/* TC-UT-BL-012: 메타 유효 + size > slot_max → 거부 */
void test_verify_decision_size_overflow_refuse(void)
{
    OTA_Metadata_t m = make_meta(0, SLOT_CONFIRMED, 0xFF);
    TEST_ASSERT_EQUAL_INT(BL_VERIFY_REFUSE, bootloader_verify_decision(&m, SLOT_MAX_A + 1, SLOT_MAX_A));
}

/* TC-UT-BL-013: 경계 size==64 (서명만·코드 0바이트) → 거부 */
void test_verify_decision_size_64_refuse(void)
{
    OTA_Metadata_t m = make_meta(0, SLOT_CONFIRMED, 0xFF);
    TEST_ASSERT_EQUAL_INT(BL_VERIFY_REFUSE, bootloader_verify_decision(&m, 64, SLOT_MAX_A));
}

/* TC-UT-BL-014: 경계 size==slot_max (정확히 한계) → 검증 필수 */
void test_verify_decision_size_at_max_required(void)
{
    OTA_Metadata_t m = make_meta(0, SLOT_CONFIRMED, 0xFF);
    TEST_ASSERT_EQUAL_INT(BL_VERIFY_REQUIRED, bootloader_verify_decision(&m, SLOT_MAX_A, SLOT_MAX_A));
}

/* TC-UT-BL-015: 메타 없음(factory/ST-Link 미서명) → 검증 스킵(dev 경로) */
void test_verify_decision_no_magic_skip(void)
{
    OTA_Metadata_t m = {0};  /* magic = 0 */
    TEST_ASSERT_EQUAL_INT(BL_VERIFY_SKIP, bootloader_verify_decision(&m, 0, SLOT_MAX_A));
}
