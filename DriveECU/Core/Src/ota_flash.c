#include "ota_flash.h"
#include "ota_meta.h"
#include <string.h>

/* Redundant Boot Metadata (FR-AB-005): two copies in sectors 2 & 3.
 * Reads pick the CRC-valid copy with the highest seq (ota_meta_select).
 * Writes go to the *other* (non-current) copy with seq+1 and CRC as the last
 * word, so the current copy stays intact until the new copy is fully sealed
 * (power-loss-safe / atomic commit). */

#ifdef UNIT_TEST
/* Host test: two RAM buffers model the two flash sectors. */
static uint8_t s_copy_a[sizeof(OTA_Metadata_t)];
static uint8_t s_copy_b[sizeof(OTA_Metadata_t)];
#define COPY_A ((const OTA_Metadata_t *)s_copy_a)
#define COPY_B ((const OTA_Metadata_t *)s_copy_b)

static HAL_StatusTypeDef meta_write_copy(int to_b, const OTA_Metadata_t *m)
{
    memcpy(to_b ? s_copy_b : s_copy_a, m, sizeof(*m));
    return HAL_OK;
}

void ota_test_init_meta(const OTA_Metadata_t *m)
{
    OTA_Metadata_t s = *m;
    if (s.magic == METADATA_MAGIC) {
        if (s.seq_counter == 0) s.seq_counter = 1;
        s.crc32 = ota_meta_crc(&s);
    }
    memcpy(s_copy_a, &s, sizeof(s));
    memset(s_copy_b, 0xFF, sizeof(s_copy_b));   /* copy B erased */
}

void ota_test_get_meta(OTA_Metadata_t *m)
{
    if (!ota_meta_select(COPY_A, COPY_B, m)) memset(m, 0, sizeof(*m));
}
#else
#define COPY_A ((const OTA_Metadata_t *)METADATA_A_ADDR)
#define COPY_B ((const OTA_Metadata_t *)METADATA_B_ADDR)

extern IWDG_HandleTypeDef hiwdg;

/* Erase + program a sealed metadata struct to one physical copy.
 * crc32 is the struct's last word, so it is programmed last → a power-loss
 * mid-write leaves crc invalid and that copy is rejected on the next boot. */
static HAL_StatusTypeDef meta_write_copy(int to_b, const OTA_Metadata_t *m)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t err;
    uint32_t addr = to_b ? METADATA_B_ADDR : METADATA_A_ADDR;
    HAL_StatusTypeDef ret;

    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector       = to_b ? FLASH_SECTOR_3 : FLASH_SECTOR_2;
    erase.NbSectors    = 1;

    HAL_FLASH_Unlock();
    ret = HAL_FLASHEx_Erase(&erase, &err);
    if (ret != HAL_OK) { HAL_FLASH_Lock(); return ret; }

    const uint8_t *src = (const uint8_t *)m;
    for (uint32_t i = 0; i < sizeof(*m); i += 4) {
        uint32_t word;
        memcpy(&word, &src[i], 4);
        ret = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i, word);
        if (ret != HAL_OK) break;
    }
    HAL_FLASH_Lock();
    return ret;
}

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
    OTA_Metadata_t m;
    if (!ota_meta_select(COPY_A, COPY_B, &m)) return 0;
    return (uint8_t)(m.active_slot & 0x1);
}

/*
 * slot: 0 = Slot A, 1 = Slot B
 * fw_size: total signed binary size (firmware + 64-byte signature)
 *
 * Stages the target slot as PENDING, the other as CONFIRMED (fallback), and
 * commits it atomically to the inactive metadata copy (ping-pong).
 */
HAL_StatusTypeDef ota_meta_write_pending(uint8_t slot, uint32_t fw_size)
{
    OTA_Metadata_t cur;
    int have = ota_meta_select(COPY_A, COPY_B, &cur);

    /* Pick the copy that is NOT current → current stays intact during the write. */
    int va = ota_meta_valid(COPY_A);
    int vb = ota_meta_valid(COPY_B);
    int to_b;
    if (va && vb) to_b = (COPY_A->seq_counter >= COPY_B->seq_counter) ? 1 : 0;
    else if (va)  to_b = 1;
    else          to_b = 0;

    OTA_Metadata_t meta;
    memset(&meta, 0, sizeof(meta));
    meta.magic       = METADATA_MAGIC;
    meta.seq_counter = have ? cur.seq_counter + 1 : 1;

    if (slot == 0) {
        meta.active_slot    = 0;
        meta.slot_a_status  = SLOT_UPDATED;
        meta.slot_b_status  = SLOT_CONFIRMED;
        meta.slot_a_version = have ? cur.slot_a_version + 1 : 1;
        meta.slot_b_version = have ? cur.slot_b_version : 1;
        meta.slot_a_size    = fw_size;
        meta.slot_b_size    = have ? cur.slot_b_size : 0;
    } else {
        meta.active_slot    = 1;
        meta.slot_a_status  = SLOT_CONFIRMED;
        meta.slot_b_status  = SLOT_UPDATED;
        meta.slot_a_version = have ? cur.slot_a_version : 1;
        meta.slot_b_version = have ? cur.slot_b_version + 1 : 2;
        meta.slot_a_size    = have ? cur.slot_a_size : 0;
        meta.slot_b_size    = fw_size;
    }

    meta.boot_count = have ? cur.boot_count : 0;
    meta.crc32      = ota_meta_crc(&meta);

    return meta_write_copy(to_b, &meta);
}

/* App self-test 통과 후 호출: 내 슬롯이 TRIAL/UPDATED이면 CONFIRMED로 commit
 * (attempt=0). TRIAL이 아니면 no-op. 비활성 사본에 ping-pong 기록(FR-AB-004). */
HAL_StatusTypeDef ota_meta_self_confirm(uint8_t my_slot)
{
    OTA_Metadata_t cur, out;
    if (!ota_meta_select(COPY_A, COPY_B, &cur)) return HAL_ERROR;
    if (!ota_meta_plan_confirm(&cur, my_slot, &out)) return HAL_OK;   /* 확정할 것 없음 */

    int va = ota_meta_valid(COPY_A);
    int vb = ota_meta_valid(COPY_B);
    int to_b;
    if (va && vb) to_b = (COPY_A->seq_counter >= COPY_B->seq_counter) ? 1 : 0;
    else if (va)  to_b = 1;
    else          to_b = 0;

    out.crc32 = ota_meta_crc(&out);
    return meta_write_copy(to_b, &out);
}
