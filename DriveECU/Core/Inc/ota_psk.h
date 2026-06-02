#ifndef OTA_PSK_H
#define OTA_PSK_H

/* UDS SecurityAccess PreSharedKey provisioning.
 * The PSK lives in the WRP-protected Bootloader Flash region (SR-KEY-003) at a
 * fixed address, so the App (which runs SecurityAccess) can read it without the
 * PSK being part of the updatable App image.
 * Key derivation: SecurityAccess Key = HMAC-SHA256(PSK, Seed)[0:4]. */

#define OTA_PSK_ADDR  0x08007FE0UL   /* last 32 B of the 32KB Bootloader region */
#define OTA_PSK_LEN   32

#endif /* OTA_PSK_H */
