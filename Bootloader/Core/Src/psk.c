/* PreSharedKey for UDS SecurityAccess key derivation:
 *     SecurityAccess Key = HMAC-SHA256(PSK, Seed)[0:4]
 *
 * DEV PLACEHOLDER — treat as a secret like the ECDSA private key. For production:
 * replace this value, provision it via ST-Link, and do NOT commit the real value.
 *
 * SR-KEY-003: the PSK resides in the WRP-protected Bootloader Flash region. The
 * linker (.psk section, STM32F446RETX_FLASH.ld) places this array at OTA_PSK_ADDR
 * (0x08007FE0) so the App can read it from a fixed address.
 */
#include <stdint.h>
#include "ota_psk.h"

/* ASCII "OTA-DEV-PSK-DO-NOT-USE-IN-PROD!!" (32 bytes) */
__attribute__((section(".psk"), used))
const uint8_t g_ota_psk[OTA_PSK_LEN] = {
    0x4F,0x54,0x41,0x2D, 0x44,0x45,0x56,0x2D,  /* "OTA-DEV-" */
    0x50,0x53,0x4B,0x2D, 0x44,0x4F,0x2D,0x4E,  /* "PSK-DO-N" */
    0x4F,0x54,0x2D,0x55, 0x53,0x45,0x2D,0x49,  /* "OT-USE-I" */
    0x4E,0x2D,0x50,0x52, 0x4F,0x44,0x21,0x21   /* "N-PROD!!" */
};
