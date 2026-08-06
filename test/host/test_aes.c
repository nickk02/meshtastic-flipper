/* Known-answer tests for the vendored AES128-CTR core.
 *
 * The expected ciphertext below is NIST SP 800-38A F.5.1 CTR-AES128.Encrypt,
 * independently reproduced with OpenSSL before being written here. If this
 * test ever fails, do not edit the expected bytes to match the output. A
 * known-answer test adjusted to agree with itself tests nothing. */
#include "tinytest.h"
#include "aes.h"

TEST(test_aes128_ctr_known_answer) {
    const uint8_t key[16] = {
        0x2b,
        0x7e,
        0x15,
        0x16,
        0x28,
        0xae,
        0xd2,
        0xa6,
        0xab,
        0xf7,
        0x15,
        0x88,
        0x09,
        0xcf,
        0x4f,
        0x3c};
    const uint8_t iv[16] = {
        0xf0,
        0xf1,
        0xf2,
        0xf3,
        0xf4,
        0xf5,
        0xf6,
        0xf7,
        0xf8,
        0xf9,
        0xfa,
        0xfb,
        0xfc,
        0xfd,
        0xfe,
        0xff};
    uint8_t buf[16] = {
        0x6b,
        0xc1,
        0xbe,
        0xe2,
        0x2e,
        0x40,
        0x9f,
        0x96,
        0xe9,
        0x3d,
        0x7e,
        0x11,
        0x73,
        0x93,
        0x17,
        0x2a};
    const uint8_t expected[16] = {
        0x87,
        0x4d,
        0x61,
        0x91,
        0xb6,
        0x20,
        0xe3,
        0x26,
        0x1b,
        0xef,
        0x68,
        0x64,
        0x99,
        0x0d,
        0xb6,
        0xce};
    struct AES_ctx ctx;

    AES_init_ctx_iv(&ctx, key, iv);
    AES_CTR_xcrypt_buffer(&ctx, buf, sizeof(buf));

    ASSERT_EQ_MEM(buf, expected, 16);
}

/* CTR is its own inverse, which is what lets one function serve both
   directions in mesh_crypto. */
TEST(test_aes128_ctr_roundtrip) {
    const uint8_t key[16] = {0};
    const uint8_t iv[16] = {0};
    const uint8_t original[19] = "hello meshtastic!!";
    uint8_t buf[19];
    struct AES_ctx ctx;

    memcpy(buf, original, sizeof(buf));

    AES_init_ctx_iv(&ctx, key, iv);
    AES_CTR_xcrypt_buffer(&ctx, buf, sizeof(buf));
    ASSERT_TRUE(memcmp(buf, original, sizeof(buf)) != 0);

    AES_init_ctx_iv(&ctx, key, iv);
    AES_CTR_xcrypt_buffer(&ctx, buf, sizeof(buf));
    ASSERT_EQ_MEM(buf, original, sizeof(buf));
}

/* Meshtastic frames run up to 255 bytes, so the counter must advance correctly
   across 16 blocks. A counter that fails to increment would still pass a
   single-block test. */
TEST(test_aes128_ctr_multi_block_matches_split_calls) {
    const uint8_t key[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const uint8_t iv[16] = {0};
    uint8_t whole[255];
    uint8_t piecewise[255];
    struct AES_ctx ctx;

    for(size_t i = 0; i < sizeof(whole); i++) {
        whole[i] = (uint8_t)i;
        piecewise[i] = (uint8_t)i;
    }

    AES_init_ctx_iv(&ctx, key, iv);
    AES_CTR_xcrypt_buffer(&ctx, whole, sizeof(whole));

    /* Same keystream, consumed in two calls on one context. */
    AES_init_ctx_iv(&ctx, key, iv);
    AES_CTR_xcrypt_buffer(&ctx, piecewise, 16);
    AES_CTR_xcrypt_buffer(&ctx, piecewise + 16, sizeof(piecewise) - 16);

    ASSERT_EQ_MEM(whole, piecewise, sizeof(whole));

    /* And the tail must differ from the head, which it would not if the
       counter were stuck. */
    ASSERT_TRUE(memcmp(whole, whole + 16, 16) != 0);
}

TEST_MAIN_BEGIN()
RUN_TEST(test_aes128_ctr_known_answer);
RUN_TEST(test_aes128_ctr_roundtrip);
RUN_TEST(test_aes128_ctr_multi_block_matches_split_calls);
TEST_MAIN_END()
