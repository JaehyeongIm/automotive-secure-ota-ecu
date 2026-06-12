#include "uds.h"
#include "isotp.h"
#include "ota_flash.h"
#include "ota_meta.h"
#include "main.h"
#include <string.h>
#include <stdio.h>

#include "hmac_sha256.h"
#include "sha256.h"
#include "ota_psk.h"

#define STATE_DEFAULT     0
#define STATE_EXTENDED    1
#define STATE_SEED_SENT   2
#define STATE_UNLOCKED    3
#define STATE_DOWNLOADING 4

#define SEC_MAX_FAIL    3       /* consecutive Key failures before lockout */
#define SEC_LOCK_MS     10000U  /* SecurityAccess lockout duration (ISO 14229) */
#define S3_TIMEOUT_MS    5000U  /* ISO 14229 S3server — 무요청 시 세션 abort (FR-CAN-019/NFR-REL-003) */
#define BUF_SIZE    512U
#define OTA_ECU_ID  OTA_ECU_ID_SENSOR  /* 이 ECU의 식별자 (FR-CAN-011) */

static uint8_t  g_state = STATE_DEFAULT;
static uint32_t g_seed;
static uint32_t g_fw_addr;
static uint32_t g_fw_size;
static uint32_t g_fw_written;
static uint8_t  g_block_seq;
static uint8_t  g_target_slot;
static uint32_t g_last_req_tick;   /* S3 타임아웃: 마지막 UDS 요청 수신 시각 */
static uint8_t  g_tx_fail_baseline;

static uint8_t          g_pending_buf[BUF_SIZE];
static uint16_t         g_pending_len;
static volatile uint8_t g_pending_ready;

/* ── SecurityAccess: HMAC-SHA256(PSK, Seed) challenge-response + RAM lockout ──
   PSK is read from the WRP Bootloader region (OTA_PSK_ADDR); host unit tests use
   a local copy. Lockout is RAM-only — NV persistence is a follow-up (SRS §13.6). */
static uint8_t  g_sec_fail;
static uint8_t  g_sec_locked;
static uint32_t g_sec_lock_until;

#ifdef UNIT_TEST
static const uint8_t s_psk[OTA_PSK_LEN] = {
    0x4F,0x54,0x41,0x2D,0x44,0x45,0x56,0x2D,0x50,0x53,0x4B,0x2D,0x44,0x4F,0x2D,0x4E,
    0x4F,0x54,0x2D,0x55,0x53,0x45,0x2D,0x49,0x4E,0x2D,0x50,0x52,0x4F,0x44,0x21,0x21
};
#define SEC_PSK_PTR (s_psk)
#else
#define SEC_PSK_PTR ((const uint8_t *)OTA_PSK_ADDR)
#endif

/* 순수 seed 유도(호스트 테스트 가능): SHA-256(UID ‖ mid ‖ ctr)[0:4].
 * mid·ctr이 재부팅 시 리셋되면 seed가 재현되어 replay가 가능함을 단위테스트로
 * 실증하고, mid를 영속 카운터로 바꾸면 닫힘을 같은 함수로 검증한다(SR-ATK-005). */
uint32_t sec_derive_seed(const uint8_t uid[12], uint32_t mid, uint32_t ctr)
{
    uint8_t buf[20];
    memcpy(buf,      uid, 12);
    memcpy(buf + 12, &mid, 4);
    memcpy(buf + 16, &ctr, 4);
    uint8_t h[32];
    SHA256_CTX c;
    sha256_init(&c);
    sha256_update(&c, buf, sizeof buf);
    sha256_final(&c, h);
    return ((uint32_t)h[0] << 24) | ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | h[3];
}

/* Boot-epoch freshness 상태(SR-ATK-005). boot_epoch = 영속 seq_counter에서 부팅당
 * 1회 lazy bump(재부팅에도 유지). session_ctr = 부팅 내 요청별 증가. seed=SHA-256(
 * UID‖boot_epoch‖session_ctr) → 재부팅 가로질러 비반복 → 구 SysTick(재부팅 리셋)의
 * reboot-replay 창을 닫음(ADR-004→영속 카운터). */
static uint8_t  s_epoch_ready;
#ifndef UNIT_TEST
static uint32_t s_boot_epoch;     /* 영속 seq_counter(부팅당 bump) — 호스트 미사용 */
static uint32_t s_session_ctr;
#endif

/* 첫 호출에 boot-epoch을 영속 seq_counter에서 bump+seal. 성공 시 1.
 * handle()는 메인루프(uds_process)에서 실행되므로 1회 flash bump가 안전(ISR 아님). */
