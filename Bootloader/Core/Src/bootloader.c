#include "bootloader.h"
#include "main.h"
#include <stdio.h>

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

void bootloader_run(void)
{
    OTA_Metadata_t *meta = (OTA_Metadata_t *)METADATA_ADDR;
    uint32_t boot_addr;

    printf("[BL] Bootloader v1.0\r\n");

    if (meta->magic != METADATA_MAGIC) {
        printf("[BL] No metadata, defaulting to Slot A\r\n");
        boot_addr = SLOT_A_ADDR;
    } else {
        printf("[BL] active_slot=%lu  A=0x%08lX  B=0x%08lX\r\n",
               meta->active_slot, meta->slot_a_status, meta->slot_b_status);

        if (meta->active_slot == 0) {
            if (meta->slot_a_status == SLOT_CONFIRMED ||
                meta->slot_a_status == SLOT_PENDING) {
                boot_addr = SLOT_A_ADDR;
                printf("[BL] Slot A selected\r\n");
            } else if (meta->slot_b_status == SLOT_CONFIRMED) {
                boot_addr = SLOT_B_ADDR;
                printf("[BL] Slot A invalid, fallback to Slot B\r\n");
            } else {
                safe_state();
                return;
            }
        } else {
            if (meta->slot_b_status == SLOT_CONFIRMED ||
                meta->slot_b_status == SLOT_PENDING) {
                boot_addr = SLOT_B_ADDR;
                printf("[BL] Slot B selected\r\n");
            } else if (meta->slot_a_status == SLOT_CONFIRMED) {
                boot_addr = SLOT_A_ADDR;
                printf("[BL] Slot B invalid, fallback to Slot A\r\n");
            } else {
                safe_state();
                return;
            }
        }
    }

    if (!is_valid_app(boot_addr)) {
        printf("[BL] No valid app at 0x%08lX, trying fallback\r\n", boot_addr);
        /* PENDING 슬롯이 손상된 경우 반대 슬롯으로 폴백 */
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

    /* IWDG 시작: 앱이 8초 내 kick 안 하면 리셋 */
    hiwdg.Instance       = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
    hiwdg.Init.Reload    = 999;
    HAL_IWDG_Init(&hiwdg);

    jump_to_app(boot_addr);
}
