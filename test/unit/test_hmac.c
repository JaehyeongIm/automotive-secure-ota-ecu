#include "unity.h"
#include "hmac_sha256.h"
#include "sha256.h"   /* force Ceedling to link sha256.c (used by hmac_sha256.c) */
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* RFC 4231 Test Case 1: key = 0x0b x20, data = "Hi There" */
void test_hmac_rfc4231_case1(void)
{
    uint8_t key[20];
    memset(key, 0x0b, sizeof(key));
    const uint8_t msg[] = "Hi There";
    const uint8_t exp[32] = {
        0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53,0x5c,0xa8,0xaf,0xce,0xaf,0x0b,0xf1,0x2b,
        0x88,0x1d,0xc2,0x00,0xc9,0x83,0x3d,0xa7,0x26,0xe9,0x37,0x6c,0x2e,0x32,0xcf,0xf7
    };
    uint8_t out[32];
    hmac_sha256(key, sizeof(key), msg, 8, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp, out, 32);
}

/* RFC 4231 Test Case 2: key = "Jefe", data = "what do ya want for nothing?" */
void test_hmac_rfc4231_case2(void)
{
    const uint8_t key[] = "Jefe";
    const uint8_t msg[] = "what do ya want for nothing?";
    const uint8_t exp[32] = {
        0x5b,0xdc,0xc1,0x46,0xbf,0x60,0x75,0x4e,0x6a,0x04,0x24,0x26,0x08,0x95,0x75,0xc7,
        0x5a,0x00,0x3f,0x08,0x9d,0x27,0x39,0x83,0x9d,0xec,0x58,0xb9,0x64,0xec,0x38,0x43
    };
    uint8_t out[32];
    hmac_sha256(key, 4, msg, 28, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp, out, 32);
}

/* SecurityAccess key derivation must match the gateway (Python hmac):
 * Key = HMAC-SHA256(PSK, Seed_be4)[0:4]
 * PSK = 0x01..0x20, Seed = 0x12345678  ->  expected 94 3f d4 f2 */
void test_sec_key_matches_gateway(void)
{
    uint8_t psk[32];
    for (uint8_t i = 0; i < 32; ++i) psk[i] = (uint8_t)(i + 1);
    const uint8_t seed[4] = {0x12, 0x34, 0x56, 0x78};
    const uint8_t exp4[4] = {0x94, 0x3f, 0xd4, 0xf2};
    uint8_t out[32];
    hmac_sha256(psk, sizeof(psk), seed, sizeof(seed), out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp4, out, 4);
}
