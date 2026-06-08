/* FH-3 하니스 — UDS TransferData(0x36) 쓰기 경로 퍼징.
 *
 * 구조:  ① 손 스텁 + 플래시 모델   ② 상태 프라이밍   ③ 입력 주입
 * 빌드:  harness_uds.c + uds.c(대상) + ota_meta.c + sha256/hmac_sha256 + hal_stubs.c
 *        -DUNIT_TEST → uds.c가 결정적 seed + 로컬 s_psk 사용 (프라이밍이 키 계산 가능)
 *
 * 노리는 것: (v1) 단일 큰 블록 → padded[260] 스택 오버플로 가설
 *           (v2) 다중 블록     → endless-data 누적 → 플래시 모델 경계초과
 */
#include "uds.h"
#include "isotp.h"
#include "ota_flash.h"
#include "ota_meta.h"
#include "ota_psk.h"
#include "hmac_sha256.h"
#include "hal_stubs.h"          /* g_hal_tick (extern) */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ─────────────────────── ① 손 스텁 + 플래시 모델 ───────────────────────
   uds.c가 호출하는 HAL-backed 함수들(원래 ota_flash.c / isotp.c)을 호스트용으로 대체. */

/* isotp_send: ECU가 보내는 UDS 응답을 캡처 → 프라이밍이 seed를 읽고, sanity가 응답을 본다. */
static uint8_t  s_tx[64];
static uint16_t s_tx_len;
void isotp_send(const uint8_t *d, uint16_t len) {
    if (len > sizeof(s_tx)) len = sizeof(s_tx);
    memcpy(s_tx, d, len);
    s_tx_len = len;
}

/* 플래시 모델: 대상 슬롯(Slot B)을 ASAN이 지키는 힙 버퍼로 흉내낸다.
   경계초과 쓰기(endless-data)면 memcpy가 버퍼를 넘겨 ASAN이 즉시 크래시. */
#define SLOT_BASE  SLOT_B_START_ADDR
#define SLOT_SIZE  (size_t)(SLOT_B_END_ADDR - SLOT_B_START_ADDR)   /* 256KB */
static uint8_t *g_slot;
static void flash_reset(void) { free(g_slot); g_slot = (uint8_t *)malloc(SLOT_SIZE); }

uint8_t           ota_get_active_slot(void)    { return 0; }       /* A 활성 → 대상 = B */
HAL_StatusTypeDef ota_flash_erase_slot_a(void) { return HAL_OK; }
HAL_StatusTypeDef ota_flash_erase_slot_b(void) { return HAL_OK; }
HAL_StatusTypeDef ota_meta_write_pending(uint8_t s, uint32_t sz, uint32_t v) {
    (void)s; (void)sz; (void)v; return HAL_OK;
}
HAL_StatusTypeDef ota_flash_write(uint32_t addr, const uint8_t *d, uint16_t len) {
    size_t off = (size_t)(addr - SLOT_BASE);
    memcpy(g_slot + off, d, len);     /* ★ off+len > SLOT_SIZE 이면 ASAN heap-overflow ★ */
    return HAL_OK;
}

/* ─────────────────────── ② 상태 프라이밍 ───────────────────────
   uds.c의 UNIT_TEST PSK와 동일 (ASCII "OTA-DEV-PSK-DO-NOT-USE-IN-PROD!!"). */
static const uint8_t s_psk[OTA_PSK_LEN] = {
    0x4F,0x54,0x41,0x2D,0x44,0x45,0x56,0x2D,0x50,0x53,0x4B,0x2D,0x44,0x4F,0x2D,0x4E,
    0x4F,0x54,0x2D,0x55,0x53,0x45,0x2D,0x49,0x4E,0x2D,0x50,0x52,0x4F,0x44,0x21,0x21
};

/* CAN 인터럽트(uds_on_isotp_rx) + 메인루프(uds_process) 한 사이클 */
static void send(const uint8_t *req, uint16_t len) {
    uds_on_isotp_rx(req, len);
    uds_process();
}

/* DEFAULT → … → DOWNLOADING 까지 정상 시퀀스로 진입 (test_uds_state.c do_unlock+do_request_download). */
static void prime_to_downloading(void) {
    uint8_t ext[]  = {0x10, 0x02};                 send(ext,  sizeof(ext));   /* ExtendedSession */
    uint8_t sreq[] = {0x27, 0x01};                 send(sreq, sizeof(sreq));  /* requestSeed */
    /* s_tx = [0x67,0x01, seed_b3..seed_b0] (Seed는 HMAC 메시지, big-endian) */
    uint8_t seed_be[4] = { s_tx[2], s_tx[3], s_tx[4], s_tx[5] };
    uint8_t mac[32];
    hmac_sha256(s_psk, OTA_PSK_LEN, seed_be, 4, mac);
    uint8_t key[] = {0x27, 0x02, mac[0], mac[1], mac[2], mac[3]};
    send(key, sizeof(key));                                                    /* sendKey → UNLOCKED */
    uint8_t dl[]  = {0x34, 0x00, 0x44, 0,0,0,0, 0,0,0x80,0};                    /* size=0x8000 */
    send(dl, sizeof(dl));                                                       /* RequestDownload → DOWNLOADING */
}

/* 매 입력마다: 상태 리셋 → 프라이밍 → 0x36 한 블록(seq=1) 주입. 첫 응답 바이트 반환. */
static uint8_t run_block(const uint8_t *payload, uint16_t plen) {
    g_hal_tick = 0;
    uds_init();
    flash_reset();
    prime_to_downloading();

    if (plen > 510) plen = 510;            /* uds_on_isotp_rx 상한(BUF_SIZE 512) 안에서 */
    uint8_t req[512];
    req[0] = 0x36; req[1] = 0x01;          /* TransferData, blockSeq=1 (첫 블록) */
    memcpy(req + 2, payload, plen);
    send(req, (uint16_t)(2 + plen));
    return s_tx[0];
}

/* ─────────────────────── ③ 입력 주입 ─────────────────────── */
#ifdef HARNESS_SANITY
/* 1단계 검증: 퍼징 전에, 정상 블록이 0x76 응답까지 가는지 확인 (프라이밍 동작 확인). */
int main(void) {
    uint8_t normal[] = {0xAA, 0xBB};
    uint8_t resp = run_block(normal, sizeof(normal));
    if (resp == 0x76) {
        fprintf(stderr, "[sanity] prime+0x36 -> 0x76  OK (DOWNLOADING 도달·정상 쓰기)\n");
        return 0;
    }
    fprintf(stderr, "[sanity] FAIL: resp=0x%02X (expected 0x76)\n", resp);
    return 1;
}
#else
/* 2단계: 퍼징 중 uds.c 진단 로그 폭주 방지(stdout→/dev/null). ASAN/libFuzzer는 stderr라 유지. */
__attribute__((constructor)) static void silence_stdout(void) {
    if (!freopen("/dev/null", "w", stdout)) { /* 무시 */ }
}
/* libFuzzer가 만든 바이트를 0x36 payload로 주입 (v1: 단일 블록). */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    run_block(data, (uint16_t)(size > 510 ? 510 : size));
    return 0;
}
#endif
