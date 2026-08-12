/* Handshake state machine tests.
 *
 * The sequence mirrors the Android client's own test:
 *   handleMyInfo(protoMyNodeInfo)
 *   handleConfigComplete(CONFIG_NONCE)
 * then the same for the node info stage. */
#include "tinytest.h"

#include "src/ble/meshtastic_handshake.h"
#include "src/proto/pb_write.h"

/* Substring search. memmem is not in C99, and the tests must build with the
 * same -std=c99 -Werror as everything else. */
static bool
    memmem_present(const uint8_t* hay, size_t hay_len, const uint8_t* needle, size_t needle_len) {
    if(needle_len == 0 || hay_len < needle_len) return false;
    for(size_t i = 0; i + needle_len <= hay_len; i++) {
        if(memcmp(hay + i, needle, needle_len) == 0) return true;
    }
    return false;
}

static MeshConfig identity(void) {
    MeshConfig c;
    mesh_config_defaults(&c, 0x11223344);
    return c;
}

static size_t make_want_config(uint32_t nonce, uint8_t* buf, size_t cap) {
    PbWriter w;
    pb_writer_init(&w, buf, cap);
    pb_write_varint_field_always(&w, TORADIO_FIELD_WANT_CONFIG_ID, nonce);
    return pb_writer_len(&w);
}

/* Finds a top-level varint field. */
static bool has_varint_field(const uint8_t* buf, size_t len, uint32_t want, uint64_t* out) {
    size_t pos = 0;
    while(pos < len) {
        uint64_t tag = 0;
        unsigned shift = 0;
        while(pos < len) {
            uint8_t b = buf[pos++];
            tag |= (uint64_t)(b & 0x7F) << shift;
            if((b & 0x80) == 0) break;
            shift += 7;
        }
        uint32_t field = (uint32_t)(tag >> 3);
        uint8_t wire = (uint8_t)(tag & 0x07);

        if(wire == 0) {
            uint64_t value = 0;
            shift = 0;
            while(pos < len) {
                uint8_t b = buf[pos++];
                value |= (uint64_t)(b & 0x7F) << shift;
                if((b & 0x80) == 0) break;
                shift += 7;
            }
            if(field == want) {
                *out = value;
                return true;
            }
        } else if(wire == 2) {
            uint64_t size = 0;
            shift = 0;
            while(pos < len) {
                uint8_t b = buf[pos++];
                size |= (uint64_t)(b & 0x7F) << shift;
                if((b & 0x80) == 0) break;
                shift += 7;
            }
            pos += (size_t)size;
        } else {
            return false;
        }
    }
    return false;
}

TEST(test_starts_idle) {
    Handshake h;
    MeshConfig id = identity();
    handshake_init(&h, &id);
    ASSERT_EQ_INT(handshake_stage(&h), HandshakeIdle);
    ASSERT_TRUE(!handshake_is_complete(&h));
}

/* Stage 1 must carry this device's own NodeInfo, not just my_info.
 *
 * Sending the NodeInfo only in stage 2 still completed the handshake, and the
 * phone was left showing a node with no name. The order mirrors the firmware's
 * own state machine in PhoneAPI.cpp getFromRadio(). */
