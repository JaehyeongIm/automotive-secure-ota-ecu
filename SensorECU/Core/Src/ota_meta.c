#include "ota_meta.h"
#include <stddef.h>

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
