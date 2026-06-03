#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <stdint.h>
#include "ota_meta.h"   /* OTA_Metadata_t, METADATA_MAGIC, SLOT_*, METADATA_A/B_ADDR */

#define SLOT_A_ADDR         0x08010000UL
#define SLOT_B_ADDR         0x08040000UL

/* ECDSA 검증 게이팅 결정 (FR-AB-003 deny-by-default / fail-closed, ADR-007).
 * "검증을 건너뛰는 경로(size==0 등)"가 곧 서명검증 우회라, 메타가 있으면 size를
 * 강제 검사해 비정상은 거부한다(CWE-636 failing-open 방지). */
typedef enum {
    BL_VERIFY_REQUIRED = 0,   /* 실제 OTA 슬롯(정상 size) → ECDSA 통과 필수 */
    BL_VERIFY_SKIP,           /* 메타 없음(factory/ST-Link 미서명 dev) → 검증 없이 부팅 */
    BL_VERIFY_REFUSE          /* 메타는 있으나 size 비정상(0/초과/0xFFFFFFFF) → fail-closed */
} bl_verify_decision_t;

void     bootloader_run(void);
uint32_t bootloader_select_boot_addr(const OTA_Metadata_t *meta);
bl_verify_decision_t bootloader_verify_decision(const OTA_Metadata_t *meta,
                                                uint32_t fw_total, uint32_t slot_max);

#endif /* BOOTLOADER_H */
