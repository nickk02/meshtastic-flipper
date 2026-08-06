/* Transmit path tests.
 *
 * The strongest check available without a radio: encoding a frame from the
 * same inputs the Python generator used must reproduce that generator's bytes
 * exactly. Python builds the Data message with the real meshtastic package and
 * encrypts with OpenSSL, so agreement means our transmit format matches an
 * independent implementation rather than merely matching our own decoder. */
#include "tinytest.h"

#include "mesh_channel.h"
#include "mesh_decode.h"
#include "mesh_encode.h"
#include "vectors.h"

static MeshTxParams vec0_params(const uint8_t* key) {
    MeshTxParams p;
    memset(&p, 0, sizeof(p));
    p.to = VEC0_TO_NODE;
    p.from = VEC0_FROM_NODE;
    p.id = VEC0_PACKET_ID;
    p.hop_limit = VEC0_HOP_LIMIT;
    p.hop_start = VEC0_HOP_START;
    p.want_ack = false;
    p.channel_hash = VEC0_CHANNEL_HASH;
    p.key = key;
    p.text = VEC0_TEXT;
    p.text_len = VEC0_TEXT_LEN;
    return p;
}

/* Data protobuf encoding */

TEST(test_encode_data_matches_generator) {
    uint8_t buf[64];
    size_t len = mesh_encode_data(
        MESH_PORTNUM_TEXT_MESSAGE_APP, VEC0_TEXT, VEC0_TEXT_LEN, buf, sizeof(buf));

    ASSERT_EQ_INT(len, VEC0_PLAINTEXT_LEN);
    ASSERT_EQ_MEM(buf, VEC0_PLAINTEXT, VEC0_PLAINTEXT_LEN);
}

TEST(test_encode_data_empty_payload_matches_generator) {
    /* proto3 omits an empty bytes field, so this is just the portnum. */
    uint8_t buf[16];
    size_t len = mesh_encode_data(MESH_PORTNUM_TEXT_MESSAGE_APP, NULL, 0, buf, sizeof(buf));

    ASSERT_EQ_INT(len, VEC1_PLAINTEXT_LEN);
    ASSERT_EQ_MEM(buf, VEC1_PLAINTEXT, VEC1_PLAINTEXT_LEN);
}

TEST(test_encode_data_long_payload_uses_two_byte_length) {
    /* 180 bytes needs a multi-byte length varint. Matching the generator here
       proves the varint encoder, not just the copy. */
    uint8_t buf[256];
    size_t len = mesh_encode_data(
        MESH_PORTNUM_TEXT_MESSAGE_APP, VEC2_TEXT, VEC2_TEXT_LEN, buf, sizeof(buf));

    ASSERT_EQ_INT(len, VEC2_PLAINTEXT_LEN);
    ASSERT_EQ_MEM(buf, VEC2_PLAINTEXT, VEC2_PLAINTEXT_LEN);
}

TEST(test_encode_data_utf8_is_byte_exact) {
    uint8_t buf[64];
    size_t len = mesh_encode_data(
        MESH_PORTNUM_TEXT_MESSAGE_APP, VEC4_TEXT, VEC4_TEXT_LEN, buf, sizeof(buf));

    ASSERT_EQ_INT(len, VEC4_PLAINTEXT_LEN);
    ASSERT_EQ_MEM(buf, VEC4_PLAINTEXT, VEC4_PLAINTEXT_LEN);
}

TEST(test_encode_data_rejects_small_buffer) {
    uint8_t buf[3];
    ASSERT_EQ_INT(
        mesh_encode_data(MESH_PORTNUM_TEXT_MESSAGE_APP, VEC0_TEXT, VEC0_TEXT_LEN, buf, sizeof(buf)),
        0);
}

TEST(test_encode_data_rejects_null) {
    uint8_t buf[32];
    ASSERT_EQ_INT(mesh_encode_data(1, NULL, 5, buf, sizeof(buf)), 0);
    ASSERT_EQ_INT(mesh_encode_data(1, VEC0_TEXT, 1, NULL, 10), 0);
}

/* Header encoding */

TEST(test_encode_header_matches_generator) {
    uint8_t key[MESH_PSK_LEN];
    uint8_t buf[MESH_HEADER_LEN];

    mesh_channel_expand_psk(1, key);
    MeshTxParams p = vec0_params(key);

    ASSERT_EQ_INT(mesh_encode_header(&p, buf, sizeof(buf)), MESH_HEADER_LEN);
    ASSERT_EQ_MEM(buf, VEC0_FRAME, MESH_HEADER_LEN);
}

