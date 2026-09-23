/* Tests for the phone handshake messages.
 *
 * These are checked structurally rather than against a reference encoder,
 * because the reference here is the Android client and it cannot be run from
 * a C test. Each test therefore decodes what was written and asserts the field
 * numbers, wire types and values the client actually reads. */
#include "tinytest.h"

#include "mesh_data.h"
#include "pb_write.h"
#include "phone_encode.h"

/* Minimal reader, so the tests verify structure rather than trusting the
   writer to agree with itself. */
typedef struct {
    const uint8_t* buf;
    size_t len;
    size_t pos;
} Reader;

static bool read_varint(Reader* r, uint64_t* out) {
    uint64_t value = 0;
    unsigned shift = 0;
    while(r->pos < r->len) {
        uint8_t byte = r->buf[r->pos++];
        value |= (uint64_t)(byte & 0x7F) << shift;
        if((byte & 0x80) == 0) {
            *out = value;
            return true;
        }
        shift += 7;
    }
    return false;
}

/* Finds a top-level field. For length-delimited fields sets sub to the body. */
static bool find_field(
    const uint8_t* buf,
    size_t len,
    uint32_t want_field,
    uint64_t* varint_out,
    const uint8_t** sub,
    size_t* sub_len) {
    Reader r = {buf, len, 0};
    while(r.pos < r.len) {
        uint64_t tag;
        if(!read_varint(&r, &tag)) return false;
        uint32_t field = (uint32_t)(tag >> 3);
        uint8_t wire = (uint8_t)(tag & 0x07);

        if(wire == 0) {
            uint64_t value;
            if(!read_varint(&r, &value)) return false;
            if(field == want_field) {
                if(varint_out) *varint_out = value;
                return true;
            }
        } else if(wire == 2) {
            uint64_t size;
            if(!read_varint(&r, &size)) return false;
            if(size > r.len - r.pos) return false;
            if(field == want_field) {
                if(sub) *sub = r.buf + r.pos;
                if(sub_len) *sub_len = (size_t)size;
                return true;
            }
            r.pos += (size_t)size;
        } else {
            return false;
        }
    }
    return false;
}

static PhoneIdentity identity(void) {
    PhoneIdentity id;
    phone_identity_init(&id, 0x11223344, "Flipper Mesh", "FLPR");
    return id;
}

/* pb_write */

TEST(test_writer_omits_zero_varint) {
    uint8_t buf[8];
    PbWriter w;
    pb_writer_init(&w, buf, sizeof(buf));
    pb_write_varint_field(&w, 1, 0);
    ASSERT_TRUE(pb_writer_ok(&w));
    ASSERT_EQ_INT(pb_writer_len(&w), 0);
}

TEST(test_writer_always_variant_writes_zero) {
    uint8_t buf[8];
    PbWriter w;
    pb_writer_init(&w, buf, sizeof(buf));
    pb_write_varint_field_always(&w, 1, 0);
    ASSERT_TRUE(pb_writer_ok(&w));
    ASSERT_EQ_INT(pb_writer_len(&w), 2);
    ASSERT_EQ_INT(buf[0], 0x08); /* field 1, varint */
    ASSERT_EQ_INT(buf[1], 0x00);
}

TEST(test_writer_multibyte_varint) {
    uint8_t buf[16];
    PbWriter w;
    pb_writer_init(&w, buf, sizeof(buf));
    pb_write_varint_field_always(&w, 1, 300);
    ASSERT_EQ_INT(pb_writer_len(&w), 3);
    ASSERT_EQ_INT(buf[1], 0xAC);
    ASSERT_EQ_INT(buf[2], 0x02);
}

TEST(test_writer_error_is_sticky) {
    uint8_t buf[2];
    PbWriter w;
    pb_writer_init(&w, buf, sizeof(buf));
    pb_write_string_field(&w, 1, "far too long for this buffer");
    ASSERT_TRUE(!pb_writer_ok(&w));
    /* A later small write must not clear the failure. */
    pb_write_varint_field_always(&w, 2, 1);
    ASSERT_TRUE(!pb_writer_ok(&w));
}

TEST(test_writer_empty_submessage_is_written) {
    /* An empty submessage differs from an absent one. */
    uint8_t buf[8];
    PbWriter w;
    pb_writer_init(&w, buf, sizeof(buf));
    pb_write_submessage(&w, 3, NULL, 0);
    ASSERT_TRUE(pb_writer_ok(&w));
    ASSERT_EQ_INT(pb_writer_len(&w), 2);
    ASSERT_EQ_INT(buf[0], 0x1A); /* field 3, length delimited */
    ASSERT_EQ_INT(buf[1], 0x00);
}

