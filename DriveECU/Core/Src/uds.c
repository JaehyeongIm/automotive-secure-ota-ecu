#include "uds.h"
#include "isotp.h"
#include "ota_flash.h"
#include "drive.h"
#include "main.h"
#include <string.h>
#include <stdio.h>

#define STATE_DEFAULT     0
#define STATE_EXTENDED    1
#define STATE_SEED_SENT   2
#define STATE_UNLOCKED    3
#define STATE_DOWNLOADING 4

#define SEC_MASK    0xDEADBEEFUL
#define BUF_SIZE    512U


static uint8_t  g_state = STATE_DEFAULT;
static uint32_t g_seed;
static uint32_t g_fw_addr;
static uint32_t g_fw_size;
static uint32_t g_fw_written;
static uint8_t  g_block_seq;
static uint8_t  g_target_slot;

static uint8_t          g_pending_buf[BUF_SIZE];
static uint16_t         g_pending_len;
static volatile uint8_t g_pending_ready;

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
            g_state      = STATE_DEFAULT;
            g_ota_active = 0;
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

        if (req[1] == 0x01) {                   /* Seed request */
            g_seed = HAL_GetTick() ^ 0xA5A5A5A5UL;
            uint8_t r[6] = {0x67, 0x01,
                (uint8_t)(g_seed >> 24), (uint8_t)(g_seed >> 16),
                (uint8_t)(g_seed >>  8), (uint8_t)(g_seed)};
            g_state = STATE_SEED_SENT;
            printf("[UDS] Seed=0x%08lX\r\n", g_seed);
            isotp_send(r, sizeof(r));
        } else if (req[1] == 0x02) {            /* Key send */
            if (g_state != STATE_SEED_SENT) { nrc(sid, 0x24); break; }
            if (len < 6) { nrc(sid, 0x13); break; }
            uint32_t key = ((uint32_t)req[2] << 24) | ((uint32_t)req[3] << 16)
                         | ((uint32_t)req[4] <<  8) |  (uint32_t)req[5];
            if (key == (g_seed ^ SEC_MASK)) {
                g_state = STATE_UNLOCKED;
                uint8_t r[] = {0x67, 0x02};
                printf("[UDS] Unlocked\r\n");
                isotp_send(r, sizeof(r));
            } else {
                g_state = STATE_EXTENDED;
                nrc(sid, 0x35);
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
            slot_max  = SLOT_A_END_ADDR - SLOT_A_START_ADDR;   /* 192 KB */
        } else {
            g_fw_addr = SLOT_B_START_ADDR;
            slot_max  = SLOT_B_END_ADDR - SLOT_B_START_ADDR;   /* 256 KB */
        }

        if (g_fw_size == 0 || g_fw_size > slot_max) { nrc(sid, 0x31); break; }
        printf("[UDS] Target Slot %c  addr=0x%08lX  size=%lu\r\n",
               g_target_slot == 0 ? 'A' : 'B', g_fw_addr, g_fw_size);

        g_ota_active = 1;
        HAL_StatusTypeDef er = (g_target_slot == 0)
            ? ota_flash_erase_slot_a()
            : ota_flash_erase_slot_b();
        g_ota_active = 0;
        if (er != HAL_OK) { nrc(sid, 0x72); break; }

        g_fw_written = 0;
        g_block_seq  = 1;
        g_state      = STATE_DOWNLOADING;
        /* maxBlockLen=258: 1 blockSeq + 256 data + 1 SID (SID not counted per spec) */
        uint8_t r[] = {0x74, 0x20, 0x01, 0x02};
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
        uint16_t write_len   = (uint16_t)((chunk_len + 3u) & ~3u);  /* 4-byte align */

        uint8_t padded[260];
        memset(padded, 0xFF, write_len);
        memcpy(padded, chunk, chunk_len);

        if (ota_flash_write(g_fw_addr + g_fw_written, padded, write_len) != HAL_OK) {
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

        if (ota_meta_write_pending(g_target_slot, g_fw_size) != HAL_OK) { nrc(sid, 0x72); break; }

        uint8_t r[] = {0x77};
        isotp_send(r, sizeof(r));
        printf("[UDS] FW Slot %c 완료 → 재부팅\r\n", g_target_slot == 0 ? 'A' : 'B');
        HAL_Delay(100);
        NVIC_SystemReset();
        break;
    }

    default:
        nrc(sid, 0x11);
        break;
    }
}

void uds_process(void)
{
    if (!g_pending_ready) return;
    uint8_t  buf[BUF_SIZE];
    uint16_t len = g_pending_len;
    memcpy(buf, g_pending_buf, len);
    g_pending_ready = 0;
    handle(buf, len);
}
