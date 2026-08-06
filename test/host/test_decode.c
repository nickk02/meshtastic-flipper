/* M0 acceptance: a synthesized encrypted frame must produce the exact
   original text, byte for byte, through the whole chain. */
#include "tinytest.h"
#include "mesh_channel.h"
#include "mesh_decode.h"
#include "vectors.h"

TEST(test_acceptance_frame_to_text) {
    uint8_t key[MESH_PSK_LEN];
    MeshDecoded d;

    ASSERT_TRUE(mesh_channel_expand_psk(1, key));
    ASSERT_EQ_INT(
        mesh_decode_frame(VEC0_FRAME, VEC0_FRAME_LEN, key, VEC0_CHANNEL_HASH, &d),
        MESH_OK);
    ASSERT_EQ_INT(d.data.portnum, MESH_PORTNUM_TEXT_MESSAGE_APP);
    ASSERT_EQ_INT(d.data.payload_len, VEC0_TEXT_LEN);
    ASSERT_EQ_MEM(d.data.payload, VEC0_TEXT, VEC0_TEXT_LEN);
    ASSERT_EQ_INT(d.header.from, VEC0_FROM_NODE);
    ASSERT_EQ_INT(d.header.to, VEC0_TO_NODE);
    ASSERT_EQ_INT(mesh_header_hop_limit(&d.header), VEC0_HOP_LIMIT);
}

TEST(test_acceptance_empty_message) {
    uint8_t key[MESH_PSK_LEN];
    MeshDecoded d;
    ASSERT_TRUE(mesh_channel_expand_psk(1, key));
    ASSERT_EQ_INT(
        mesh_decode_frame(VEC1_FRAME, VEC1_FRAME_LEN, key, VEC1_CHANNEL_HASH, &d),
        MESH_OK);
    ASSERT_EQ_INT(d.data.payload_len, 0);
}

TEST(test_acceptance_long_frame) {
    uint8_t key[MESH_PSK_LEN];
    MeshDecoded d;
    ASSERT_TRUE(mesh_channel_expand_psk(1, key));
    ASSERT_EQ_INT(
        mesh_decode_frame(VEC2_FRAME, VEC2_FRAME_LEN, key, VEC2_CHANNEL_HASH, &d),
        MESH_OK);
    ASSERT_EQ_INT(d.data.payload_len, VEC2_TEXT_LEN);
    ASSERT_EQ_MEM(d.data.payload, VEC2_TEXT, VEC2_TEXT_LEN);
}

TEST(test_acceptance_second_psk_frame) {
    uint8_t key[MESH_PSK_LEN];
    MeshDecoded d;
    ASSERT_TRUE(mesh_channel_expand_psk(2, key));
    ASSERT_EQ_INT(
        mesh_decode_frame(VEC3_FRAME, VEC3_FRAME_LEN, key, VEC3_CHANNEL_HASH, &d),
        MESH_OK);
    ASSERT_EQ_MEM(d.data.payload, VEC3_TEXT, VEC3_TEXT_LEN);
}

TEST(test_acceptance_utf8_frame) {
    uint8_t key[MESH_PSK_LEN];
    MeshDecoded d;
    ASSERT_TRUE(mesh_channel_expand_psk(1, key));
    ASSERT_EQ_INT(
        mesh_decode_frame(VEC4_FRAME, VEC4_FRAME_LEN, key, VEC4_CHANNEL_HASH, &d),
        MESH_OK);
    ASSERT_EQ_INT(d.data.payload_len, VEC4_TEXT_LEN);
    ASSERT_EQ_MEM(d.data.payload, VEC4_TEXT, VEC4_TEXT_LEN);
}

TEST(test_rejects_short_frame) {
    uint8_t key[MESH_PSK_LEN];
    MeshDecoded d;
    uint8_t frame[8] = {0};
    ASSERT_TRUE(mesh_channel_expand_psk(1, key));
    ASSERT_EQ_INT(mesh_decode_frame(frame, sizeof(frame), key, 0, &d),
                  MESH_ERR_TOO_SHORT);
}

