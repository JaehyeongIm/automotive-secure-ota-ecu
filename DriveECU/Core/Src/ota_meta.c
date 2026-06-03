#include "ota_meta.h"
#include <stddef.h>
#include <string.h>

/* CRC-32 (zlib / ISO-HDLC): reflected poly 0xEDB88320, init/xorout 0xFFFFFFFF.
 * Software implementation so the bootloader and both ECUs compute identically
 * and the result is host-verifiable (matches Python zlib.crc32). */
uint32_t crc32_compute(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            if (crc & 1u) crc = (crc >> 1) ^ 0xEDB88320u;
            else          crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

uint32_t ota_meta_crc(const OTA_Metadata_t *m)
{
    return crc32_compute((const uint8_t *)m, (uint32_t)offsetof(OTA_Metadata_t, crc32));
}

int ota_meta_valid(const OTA_Metadata_t *m)
{
    return (m->magic == METADATA_MAGIC) && (ota_meta_crc(m) == m->crc32);
}

int ota_meta_select(const OTA_Metadata_t *a, const OTA_Metadata_t *b, OTA_Metadata_t *out)
{
    int va = ota_meta_valid(a);
    int vb = ota_meta_valid(b);

    if (va && vb) {
        *out = (a->seq_counter >= b->seq_counter) ? *a : *b;
        return 1;
    }
    if (va) { *out = *a; return 1; }
    if (vb) { *out = *b; return 1; }
    return 0;
}

OTA_BootPlan_t ota_meta_plan_boot(const OTA_Metadata_t *in, OTA_Metadata_t *out, uint32_t max_attempts)
{
    OTA_BootPlan_t p;
    p.write     = 0;
    p.boot_slot = -1;
    *out = *in;

    if (in->magic != METADATA_MAGIC) {
        p.boot_slot = 0;            /* 메타 없음 → Slot A (factory/ST-Link) */
        return p;
    }

    uint32_t cand = in->active_slot & 1u;
    uint32_t cs   = cand ? in->slot_b_status : in->slot_a_status;
    uint32_t othr = cand ^ 1u;
    uint32_t os   = othr ? in->slot_b_status : in->slot_a_status;

    if (cs == SLOT_CONFIRMED) {
        p.boot_slot = (int)cand;    /* known-good → 그냥 부팅 */
        return p;
    }

    if (cs == SLOT_UPDATED) {       /* 첫 시험 부팅 */
        if (cand) out->slot_b_status = SLOT_TRIAL; else out->slot_a_status = SLOT_TRIAL;
        out->boot_count  = 1;
        out->seq_counter = in->seq_counter + 1u;
        p.write     = 1;
        p.boot_slot = (int)cand;
        return p;
    }

    if (cs == SLOT_TRIAL) {
        if (in->boot_count + 1u > max_attempts) {   /* 3-strike: 포기 */
            if (cand) out->slot_b_status = SLOT_INVALID; else out->slot_a_status = SLOT_INVALID;
            out->seq_counter = in->seq_counter + 1u;
            p.write = 1;
            if (os == SLOT_CONFIRMED) {
                out->active_slot = othr;
                p.boot_slot = (int)othr;            /* 롤백 */
            } else {
                p.boot_slot = -1;                   /* 폴백 없음 → safe state */
            }
            return p;
        }
        out->boot_count  = in->boot_count + 1u;     /* 재시험 */
        out->seq_counter = in->seq_counter + 1u;
        p.write     = 1;
        p.boot_slot = (int)cand;
        return p;
    }

    /* 후보가 INVALID/UPDATING/미지 → 반대 CONFIRMED 폴백 */
    if (os == SLOT_CONFIRMED) {
        p.boot_slot = (int)othr;
        return p;
    }
    p.boot_slot = -1;               /* safe state */
    return p;
}

int ota_meta_plan_confirm(const OTA_Metadata_t *in, uint32_t my_slot, OTA_Metadata_t *out)
{
    *out = *in;
    if (in->magic != METADATA_MAGIC) return 0;

    uint32_t st = (my_slot & 1u) ? in->slot_b_status : in->slot_a_status;
    if (st != SLOT_TRIAL && st != SLOT_UPDATED) return 0;   /* 확정할 것 없음 */

    if (my_slot & 1u) out->slot_b_status = SLOT_CONFIRMED; else out->slot_a_status = SLOT_CONFIRMED;
    out->boot_count  = 0;
    out->seq_counter = in->seq_counter + 1u;
    return 1;
}

/* 이미지 맨 앞(offset 0)의 서명 헤더를 읽는다(앞 헤더). magic 유효 시 1. */
int ota_img_header_read(const uint8_t *image_start, OTA_ImgHeader_t *out)
{
    OTA_ImgHeader_t h;
    memcpy(&h, image_start, sizeof(h));
    if (h.magic != IMG_HEADER_MAGIC) return 0;
    *out = h;
    return 1;
}

/* 기준선 = CONFIRMED 슬롯들의 최고 버전. incoming이 그 이상이면 허용(1), 낮으면 거부(0). */
int ota_meta_version_allowed(const OTA_Metadata_t *m, uint32_t incoming_version)
{
    uint32_t baseline = 0;
    if (m->slot_a_status == SLOT_CONFIRMED && m->slot_a_version > baseline) baseline = m->slot_a_version;
    if (m->slot_b_status == SLOT_CONFIRMED && m->slot_b_version > baseline) baseline = m->slot_b_version;
    return incoming_version >= baseline;
}
