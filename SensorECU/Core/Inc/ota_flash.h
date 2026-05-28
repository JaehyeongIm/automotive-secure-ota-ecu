#ifndef OTA_FLASH_H
#define OTA_FLASH_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

#define METADATA_MAGIC     0xDEADBEEFUL
#define SLOT_CONFIRMED     0xAAAAAAAAUL
#define SLOT_PENDING       0xBBBBBBBBUL

#define SLOT_A_START_ADDR  0x08010000UL
#define SLOT_A_END_ADDR    0x08040000UL   /* 192KB */
#define SLOT_B_START_ADDR  0x08040000UL
#define SLOT_B_END_ADDR    0x08080000UL   /* 256KB */

/* Must stay in sync with Bootloader/Core/Inc/bootloader.h */
typedef struct {
    uint32_t magic;
    uint32_t active_slot;
    uint32_t slot_a_status;
    uint32_t slot_b_status;
    uint32_t slot_a_version;
    uint32_t slot_b_version;
    uint32_t boot_count;
    uint32_t slot_a_size;
    uint32_t slot_b_size;
} OTA_Metadata_t;

HAL_StatusTypeDef ota_flash_erase_slot_a(void);
HAL_StatusTypeDef ota_flash_erase_slot_b(void);
HAL_StatusTypeDef ota_flash_write(uint32_t addr, const uint8_t *data, uint16_t len);
HAL_StatusTypeDef ota_meta_write_pending(uint8_t slot, uint32_t fw_size);
uint8_t           ota_get_active_slot(void);

#ifdef UNIT_TEST
void ota_test_init_meta(const OTA_Metadata_t *m);
void ota_test_get_meta(OTA_Metadata_t *m);
#endif

#endif