static int sec_freshness_ensure(void)
{
#ifdef UNIT_TEST
    s_epoch_ready = 1;                 /* 호스트: 결정론적 seed라 epoch 미사용 */
    return 1;
#else
    if (s_epoch_ready) return 1;
    if (ota_meta_bump_seq(&s_boot_epoch) != HAL_OK) return 0;   /* NV 불가 → 호출측 거부 */
    s_epoch_ready = 1;
    return 1;
#endif
}

static uint32_t sec_make_seed(void)
{
#ifdef UNIT_TEST
    return HAL_GetTick() ^ 0xA5A5A5A5UL;   /* deterministic seed for host tests */
#else
    /* 영속 boot_epoch + per-session ctr → seed 재부팅 비반복(replay 차단). */
    s_session_ctr++;
    return sec_derive_seed((const uint8_t *)0x1FFF7A10UL, s_boot_epoch, s_session_ctr);
#endif
}

static int sec_is_locked(void)
{
    if (!g_sec_locked) return 0;
    if ((int32_t)(HAL_GetTick() - g_sec_lock_until) >= 0) { g_sec_locked = 0; return 0; }
    return 1;
}

static void nrc(uint8_t sid, uint8_t code)
{
    uint8_t r[3] = {0x7F, sid, code};
    printf("[UDS] NRC SID=0x%02X code=0x%02X\r\n", sid, code);
    isotp_send(r, 3);
}

void uds_init(void)
{
    g_state         = STATE_DEFAULT;
    g_pending_ready = 0;
    g_pending_len   = 0;
    g_sec_fail      = 0;
    g_sec_locked    = 0;
    g_last_req_tick = HAL_GetTick();
}

/* Called from CAN interrupt — copy only, no processing */
void uds_on_isotp_rx(const uint8_t *data, uint16_t len)
{
    if (g_pending_ready || len > BUF_SIZE) return;
    memcpy(g_pending_buf, data, len);
    g_pending_len   = len;
    g_pending_ready = 1;
}