TEST(test_rejects_channel_hash_mismatch) {
    uint8_t key[MESH_PSK_LEN];
    MeshDecoded d;
    ASSERT_TRUE(mesh_channel_expand_psk(1, key));
    ASSERT_EQ_INT(
        mesh_decode_frame(VEC0_FRAME, VEC0_FRAME_LEN, key,
                          (uint8_t)(VEC0_CHANNEL_HASH ^ 0xFF), &d),
        MESH_ERR_CHANNEL_MISMATCH);
}

TEST(test_header_survives_channel_mismatch) {
    /* Even an undecryptable frame should tell us who sent it. */
    uint8_t key[MESH_PSK_LEN];
    MeshDecoded d;
    ASSERT_TRUE(mesh_channel_expand_psk(1, key));
    mesh_decode_frame(VEC0_FRAME, VEC0_FRAME_LEN, key,
                      (uint8_t)(VEC0_CHANNEL_HASH ^ 0xFF), &d);
    ASSERT_EQ_INT(d.header.from, VEC0_FROM_NODE);
}

TEST(test_wrong_key_never_yields_original_text) {
    uint8_t key[MESH_PSK_LEN];
    MeshDecoded d;
    MeshDecodeResult r;

    ASSERT_TRUE(mesh_channel_expand_psk(1, key));
    key[0] ^= 0xFF;

    r = mesh_decode_frame(VEC0_FRAME, VEC0_FRAME_LEN, key, VEC0_CHANNEL_HASH, &d);
    if(r == MESH_OK) {
        /* Garbage may parse as protobuf by luck, but must never reproduce the
           original message. */
        ASSERT_TRUE(d.data.payload_len != VEC0_TEXT_LEN ||
                    memcmp(d.data.payload, VEC0_TEXT, VEC0_TEXT_LEN) != 0);
    } else {
        ASSERT_TRUE(r == MESH_ERR_BAD_PROTOBUF || r == MESH_ERR_NOT_TEXT);
    }
}

TEST(test_payload_points_into_decoded_struct) {
    uint8_t key[MESH_PSK_LEN];
    MeshDecoded d;
    ASSERT_TRUE(mesh_channel_expand_psk(1, key));
    ASSERT_EQ_INT(
        mesh_decode_frame(VEC0_FRAME, VEC0_FRAME_LEN, key, VEC0_CHANNEL_HASH, &d),
        MESH_OK);
    ASSERT_TRUE(d.data.payload >= d.plaintext);
    ASSERT_TRUE(d.data.payload + d.data.payload_len <= d.plaintext + d.plaintext_len);
}

TEST(test_result_names_are_distinct_and_present) {
    ASSERT_TRUE(mesh_decode_result_name(MESH_OK) != NULL);
    ASSERT_TRUE(mesh_decode_result_name(MESH_ERR_TOO_SHORT) != NULL);
    ASSERT_TRUE(mesh_decode_result_name(MESH_ERR_CHANNEL_MISMATCH) != NULL);
    ASSERT_TRUE(mesh_decode_result_name(MESH_ERR_BAD_PROTOBUF) != NULL);
    ASSERT_TRUE(mesh_decode_result_name(MESH_ERR_NOT_TEXT) != NULL);
    ASSERT_TRUE(strcmp(mesh_decode_result_name(MESH_OK),
                       mesh_decode_result_name(MESH_ERR_NOT_TEXT)) != 0);
}

TEST_MAIN_BEGIN()
RUN_TEST(test_acceptance_frame_to_text);
RUN_TEST(test_acceptance_empty_message);
RUN_TEST(test_acceptance_long_frame);
RUN_TEST(test_acceptance_second_psk_frame);
RUN_TEST(test_acceptance_utf8_frame);
RUN_TEST(test_rejects_short_frame);
RUN_TEST(test_rejects_channel_hash_mismatch);
RUN_TEST(test_header_survives_channel_mismatch);
RUN_TEST(test_wrong_key_never_yields_original_text);
RUN_TEST(test_payload_points_into_decoded_struct);
RUN_TEST(test_result_names_are_distinct_and_present);
TEST_MAIN_END()
