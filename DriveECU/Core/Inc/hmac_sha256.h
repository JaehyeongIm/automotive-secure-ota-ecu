#ifndef HMAC_SHA256_H
#define HMAC_SHA256_H

#include <stddef.h>
#include <stdint.h>

/* HMAC-SHA256 (RFC 2104 / FIPS 198-1).
 * key/key_len: secret key (PreSharedKey).  msg/msg_len: message (Seed).
 * out: 32-byte MAC. SecurityAccess Key = first 4 bytes of out. */
void hmac_sha256(const uint8_t *key, size_t key_len,
                 const uint8_t *msg, size_t msg_len,
                 uint8_t out[32]);

#endif /* HMAC_SHA256_H */
