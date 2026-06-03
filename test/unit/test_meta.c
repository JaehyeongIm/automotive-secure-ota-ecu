#include "unity.h"
#include "ota_meta.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* 표준 CRC-32 check 값: crc32("123456789") == 0xCBF43926 (Python zlib.crc32 일치) */
void test_crc32_check_value(void)
{
    const uint8_t msg[] = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32_compute(msg, 9));
}

/* CRC를 마지막에 봉인한 유효 사본을 만든다. */
static OTA_Metadata_t make_meta(uint32_t seq, uint32_t active)
{
    OTA_Metadata_t m;
    memset(&m, 0, sizeof(m));
    m.magic         = METADATA_MAGIC;
    m.seq_counter   = seq;
    m.active_slot   = active;
    m.slot_a_status = SLOT_CONFIRMED;
    m.slot_b_status = SLOT_INVALID;
    m.crc32         = ota_meta_crc(&m);
    return m;
}

void test_valid_copy_passes(void)
{
    OTA_Metadata_t m = make_meta(5, 0);
    TEST_ASSERT_TRUE(ota_meta_valid(&m));
}

/* 본문을 건드리고 CRC를 안 고치면 무효 (torn-write/비트오류 검출) */
void test_corrupt_body_fails(void)
{
    OTA_Metadata_t m = make_meta(5, 0);
    m.boot_count ^= 0xFFu;
    TEST_ASSERT_FALSE(ota_meta_valid(&m));
}

/* erase 직후(0xFF) 사본은 무효 (CRC 아직 안 써짐) */
void test_erased_copy_fails(void)
{
    OTA_Metadata_t m;
    memset(&m, 0xFF, sizeof(m));
    TEST_ASSERT_FALSE(ota_meta_valid(&m));
}

/* 둘 다 유효 → seq 높은 쪽 선택 */
void test_select_picks_higher_seq(void)
{
    OTA_Metadata_t a = make_meta(7, 0);
    OTA_Metadata_t b = make_meta(8, 1);
    OTA_Metadata_t out;
    TEST_ASSERT_EQUAL_INT(1, ota_meta_select(&a, &b, &out));
    TEST_ASSERT_EQUAL_UINT32(8, out.seq_counter);
    TEST_ASSERT_EQUAL_UINT32(1, out.active_slot);
}

/* 더 새 사본이 손상되면 → 유효한 옛 사본으로 폴백 (원자성의 핵심) */
void test_select_skips_corrupt_newer(void)
{
    OTA_Metadata_t a = make_meta(7, 0);
    OTA_Metadata_t b = make_meta(9, 1);
    b.crc32 ^= 0x1u;                       /* 새 사본이 torn */
    OTA_Metadata_t out;
    TEST_ASSERT_EQUAL_INT(1, ota_meta_select(&a, &b, &out));
    TEST_ASSERT_EQUAL_UINT32(7, out.seq_counter);
}

/* 둘 다 무효 → 0 (caller는 safe state) */
void test_select_none_valid_returns_zero(void)
{
    OTA_Metadata_t a, b;
    memset(&a, 0xFF, sizeof(a));
    memset(&b, 0xFF, sizeof(b));
    OTA_Metadata_t out;
    TEST_ASSERT_EQUAL_INT(0, ota_meta_select(&a, &b, &out));
}
