#include "unity.h"
#include "ota_meta.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ═══════════════════════════════════════════════════════════════════════════
   ota_img_header_read — 앞 헤더(offset 0) 파싱 (FR-AB-008)
   ═══════════════════════════════════════════════════════════════════════════ */

/* TC-UT-AR-001: 유효 magic → 1, fw_version/ecu_id 추출 */
void test_img_header_read_valid(void)
{
    uint8_t buf[IMG_HEADER_SIZE];
    memset(buf, 0xFF, sizeof(buf));
    OTA_ImgHeader_t in = { IMG_HEADER_MAGIC, 7u, 2u, 0u };
    memcpy(buf, &in, sizeof(in));

    OTA_ImgHeader_t out;
    TEST_ASSERT_EQUAL_INT(1, ota_img_header_read(buf, &out));
    TEST_ASSERT_EQUAL_UINT32(7u, out.fw_version);
    TEST_ASSERT_EQUAL_UINT32(2u, out.target_ecu_id);
}

/* TC-UT-AR-002: magic 불일치 → 0 */
void test_img_header_read_bad_magic(void)
{
    uint8_t buf[IMG_HEADER_SIZE];
    memset(buf, 0xFF, sizeof(buf));
    OTA_ImgHeader_t in = { 0x12345678u, 7u, 2u, 0u };
    memcpy(buf, &in, sizeof(in));

    OTA_ImgHeader_t out;
    TEST_ASSERT_EQUAL_INT(0, ota_img_header_read(buf, &out));
}

/* ═══════════════════════════════════════════════════════════════════════════
   ota_meta_version_allowed — anti-rollback 기준선 비교 (FR-BL-008)
   ═══════════════════════════════════════════════════════════════════════════ */

static OTA_Metadata_t mk(uint32_t a_st, uint32_t a_ver, uint32_t b_st, uint32_t b_ver)
{
    OTA_Metadata_t m = {0};
    m.magic = METADATA_MAGIC;
    m.slot_a_status = a_st; m.slot_a_version = a_ver;
    m.slot_b_status = b_st; m.slot_b_version = b_ver;
    return m;
}

/* TC-UT-AR-003: incoming == 또는 > 기준선 → 허용 */
void test_version_allow_equal_or_higher(void)
{
    OTA_Metadata_t m = mk(SLOT_CONFIRMED, 5u, SLOT_INVALID, 0u);
    TEST_ASSERT_EQUAL_INT(1, ota_meta_version_allowed(&m, 5u));   /* == */
    TEST_ASSERT_EQUAL_INT(1, ota_meta_version_allowed(&m, 6u));   /* >  */
}

/* TC-UT-AR-004: incoming < 기준선 → 다운그레이드 거부 */
void test_version_reject_downgrade(void)
{
    OTA_Metadata_t m = mk(SLOT_CONFIRMED, 5u, SLOT_INVALID, 0u);
    TEST_ASSERT_EQUAL_INT(0, ota_meta_version_allowed(&m, 4u));
}

/* TC-UT-AR-005: CONFIRMED 슬롯 없음 → 기준선 0 → 어떤 버전도 허용(공장 초기) */
void test_version_no_confirmed_allows_any(void)
{
    OTA_Metadata_t m = mk(SLOT_UPDATED, 9u, SLOT_INVALID, 0u);
    TEST_ASSERT_EQUAL_INT(1, ota_meta_version_allowed(&m, 1u));
}

/* TC-UT-AR-006: 기준선 = CONFIRMED 슬롯들의 최고 버전 */
void test_version_baseline_is_max_confirmed(void)
{
    OTA_Metadata_t m = mk(SLOT_CONFIRMED, 3u, SLOT_CONFIRMED, 8u);  /* baseline = 8 */
    TEST_ASSERT_EQUAL_INT(0, ota_meta_version_allowed(&m, 7u));     /* 7 < 8 → 거부 */
    TEST_ASSERT_EQUAL_INT(1, ota_meta_version_allowed(&m, 8u));     /* 8 == 8 → 허용 */
}

/* ═══════════════════════════════════════════════════════════════════════════
   ota_meta_ecu_id_allowed — ECU 식별: 타 ECU용 이미지 거부 (FR-CAN-011)
   ═══════════════════════════════════════════════════════════════════════════ */

/* TC-UT-AR-007: 헤더 ecu_id == 이 ECU → 허용 */
void test_ecu_id_match_allows(void)
{
    TEST_ASSERT_EQUAL_INT(1, ota_meta_ecu_id_allowed(OTA_ECU_ID_SENSOR, OTA_ECU_ID_SENSOR));
}

/* TC-UT-AR-008: 핵심 위협 — Drive 이미지(1)를 Sensor 보드(2)에 → 거부 */
void test_ecu_id_mismatch_rejects(void)
{
    TEST_ASSERT_EQUAL_INT(0, ota_meta_ecu_id_allowed(OTA_ECU_ID_DRIVE, OTA_ECU_ID_SENSOR));
    TEST_ASSERT_EQUAL_INT(0, ota_meta_ecu_id_allowed(OTA_ECU_ID_SENSOR, OTA_ECU_ID_DRIVE));
}

/* TC-UT-AR-009: 헤더 ecu_id == 0(미지정, --ecu-id 미전달 서명) → 호환 위해 허용 */
void test_ecu_id_unspecified_allows(void)
{
    TEST_ASSERT_EQUAL_INT(1, ota_meta_ecu_id_allowed(0u, OTA_ECU_ID_DRIVE));
    TEST_ASSERT_EQUAL_INT(1, ota_meta_ecu_id_allowed(0u, OTA_ECU_ID_SENSOR));
}

/* TC-UT-AR-010: 알 수 없는 ecu_id(미래 ECU 3) → 현재 ECU와 불일치 시 거부 */
void test_ecu_id_unknown_rejects(void)
{
    TEST_ASSERT_EQUAL_INT(0, ota_meta_ecu_id_allowed(3u, OTA_ECU_ID_DRIVE));
}
