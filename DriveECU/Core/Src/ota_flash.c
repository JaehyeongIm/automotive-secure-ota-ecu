#include "ota_flash.h"
#include <string.h>

/* Must stay in sync with Bootloader/Core/Inc/bootloader.h */
#define METADATA_MAGIC     0xDEADBEEFUL
#define METADATA_ADDR      0x08008000UL
#define SLOT_CONFIRMED     0xAAAAAAAAUL
#define SLOT_PENDING       0xBBBBBBBBUL

typedef struct {
    uint32_t magic;
    uint32_t active_slot;
    uint32_t slot_a_status;
    uint32_t slot_b_status;
    uint32_t slot_a_version;
    uint32_t slot_b_version;
    uint32_t boot_count;
    uint32_t reserved;
} OTA_Metadata_t;

extern IWDG_HandleTypeDef hiwdg;

HAL_StatusTypeDef ota_flash_erase_slot_b(void)
{
    FLASH_EraseInitTypeDef erase = {0};
    HAL_StatusTypeDef ret;
    uint32_t err;

    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.NbSectors    = 1;

    HAL_FLASH_Unlock();

    erase.Sector = FLASH_SECTOR_6;
    ret = HAL_FLASHEx_Erase(&erase, &err);
    HAL_IWDG_Refresh(&hiwdg);
    if (ret != HAL_OK) { HAL_FLASH_Lock(); return ret; }

    erase.Sector = FLASH_SECTOR_7;
    ret = HAL_FLASHEx_Erase(&erase, &err);
    HAL_IWDG_Refresh(&hiwdg);

    HAL_FLASH_Lock();
    return ret;
}

HAL_StatusTypeDef ota_flash_write(uint32_t addr, const uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef ret = HAL_OK;

    if (addr < SLOT_B_START_ADDR || addr + len > SLOT_B_END_ADDR) return HAL_ERROR;

    HAL_FLASH_Unlock();
    for (uint16_t i = 0; i < len; i += 4) {
        uint32_t word;
        memcpy(&word, &data[i], 4);
        ret = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i, word);
        if (ret != HAL_OK) break;
    }
    HAL_FLASH_Lock();
    return ret;
}

HAL_StatusTypeDef ota_meta_write_pending_b(void)
{
    FLASH_EraseInitTypeDef erase = {0};
    HAL_StatusTypeDef ret;
    uint32_t err;
    OTA_Metadata_t meta = {0};

    meta.magic          = METADATA_MAGIC;
    meta.active_slot    = 1;              /* Slot B */
    meta.slot_a_status  = SLOT_CONFIRMED; /* A as fallback */
    meta.slot_b_status  = SLOT_PENDING;
    meta.slot_a_version = 1;
    meta.slot_b_version = 2;

    HAL_FLASH_Unlock();

    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector       = FLASH_SECTOR_2;
    erase.NbSectors    = 1;
    ret = HAL_FLASHEx_Erase(&erase, &err);
    if (ret != HAL_OK) { HAL_FLASH_Lock(); return ret; }

    const uint8_t *src = (const uint8_t *)&meta;
    for (uint32_t i = 0; i < sizeof(meta); i += 4) {
        uint32_t word;
        memcpy(&word, &src[i], 4);
        ret = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, METADATA_ADDR + i, word);
        if (ret != HAL_OK) break;
    }

    HAL_FLASH_Lock();
    return ret;
}
