/* FH-3 하니스 — UDS TransferData(0x36) 쓰기 경로 퍼징.
 *
 * v1(단일블록 → padded[260] 스택오버플로 F-003)은 수정·회귀테스트로 봉인됨.
 * v2(현재): 다중블록으로 endless-data 누적 경로를 퍼징 + 플래시 모델 실가동.
 *
 * 구조:  ① 손 스텁 + 플래시 모델   ② 상태 프라이밍   ③ 다중블록 주입
 * 빌드:  harness_uds.c + uds.c(대상) + ota_meta.c + sha256/hmac_sha256 + hal_stubs.c
 *        -DUNIT_TEST → uds.c가 결정적 seed + 로컬 s_psk 사용
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

/* isotp_send: ECU 응답 캡처 → 프라이밍의 seed 추출 + 블록 수락(0x76) 판별. */
static uint8_t  s_tx[64];
static uint16_t s_tx_len;
void isotp_send(const uint8_t *d, uint16_t len) {
    if (len > sizeof(s_tx)) len = sizeof(s_tx);
    memcpy(s_tx, d, len);
    s_tx_len = len;
}

/* 플래시 모델 — 대상 슬롯을 ASAN이 지키는 힙 버퍼로 흉내.
   경계초과 쓰기(endless-data)면 memcpy가 버퍼를 넘겨 ASAN이 즉시 크래시.
   퍼저가 슬롯 경계에 도달 가능하도록 16KB로 *축소* 모델링(실 256KB의 1/16, 메커니즘 동일). */
#define SLOT_BASE   SLOT_B_START_ADDR
#define SLOT_MODEL  16384u                 /* 축소 슬롯(16KB) */
#define DECL_SIZE   15360u                 /* 선언 image_size(15KB) < 슬롯 → 정상시 항상 안전 */
static uint8_t *g_slot;
static void flash_reset(void) { free(g_slot); g_slot = (uint8_t *)malloc(SLOT_MODEL); }

uint8_t           ota_get_active_slot(void)    { return 0; }       /* A 활성 → 대상 = B */
HAL_StatusTypeDef ota_flash_erase_slot_a(void) { return HAL_OK; }
HAL_StatusTypeDef ota_flash_erase_slot_b(void) { return HAL_OK; }
HAL_StatusTypeDef ota_meta_write_pending(uint8_t s, uint32_t sz, uint32_t v) {
    (void)s; (void)sz; (void)v; return HAL_OK;
}
HAL_StatusTypeDef ota_flash_write(uint32_t addr, const uint8_t *d, uint16_t len) {
    size_t off = (size_t)(addr - SLOT_BASE);
    memcpy(g_slot + off, d, len);     /* ★ off+len > SLOT_MODEL 이면 ASAN heap-overflow ★ */
    return HAL_OK;
}

/* ─────────────────────── ② 상태 프라이밍 ───────────────────────
   uds.c의 UNIT_TEST PSK와 동일 (ASCII "OTA-DEV-PSK-DO-NOT-USE-IN-PROD!!"). */
static const uint8_t s_psk[OTA_PSK_LEN] = {
    0x4F,0x54,0x41,0x2D,0x44,0x45,0x56,0x2D,0x50,0x53,0x4B,0x2D,0x44,0x4F,0x2D,0x4E,
    0x4F,0x54,0x2D,0x55,0x53,0x45,0x2D,0x49,0x4E,0x2D,0x50,0x52,0x4F,0x44,0x21,0x21
};

static void send(const uint8_t *req, uint16_t len) {
    uds_on_isotp_rx(req, len);
    uds_process();
}

/* DEFAULT → … → DOWNLOADING 까지 정상 시퀀스로 진입 (declared image_size = DECL_SIZE). */
static void prime_to_downloading(void) {
    uint8_t ext[]  = {0x10, 0x02};                 send(ext,  sizeof(ext));   /* ExtendedSession */
    uint8_t sreq[] = {0x27, 0x01};                 send(sreq, sizeof(sreq));  /* requestSeed */
    uint8_t seed_be[4] = { s_tx[2], s_tx[3], s_tx[4], s_tx[5] };
    uint8_t mac[32];
    hmac_sha256(s_psk, OTA_PSK_LEN, seed_be, 4, mac);
    uint8_t key[] = {0x27, 0x02, mac[0], mac[1], mac[2], mac[3]};
    send(key, sizeof(key));                                                    /* sendKey → UNLOCKED */
    /* RequestDownload: size = DECL_SIZE(15360 = 0x3C00) → DOWNLOADING */
    uint8_t dl[] = {0x34, 0x00, 0x44, 0,0,0,0,
                    (uint8_t)(DECL_SIZE>>24),(uint8_t)(DECL_SIZE>>16),
                    (uint8_t)(DECL_SIZE>>8), (uint8_t)(DECL_SIZE)};
    send(dl, sizeof(dl));
}

/* ─────────────────────── ③ 다중블록 주입 ───────────────────────
   입력을 [길이바이트]+[길이바이트]+… 로 해석:
   각 바이트 L(1~255)을 한 블록의 데이터 길이로 보고 filler L바이트를 0x36 블록으로 전송(증폭).
   수락(0x76)되면 seq 진행하며 누적, 거부(누적가드 등)되면 중단. → endless-data 누적 경로 퍼징. */
static uint8_t run_blocks(const uint8_t *data, size_t size) {
    g_hal_tick = 0;
    uds_init();
    flash_reset();
    prime_to_downloading();

    uint8_t  seq  = 1;
    uint8_t  last = 0;
    uint8_t  block[2 + 256];
    for (size_t i = 0; i < size; ) {
        uint16_t L = data[i++];
        if (L == 0) L = 1;                        /* 0-len 블록은 1로 (무한루프 방지) */
        block[0] = 0x36; block[1] = seq;
        memset(block + 2, 0xAB, L);               /* filler — 내용보다 '길이·개수'를 퍼징 */
        send(block, (uint16_t)(2 + L));
        last = s_tx[0];
        if (last == 0x76) seq = (seq == 0xFF) ? 0 : (uint8_t)(seq + 1);  /* 수락 → seq 진행 */
        else break;                               /* 거부/세션종료 → 중단 */
    }
    return last;
}

/* ─────────────────────── 입력 진입점 ─────────────────────── */
#ifdef HARNESS_SANITY
int main(void) {
    uint8_t in[] = {4};                           /* 정상 블록 1개(데이터 4B) */
    uint8_t r = run_blocks(in, sizeof(in));
    if (r == 0x76) { fprintf(stderr, "[sanity v2] 다중블록 경로 OK — prime+0x36 -> 0x76\n"); return 0; }
    fprintf(stderr, "[sanity v2] FAIL: r=0x%02X\n", r);
    return 1;
}
#else
__attribute__((constructor)) static void silence_stdout(void) {
    if (!freopen("/dev/null", "w", stdout)) { /* 무시 */ }
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    run_blocks(data, size);
    return 0;
}
#endif
