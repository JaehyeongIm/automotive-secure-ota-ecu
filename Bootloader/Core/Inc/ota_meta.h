#ifndef OTA_META_H
#define OTA_META_H

#include <stdint.h>

#define METADATA_MAGIC   0xDEADBEEFUL
#define SLOT_CONFIRMED   0xAAAAAAAAUL
#define SLOT_PENDING     0xBBBBBBBBUL
#define SLOT_INVALID     0xCCCCCCCCUL

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

#endif /* OTA_META_H */
