#ifndef OTA_FLASH_H
#define OTA_FLASH_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

#define SLOT_B_START_ADDR  0x08040000UL
#define SLOT_B_END_ADDR    0x08080000UL   /* 256KB */

HAL_StatusTypeDef ota_flash_erase_slot_b(void);
HAL_StatusTypeDef ota_flash_write(uint32_t addr, const uint8_t *data, uint16_t len);
HAL_StatusTypeDef ota_meta_write_pending_b(void);

#endif