TEST(test_encode_header_flag_packing) {
    uint8_t key[MESH_PSK_LEN];
    uint8_t buf[MESH_HEADER_LEN];
    MeshHeader parsed;

    mesh_channel_expand_psk(1, key);
    MeshTxParams p = vec0_params(key);
    p.hop_limit = 5;
    p.hop_start = 7;
    p.want_ack = true;

    ASSERT_EQ_INT(mesh_encode_header(&p, buf, sizeof(buf)), MESH_HEADER_LEN);
    ASSERT_TRUE(mesh_header_parse(buf, sizeof(buf), &parsed));
    ASSERT_EQ_INT(mesh_header_hop_limit(&parsed), 5);
    ASSERT_EQ_INT(mesh_header_hop_start(&parsed), 7);
    ASSERT_TRUE(mesh_header_want_ack(&parsed));
    ASSERT_TRUE(!mesh_header_via_mqtt(&parsed));
}

TEST(test_encode_header_rejects_small_buffer) {
    uint8_t key[MESH_PSK_LEN];
    uint8_t buf[MESH_HEADER_LEN - 1];
    mesh_channel_expand_psk(1, key);
    MeshTxParams p = vec0_params(key);
    ASSERT_EQ_INT(mesh_encode_header(&p, buf, sizeof(buf)), 0);
}

/* Whole frame. The load-bearing tests. */

TEST(test_encode_frame_reproduces_generator_byte_for_byte) {
    uint8_t key[MESH_PSK_LEN];
    uint8_t buf[MESH_MAX_PAYLOAD];

    mesh_channel_expand_psk(1, key);
    MeshTxParams p = vec0_params(key);

    size_t len = mesh_encode_frame(&p, buf, sizeof(buf));
    ASSERT_EQ_INT(len, VEC0_FRAME_LEN);
    ASSERT_EQ_MEM(buf, VEC0_FRAME, VEC0_FRAME_LEN);
}

TEST(test_encode_frame_matches_generator_for_long_text) {
    uint8_t key[MESH_PSK_LEN];
    uint8_t buf[MESH_MAX_PAYLOAD];

    mesh_channel_expand_psk(1, key);
    MeshTxParams p = vec0_params(key);
    p.from = VEC2_FROM_NODE;
    p.id = VEC2_PACKET_ID;
    p.hop_limit = VEC2_HOP_LIMIT;
    p.hop_start = VEC2_HOP_START;
    p.channel_hash = VEC2_CHANNEL_HASH;
    p.text = VEC2_TEXT;
    p.text_len = VEC2_TEXT_LEN;

    size_t len = mesh_encode_frame(&p, buf, sizeof(buf));
    ASSERT_EQ_INT(len, VEC2_FRAME_LEN);
    ASSERT_EQ_MEM(buf, VEC2_FRAME, VEC2_FRAME_LEN);
}

TEST(test_encode_frame_with_second_psk_matches_generator) {
    uint8_t key[MESH_PSK_LEN];
    uint8_t buf[MESH_MAX_PAYLOAD];

    mesh_channel_expand_psk(2, key);
    MeshTxParams p = vec0_params(key);
    p.from = VEC3_FROM_NODE;
    p.id = VEC3_PACKET_ID;
    p.hop_limit = VEC3_HOP_LIMIT;
    p.hop_start = VEC3_HOP_START;
    p.channel_hash = VEC3_CHANNEL_HASH;
    p.text = VEC3_TEXT;
    p.text_len = VEC3_TEXT_LEN;

    size_t len = mesh_encode_frame(&p, buf, sizeof(buf));
    ASSERT_EQ_INT(len, VEC3_FRAME_LEN);
    ASSERT_EQ_MEM(buf, VEC3_FRAME, VEC3_FRAME_LEN);
}

/* Round trip through our own decoder. Weaker than the generator comparison
   above, since a shared misunderstanding would pass, but it catches wiring
   mistakes between the two halves. */
TEST(test_encode_then_decode_round_trip) {
    uint8_t key[MESH_PSK_LEN];
    uint8_t frame[MESH_MAX_PAYLOAD];
    MeshDecoded decoded;
    const char* message = "round trip through both halves";

    mesh_channel_expand_psk(1, key);
    MeshTxParams p = vec0_params(key);
    p.text = (const uint8_t*)message;
    p.text_len = strlen(message);

    size_t len = mesh_encode_frame(&p, frame, sizeof(frame));
    ASSERT_TRUE(len > MESH_HEADER_LEN);

    ASSERT_EQ_INT(mesh_decode_frame(frame, len, key, p.channel_hash, &decoded), MESH_OK);
    ASSERT_EQ_INT(decoded.header.from, VEC0_FROM_NODE);
    ASSERT_EQ_INT(decoded.data.payload_len, strlen(message));
    ASSERT_EQ_MEM(decoded.data.payload, message, strlen(message));
}

