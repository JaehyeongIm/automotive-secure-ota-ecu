#ifndef OTA_META_H
#define OTA_META_H

#include <stdint.h>

#define METADATA_MAGIC   0xDEADBEEFUL
/* Slot lifecycle (§7.3.1): INVALID → UPDATING → UPDATED → TRIAL → CONFIRMED */
#define SLOT_CONFIRMED   0xAAAAAAAAUL   /* self-test 통과, known-good */
#define SLOT_UPDATED     0xBBBBBBBBUL   /* 기록+정적검증 완료, 첫 시험부팅 대기 (구 PENDING) */
#define SLOT_INVALID     0xCCCCCCCCUL   /* 무효 / 3-strike 실패 마킹 */
#define SLOT_UPDATING    0xDDDDDDDDUL   /* 기록 진행 중(erase/program) */
#define SLOT_TRIAL       0xEEEEEEEEUL   /* 시험부팅됨(attempt 카운트), self-test 대기 */

#define METADATA_A_ADDR  0x08008000UL   /* sector 2 — metadata copy A */
#define METADATA_B_ADDR  0x0800C000UL   /* sector 3 — metadata copy B */

/* Redundant Boot Metadata (FR-AB-005): two copies in flash sectors 2 & 3.
 * - crc32 is the LAST field, written last → a torn (power-loss) copy fails CRC
 *   and is rejected, so the other copy stays the source of truth (atomic commit).
 * - seq_counter is monotonic; the valid copy with the highest seq is "current". */
typedef struct {
    uint32_t magic;
    uint32_t seq_counter;     /* monotonic; higher = newer */
    uint32_t active_slot;     /* 0 = Slot A, 1 = Slot B */
    uint32_t slot_a_status;
    uint32_t slot_b_status;
    uint32_t slot_a_version;
    uint32_t slot_b_version;
    uint32_t boot_count;
    uint32_t slot_a_size;
    uint32_t slot_b_size;
    uint32_t crc32;           /* CRC-32 over all preceding fields */
} OTA_Metadata_t;

/* CRC-32 (zlib / ISO-HDLC: poly 0xEDB88320, init/xorout 0xFFFFFFFF). */
uint32_t crc32_compute(const uint8_t *data, uint32_t len);

/* CRC over the metadata struct excluding its trailing crc32 field. */
uint32_t ota_meta_crc(const OTA_Metadata_t *m);

/* 1 if magic and crc32 are valid, else 0. */
int      ota_meta_valid(const OTA_Metadata_t *m);

/* Select the valid copy with the highest seq into *out.
 * Returns 1 if at least one copy is valid, else 0 (caller → safe state). */
int      ota_meta_select(const OTA_Metadata_t *a, const OTA_Metadata_t *b, OTA_Metadata_t *out);

/* ── 부팅 시 생명주기 결정 (FR-AB-007: trial + 3-strike 롤백) ── */
typedef struct {
    int write;       /* 1 → 점프 전에 *out을 플래시에 기록해야 함 */
    int boot_slot;   /* 0=Slot A, 1=Slot B, -1=safe state */
} OTA_BootPlan_t;

/* 현재 메타(in)로부터 부팅 슬롯과, 점프 전 커밋할 메타(*out)를 결정한다(순수).
 * UPDATED→TRIAL(count=1), TRIAL→count++ 또는 3-strike 시 INVALID+롤백. */
OTA_BootPlan_t ota_meta_plan_boot(const OTA_Metadata_t *in, OTA_Metadata_t *out, uint32_t max_attempts);

/* App self-test 통과 → my_slot 확정(TRIAL→CONFIRMED, attempt=0). 기록 필요 시 1 반환. */
int ota_meta_plan_confirm(const OTA_Metadata_t *in, uint32_t my_slot, OTA_Metadata_t *out);

#endif /* OTA_META_H */