TEST(test_stage_one_follows_the_firmware_order) {
    Handshake h;
    MeshConfig id = identity();
    HandshakeReply reply;
    uint8_t to_radio[16];
    uint64_t value = 0;

    handshake_init(&h, &id);
    size_t len = make_want_config(PHONE_NONCE_CONFIG, to_radio, sizeof(to_radio));

    ASSERT_TRUE(handshake_handle_to_radio(&h, to_radio, len, &reply));
    /* my_info, own node_info, metadata, one channel, ten config variants,
     * thirteen module config variants, config_complete. */
    /* my_info, deviceuiConfig, own node_info, metadata, eight channel slots,
     * ten config variants, thirteen module config variants, config_complete. */
    ASSERT_EQ_INT(reply.count, 36);

    ASSERT_EQ_INT(reply.messages[0].data[0] >> 3, FROMRADIO_FIELD_MY_INFO);
    ASSERT_EQ_INT(reply.messages[1].data[0] >> 3, FROMRADIO_FIELD_DEVICEUI);
    /* This device's own NodeInfo carries its name. */
    ASSERT_EQ_INT(reply.messages[2].data[0] >> 3, FROMRADIO_FIELD_NODE_INFO);
    /* metadata, the firmware version the app checks. */
    ASSERT_EQ_INT(reply.messages[3].data[0] >> 3, FROMRADIO_FIELD_METADATA);

    /* All eight channel slots. A client that gets one channel is still waiting
     * for seven more, which is why a complete looking stage one was refused. */
    for(size_t i = 4; i < 4 + PHONE_CHANNEL_SLOTS; i++) {
        ASSERT_TRUE(reply.messages[i].len > 0);
        ASSERT_EQ_INT(reply.messages[i].data[0] >> 3, FROMRADIO_FIELD_CHANNEL);
    }

    for(size_t i = 12; i < 12 + PHONE_CONFIG_VARIANTS; i++) {
        ASSERT_TRUE(reply.messages[i].len > 0);
        ASSERT_EQ_INT(reply.messages[i].data[0] >> 3, FROMRADIO_FIELD_CONFIG);
    }

    for(size_t i = 22; i < 22 + PHONE_MODULECONFIG_VARIANTS; i++) {
        ASSERT_TRUE(reply.messages[i].len > 0);
        ASSERT_EQ_INT(reply.messages[i].data[0] >> 3, FROMRADIO_FIELD_MODULECONFIG);
    }

    /* config_complete_id last. PhoneAPI.cpp calls it the sentinel: it ends the
     * stage, so anything after it is a truncated sequence to the client. */
    ASSERT_TRUE(has_varint_field(
        reply.messages[35].data,
        reply.messages[35].len,
        FROMRADIO_FIELD_CONFIG_COMPLETE_ID,
        &value));
    ASSERT_EQ_INT(value, PHONE_NONCE_CONFIG);

    ASSERT_EQ_INT(handshake_stage(&h), HandshakeConfigRequested);
    ASSERT_TRUE(!handshake_is_complete(&h));
}

TEST(test_stage_two_returns_node_info_then_config_complete) {
    Handshake h;
    MeshConfig id = identity();
    HandshakeReply reply;
    uint8_t to_radio[16];
    uint64_t value = 0;

    handshake_init(&h, &id);
    size_t len = make_want_config(PHONE_NONCE_NODE_INFO, to_radio, sizeof(to_radio));

    ASSERT_TRUE(handshake_handle_to_radio(&h, to_radio, len, &reply));
    /* Each message repeats four times: this is not a single unreliable pass,
     * since a device that cannot detect reads has no other way to give a slow
     * reader more than one chance. */
    ASSERT_EQ_INT(reply.count, 8);
    for(size_t i = 0; i < 4; i++) {
        ASSERT_EQ_INT(reply.messages[i].data[0] >> 3, FROMRADIO_FIELD_NODE_INFO);
    }
    for(size_t i = 4; i < 8; i++) {
        ASSERT_TRUE(has_varint_field(
            reply.messages[i].data,
            reply.messages[i].len,
            FROMRADIO_FIELD_CONFIG_COMPLETE_ID,
            &value));
        ASSERT_EQ_INT(value, PHONE_NONCE_NODE_INFO);
    }

    ASSERT_TRUE(handshake_is_complete(&h));
}

TEST(test_full_two_stage_sequence) {
    Handshake h;
    MeshConfig id = identity();
    HandshakeReply reply;
    uint8_t to_radio[16];
    size_t len;

    handshake_init(&h, &id);

    len = make_want_config(PHONE_NONCE_CONFIG, to_radio, sizeof(to_radio));
    ASSERT_TRUE(handshake_handle_to_radio(&h, to_radio, len, &reply));
    ASSERT_EQ_INT(handshake_stage(&h), HandshakeConfigRequested);

    len = make_want_config(PHONE_NONCE_NODE_INFO, to_radio, sizeof(to_radio));
    ASSERT_TRUE(handshake_handle_to_radio(&h, to_radio, len, &reply));
    ASSERT_TRUE(handshake_is_complete(&h));
}

TEST(test_unknown_nonce_is_rejected_and_sends_nothing) {
    /* Answering a stage the app did not ask for makes it discard the reply and
       stall, so an unrecognized nonce must produce no messages at all. */
    Handshake h;
    MeshConfig id = identity();
    HandshakeReply reply;
    uint8_t to_radio[16];

    handshake_init(&h, &id);
    size_t len = make_want_config(12345, to_radio, sizeof(to_radio));

    ASSERT_TRUE(!handshake_handle_to_radio(&h, to_radio, len, &reply));
    ASSERT_EQ_INT(reply.count, 0);
    ASSERT_EQ_INT(handshake_stage(&h), HandshakeIdle);
}