TEST(test_round_trip_at_maximum_text_length) {
    uint8_t key[MESH_PSK_LEN];
    uint8_t frame[MESH_MAX_PAYLOAD];
    uint8_t text[MESH_MAX_PAYLOAD];
    MeshDecoded decoded;
    size_t max = mesh_encode_max_text_len();

    memset(text, 'A', max);
    mesh_channel_expand_psk(1, key);
    MeshTxParams p = vec0_params(key);
    p.text = text;
    p.text_len = max;

    size_t len = mesh_encode_frame(&p, frame, sizeof(frame));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(len <= MESH_MAX_PAYLOAD);

    ASSERT_EQ_INT(mesh_decode_frame(frame, len, key, p.channel_hash, &decoded), MESH_OK);
    ASSERT_EQ_INT(decoded.data.payload_len, max);
}

TEST(test_encode_frame_refuses_oversized_text) {
    /* Refusing beats truncating: a cut ciphertext decodes to garbage at the
       far end, which is far worse than not sending. */
    uint8_t key[MESH_PSK_LEN];
    uint8_t frame[MESH_MAX_PAYLOAD];
    uint8_t text[MESH_MAX_PAYLOAD];

    memset(text, 'A', sizeof(text));
    mesh_channel_expand_psk(1, key);
    MeshTxParams p = vec0_params(key);
    p.text = text;
    p.text_len = sizeof(text);

    ASSERT_EQ_INT(mesh_encode_frame(&p, frame, sizeof(frame)), 0);
}

TEST(test_encode_frame_rejects_null) {
    uint8_t key[MESH_PSK_LEN];
    uint8_t frame[MESH_MAX_PAYLOAD];
    mesh_channel_expand_psk(1, key);
    MeshTxParams p = vec0_params(key);

    ASSERT_EQ_INT(mesh_encode_frame(NULL, frame, sizeof(frame)), 0);
    ASSERT_EQ_INT(mesh_encode_frame(&p, NULL, sizeof(frame)), 0);

    p.key = NULL;
    ASSERT_EQ_INT(mesh_encode_frame(&p, frame, sizeof(frame)), 0);
}

TEST(test_wrong_key_frame_does_not_decode_as_text) {
    /* Encoding under one key must not be readable under another. */
    uint8_t key[MESH_PSK_LEN];
    uint8_t other[MESH_PSK_LEN];
    uint8_t frame[MESH_MAX_PAYLOAD];
    MeshDecoded decoded;

    mesh_channel_expand_psk(1, key);
    mesh_channel_expand_psk(2, other);
    MeshTxParams p = vec0_params(key);

    size_t len = mesh_encode_frame(&p, frame, sizeof(frame));
    MeshDecodeResult r = mesh_decode_frame(frame, len, other, p.channel_hash, &decoded);

    if(r == MESH_OK) {
        ASSERT_TRUE(
            decoded.data.payload_len != VEC0_TEXT_LEN ||
            memcmp(decoded.data.payload, VEC0_TEXT, VEC0_TEXT_LEN) != 0);
    } else {
        ASSERT_TRUE(r == MESH_ERR_BAD_PROTOBUF || r == MESH_ERR_NOT_TEXT);
    }
}

TEST_MAIN_BEGIN()
RUN_TEST(test_encode_data_matches_generator);
RUN_TEST(test_encode_data_empty_payload_matches_generator);
RUN_TEST(test_encode_data_long_payload_uses_two_byte_length);
RUN_TEST(test_encode_data_utf8_is_byte_exact);
RUN_TEST(test_encode_data_rejects_small_buffer);
RUN_TEST(test_encode_data_rejects_null);
RUN_TEST(test_encode_header_matches_generator);
RUN_TEST(test_encode_header_flag_packing);
RUN_TEST(test_encode_header_rejects_small_buffer);
RUN_TEST(test_encode_frame_reproduces_generator_byte_for_byte);
RUN_TEST(test_encode_frame_matches_generator_for_long_text);
RUN_TEST(test_encode_frame_with_second_psk_matches_generator);
RUN_TEST(test_encode_then_decode_round_trip);
RUN_TEST(test_round_trip_at_maximum_text_length);
RUN_TEST(test_encode_frame_refuses_oversized_text);
RUN_TEST(test_encode_frame_rejects_null);
RUN_TEST(test_wrong_key_frame_does_not_decode_as_text);
TEST_MAIN_END()
