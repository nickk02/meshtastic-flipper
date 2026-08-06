#include "tinytest.h"
#include "mesh_data.h"
#include "vectors.h"

TEST(test_parse_simple_message) {
    MeshData d;
    ASSERT_TRUE(mesh_data_parse(VEC0_PLAINTEXT, VEC0_PLAINTEXT_LEN, &d));
    ASSERT_EQ_INT(d.portnum, VEC0_PORTNUM);
    ASSERT_EQ_INT(d.portnum, MESH_PORTNUM_TEXT_MESSAGE_APP);
    ASSERT_EQ_INT(d.payload_len, VEC0_TEXT_LEN);
    ASSERT_EQ_MEM(d.payload, VEC0_TEXT, VEC0_TEXT_LEN);
}

TEST(test_parse_empty_payload) {
    /* protobuf omits an empty bytes field entirely, so this message is just
       the portnum. */
    MeshData d;
    ASSERT_TRUE(mesh_data_parse(VEC1_PLAINTEXT, VEC1_PLAINTEXT_LEN, &d));
    ASSERT_EQ_INT(d.portnum, MESH_PORTNUM_TEXT_MESSAGE_APP);
    ASSERT_EQ_INT(d.payload_len, 0);
}

TEST(test_parse_long_payload_multibyte_length) {
    /* 180 bytes, so the length prefix is a two byte varint. Catches a parser
       that assumes single byte lengths. */
    MeshData d;
    ASSERT_TRUE(mesh_data_parse(VEC2_PLAINTEXT, VEC2_PLAINTEXT_LEN, &d));
    ASSERT_EQ_INT(d.payload_len, VEC2_TEXT_LEN);
    ASSERT_EQ_MEM(d.payload, VEC2_TEXT, VEC2_TEXT_LEN);
}

TEST(test_parse_utf8_payload_is_byte_exact) {
    MeshData d;
    ASSERT_TRUE(mesh_data_parse(VEC4_PLAINTEXT, VEC4_PLAINTEXT_LEN, &d));
    ASSERT_EQ_INT(d.payload_len, VEC4_TEXT_LEN);
    ASSERT_EQ_MEM(d.payload, VEC4_TEXT, VEC4_TEXT_LEN);
}

TEST(test_payload_points_into_caller_buffer) {
    /* Documented contract: no copy, no allocation. */
    MeshData d;
    ASSERT_TRUE(mesh_data_parse(VEC0_PLAINTEXT, VEC0_PLAINTEXT_LEN, &d));
    ASSERT_TRUE(d.payload >= VEC0_PLAINTEXT);
    ASSERT_TRUE(d.payload + d.payload_len <= VEC0_PLAINTEXT + VEC0_PLAINTEXT_LEN);
}

TEST(test_skips_unknown_varint_field) {
    /* field 1 varint = 1, field 3 varint = 1 (want_response, which we do not
       decode), field 2 bytes = "hi". The unknown field must be skipped. */
    const uint8_t buf[] = {0x08, 0x01, 0x18, 0x01, 0x12, 0x02, 'h', 'i'};
    MeshData d;
    ASSERT_TRUE(mesh_data_parse(buf, sizeof(buf), &d));
    ASSERT_EQ_INT(d.portnum, 1);
    ASSERT_EQ_INT(d.payload_len, 2);
    ASSERT_EQ_MEM(d.payload, "hi", 2);
}

TEST(test_skips_unknown_length_delimited_field) {
    /* field 9 bytes, then field 2 bytes. */
    const uint8_t buf[] = {0x4a, 0x03, 'x', 'y', 'z', 0x12, 0x02, 'o', 'k'};
    MeshData d;
    ASSERT_TRUE(mesh_data_parse(buf, sizeof(buf), &d));
    ASSERT_EQ_INT(d.payload_len, 2);
    ASSERT_EQ_MEM(d.payload, "ok", 2);
}

TEST(test_skips_unknown_fixed32_field) {
    const uint8_t buf[] = {0x2d, 0xAA, 0xBB, 0xCC, 0xDD, 0x12, 0x02, 'o', 'k'};
    MeshData d;
    ASSERT_TRUE(mesh_data_parse(buf, sizeof(buf), &d));
    ASSERT_EQ_INT(d.payload_len, 2);
    ASSERT_EQ_MEM(d.payload, "ok", 2);
}

TEST(test_skips_unknown_fixed64_field) {
    const uint8_t buf[] = {0x29, 1, 2, 3, 4, 5, 6, 7, 8, 0x12, 0x02, 'o', 'k'};
    MeshData d;
    ASSERT_TRUE(mesh_data_parse(buf, sizeof(buf), &d));
    ASSERT_EQ_INT(d.payload_len, 2);
    ASSERT_EQ_MEM(d.payload, "ok", 2);
}

