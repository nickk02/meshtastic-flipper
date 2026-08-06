#include "tinytest.h"
#include "mesh_channel.h"
#include "mesh_crypto.h"
#include "vectors.h"

TEST(test_nonce_layout_matches_generator) {
    uint8_t nonce[MESH_NONCE_LEN];
    mesh_crypto_build_nonce(VEC0_PACKET_ID, VEC0_FROM_NODE, nonce);
    ASSERT_EQ_MEM(nonce, VEC0_NONCE, MESH_NONCE_LEN);
}

TEST(test_nonce_reserved_regions_are_zero) {
    /* Packet id is 32 bits widened to 64, so bytes 4 to 7 stay zero. Bytes 12
       to 15 are the block counter and start at zero. */
    uint8_t nonce[MESH_NONCE_LEN];
    mesh_crypto_build_nonce(0xFFFFFFFFu, 0xFFFFFFFFu, nonce);
    for(int i = 4; i < 8; i++) ASSERT_EQ_INT(nonce[i], 0);
    for(int i = 12; i < 16; i++) ASSERT_EQ_INT(nonce[i], 0);
}

TEST(test_nonce_is_little_endian) {
    uint8_t nonce[MESH_NONCE_LEN];
    mesh_crypto_build_nonce(0x04030201u, 0x08070605u, nonce);
    ASSERT_EQ_INT(nonce[0], 0x01);
    ASSERT_EQ_INT(nonce[3], 0x04);
    ASSERT_EQ_INT(nonce[8], 0x05);
    ASSERT_EQ_INT(nonce[11], 0x08);
}

TEST(test_decrypt_recovers_plaintext) {
    uint8_t out[256];
    mesh_crypto_xcrypt(VEC0_KEY, VEC0_PACKET_ID, VEC0_FROM_NODE,
                       VEC0_CIPHERTEXT, VEC0_CIPHERTEXT_LEN, out);
    ASSERT_EQ_MEM(out, VEC0_PLAINTEXT, VEC0_PLAINTEXT_LEN);
}

TEST(test_decrypt_long_payload_spans_blocks) {
    /* 180 characters of text, well over one AES block. Catches a counter that
       fails to advance between blocks. */
    uint8_t out[256];
    mesh_crypto_xcrypt(VEC2_KEY, VEC2_PACKET_ID, VEC2_FROM_NODE,
                       VEC2_CIPHERTEXT, VEC2_CIPHERTEXT_LEN, out);
    ASSERT_EQ_MEM(out, VEC2_PLAINTEXT, VEC2_PLAINTEXT_LEN);
}

TEST(test_decrypt_with_second_psk) {
    uint8_t out[256];
    mesh_crypto_xcrypt(VEC3_KEY, VEC3_PACKET_ID, VEC3_FROM_NODE,
                       VEC3_CIPHERTEXT, VEC3_CIPHERTEXT_LEN, out);
    ASSERT_EQ_MEM(out, VEC3_PLAINTEXT, VEC3_PLAINTEXT_LEN);
}

TEST(test_wrong_key_does_not_recover) {
    uint8_t bad_key[MESH_PSK_LEN];
    uint8_t out[256];
    memcpy(bad_key, VEC0_KEY, MESH_PSK_LEN);
    bad_key[0] ^= 0xFF;

    mesh_crypto_xcrypt(bad_key, VEC0_PACKET_ID, VEC0_FROM_NODE,
                       VEC0_CIPHERTEXT, VEC0_CIPHERTEXT_LEN, out);
    ASSERT_TRUE(memcmp(out, VEC0_PLAINTEXT, VEC0_PLAINTEXT_LEN) != 0);
}

TEST(test_wrong_packet_id_does_not_recover) {
    uint8_t out[256];
    mesh_crypto_xcrypt(VEC0_KEY, VEC0_PACKET_ID + 1, VEC0_FROM_NODE,
                       VEC0_CIPHERTEXT, VEC0_CIPHERTEXT_LEN, out);
    ASSERT_TRUE(memcmp(out, VEC0_PLAINTEXT, VEC0_PLAINTEXT_LEN) != 0);
}

TEST(test_wrong_source_node_does_not_recover) {
    uint8_t out[256];
    mesh_crypto_xcrypt(VEC0_KEY, VEC0_PACKET_ID, VEC0_FROM_NODE + 1,
                       VEC0_CIPHERTEXT, VEC0_CIPHERTEXT_LEN, out);
    ASSERT_TRUE(memcmp(out, VEC0_PLAINTEXT, VEC0_PLAINTEXT_LEN) != 0);
}

TEST(test_xcrypt_is_its_own_inverse) {
    uint8_t once[256];
    uint8_t twice[256];
    mesh_crypto_xcrypt(VEC0_KEY, VEC0_PACKET_ID, VEC0_FROM_NODE,
                       VEC0_PLAINTEXT, VEC0_PLAINTEXT_LEN, once);
    /* Encrypting the plaintext must reproduce the generator's ciphertext. */
    ASSERT_EQ_MEM(once, VEC0_CIPHERTEXT, VEC0_CIPHERTEXT_LEN);

    mesh_crypto_xcrypt(VEC0_KEY, VEC0_PACKET_ID, VEC0_FROM_NODE,
                       once, VEC0_PLAINTEXT_LEN, twice);
    ASSERT_EQ_MEM(twice, VEC0_PLAINTEXT, VEC0_PLAINTEXT_LEN);
}

TEST(test_zero_length_is_safe) {
    uint8_t out[4] = {0xAA, 0xAA, 0xAA, 0xAA};
    mesh_crypto_xcrypt(VEC0_KEY, VEC0_PACKET_ID, VEC0_FROM_NODE,
                       VEC0_CIPHERTEXT, 0, out);
    ASSERT_EQ_INT(out[0], 0xAA);
}

TEST_MAIN_BEGIN()
RUN_TEST(test_nonce_layout_matches_generator);
RUN_TEST(test_nonce_reserved_regions_are_zero);
RUN_TEST(test_nonce_is_little_endian);
RUN_TEST(test_decrypt_recovers_plaintext);
RUN_TEST(test_decrypt_long_payload_spans_blocks);
RUN_TEST(test_decrypt_with_second_psk);
RUN_TEST(test_wrong_key_does_not_recover);
RUN_TEST(test_wrong_packet_id_does_not_recover);
RUN_TEST(test_wrong_source_node_does_not_recover);
RUN_TEST(test_xcrypt_is_its_own_inverse);
RUN_TEST(test_zero_length_is_safe);
TEST_MAIN_END()
