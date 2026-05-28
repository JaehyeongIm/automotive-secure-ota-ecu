#include "bootloader.h"

#ifndef UNIT_TEST
#include "ecdsa_pubkey.h"
#include "main.h"
#include <stdio.h>

#define uECC_SUPPORTS_secp160r1 0
#define uECC_SUPPORTS_secp192r1 0
#define uECC_SUPPORTS_secp224r1 0
#define uECC_SUPPORTS_secp256r1 1
#define uECC_SUPPORTS_secp256k1 0
#include "../Lib/uECC.h"
#include "../Lib/sha256.h"

typedef void (*pFunction)(void);

extern IWDG_HandleTypeDef hiwdg;

static int is_valid_app(uint32_t addr)
{
    uint32_t sp = *(volatile uint32_t *)addr;
    /* STM32F446RE SRAM: 0x20000000 ~ 0x2002FFFF (128KB) */
    return ((sp & 0xFF000000) == 0x20000000);
}

static void jump_to_app(uint32_t addr)
{
    uint32_t sp = *(volatile uint32_t *)addr;
    uint32_t pc = *(volatile uint32_t *)(addr + 4);

    printf("[BL] Jump to 0x%08lX  SP=0x%08lX  PC=0x%08lX\r\n", addr, sp, pc);
    HAL_Delay(10);

    __disable_irq();
    HAL_DeInit();

    SCB->VTOR = addr;
    __set_MSP(sp);

    pFunction reset_handler = (pFunction)pc;
    reset_handler();

    while (1);
}

static void safe_state(void)
{
    printf("[BL] Safe State: all slots invalid. Reflash via ST-Link.\r\n");
    while (1) {
        HAL_IWDG_Refresh(&hiwdg);
        HAL_Delay(1000);
        printf("[BL] Waiting...\r\n");
    }
}
#endif /* UNIT_TEST */

/* 메타데이터만으로 부팅 주소를 결정한다.
   HAL/ECDSA 없이 순수 로직만 포함 — 호스트 단위 테스트 가능. */
uint32_t bootloader_select_boot_addr(const OTA_Metadata_t *meta)
{
    if (meta->magic != METADATA_MAGIC)
        return SLOT_A_ADDR;

    if (meta->active_slot == 0) {
        if (meta->slot_a_status == SLOT_CONFIRMED || meta->slot_a_status == SLOT_PENDING)
            return SLOT_A_ADDR;
        if (meta->slot_b_status == SLOT_CONFIRMED)
            return SLOT_B_ADDR;
        return 0;  /* 두 슬롯 모두 유효하지 않음 → safe_state 진입 */
    } else {
        if (meta->slot_b_status == SLOT_CONFIRMED || meta->slot_b_status == SLOT_PENDING)
            return SLOT_B_ADDR;
        if (meta->slot_a_status == SLOT_CONFIRMED)
            return SLOT_A_ADDR;
        return 0;
    }
}

#ifndef UNIT_TEST
void bootloader_run(void)
{
    OTA_Metadata_t *meta = (OTA_Metadata_t *)METADATA_ADDR;

    printf("[BL] Bootloader v1.0\r\n");

    if (meta->magic == METADATA_MAGIC)
        printf("[BL] active_slot=%lu  A=0x%08lX  B=0x%08lX\r\n",
               meta->active_slot, meta->slot_a_status, meta->slot_b_status);
    else
        printf("[BL] No metadata, defaulting to Slot A\r\n");

    uint32_t boot_addr = bootloader_select_boot_addr(meta);
    if (boot_addr == 0) {
        safe_state();
        return;
    }

    if (!is_valid_app(boot_addr)) {
        printf("[BL] No valid app at 0x%08lX, trying fallback\r\n", boot_addr);
        if (boot_addr == SLOT_B_ADDR && meta->slot_a_status == SLOT_CONFIRMED) {
            boot_addr = SLOT_A_ADDR;
            printf("[BL] Fallback to Slot A\r\n");
        } else if (boot_addr == SLOT_A_ADDR && meta->slot_b_status == SLOT_CONFIRMED) {
            boot_addr = SLOT_B_ADDR;
            printf("[BL] Fallback to Slot B\r\n");
        } else {
            safe_state();
            return;
        }
        if (!is_valid_app(boot_addr)) {
            printf("[BL] Fallback slot also invalid, entering Safe State\r\n");
            safe_state();
            return;
        }
    }

    /* ECDSA 서명 검증 — size==0: ST-Link 플래싱(건너뜀), size==0xFFFFFFFF: 구 메타데이터(건너뜀) */
    if (meta->magic == METADATA_MAGIC) {
        uint32_t fw_total = (boot_addr == SLOT_A_ADDR) ? meta->slot_a_size
                                                       : meta->slot_b_size;
        uint32_t slot_max = (boot_addr == SLOT_A_ADDR) ? 0x30000UL : 0x40000UL;
        if (fw_total > 64 && fw_total <= slot_max) {
            uint32_t fw_data_size  = fw_total - 64;
            const uint8_t *fw_ptr  = (const uint8_t *)boot_addr;
            const uint8_t *sig_ptr = fw_ptr + fw_data_size;

            SHA256_CTX ctx;
            uint8_t hash[32];
            sha256_init(&ctx);
            sha256_update(&ctx, fw_ptr, fw_data_size);
            sha256_final(&ctx, hash);

            if (!uECC_verify(ecdsa_pubkey, hash, 32, sig_ptr, uECC_secp256r1())) {
                printf("[BL] ECDSA FAILED at 0x%08lX — refusing to boot\r\n", boot_addr);
                safe_state();
                return;
            }
            printf("[BL] ECDSA OK\r\n");
        }
    }

    /* IWDG 시작: 앱이 8초 내 kick 안 하면 리셋 */
    hiwdg.Instance       = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
    hiwdg.Init.Reload    = 999;
    HAL_IWDG_Init(&hiwdg);

    jump_to_app(boot_addr);
}
#endif /* UNIT_TEST */
