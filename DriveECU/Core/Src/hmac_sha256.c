#include "hmac_sha256.h"
#include "sha256.h"
#include <string.h>

#define HMAC_BLOCK_LEN  64u
#define HMAC_DIGEST_LEN 32u

void hmac_sha256(const uint8_t *key, size_t key_len,
                 const uint8_t *msg, size_t msg_len,
                 uint8_t out[32])
{
    uint8_t    k_block[HMAC_BLOCK_LEN];
    uint8_t    k_ipad[HMAC_BLOCK_LEN];
    uint8_t    k_opad[HMAC_BLOCK_LEN];
    uint8_t    inner[HMAC_DIGEST_LEN];
    SHA256_CTX ctx;
    size_t     i;

    /* Keys longer than the block size are first hashed (RFC 2104). */
    if (key_len > HMAC_BLOCK_LEN) {
        sha256_init(&ctx);
        sha256_update(&ctx, key, key_len);
        sha256_final(&ctx, k_block);
        memset(k_block + HMAC_DIGEST_LEN, 0, HMAC_BLOCK_LEN - HMAC_DIGEST_LEN);
    } else {
        memcpy(k_block, key, key_len);
        memset(k_block + key_len, 0, HMAC_BLOCK_LEN - key_len);
    }

    for (i = 0; i < HMAC_BLOCK_LEN; ++i) {
        k_ipad[i] = (uint8_t)(k_block[i] ^ 0x36);
        k_opad[i] = (uint8_t)(k_block[i] ^ 0x5c);
    }

    /* inner = H(k_ipad || msg) */
    sha256_init(&ctx);
    sha256_update(&ctx, k_ipad, HMAC_BLOCK_LEN);
    sha256_update(&ctx, msg, msg_len);
    sha256_final(&ctx, inner);

    /* out = H(k_opad || inner) */
    sha256_init(&ctx);
    sha256_update(&ctx, k_opad, HMAC_BLOCK_LEN);
    sha256_update(&ctx, inner, HMAC_DIGEST_LEN);
    sha256_final(&ctx, out);
}
