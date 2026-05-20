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
    uint32_t slot_a_size;
    uint32_t slot_b_size;
} OTA_Metadata_t;

extern IWDG_HandleTypeDef hiwdg;

HAL_StatusTypeDef ota_flash_erase_slot_a(void)
{
    FLASH_EraseInitTypeDef erase = {0};
    HAL_StatusTypeDef ret;
    uint32_t err;

    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.NbSectors    = 1;

    HAL_FLASH_Unlock();

    /* Slot A: Sector 4 (64KB) + Sector 5 (128KB) */
    erase.Sector = FLASH_SECTOR_4;
    ret = HAL_FLASHEx_Erase(&erase, &err);
    HAL_IWDG_Refresh(&hiwdg);
    if (ret != HAL_OK) { HAL_FLASH_Lock(); return ret; }

    erase.Sector = FLASH_SECTOR_5;
    ret = HAL_FLASHEx_Erase(&erase, &err);
    HAL_IWDG_Refresh(&hiwdg);

    HAL_FLASH_Lock();
    return ret;
}

HAL_StatusTypeDef ota_flash_erase_slot_b(void)
{
    FLASH_EraseInitTypeDef erase = {0};
    HAL_StatusTypeDef ret;
    uint32_t err;

    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.NbSectors    = 1;

    HAL_FLASH_Unlock();

    /* Slot B: Sector 6 (128KB) + Sector 7 (128KB) */
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
    int in_slot_a = (addr >= SLOT_A_START_ADDR && addr + len <= SLOT_A_END_ADDR);
    int in_slot_b = (addr >= SLOT_B_START_ADDR && addr + len <= SLOT_B_END_ADDR);

    if (!in_slot_a && !in_slot_b) return HAL_ERROR;

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

/* Returns the currently active slot (0 = A, 1 = B).
 * Defaults to 0 (Slot A) when no metadata exists. */
uint8_t ota_get_active_slot(void)
{
    OTA_Metadata_t *meta = (OTA_Metadata_t *)METADATA_ADDR;
    if (meta->magic != METADATA_MAGIC) return 0;
    return (uint8_t)(meta->active_slot & 0x1);
}

/*
 * slot: 0 = Slot A, 1 = Slot B
 * fw_size: total signed binary size (firmware + 64-byte signature)
 *
 * Sets the target slot to PENDING, the other slot to CONFIRMED (fallback).
 */
HAL_StatusTypeDef ota_meta_write_pending(uint8_t slot, uint32_t fw_size)
{
    FLASH_EraseInitTypeDef erase = {0};
    HAL_StatusTypeDef ret;
    uint32_t err;

    /* Read current metadata to preserve boot_count and versions */
    OTA_Metadata_t *cur = (OTA_Metadata_t *)METADATA_ADDR;
    OTA_Metadata_t meta = {0};

    meta.magic = METADATA_MAGIC;

    if (slot == 0) {
        meta.active_slot    = 0;
        meta.slot_a_status  = SLOT_PENDING;
        meta.slot_b_status  = SLOT_CONFIRMED;
        meta.slot_a_version = (cur->magic == METADATA_MAGIC) ? cur->slot_a_version + 1 : 1;
        meta.slot_b_version = (cur->magic == METADATA_MAGIC) ? cur->slot_b_version : 1;
        meta.slot_a_size    = fw_size;
        meta.slot_b_size    = (cur->magic == METADATA_MAGIC) ? cur->slot_b_size : 0;
    } else {
        meta.active_slot    = 1;
        meta.slot_a_status  = SLOT_CONFIRMED;
        meta.slot_b_status  = SLOT_PENDING;
        meta.slot_a_version = (cur->magic == METADATA_MAGIC) ? cur->slot_a_version : 1;
        meta.slot_b_version = (cur->magic == METADATA_MAGIC) ? cur->slot_b_version + 1 : 2;
        meta.slot_a_size    = (cur->magic == METADATA_MAGIC) ? cur->slot_a_size : 0;
        meta.slot_b_size    = fw_size;
    }

    meta.boot_count = (cur->magic == METADATA_MAGIC) ? cur->boot_count : 0;

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
