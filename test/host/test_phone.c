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
#include "src/ble/meshtastic_handshake.h"

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

/* Frame size cap */

/* The largest identity a PhoneIdentity can hold: every string at its array
 * size less the terminator, and every integer at its widest varint. Filled
 * directly rather than through phone_identity_init, so the bound tested is
 * the struct's and not whatever the init path happens to produce. */
static PhoneIdentity maximal_identity(void) {
    PhoneIdentity id;
    memset(&id, 0, sizeof(id));
    id.node_num = 0xFFFFFFFF;
    id.hw_model = 0xFFFFFFFF;
    memset(id.id, 'I', sizeof(id.id) - 1);
    memset(id.long_name, 'L', sizeof(id.long_name) - 1);
    memset(id.short_name, 'S', sizeof(id.short_name) - 1);
    return id;
}

static size_t frame_max_seen = 0;

/* Built in a buffer larger than the cap, so a frame over it is measured
 * rather than refused by out_len. */
static void check_fits(const char* what, size_t len) {
    ASSERT_TRUE(len > 0);
    if(len > PHONE_FRAME_MAX) {
        printf("  %s: %u bytes, over %d\n", what, (unsigned)len, PHONE_FRAME_MAX);
    }
    ASSERT_TRUE(len <= PHONE_FRAME_MAX);
    if(len > frame_max_seen) frame_max_seen = len;
}

TEST(test_every_frame_fits_the_phone_read_limit) {
    PhoneIdentity id = maximal_identity();
    uint8_t out[256];
    uint8_t passkey[PHONE_SESSION_PASSKEY_LEN];
    /* The longest channel name the config record holds. */
    char channel[MESH_CONFIG_CHANNEL_NAME_MAX];
    size_t len;

    memset(passkey, 0xFF, sizeof(passkey));
    memset(channel, 'C', sizeof(channel) - 1);
    channel[sizeof(channel) - 1] = '\0';
    frame_max_seen = 0;

    /* Stage one, in handshake order. */
    len = phone_encode_my_node_info(&id, out, sizeof(out));
    printf("  my_info %u", (unsigned)len);
    check_fits("my_info", len);
    check_fits("deviceuiConfig", phone_encode_device_ui(out, sizeof(out)));
    len = phone_encode_node_info(&id, out, sizeof(out));
    printf(", node_info %u", (unsigned)len);
    check_fits("node_info", len);
    len = phone_encode_device_metadata(&id, out, sizeof(out));
    printf(", metadata %u", (unsigned)len);
    check_fits("metadata", len);
    len = phone_encode_primary_channel(channel, 0xFF, out, sizeof(out));
    printf(", primary channel %u", (unsigned)len);
    check_fits("primary channel", len);
    for(uint32_t slot = 1; slot < PHONE_CHANNEL_SLOTS; slot++) {
        check_fits("empty channel", phone_encode_empty_channel(slot, out, sizeof(out)));
    }
    check_fits("empty channel", phone_encode_empty_channel(0xFFFFFFFF, out, sizeof(out)));
    len = phone_encode_lora_config(0xFFFFFFFF, out, sizeof(out));
    printf(", lora %u\n", (unsigned)len);
    check_fits("lora config", len);
    for(uint32_t v = 1; v <= PHONE_CONFIG_VARIANTS; v++) {
        check_fits("config variant", phone_encode_config_variant(v, NULL, 0, out, sizeof(out)));
    }
    for(uint32_t v = 1; v <= PHONE_MODULECONFIG_VARIANTS; v++) {
        check_fits(
            "module config variant", phone_encode_moduleconfig_variant(v, out, sizeof(out)));
    }
    check_fits("config_complete", phone_encode_config_complete(0xFFFFFFFF, out, sizeof(out)));

    /* Admin replies, with addressing at its widest. */
    const uint32_t fields[] = {
        ADMIN_GET_OWNER_REQUEST,
        ADMIN_GET_CANNED_REQUEST,
        ADMIN_GET_RINGTONE_REQUEST,
        ADMIN_SET_CONFIG};
    for(size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        PhoneAdminRequest req = {
            .packet_id = 0xFFFFFFFF,
            .from = 0xFFFFFFFF,
            .admin_field = fields[i],
            .want_response = true};
        len = phone_encode_admin_reply(&id, &req, passkey, out, sizeof(out));
        printf("  admin reply to field %u: %u bytes\n", (unsigned)fields[i], (unsigned)len);
        check_fits("admin reply", len);
    }

    printf(
        "  largest frame with maximal fields: %u of %d\n",
        (unsigned)frame_max_seen,
        PHONE_FRAME_MAX);
}

/* The one encoder with no fixed bound is phone_encode_packet, which wraps
 * whatever it is given. Past the cap it must refuse in a cap-sized buffer, not
 * truncate, so an oversize frame never reaches the queue as a short one. */
TEST(test_packet_over_the_read_limit_is_refused_not_truncated) {
    uint8_t payload[190];
    uint8_t out[PHONE_FRAME_MAX];
    uint8_t wide[256];

    memset(payload, 'x', sizeof(payload));
    /* Tag, two byte length, 181 bytes: exactly the cap. */
    ASSERT_EQ_INT(phone_encode_packet(payload, 181, out, sizeof(out)), PHONE_FRAME_MAX);
    ASSERT_EQ_INT(phone_encode_packet(payload, 182, out, sizeof(out)), 0);
    /* Given the room, it builds the frame, and it is over the cap. */
    ASSERT_EQ_INT(phone_encode_packet(payload, sizeof(payload), wide, sizeof(wide)), 193);
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
RUN_TEST(test_every_frame_fits_the_phone_read_limit);
RUN_TEST(test_packet_over_the_read_limit_is_refused_not_truncated);
TEST_MAIN_END()