TEST(test_to_radio_without_want_config_is_ignored) {
    Handshake h;
    MeshConfig id = identity();
    HandshakeReply reply;
    uint8_t to_radio[16];
    PbWriter w;

    handshake_init(&h, &id);
    pb_writer_init(&w, to_radio, sizeof(to_radio));
    pb_write_varint_field_always(&w, TORADIO_FIELD_DISCONNECT, 1);

    ASSERT_TRUE(!handshake_handle_to_radio(&h, to_radio, pb_writer_len(&w), &reply));
    ASSERT_EQ_INT(reply.count, 0);
}

TEST(test_malformed_to_radio_is_ignored) {
    Handshake h;
    MeshConfig id = identity();
    HandshakeReply reply;
    const uint8_t truncated[] = {0x18, 0x80};

    handshake_init(&h, &id);
    ASSERT_TRUE(!handshake_handle_to_radio(&h, truncated, sizeof(truncated), &reply));
    ASSERT_EQ_INT(reply.count, 0);
}

TEST(test_reset_returns_to_idle) {
    Handshake h;
    MeshConfig id = identity();
    HandshakeReply reply;
    uint8_t to_radio[16];

    handshake_init(&h, &id);
    size_t len = make_want_config(PHONE_NONCE_NODE_INFO, to_radio, sizeof(to_radio));
    handshake_handle_to_radio(&h, to_radio, len, &reply);
    ASSERT_TRUE(handshake_is_complete(&h));

    handshake_reset(&h);
    ASSERT_EQ_INT(handshake_stage(&h), HandshakeIdle);
    ASSERT_TRUE(!handshake_is_complete(&h));
}

TEST(test_stages_may_repeat) {
    /* A phone that reconnects re-runs the whole handshake. Handling stage 1
       twice must work rather than being treated as out of order. */
    Handshake h;
    MeshConfig id = identity();
    HandshakeReply reply;
    uint8_t to_radio[16];
    size_t len = make_want_config(PHONE_NONCE_CONFIG, to_radio, sizeof(to_radio));

    handshake_init(&h, &id);
    ASSERT_TRUE(handshake_handle_to_radio(&h, to_radio, len, &reply));
    ASSERT_EQ_INT(reply.count, 36);
    ASSERT_TRUE(handshake_handle_to_radio(&h, to_radio, len, &reply));
    ASSERT_EQ_INT(reply.count, 36);
}

TEST(test_tolerates_null) {
    HandshakeReply reply;
    uint8_t to_radio[4] = {0};
    ASSERT_TRUE(!handshake_handle_to_radio(NULL, to_radio, sizeof(to_radio), &reply));
    ASSERT_TRUE(!handshake_is_complete(NULL));
    ASSERT_EQ_INT(handshake_stage(NULL), HandshakeIdle);
    handshake_reset(NULL);
    handshake_init(NULL, NULL);
}

/* Builds ToRadio { packet { decoded { portnum: ADMIN_APP, payload:
 * AdminMessage { get_owner_request: true } } , id, from } }. */
static size_t make_get_owner(uint32_t packet_id, uint32_t from, uint8_t* buf, size_t cap) {
    uint8_t admin[16];
    uint8_t data[64];
    uint8_t packet[96];
    PbWriter w;

    pb_writer_init(&w, admin, sizeof(admin));
    pb_write_varint_field_always(&w, 3, 1); /* get_owner_request */
    size_t admin_len = pb_writer_len(&w);

    pb_writer_init(&w, data, sizeof(data));
    pb_write_varint_field_always(&w, 1, 6); /* portnum ADMIN_APP */
    pb_write_bytes_field(&w, 2, admin, admin_len);
    pb_write_varint_field_always(&w, 3, 1); /* want_response */
    size_t data_len = pb_writer_len(&w);

    pb_writer_init(&w, packet, sizeof(packet));
    pb_write_fixed32_field_always(&w, 1, from);
    pb_write_submessage(&w, 4, data, data_len);
    pb_write_fixed32_field_always(&w, 6, packet_id);
    size_t packet_len = pb_writer_len(&w);

    pb_writer_init(&w, buf, cap);
    pb_write_submessage(&w, 1, packet, packet_len);
    return pb_writer_len(&w);
}

