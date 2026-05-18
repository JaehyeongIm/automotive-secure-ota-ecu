#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <stdint.h>

#define METADATA_MAGIC      0xDEADBEEFUL
#define SLOT_CONFIRMED      0xAAAAAAAAUL
#define SLOT_PENDING        0xBBBBBBBBUL
#define SLOT_INVALID        0xCCCCCCCCUL

#define METADATA_ADDR       0x08008000UL
#define SLOT_A_ADDR         0x08010000UL
#define SLOT_B_ADDR         0x08040000UL

typedef struct {
    uint32_t magic;
    uint32_t active_slot;     /* 0 = Slot A, 1 = Slot B */
    uint32_t slot_a_status;
    uint32_t slot_b_status;
    uint32_t slot_a_version;
    uint32_t slot_b_version;
    uint32_t boot_count;
    uint32_t reserved;
} OTA_Metadata_t;

void bootloader_run(void);

#endif /* BOOTLOADER_H */