TEST(test_rejects_truncated_varint) {
    const uint8_t buf[] = {0x08, 0x80}; /* continuation bit set on last byte */
    MeshData d;
    ASSERT_TRUE(!mesh_data_parse(buf, sizeof(buf), &d));
}

TEST(test_rejects_tag_with_no_value) {
    const uint8_t buf[] = {0x08}; /* varint tag, nothing follows */
    MeshData d;
    ASSERT_TRUE(!mesh_data_parse(buf, sizeof(buf), &d));
}

TEST(test_rejects_length_past_end) {
    /* field 2 claims 99 bytes but only 2 follow. Garbage from a failed
       decrypt looks exactly like this, so it must not read out of bounds. */
    const uint8_t buf[] = {0x12, 0x63, 'a', 'b'};
    MeshData d;
    ASSERT_TRUE(!mesh_data_parse(buf, sizeof(buf), &d));
}

TEST(test_rejects_fixed32_past_end) {
    const uint8_t buf[] = {0x2d, 0xAA, 0xBB};
    MeshData d;
    ASSERT_TRUE(!mesh_data_parse(buf, sizeof(buf), &d));
}

TEST(test_rejects_fixed64_past_end) {
    const uint8_t buf[] = {0x29, 1, 2, 3};
    MeshData d;
    ASSERT_TRUE(!mesh_data_parse(buf, sizeof(buf), &d));
}

TEST(test_rejects_unsupported_wire_type) {
    const uint8_t buf[] = {0x0e, 0x01}; /* wire type 6 is not valid */
    MeshData d;
    ASSERT_TRUE(!mesh_data_parse(buf, sizeof(buf), &d));
}

TEST(test_rejects_field_number_zero) {
    const uint8_t buf[] = {0x00, 0x01}; /* field 0 is not valid protobuf */
    MeshData d;
    ASSERT_TRUE(!mesh_data_parse(buf, sizeof(buf), &d));
}

TEST(test_rejects_null_but_accepts_zero_length) {
    MeshData d;
    ASSERT_TRUE(!mesh_data_parse(NULL, 4, &d));
    ASSERT_TRUE(!mesh_data_parse(VEC0_PLAINTEXT, VEC0_PLAINTEXT_LEN, NULL));
    /* Zero length is a valid but empty message. */
    ASSERT_TRUE(mesh_data_parse((const uint8_t*)"", 0, &d));
    ASSERT_EQ_INT(d.portnum, 0);
    ASSERT_EQ_INT(d.payload_len, 0);
}

TEST(test_random_bytes_mostly_rejected) {
    /* A wrong key yields effectively random plaintext. Most such buffers must
       be rejected rather than surfacing a bogus message to the user. */
    uint8_t buf[64];
    MeshData d;
    int accepted = 0;
    for(int seed = 0; seed < 1000; seed++) {
        uint32_t x = (uint32_t)seed * 2654435761u + 1u;
        for(size_t i = 0; i < sizeof(buf); i++) {
            x = x * 1103515245u + 12345u;
            buf[i] = (uint8_t)(x >> 16);
        }
        if(mesh_data_parse(buf, sizeof(buf), &d)) accepted++;
    }
    printf("  random buffers accepted: %d of 1000\n", accepted);
    ASSERT_TRUE(accepted < 200);
}

TEST_MAIN_BEGIN()
RUN_TEST(test_parse_simple_message);
RUN_TEST(test_parse_empty_payload);
RUN_TEST(test_parse_long_payload_multibyte_length);
RUN_TEST(test_parse_utf8_payload_is_byte_exact);
RUN_TEST(test_payload_points_into_caller_buffer);
RUN_TEST(test_skips_unknown_varint_field);
RUN_TEST(test_skips_unknown_length_delimited_field);
RUN_TEST(test_skips_unknown_fixed32_field);
RUN_TEST(test_skips_unknown_fixed64_field);
RUN_TEST(test_rejects_truncated_varint);
RUN_TEST(test_rejects_tag_with_no_value);
RUN_TEST(test_rejects_length_past_end);
RUN_TEST(test_rejects_fixed32_past_end);
RUN_TEST(test_rejects_fixed64_past_end);
RUN_TEST(test_rejects_unsupported_wire_type);
RUN_TEST(test_rejects_field_number_zero);
RUN_TEST(test_rejects_null_but_accepts_zero_length);
RUN_TEST(test_random_bytes_mostly_rejected);
TEST_MAIN_END()