TEST(test_get_owner_request_is_recognised) {
    uint8_t buf[128];
    PhoneAdminRequest req;
    size_t len = make_get_owner(0xAABBCCDD, 0x11223344, buf, sizeof(buf));

    ASSERT_TRUE(phone_decode_get_owner_request(buf, len, &req));
    /* fixed32, so these only come back right if both sides agree on wire type
     * 5. A varint here decodes to garbage or not at all. */
    ASSERT_EQ_INT(req.packet_id, 0xAABBCCDD);
    ASSERT_EQ_INT(req.from, 0x11223344);
}

TEST(test_non_admin_packet_is_not_a_get_owner) {
    /* A text message must not be answered with an owner record. */
    uint8_t buf[128];
    PhoneAdminRequest req;
    PbWriter w;
    uint8_t data[32];
    uint8_t packet[64];

    pb_writer_init(&w, data, sizeof(data));
    pb_write_varint_field_always(&w, 1, 1); /* TEXT_MESSAGE_APP */
    pb_write_string_field(&w, 2, "hello");
    size_t data_len = pb_writer_len(&w);

    pb_writer_init(&w, packet, sizeof(packet));
    pb_write_submessage(&w, 4, data, data_len);
    size_t packet_len = pb_writer_len(&w);

    pb_writer_init(&w, buf, sizeof(buf));
    pb_write_submessage(&w, 1, packet, packet_len);

    ASSERT_TRUE(!phone_decode_get_owner_request(buf, pb_writer_len(&w), &req));
}

TEST(test_want_config_is_not_a_get_owner) {
    uint8_t buf[16];
    PhoneAdminRequest req;
    size_t len = make_want_config(PHONE_NONCE_CONFIG, buf, sizeof(buf));
    ASSERT_TRUE(!phone_decode_get_owner_request(buf, len, &req));
}

TEST(test_handshake_answers_get_owner_at_any_stage) {
    /* The phone asks after stage two, but answering only in one stage would
     * make a retry after a reconnect fall through silently. */
    Handshake h;
    MeshConfig id = identity();
    HandshakeReply reply;
    uint8_t to_radio[128];
    const uint8_t passkey[PHONE_SESSION_PASSKEY_LEN] = {1, 2, 3, 4, 5, 6, 7, 8};
    size_t len = make_get_owner(0x01020304, 0, to_radio, sizeof(to_radio));

    handshake_init(&h, &id);
    handshake_set_session_passkey(&h, passkey);

    ASSERT_TRUE(handshake_handle_to_radio(&h, to_radio, len, &reply));
    ASSERT_EQ_INT(reply.count, 1);
    /* FromRadio.packet is field 2. */
    ASSERT_EQ_INT(reply.messages[0].data[0] >> 3, FROMRADIO_FIELD_PACKET);
    /* The passkey must appear, or the client never latches a session and keeps
     * reconnecting. */
    ASSERT_TRUE(
        memmem_present(reply.messages[0].data, reply.messages[0].len, passkey, sizeof(passkey)));
}

TEST(test_get_owner_response_carries_the_name) {
    Handshake h;
    MeshConfig id = identity();
    HandshakeReply reply;
    uint8_t to_radio[128];
    const uint8_t passkey[PHONE_SESSION_PASSKEY_LEN] = {9, 9, 9, 9, 9, 9, 9, 9};
    size_t len = make_get_owner(7, 0, to_radio, sizeof(to_radio));

    handshake_init(&h, &id);
    handshake_set_session_passkey(&h, passkey);
    ASSERT_TRUE(handshake_handle_to_radio(&h, to_radio, len, &reply));

    ASSERT_TRUE(memmem_present(
        reply.messages[0].data,
        reply.messages[0].len,
        (const uint8_t*)id.owner.long_name,
        strlen(id.owner.long_name)));
}

/* The exact bytes a real Android client sent, from a serial capture. The
 * payload is field 14, get_ringtone_request, not field 3. */
TEST(test_real_ringtone_request_is_recognised) {
    const uint8_t observed[] = {0x0a, 0x1d, 0x0d, 0x26, 0x69, 0x6c, 0x46, 0x15, 0x26, 0x69, 0x6c,
                                0x46, 0x22, 0x08, 0x08, 0x06, 0x12, 0x02, 0x70, 0x01, 0x18, 0x01,
                                0x35, 0xf6, 0x33, 0x77, 0x04, 0x50, 0x01, 0x58, 0x46};
    PhoneAdminRequest req;

    ASSERT_TRUE(phone_decode_admin_request(observed, sizeof(observed), &req));
    ASSERT_EQ_INT(req.admin_field, ADMIN_GET_RINGTONE_REQUEST);
    ASSERT_TRUE(req.want_response);
    ASSERT_EQ_INT(req.packet_id, 0x047733f6);
    /* get_owner specifically must reject it, which is why answering only
     * get_owner left the client waiting. */
    ASSERT_TRUE(!phone_decode_get_owner_request(observed, sizeof(observed), &req));
}