static void handle(const uint8_t *req, uint16_t len)
{
    if (len < 1) return;
    uint8_t sid = req[0];

    switch (sid) {

    case 0x10: {                                /* DiagnosticSessionControl */
        if (len < 2) { nrc(sid, 0x13); break; }
        if (req[1] == 0x01) {
            g_state = STATE_DEFAULT;
            uint8_t r[] = {0x50, 0x01};
            printf("[UDS] Default session\r\n");
            isotp_send(r, sizeof(r));
        } else if (req[1] == 0x02) {
            g_state = STATE_EXTENDED;
            uint8_t r[] = {0x50, 0x02};
            printf("[UDS] Extended session\r\n");
            isotp_send(r, sizeof(r));
        } else {
            nrc(sid, 0x12);
        }
        break;
    }

    case 0x27: {                                /* SecurityAccess */
        if (len < 2) { nrc(sid, 0x13); break; }
        if (g_state < STATE_EXTENDED) { nrc(sid, 0x22); break; }

        if (sec_is_locked()) { nrc(sid, 0x37); break; }  /* requiredTimeDelayNotExpired */

        if (req[1] == 0x01) {                   /* requestSeed */
            if (!sec_freshness_ensure()) { nrc(sid, 0x22); break; }  /* freshness NV 불가 → conditionsNotCorrect */
            g_seed = sec_make_seed();
            uint8_t r[6] = {0x67, 0x01,
                (uint8_t)(g_seed >> 24), (uint8_t)(g_seed >> 16),
                (uint8_t)(g_seed >>  8), (uint8_t)(g_seed)};
            g_state = STATE_SEED_SENT;
            printf("[UDS] Seed=0x%08lX\r\n", g_seed);
            isotp_send(r, sizeof(r));
        } else if (req[1] == 0x02) {            /* sendKey */
            if (g_state != STATE_SEED_SENT) { nrc(sid, 0x24); break; }
            if (len < 6) { nrc(sid, 0x13); break; }

            /* Expected Key = HMAC-SHA256(PSK, Seed)[0:4] */
            uint8_t seed_be[4] = {
                (uint8_t)(g_seed >> 24), (uint8_t)(g_seed >> 16),
                (uint8_t)(g_seed >>  8), (uint8_t)(g_seed)};
            uint8_t mac[32];
            hmac_sha256(SEC_PSK_PTR, OTA_PSK_LEN, seed_be, 4, mac);

            if (memcmp(&req[2], mac, 4) == 0) {
                g_state    = STATE_UNLOCKED;
                g_sec_fail = 0;
                uint8_t r[] = {0x67, 0x02};
                printf("[UDS] Unlocked\r\n");
                isotp_send(r, sizeof(r));
            } else {
                g_state = STATE_EXTENDED;
                if (++g_sec_fail >= SEC_MAX_FAIL) {
                    g_sec_fail       = 0;
                    g_sec_locked     = 1;
                    g_sec_lock_until = HAL_GetTick() + SEC_LOCK_MS;
                    printf("[UDS] SecurityAccess locked %lu ms\r\n", (unsigned long)SEC_LOCK_MS);
                    nrc(sid, 0x36);             /* exceededNumberOfAttempts */
                } else {
                    nrc(sid, 0x35);             /* invalidKey */
                }
            }
        } else {
            nrc(sid, 0x12);
        }
        break;
    }

    case 0x34: {                                /* RequestDownload */
        if (g_state != STATE_UNLOCKED) { nrc(sid, 0x22); break; }
        /* Format: 34 00 44 [4B addr ignored] [4B size] */
        if (len < 11) { nrc(sid, 0x13); break; }
        if (req[1] != 0x00 || req[2] != 0x44) { nrc(sid, 0x31); break; }

        g_fw_size = ((uint32_t)req[7] << 24) | ((uint32_t)req[8] << 16)
                  | ((uint32_t)req[9] <<  8) |  (uint32_t)req[10];

        /* ECU selects the inactive slot as OTA target */
        uint8_t active = ota_get_active_slot();
        g_target_slot  = (active == 0) ? 1 : 0;

        uint32_t slot_max;
        if (g_target_slot == 0) {
            g_fw_addr = SLOT_A_START_ADDR;
            slot_max  = SLOT_A_END_ADDR - SLOT_A_START_ADDR;
        } else {
            g_fw_addr = SLOT_B_START_ADDR;
            slot_max  = SLOT_B_END_ADDR - SLOT_B_START_ADDR;
        }

        if (g_fw_size == 0 || g_fw_size > slot_max) { nrc(sid, 0x31); break; }
        g_tx_fail_baseline = g_isotp_tx_fail_count;
        printf("[UDS][34] start active=%u target=%c addr=0x%08lX size=%lu max=%lu\r\n",
               active, g_target_slot == 0 ? 'A' : 'B',
               (unsigned long)g_fw_addr, (unsigned long)g_fw_size, (unsigned long)slot_max);

        uint32_t erase_t0 = HAL_GetTick();
        printf("[UDS][34] erase start slot=%c\r\n", g_target_slot == 0 ? 'A' : 'B');
        HAL_StatusTypeDef er = (g_target_slot == 0)
            ? ota_flash_erase_slot_a()
            : ota_flash_erase_slot_b();
        printf("[UDS][34] erase done ret=%d dt=%lums\r\n",
               (int)er, (unsigned long)(HAL_GetTick() - erase_t0));
        if (er != HAL_OK) { nrc(sid, 0x72); break; }

        g_fw_written = 0;
        g_block_seq  = 1;
        g_state      = STATE_DOWNLOADING;
        /* maxBlockLen=258: 1 blockSeq + 256 data */
        uint8_t r[] = {0x74, 0x20, 0x01, 0x02};
        printf("[UDS][34] queue 0x74 maxBlockLen=258\r\n");
        isotp_send(r, sizeof(r));
        break;
    }

    case 0x36: {                                /* TransferData */
        if (g_state != STATE_DOWNLOADING) { nrc(sid, 0x22); break; }
        if (len < 2) { nrc(sid, 0x13); break; }
        uint8_t bsq = req[1];
        if (bsq != g_block_seq) { nrc(sid, 0x73); break; }

        const uint8_t *chunk = &req[2];
        uint16_t chunk_len   = len - 2;
        /* 한 블록 데이터가 광고한 maxBlockLen(256B)을 넘으면 거부 → padded[260] 보호.
           (F-003: per-block 미검증 시 257B↑ 단일 블록이 스택 오버플로(CWE-121). endless-data 누적가드와 별개.) */
        if (chunk_len > 256) { nrc(sid, 0x31); break; }
        uint16_t write_len   = (uint16_t)((chunk_len + 3u) & ~3u);  /* 4-byte align */

        /* FR-CAN-012: 누적 수신이 선언 image_size를 초과하면 거부 + 세션 종료
           (endless-data 방어, SR-ATK-007) */
        if ((uint32_t)g_fw_written + chunk_len > g_fw_size) {
            g_state = STATE_DEFAULT;
            nrc(sid, 0x31);
            break;
        }

        uint8_t padded[260];
        memset(padded, 0xFF, write_len);
        memcpy(padded, chunk, chunk_len);

        if (ota_flash_write(g_fw_addr + g_fw_written, padded, write_len) != HAL_OK) {
            printf("[UDS][36] flash write fail block=%u addr=0x%08lX len=%u\r\n",
                   bsq, (unsigned long)(g_fw_addr + g_fw_written), write_len);
            nrc(sid, 0x72); break;
        }

        g_fw_written += chunk_len;
        g_block_seq   = (g_block_seq == 0xFF) ? 0x00 : g_block_seq + 1;
        printf("[UDS] Block %u  %lu/%lu\r\n", bsq, g_fw_written, g_fw_size);

        uint8_t r[] = {0x76, bsq};
        isotp_send(r, sizeof(r));
        break;
    }

    case 0x37: {                                /* RequestTransferExit */
        if (g_state != STATE_DOWNLOADING) { nrc(sid, 0x22); break; }

        /* FR-CAN-013: 누적 수신 == 선언 image_size 확인 (불완전 전송 거부) */
        if (g_fw_written != g_fw_size) { nrc(sid, 0x24); break; }

        printf("[UDS][37] start written=%lu size=%lu target=%c\r\n",
               (unsigned long)g_fw_written, (unsigned long)g_fw_size,
               g_target_slot == 0 ? 'A' : 'B');
        uint32_t meta_t0 = HAL_GetTick();
        /* 서명 헤더(slot+0)에서 실제 fw_version을 읽어 메타에 기록(anti-rollback 기준선). */
        OTA_ImgHeader_t hdr;
        int have_hdr = ota_img_header_read((const uint8_t *)g_fw_addr, &hdr);
        /* ECU 식별(FR-CAN-011): 타 ECU용 이미지면 거부. 헤더 없음=레거시→스킵. */
        if (have_hdr && !ota_meta_ecu_id_allowed(hdr.target_ecu_id, OTA_ECU_ID)) {
            printf("[UDS][37] ECU-ID mismatch: hdr=%lu me=%u → reject\r\n",
                   (unsigned long)hdr.target_ecu_id, (unsigned)OTA_ECU_ID);
            nrc(sid, 0x31); break;                  /* requestOutOfRange */
        }
        uint32_t fw_version = have_hdr ? hdr.fw_version : 0;
        HAL_StatusTypeDef meta_ret = ota_meta_write_pending(g_target_slot, g_fw_size, fw_version);
        printf("[UDS][37] metadata ret=%d dt=%lums\r\n",
               (int)meta_ret, (unsigned long)(HAL_GetTick() - meta_t0));
        if (meta_ret != HAL_OK) { nrc(sid, 0x72); break; }

        uint8_t r[] = {0x77};
        printf("[UDS][37] queue 0x77\r\n");
        isotp_send(r, sizeof(r));
        printf("[UDS] OTA done, rebooting to Slot %c  TX_FAIL_DURING_OTA=%u\r\n",
               g_target_slot == 0 ? 'A' : 'B',
               (uint8_t)(g_isotp_tx_fail_count - g_tx_fail_baseline));
        HAL_Delay(100);
        NVIC_SystemReset();
        break;
    }

    default:
        nrc(sid, 0x11);
        break;
    }
}

int uds_ota_active(void)
{
    return g_state == STATE_DOWNLOADING;
}

void uds_process(void)
{
    /* S3 타임아웃(FR-CAN-019/FR-BL-012/NFR-REL-003): 비-Default 세션에서 마지막 요청 후
       5s 무요청이면 세션 abort → Default 복귀(기존 App 유지). CAN 중단/멈춘 OTA 자동 회복. */
    if (g_state != STATE_DEFAULT &&
        (uint32_t)(HAL_GetTick() - g_last_req_tick) > S3_TIMEOUT_MS) {
        printf("[UDS] S3 timeout — session abort -> default\r\n");
        g_state = STATE_DEFAULT;
    }
    if (!g_pending_ready) return;
    uint8_t  buf[BUF_SIZE];
    uint16_t len = g_pending_len;
    memcpy(buf, g_pending_buf, len);
    g_pending_ready = 0;
    g_last_req_tick = HAL_GetTick();   /* S3: 요청 수신 시각 갱신 */
    handle(buf, len);
}
