#include "unity.h"
#include "mock_isotp.h"      /* CMock이 isotp.h를 파싱해 자동 생성 */
#include "mock_ota_flash.h"  /* CMock이 ota_flash.h를 파싱해 자동 생성 */
#include "hal_stubs.h"       /* hal_stubs.c를 링크에 포함 + 전역 변수 선언 */
#include "uds.h"

void setUp(void)
{
    /* 각 테스트 전 상태 초기화 */
    g_hal_tick         = 0;
    g_nvic_reset_count = 0;
    uds_init();
}

void tearDown(void) {}

/* ── Smoke test ────────────────────────────────────────────────────────────
   설정이 올바른지 확인하는 최소 테스트.
   uds_init() 후 uds_ota_active()가 0을 반환해야 한다. */
void test_uds_init_clears_ota_active(void)
{
    TEST_ASSERT_EQUAL_INT(0, uds_ota_active());
}
