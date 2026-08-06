#include "tinytest.h"
#include "mesh_header.h"
#include "vectors.h"

TEST(test_header_is_16_bytes) {
    ASSERT_EQ_INT(MESH_HEADER_LEN, 16);
}

TEST(test_parse_fields) {
    MeshHeader h;
    ASSERT_TRUE(mesh_header_parse(VEC0_FRAME, VEC0_FRAME_LEN, &h));
    ASSERT_EQ_INT(h.to, VEC0_TO_NODE);
    ASSERT_EQ_INT(h.from, VEC0_FROM_NODE);
    ASSERT_EQ_INT(h.id, VEC0_PACKET_ID);
    ASSERT_EQ_INT(h.flags, VEC0_FLAGS);
    ASSERT_EQ_INT(h.channel, VEC0_CHANNEL_HASH);
    ASSERT_EQ_INT(h.next_hop, 0);
    ASSERT_EQ_INT(h.relay_node, 0);
}

TEST(test_flag_accessors) {
    MeshHeader h;
    ASSERT_TRUE(mesh_header_parse(VEC0_FRAME, VEC0_FRAME_LEN, &h));
    ASSERT_EQ_INT(mesh_header_hop_limit(&h), VEC0_HOP_LIMIT);
    ASSERT_EQ_INT(mesh_header_hop_start(&h), VEC0_HOP_START);
}

TEST(test_flag_bits_decode_independently) {
    /* hop limit 5 (0x05), want_ack (0x08), via_mqtt (0x10),
       hop start 7 (0x07 << 5 = 0xE0). RadioInterface.h:24-28. */
    uint8_t frame[MESH_HEADER_LEN] = {0};
    MeshHeader h;
    frame[12] = 0x05 | 0x08 | 0x10 | 0xE0;

    ASSERT_TRUE(mesh_header_parse(frame, MESH_HEADER_LEN, &h));
    ASSERT_EQ_INT(mesh_header_hop_limit(&h), 5);
    ASSERT_EQ_INT(mesh_header_hop_start(&h), 7);
    ASSERT_TRUE(mesh_header_want_ack(&h));
    ASSERT_TRUE(mesh_header_via_mqtt(&h));
}

TEST(test_flag_bits_clear) {
    uint8_t frame[MESH_HEADER_LEN] = {0};
    MeshHeader h;
    frame[12] = 0x00;

    ASSERT_TRUE(mesh_header_parse(frame, MESH_HEADER_LEN, &h));
    ASSERT_EQ_INT(mesh_header_hop_limit(&h), 0);
    ASSERT_EQ_INT(mesh_header_hop_start(&h), 0);
    ASSERT_TRUE(!mesh_header_want_ack(&h));
    ASSERT_TRUE(!mesh_header_via_mqtt(&h));
}

TEST(test_hop_limit_does_not_leak_into_hop_start) {
    /* All hop limit bits set, hop start clear. A shift or mask error here is
       the classic way to get plausible-looking but wrong values. */
    uint8_t frame[MESH_HEADER_LEN] = {0};
    MeshHeader h;
    frame[12] = 0x07;

    ASSERT_TRUE(mesh_header_parse(frame, MESH_HEADER_LEN, &h));
    ASSERT_EQ_INT(mesh_header_hop_limit(&h), 7);
    ASSERT_EQ_INT(mesh_header_hop_start(&h), 0);
}

TEST(test_parse_rejects_short_buffer) {
    uint8_t frame[MESH_HEADER_LEN - 1] = {0};
    MeshHeader h;
    ASSERT_TRUE(!mesh_header_parse(frame, sizeof(frame), &h));
}

TEST(test_parse_accepts_exactly_header_length) {
    uint8_t frame[MESH_HEADER_LEN] = {0};
    MeshHeader h;
    ASSERT_TRUE(mesh_header_parse(frame, MESH_HEADER_LEN, &h));
}

TEST(test_parse_rejects_null) {
    MeshHeader h;
    ASSERT_TRUE(!mesh_header_parse(NULL, MESH_HEADER_LEN, &h));
    ASSERT_TRUE(!mesh_header_parse((const uint8_t*)"", 0, NULL));
}

TEST(test_little_endian_decode) {
    /* to = 0x04030201 stored as 01 02 03 04. Catches a byte order flip, which
       a symmetric test value would hide. */
    uint8_t frame[MESH_HEADER_LEN] = {0};
    MeshHeader h;
    frame[0] = 0x01;
    frame[1] = 0x02;
    frame[2] = 0x03;
    frame[3] = 0x04;

    ASSERT_TRUE(mesh_header_parse(frame, MESH_HEADER_LEN, &h));
    ASSERT_EQ_INT(h.to, 0x04030201u);
}

TEST(test_field_offsets_are_distinct) {
    /* Every field gets a unique value so a wrong offset cannot pass. */
    uint8_t frame[MESH_HEADER_LEN] = {
        0x11,
        0x00,
        0x00,
        0x00, /* to */
        0x22,
        0x00,
        0x00,
        0x00, /* from */
        0x33,
        0x00,
        0x00,
        0x00, /* id */
        0x44, /* flags */
        0x55, /* channel */
        0x66, /* next_hop */
        0x77, /* relay_node */
    };
    MeshHeader h;

    ASSERT_TRUE(mesh_header_parse(frame, MESH_HEADER_LEN, &h));
    ASSERT_EQ_INT(h.to, 0x11u);
    ASSERT_EQ_INT(h.from, 0x22u);
    ASSERT_EQ_INT(h.id, 0x33u);
    ASSERT_EQ_INT(h.flags, 0x44);
    ASSERT_EQ_INT(h.channel, 0x55);
    ASSERT_EQ_INT(h.next_hop, 0x66);
    ASSERT_EQ_INT(h.relay_node, 0x77);
}

TEST_MAIN_BEGIN()
RUN_TEST(test_header_is_16_bytes);
RUN_TEST(test_parse_fields);
RUN_TEST(test_flag_accessors);
RUN_TEST(test_flag_bits_decode_independently);
RUN_TEST(test_flag_bits_clear);
RUN_TEST(test_hop_limit_does_not_leak_into_hop_start);
RUN_TEST(test_parse_rejects_short_buffer);
RUN_TEST(test_parse_accepts_exactly_header_length);
RUN_TEST(test_parse_rejects_null);
RUN_TEST(test_little_endian_decode);
RUN_TEST(test_field_offsets_are_distinct);
TEST_MAIN_END()