/* Same capture, the canned messages request. Field 10. */
TEST(test_real_canned_message_request_is_recognised) {
    const uint8_t observed[] = {0x0a, 0x1d, 0x0d, 0x26, 0x69, 0x6c, 0x46, 0x15, 0x26, 0x69, 0x6c,
                                0x46, 0x22, 0x08, 0x08, 0x06, 0x12, 0x02, 0x50, 0x01, 0x18, 0x01,
                                0x35, 0x97, 0xbb, 0xf5, 0x8d, 0x50, 0x01, 0x58, 0x46};
    PhoneAdminRequest req;

    ASSERT_TRUE(phone_decode_admin_request(observed, sizeof(observed), &req));
    ASSERT_EQ_INT(req.admin_field, ADMIN_GET_CANNED_REQUEST);
    ASSERT_TRUE(req.want_response);
}

TEST(test_every_observed_request_is_answered) {
    /* Each of these went unanswered in the capture, and each one stalls the
     * client on its own. */
    const uint32_t fields[] = {
        ADMIN_GET_RINGTONE_REQUEST, ADMIN_GET_CANNED_REQUEST, ADMIN_SET_CONFIG};
    const uint8_t passkey[PHONE_SESSION_PASSKEY_LEN] = {1, 2, 3, 4, 5, 6, 7, 8};
    PhoneIdentity pid;
    MeshConfig cfg = identity();
    uint8_t out[HANDSHAKE_MAX_MESSAGE];

    phone_identity_from_config(&cfg, &pid);

    for(size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        PhoneAdminRequest req = {
            .packet_id = 42, .from = 7, .admin_field = fields[i], .want_response = true};
        size_t len = phone_encode_admin_reply(&pid, &req, passkey, out, sizeof(out));
        ASSERT_TRUE(len > 0);
        ASSERT_EQ_INT(out[0] >> 3, FROMRADIO_FIELD_PACKET);
    }
}

TEST(test_no_reply_when_none_wanted) {
    /* want_response false means the client is not waiting, so sending anything
     * is noise on a link that is already tight. */
    const uint8_t passkey[PHONE_SESSION_PASSKEY_LEN] = {0};
    PhoneIdentity pid;
    MeshConfig cfg = identity();
    uint8_t out[HANDSHAKE_MAX_MESSAGE];
    PhoneAdminRequest req = {
        .packet_id = 1,
        .from = 2,
        .admin_field = ADMIN_GET_RINGTONE_REQUEST,
        .want_response = false};

    phone_identity_from_config(&cfg, &pid);
    ASSERT_EQ_INT(phone_encode_admin_reply(&pid, &req, passkey, out, sizeof(out)), 0);
}

TEST_MAIN_BEGIN()
RUN_TEST(test_starts_idle);
RUN_TEST(test_stage_one_follows_the_firmware_order);
RUN_TEST(test_stage_two_returns_node_info_then_config_complete);
RUN_TEST(test_full_two_stage_sequence);
RUN_TEST(test_unknown_nonce_is_rejected_and_sends_nothing);
RUN_TEST(test_to_radio_without_want_config_is_ignored);
RUN_TEST(test_malformed_to_radio_is_ignored);
RUN_TEST(test_reset_returns_to_idle);
RUN_TEST(test_stages_may_repeat);
RUN_TEST(test_get_owner_request_is_recognised);
RUN_TEST(test_non_admin_packet_is_not_a_get_owner);
RUN_TEST(test_want_config_is_not_a_get_owner);
RUN_TEST(test_handshake_answers_get_owner_at_any_stage);
RUN_TEST(test_get_owner_response_carries_the_name);
RUN_TEST(test_real_ringtone_request_is_recognised);
RUN_TEST(test_real_canned_message_request_is_recognised);
RUN_TEST(test_every_observed_request_is_answered);
RUN_TEST(test_no_reply_when_none_wanted);
RUN_TEST(test_tolerates_null);
TEST_MAIN_END()
