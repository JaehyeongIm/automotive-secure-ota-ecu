/* FH-3(단일 블록) 하니스 — 퍼즈 입력 전체를 0x36 TransferData "한 블록"으로 주입.
 *
 * 용도: F-003(per-block 스택 오버플로, CWE-121)을 *엔진 교차검증*하는 데 쓴다.
 *   - v2(harness_uds.c)는 다중블록(블록당 ≤255B)이라 padded[260] 경계를 못 건드림.
 *   - 본 하니스는 한 블록 payload를 최대 510B까지 주입 → chunk_len≥261이면 padded 오버플로.
 *   - libFuzzer·AFL++ 어느 엔진으로 빌드해도 동일 ABI(LLVMFuzzerTestOneInput)로 돈다.
 *
 * 빌드(공통): + uds.c + ota_meta.c + sha256.c + hmac_sha256.c + hal_stubs.c, -DUNIT_TEST
 */
#include "uds.h"
#include "isotp.h"
#include "ota_flash.h"
#include "ota_meta.h"
#include "ota_psk.h"
#include "hmac_sha256.h"
#include "hal_stubs.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ── 손 스텁 + 플래시 모델 ── */
static uint8_t  s_tx[64];
static uint16_t s_tx_len;
void isotp_send(const uint8_t *d, uint16_t len) {
    if (len > sizeof(s_tx)) len = sizeof(s_tx);
    memcpy(s_tx, d, len); s_tx_len = len;
}
#define SLOT_BASE  SLOT_B_START_ADDR
#define SLOT_SIZE  (size_t)(SLOT_B_END_ADDR - SLOT_B_START_ADDR)
static uint8_t *g_slot;
static void flash_reset(void) { free(g_slot); g_slot = (uint8_t *)malloc(SLOT_SIZE); }
uint8_t           ota_get_active_slot(void)    { return 0; }
HAL_StatusTypeDef ota_flash_erase_slot_a(void) { return HAL_OK; }
HAL_StatusTypeDef ota_flash_erase_slot_b(void) { return HAL_OK; }
HAL_StatusTypeDef ota_meta_write_pending(uint8_t s, uint32_t sz, uint32_t v) { (void)s;(void)sz;(void)v; return HAL_OK; }
HAL_StatusTypeDef ota_flash_write(uint32_t addr, const uint8_t *d, uint16_t len) {
    size_t off = (size_t)(addr - SLOT_BASE);
    memcpy(g_slot + off, d, len);
    return HAL_OK;
}

/* ── 프라이밍 (DEFAULT → DOWNLOADING) ── */
static const uint8_t s_psk[OTA_PSK_LEN] = {
    0x4F,0x54,0x41,0x2D,0x44,0x45,0x56,0x2D,0x50,0x53,0x4B,0x2D,0x44,0x4F,0x2D,0x4E,
    0x4F,0x54,0x2D,0x55,0x53,0x45,0x2D,0x49,0x4E,0x2D,0x50,0x52,0x4F,0x44,0x21,0x21
};
static void send(const uint8_t *r, uint16_t l) { uds_on_isotp_rx(r, l); uds_process(); }
static void prime(void) {
    uint8_t ext[]  = {0x10, 0x02};                 send(ext, 2);
    uint8_t sreq[] = {0x27, 0x01};                 send(sreq, 2);
    uint8_t seed_be[4] = { s_tx[2], s_tx[3], s_tx[4], s_tx[5] };
    uint8_t mac[32]; hmac_sha256(s_psk, OTA_PSK_LEN, seed_be, 4, mac);
    uint8_t key[] = {0x27, 0x02, mac[0], mac[1], mac[2], mac[3]}; send(key, 6);
    uint8_t dl[]  = {0x34, 0x00, 0x44, 0,0,0,0, 0,0,0x80,0};      send(dl, 11);  /* size=0x8000 */
}

#ifndef HARNESS_SANITY
__attribute__((constructor)) static void silence_stdout(void) {
    if (!freopen("/dev/null", "w", stdout)) { /* 무시 */ }
}
#endif

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    g_hal_tick = 0; uds_init(); flash_reset(); prime();
    uint16_t plen = (size > 510) ? 510 : (uint16_t)size;     /* uds_on_isotp_rx 상한 */
    uint8_t req[512];
    req[0] = 0x36; req[1] = 0x01;                            /* 첫 블록 seq=1 */
    memcpy(req + 2, data, plen);
    send(req, (uint16_t)(2 + plen));
    return 0;
}
