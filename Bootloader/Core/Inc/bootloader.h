#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <stdint.h>
#include "ota_meta.h"   /* OTA_Metadata_t, METADATA_MAGIC, SLOT_*, METADATA_A/B_ADDR */

#define SLOT_A_ADDR         0x08010000UL
#define SLOT_B_ADDR         0x08040000UL

void     bootloader_run(void);
uint32_t bootloader_select_boot_addr(const OTA_Metadata_t *meta);

#endif /* BOOTLOADER_H */