/* MyNodeInfo */

TEST(test_my_node_info_is_wrapped_in_field_3) {
    uint8_t buf[128];
    PhoneIdentity id = identity();
    const uint8_t* sub = NULL;
    size_t sub_len = 0;

    size_t len = phone_encode_my_node_info(&id, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(find_field(buf, len, FROMRADIO_FIELD_MY_INFO, NULL, &sub, &sub_len));
    ASSERT_TRUE(sub_len > 0);
}

TEST(test_my_node_info_carries_node_num) {
    uint8_t buf[128];
    PhoneIdentity id = identity();
    const uint8_t* sub = NULL;
    size_t sub_len = 0;
    uint64_t value = 0;

    size_t len = phone_encode_my_node_info(&id, buf, sizeof(buf));
    find_field(buf, len, FROMRADIO_FIELD_MY_INFO, NULL, &sub, &sub_len);

    /* MyNodeInfo.my_node_num is field 1. */
    ASSERT_TRUE(find_field(sub, sub_len, 1, &value, NULL, NULL));
    ASSERT_EQ_INT(value, 0x11223344u);
}

TEST(test_my_node_info_node_num_zero_still_present) {
    /* my_node_num is what the client keys the whole session on. Omitting it
       when the node number is 0 would break the handshake. */
    uint8_t buf[128];
    PhoneIdentity id = identity();
    const uint8_t* sub = NULL;
    size_t sub_len = 0;
    uint64_t value = 99;

    id.node_num = 0;
    size_t len = phone_encode_my_node_info(&id, buf, sizeof(buf));
    find_field(buf, len, FROMRADIO_FIELD_MY_INFO, NULL, &sub, &sub_len);

    ASSERT_TRUE(find_field(sub, sub_len, 1, &value, NULL, NULL));
    ASSERT_EQ_INT(value, 0);
}

TEST(test_my_node_info_carries_device_id_and_pio_env) {
    uint8_t buf[128];
    PhoneIdentity id = identity();
    const uint8_t* sub = NULL;
    size_t sub_len = 0;
    const uint8_t* field = NULL;
    size_t field_len = 0;

    size_t len = phone_encode_my_node_info(&id, buf, sizeof(buf));
    find_field(buf, len, FROMRADIO_FIELD_MY_INFO, NULL, &sub, &sub_len);

    /* device_id is field 12, four bytes of node number, big endian. */
    ASSERT_TRUE(find_field(sub, sub_len, 12, NULL, &field, &field_len));
    ASSERT_EQ_INT(field_len, 4);
    ASSERT_EQ_INT(field[0], 0x11);
    ASSERT_EQ_INT(field[3], 0x44);

    /* pio_env is field 13. */
    ASSERT_TRUE(find_field(sub, sub_len, 13, NULL, &field, &field_len));
    ASSERT_TRUE(field_len > 0);
}

TEST(test_my_node_info_rejects_small_buffer) {
    uint8_t buf[4];
    PhoneIdentity id = identity();
    ASSERT_EQ_INT(phone_encode_my_node_info(&id, buf, sizeof(buf)), 0);
}

/* NodeInfo */

TEST(test_node_info_nests_user_inside_node_info) {
    uint8_t buf[192];
    PhoneIdentity id = identity();
    const uint8_t* node = NULL;
    size_t node_len = 0;
    const uint8_t* user = NULL;
    size_t user_len = 0;
    uint64_t num = 0;

    size_t len = phone_encode_node_info(&id, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    ASSERT_TRUE(find_field(buf, len, FROMRADIO_FIELD_NODE_INFO, NULL, &node, &node_len));
    ASSERT_TRUE(find_field(node, node_len, 1, &num, NULL, NULL));
    ASSERT_EQ_INT(num, 0x11223344u);

    ASSERT_TRUE(find_field(node, node_len, 2, NULL, &user, &user_len));
    ASSERT_TRUE(user_len > 0);
}

TEST(test_node_info_user_fields) {
    uint8_t buf[192];
    PhoneIdentity id = identity();
    const uint8_t* node = NULL;
    size_t node_len = 0;
    const uint8_t* user = NULL;
    size_t user_len = 0;
    const uint8_t* field = NULL;
    size_t field_len = 0;

    size_t len = phone_encode_node_info(&id, buf, sizeof(buf));
    find_field(buf, len, FROMRADIO_FIELD_NODE_INFO, NULL, &node, &node_len);
    find_field(node, node_len, 2, NULL, &user, &user_len);

    ASSERT_TRUE(find_field(user, user_len, 1, NULL, &field, &field_len));
    ASSERT_EQ_MEM(field, "!11223344", 9);

    ASSERT_TRUE(find_field(user, user_len, 2, NULL, &field, &field_len));
    ASSERT_EQ_MEM(field, "Flipper Mesh", 12);

    ASSERT_TRUE(find_field(user, user_len, 3, NULL, &field, &field_len));
    ASSERT_EQ_MEM(field, "FLPR", 4);
}

/* config_complete_id */

TEST(test_config_complete_uses_field_7) {
    /* Field 7, not 8. Field 8 is rebooted. A wrong field number here means the
       app never leaves the handshake. */
    uint8_t buf[16];
    uint64_t value = 0;

    size_t len = phone_encode_config_complete(PHONE_NONCE_CONFIG, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(find_field(buf, len, FROMRADIO_FIELD_CONFIG_COMPLETE_ID, &value, NULL, NULL));
    ASSERT_EQ_INT(value, PHONE_NONCE_CONFIG);
}

TEST(test_both_handshake_nonces_encode) {
    uint8_t buf[16];
    uint64_t value = 0;

    size_t len = phone_encode_config_complete(PHONE_NONCE_NODE_INFO, buf, sizeof(buf));
    ASSERT_TRUE(find_field(buf, len, FROMRADIO_FIELD_CONFIG_COMPLETE_ID, &value, NULL, NULL));
    ASSERT_EQ_INT(value, PHONE_NONCE_NODE_INFO);
    ASSERT_EQ_INT(PHONE_NONCE_CONFIG, 69420);
    ASSERT_EQ_INT(PHONE_NONCE_NODE_INFO, 69421);
}

TEST(test_config_complete_with_zero_nonce_still_writes) {
    uint8_t buf[16];
    uint64_t value = 99;
    size_t len = phone_encode_config_complete(0, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(find_field(buf, len, FROMRADIO_FIELD_CONFIG_COMPLETE_ID, &value, NULL, NULL));
    ASSERT_EQ_INT(value, 0);
}

/* packet wrapping */

TEST(test_packet_is_wrapped_in_field_2) {
    uint8_t buf[64];
    const uint8_t payload[] = {0x08, 0x01, 0x12, 0x02, 'h', 'i'};
    const uint8_t* sub = NULL;
    size_t sub_len = 0;

    size_t len = phone_encode_packet(payload, sizeof(payload), buf, sizeof(buf));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(find_field(buf, len, FROMRADIO_FIELD_PACKET, NULL, &sub, &sub_len));
    ASSERT_EQ_INT(sub_len, sizeof(payload));
    ASSERT_EQ_MEM(sub, payload, sizeof(payload));
}

/* ToRadio decode */

TEST(test_decode_want_config_id) {
    /* field 3 varint = 69420 */
    uint8_t buf[8];
    PbWriter w;
    uint32_t nonce = 0;

    pb_writer_init(&w, buf, sizeof(buf));
    pb_write_varint_field_always(&w, TORADIO_FIELD_WANT_CONFIG_ID, PHONE_NONCE_CONFIG);

    ASSERT_TRUE(phone_decode_want_config_id(buf, pb_writer_len(&w), &nonce));
    ASSERT_EQ_INT(nonce, PHONE_NONCE_CONFIG);
}

TEST(test_decode_skips_other_fields) {
    /* A ToRadio carrying a packet and a want_config_id. */
    uint8_t buf[32];
    PbWriter w;
    uint32_t nonce = 0;
    const uint8_t packet[] = {1, 2, 3};

    pb_writer_init(&w, buf, sizeof(buf));
    pb_write_submessage(&w, TORADIO_FIELD_PACKET, packet, sizeof(packet));
    pb_write_varint_field_always(&w, TORADIO_FIELD_WANT_CONFIG_ID, PHONE_NONCE_NODE_INFO);

    ASSERT_TRUE(phone_decode_want_config_id(buf, pb_writer_len(&w), &nonce));
    ASSERT_EQ_INT(nonce, PHONE_NONCE_NODE_INFO);
}

TEST(test_decode_reports_absent_want_config_id) {
    uint8_t buf[16];
    PbWriter w;
    uint32_t nonce = 0;

    pb_writer_init(&w, buf, sizeof(buf));
    pb_write_varint_field_always(&w, TORADIO_FIELD_DISCONNECT, 1);

    ASSERT_TRUE(!phone_decode_want_config_id(buf, pb_writer_len(&w), &nonce));
}

TEST(test_decode_rejects_malformed) {
    const uint8_t truncated[] = {0x18, 0x80}; /* varint runs off the end */
    uint32_t nonce = 0;
    ASSERT_TRUE(!phone_decode_want_config_id(truncated, sizeof(truncated), &nonce));
    ASSERT_TRUE(!phone_decode_want_config_id(NULL, 4, &nonce));
    ASSERT_TRUE(!phone_decode_want_config_id(truncated, sizeof(truncated), NULL));
}

TEST(test_decode_round_trips_with_encoder) {
    uint8_t buf[16];
    uint32_t nonce = 0;
    PbWriter w;

    pb_writer_init(&w, buf, sizeof(buf));
    pb_write_varint_field_always(&w, TORADIO_FIELD_WANT_CONFIG_ID, 0);
    ASSERT_TRUE(phone_decode_want_config_id(buf, pb_writer_len(&w), &nonce));
    ASSERT_EQ_INT(nonce, 0);
}

/* queueStatus, FromRadio field 11 */

TEST(test_queue_status_is_field_11_with_all_subfields) {
    uint8_t buf[32];
    const uint8_t* qs = NULL;
    size_t qs_len = 0;
    uint64_t value = 0;

    size_t len = phone_encode_queue_status(0, 16, 16, 0x01020304u, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);
    /* Tag byte: field 11, wire type 2. */
    ASSERT_EQ_INT(buf[0], (FROMRADIO_FIELD_QUEUE_STATUS << 3) | 2);
    ASSERT_TRUE(find_field(buf, len, FROMRADIO_FIELD_QUEUE_STATUS, NULL, &qs, &qs_len));

    /* res 0 is the proto3 default and is left out. */
    ASSERT_TRUE(!find_field(qs, qs_len, 1, &value, NULL, NULL));
    ASSERT_TRUE(find_field(qs, qs_len, 2, &value, NULL, NULL));
    ASSERT_EQ_INT(value, 16);
    ASSERT_TRUE(find_field(qs, qs_len, 3, &value, NULL, NULL));
    ASSERT_EQ_INT(value, 16);
    ASSERT_TRUE(find_field(qs, qs_len, 4, &value, NULL, NULL));
    ASSERT_EQ_INT(value, 0x01020304u);
}

TEST(test_queue_status_zeros_still_carry_free_and_maxlen) {
    /* An all-zero answer must still be a QueueStatus with fields in it, not
       an empty submessage. */
    uint8_t buf[16];
    const uint8_t* qs = NULL;
    size_t qs_len = 0;
    uint64_t value = 99;

    size_t len = phone_encode_queue_status(0, 0, 0, 0, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(find_field(buf, len, FROMRADIO_FIELD_QUEUE_STATUS, NULL, &qs, &qs_len));
    ASSERT_EQ_INT(qs_len, 4);
    ASSERT_TRUE(find_field(qs, qs_len, 2, &value, NULL, NULL));
    ASSERT_EQ_INT(value, 0);
    value = 99;
    ASSERT_TRUE(find_field(qs, qs_len, 3, &value, NULL, NULL));
    ASSERT_EQ_INT(value, 0);
    ASSERT_TRUE(!find_field(qs, qs_len, 1, &value, NULL, NULL));
    ASSERT_TRUE(!find_field(qs, qs_len, 4, &value, NULL, NULL));
}

TEST(test_queue_status_negative_res_is_ten_byte_varint) {
    /* int32 -1 is sign extended to 64 bits on the wire: tag, then ten bytes,
       nine of 0xff and a final 0x01. Five bytes would decode as 4294967295. */
    uint8_t buf[32];
    const uint8_t* qs = NULL;
    size_t qs_len = 0;
    uint64_t value = 0;
    const uint8_t expected_res[] = {
        0x08, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01};

    size_t len = phone_encode_queue_status(-1, 16, 16, 0, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(find_field(buf, len, FROMRADIO_FIELD_QUEUE_STATUS, NULL, &qs, &qs_len));
    ASSERT_EQ_INT(qs_len, sizeof(expected_res) + 4);
    ASSERT_EQ_MEM(qs, expected_res, sizeof(expected_res));

    ASSERT_TRUE(find_field(qs, qs_len, 1, &value, NULL, NULL));
    ASSERT_EQ_INT((int32_t)(int64_t)value, -1);
    ASSERT_TRUE(find_field(qs, qs_len, 2, &value, NULL, NULL));
    ASSERT_EQ_INT(value, 16);
}

TEST(test_queue_status_rejects_small_buffer) {
    uint8_t buf[8];
    ASSERT_EQ_INT(phone_encode_queue_status(-1, 16, 16, 0, buf, sizeof(buf)), 0);
    ASSERT_EQ_INT(phone_encode_queue_status(0, 16, 16, 0, NULL, 32), 0);
}

/* ToRadio heartbeat decode */

static size_t build_heartbeat(uint8_t* buf, size_t cap, bool with_nonce, uint32_t nonce) {
    uint8_t hb[8];
    PbWriter w;
    size_t hb_len = 0;
    if(with_nonce) {
        pb_writer_init(&w, hb, sizeof(hb));
        pb_write_varint_field_always(&w, 1, nonce);
        hb_len = pb_writer_len(&w);
    }
    pb_writer_init(&w, buf, cap);
    pb_write_submessage(&w, TORADIO_FIELD_HEARTBEAT, hb, hb_len);
    return pb_writer_len(&w);
}

TEST(test_decode_heartbeat_nonce) {
    uint8_t buf[16];
    uint32_t nonce = 0;
    size_t len = build_heartbeat(buf, sizeof(buf), true, 0x89abcdefu);
    ASSERT_TRUE(phone_decode_heartbeat(buf, len, &nonce));
    ASSERT_EQ_INT(nonce, 0x89abcdefu);

    len = build_heartbeat(buf, sizeof(buf), true, 1);
    ASSERT_TRUE(phone_decode_heartbeat(buf, len, &nonce));
    ASSERT_EQ_INT(nonce, 1);
}

TEST(test_decode_heartbeat_without_nonce_is_zero) {
    uint8_t buf[16];
    uint32_t nonce = 77;
    size_t len = build_heartbeat(buf, sizeof(buf), false, 0);
    ASSERT_EQ_INT(len, 2);
    ASSERT_TRUE(phone_decode_heartbeat(buf, len, &nonce));
    ASSERT_EQ_INT(nonce, 0);
}

TEST(test_decode_heartbeat_rejects_others_and_malformed) {
    uint8_t buf[16];
    uint32_t nonce = 0;
    PbWriter w;
    const uint8_t bad_inner[] = {0x3a, 0x02, 0x08, 0x80}; /* nonce varint runs off */
    const uint8_t bad_outer[] = {0x3a, 0x05, 0x08, 0x01}; /* length past the end */
    const uint8_t varint_hb[] = {0x38, 0x01}; /* field 7 as a varint */

    pb_writer_init(&w, buf, sizeof(buf));
    pb_write_varint_field_always(&w, TORADIO_FIELD_WANT_CONFIG_ID, PHONE_NONCE_CONFIG);
    ASSERT_TRUE(!phone_decode_heartbeat(buf, pb_writer_len(&w), &nonce));
    ASSERT_TRUE(!phone_decode_heartbeat(bad_inner, sizeof(bad_inner), &nonce));
    ASSERT_TRUE(!phone_decode_heartbeat(bad_outer, sizeof(bad_outer), &nonce));
    ASSERT_TRUE(!phone_decode_heartbeat(varint_hb, sizeof(varint_hb), &nonce));
    ASSERT_TRUE(!phone_decode_heartbeat(NULL, 0, &nonce));
}

/* ToRadio.packet id decode */

static size_t build_text_packet(uint8_t* buf, size_t cap, bool fixed32_id, uint32_t id) {
    uint8_t data[32];
    uint8_t packet[64];
    PbWriter w;

    pb_writer_init(&w, data, sizeof(data));
    pb_write_varint_field_always(&w, 1, 1); /* portnum TEXT_MESSAGE_APP */
    pb_write_string_field(&w, 2, "hi");
    size_t data_len = pb_writer_len(&w);

    pb_writer_init(&w, packet, sizeof(packet));
    pb_write_fixed32_field_always(&w, 2, 0xFFFFFFFFu); /* to broadcast */
    pb_write_submessage(&w, 4, data, data_len);
    if(fixed32_id) {
        pb_write_fixed32_field_always(&w, 6, id);
    } else {
        pb_write_varint_field_always(&w, 6, id);
    }
    size_t packet_len = pb_writer_len(&w);

    pb_writer_init(&w, buf, cap);
    pb_write_submessage(&w, TORADIO_FIELD_PACKET, packet, packet_len);
    return pb_writer_ok(&w) ? pb_writer_len(&w) : 0;
}

TEST(test_decode_packet_id_reads_fixed32) {
    uint8_t buf[96];
    uint32_t id = 0;
    size_t len = build_text_packet(buf, sizeof(buf), true, 0x01020304u);
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(phone_decode_packet_id(buf, len, &id));
    ASSERT_EQ_INT(id, 0x01020304u);
}

TEST(test_decode_packet_id_rejects_varint_id) {
    /* MeshPacket.id is fixed32. A varint in field 6 is not an id. */
    uint8_t buf[96];
    uint32_t id = 0;
    size_t len = build_text_packet(buf, sizeof(buf), false, 0x01020304u);
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(!phone_decode_packet_id(buf, len, &id));
}

TEST(test_decode_packet_id_rejects_malformed_and_absent) {
    uint8_t buf[96];
    uint32_t id = 0;
    PbWriter w;
    /* ToRadio.packet { id: fixed32 cut short } */
    const uint8_t cut_id[] = {0x0a, 0x03, 0x35, 0x04, 0x03};
    /* ToRadio.packet whose length runs past the end. */
    const uint8_t bad_len[] = {0x0a, 0x09, 0x35, 0x04, 0x03, 0x02, 0x01};
    /* ToRadio.packet { from } with no id. */
    const uint8_t no_id[] = {0x0a, 0x05, 0x0d, 0x01, 0x02, 0x03, 0x04};

    ASSERT_TRUE(!phone_decode_packet_id(cut_id, sizeof(cut_id), &id));
    ASSERT_TRUE(!phone_decode_packet_id(bad_len, sizeof(bad_len), &id));
    ASSERT_TRUE(!phone_decode_packet_id(no_id, sizeof(no_id), &id));

    pb_writer_init(&w, buf, sizeof(buf));
    pb_write_varint_field_always(&w, TORADIO_FIELD_WANT_CONFIG_ID, PHONE_NONCE_CONFIG);
    ASSERT_TRUE(!phone_decode_packet_id(buf, pb_writer_len(&w), &id));
    ASSERT_TRUE(!phone_decode_packet_id(NULL, 4, &id));
}

TEST_MAIN_BEGIN()
RUN_TEST(test_writer_omits_zero_varint);
RUN_TEST(test_writer_always_variant_writes_zero);
RUN_TEST(test_writer_multibyte_varint);
RUN_TEST(test_writer_error_is_sticky);
RUN_TEST(test_writer_empty_submessage_is_written);
RUN_TEST(test_my_node_info_is_wrapped_in_field_3);
RUN_TEST(test_my_node_info_carries_node_num);
RUN_TEST(test_my_node_info_node_num_zero_still_present);
RUN_TEST(test_my_node_info_carries_device_id_and_pio_env);
RUN_TEST(test_my_node_info_rejects_small_buffer);
RUN_TEST(test_node_info_nests_user_inside_node_info);
RUN_TEST(test_node_info_user_fields);
RUN_TEST(test_config_complete_uses_field_7);
RUN_TEST(test_both_handshake_nonces_encode);
RUN_TEST(test_config_complete_with_zero_nonce_still_writes);
RUN_TEST(test_packet_is_wrapped_in_field_2);
RUN_TEST(test_decode_want_config_id);
RUN_TEST(test_decode_skips_other_fields);
RUN_TEST(test_decode_reports_absent_want_config_id);
RUN_TEST(test_decode_rejects_malformed);
RUN_TEST(test_decode_round_trips_with_encoder);
RUN_TEST(test_queue_status_is_field_11_with_all_subfields);
RUN_TEST(test_queue_status_zeros_still_carry_free_and_maxlen);
RUN_TEST(test_queue_status_negative_res_is_ten_byte_varint);
RUN_TEST(test_queue_status_rejects_small_buffer);
RUN_TEST(test_decode_heartbeat_nonce);
RUN_TEST(test_decode_heartbeat_without_nonce_is_zero);
RUN_TEST(test_decode_heartbeat_rejects_others_and_malformed);
RUN_TEST(test_decode_packet_id_reads_fixed32);
RUN_TEST(test_decode_packet_id_rejects_varint_id);
RUN_TEST(test_decode_packet_id_rejects_malformed_and_absent);
TEST_MAIN_END()
