#include "tinytest.h"
#include "mesh_channel.h"
#include "vectors.h"

TEST(test_default_psk_matches_source) {
    /* Channels.h:153-154. Hardcoded here rather than read from the vectors so
       a bug in the generator cannot make this agree with itself. */
    const uint8_t expected[16] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                  0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};
    ASSERT_EQ_MEM(mesh_default_psk, expected, MESH_PSK_LEN);
}

TEST(test_expand_psk_index_1_is_verbatim) {
    uint8_t key[MESH_PSK_LEN];
    ASSERT_TRUE(mesh_channel_expand_psk(1, key));
    ASSERT_EQ_MEM(key, VEC0_KEY, MESH_PSK_LEN);
}

TEST(test_expand_psk_index_2_bumps_last_byte) {
    uint8_t key[MESH_PSK_LEN];
    ASSERT_TRUE(mesh_channel_expand_psk(2, key));
    ASSERT_EQ_MEM(key, VEC3_KEY, MESH_PSK_LEN);
    ASSERT_EQ_INT(key[MESH_PSK_LEN - 1], mesh_default_psk[MESH_PSK_LEN - 1] + 1);
}

TEST(test_expand_psk_leaves_leading_bytes_alone) {
    uint8_t key[MESH_PSK_LEN];
    ASSERT_TRUE(mesh_channel_expand_psk(9, key));
    ASSERT_EQ_MEM(key, mesh_default_psk, MESH_PSK_LEN - 1);
}

TEST(test_expand_psk_index_0_is_rejected) {
    uint8_t key[MESH_PSK_LEN];
    ASSERT_TRUE(!mesh_channel_expand_psk(0, key));
}

TEST(test_expand_psk_wraps_without_overflow) {
    /* Last defaultpsk byte is 0x01. Index is a uint8_t so the largest is 255,
       giving 0x01 + 254 = 0xFF with no carry into the preceding byte. */
    uint8_t key[MESH_PSK_LEN];
    ASSERT_TRUE(mesh_channel_expand_psk(255, key));
    ASSERT_EQ_INT(key[MESH_PSK_LEN - 1], 0xFF);
    ASSERT_EQ_INT(key[MESH_PSK_LEN - 2], mesh_default_psk[MESH_PSK_LEN - 2]);
}

TEST(test_xor_hash_folds_bytes) {
    const uint8_t data[4] = {0x0F, 0xF0, 0x00, 0xFF};
    ASSERT_EQ_INT(mesh_channel_xor_hash(data, 4), 0x00);
}

TEST(test_xor_hash_of_nothing_is_zero) {
    ASSERT_EQ_INT(mesh_channel_xor_hash((const uint8_t*)"", 0), 0);
}

TEST(test_channel_hash_matches_generator) {
    uint8_t key[MESH_PSK_LEN];
    ASSERT_TRUE(mesh_channel_expand_psk(1, key));
    ASSERT_EQ_INT(mesh_channel_hash("LongFast", key, MESH_PSK_LEN),
                  VEC0_CHANNEL_HASH);
}

TEST(test_channel_hash_changes_with_name) {
    uint8_t key[MESH_PSK_LEN];
    ASSERT_TRUE(mesh_channel_expand_psk(1, key));
    ASSERT_TRUE(mesh_channel_hash("LongFast", key, MESH_PSK_LEN) !=
                mesh_channel_hash("ShortFast", key, MESH_PSK_LEN));
}

TEST_MAIN_BEGIN()
RUN_TEST(test_default_psk_matches_source);
RUN_TEST(test_expand_psk_index_1_is_verbatim);
RUN_TEST(test_expand_psk_index_2_bumps_last_byte);
RUN_TEST(test_expand_psk_leaves_leading_bytes_alone);
RUN_TEST(test_expand_psk_index_0_is_rejected);
RUN_TEST(test_expand_psk_wraps_without_overflow);
RUN_TEST(test_xor_hash_folds_bytes);
RUN_TEST(test_xor_hash_of_nothing_is_zero);
RUN_TEST(test_channel_hash_matches_generator);
RUN_TEST(test_channel_hash_changes_with_name);
TEST_MAIN_END()
