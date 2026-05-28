#include "ota_flash.h"
#include <string.h>

#define METADATA_ADDR  0x08008000UL

/* 호스트 단위 테스트: 실제 플래시 주소 대신 정적 버퍼를 사용한다.
   MCU 빌드: 실제 플래시 주소를 직접 참조한다.                       */
#ifdef UNIT_TEST
static uint8_t s_meta_buf[sizeof(OTA_Metadata_t)];
#define META_PTR ((OTA_Metadata_t *)s_meta_buf)

void ota_test_init_meta(const OTA_Metadata_t *m) { memcpy(s_meta_buf, m, sizeof(*m)); }
void ota_test_get_meta(OTA_Metadata_t *m)        { memcpy(m, s_meta_buf, sizeof(*m)); }
#else
#define META_PTR ((OTA_Metadata_t *)METADATA_ADDR)

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
#endif /* UNIT_TEST */

uint8_t ota_get_active_slot(void)
{
    OTA_Metadata_t *meta = META_PTR;
    if (meta->magic != METADATA_MAGIC) return 0;
    return (uint8_t)(meta->active_slot & 0x1);
}

HAL_StatusTypeDef ota_meta_write_pending(uint8_t slot, uint32_t fw_size)
{
    OTA_Metadata_t *cur = META_PTR;
    OTA_Metadata_t  meta = {0};

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

#ifdef UNIT_TEST
    memcpy(s_meta_buf, &meta, sizeof(meta));
    return HAL_OK;
#else
    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase = {0};
    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector       = FLASH_SECTOR_2;
    erase.NbSectors    = 1;
    uint32_t err;
    HAL_StatusTypeDef ret = HAL_FLASHEx_Erase(&erase, &err);
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
#endif
}
