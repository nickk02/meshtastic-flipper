/* Handshake state machine tests.
 *
 * The sequence mirrors the Android client's own test:
 *   handleMyInfo(protoMyNodeInfo)
 *   handleConfigComplete(CONFIG_NONCE)
 * then the same for the node info stage. */
#include "tinytest.h"

#include "src/ble/meshtastic_handshake.h"
#include "src/proto/pb_write.h"

static PhoneIdentity identity(void) {
    PhoneIdentity id;
    phone_identity_init(&id, 0x11223344, "Flipper Mesh", "FLPR");
    return id;
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
    PhoneIdentity id = identity();
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
    PhoneIdentity id = identity();
    HandshakeReply reply;
    uint8_t to_radio[16];
    uint64_t value = 0;

    handshake_init(&h, &id);
    size_t len = make_want_config(PHONE_NONCE_CONFIG, to_radio, sizeof(to_radio));

    ASSERT_TRUE(handshake_handle_to_radio(&h, to_radio, len, &reply));
    ASSERT_EQ_INT(reply.count, 6);

    /* my_info, field 3. */
    ASSERT_TRUE(reply.messages[0].len > 0);
    ASSERT_EQ_INT(reply.messages[0].data[0] >> 3, FROMRADIO_FIELD_MY_INFO);

    /* This device's own NodeInfo, field 4. This is the one that carries the
     * name, and the one that was missing. */
    ASSERT_TRUE(reply.messages[1].len > 0);
    ASSERT_EQ_INT(reply.messages[1].data[0] >> 3, FROMRADIO_FIELD_NODE_INFO);

    /* metadata, field 13. The app reads firmware_version from it. */
    ASSERT_TRUE(reply.messages[2].len > 0);
    ASSERT_EQ_INT(reply.messages[2].data[0] >> 3, FROMRADIO_FIELD_METADATA);

    /* channel, field 10. Without it the app has a node on no channel. */
    ASSERT_TRUE(reply.messages[3].len > 0);
    ASSERT_EQ_INT(reply.messages[3].data[0] >> 3, FROMRADIO_FIELD_CHANNEL);

    /* config, field 5, carrying the lora sub-message. */
    ASSERT_TRUE(reply.messages[4].len > 0);
    ASSERT_EQ_INT(reply.messages[4].data[0] >> 3, FROMRADIO_FIELD_CONFIG);

    /* config_complete_id, field 7, carrying the nonce that was asked for.
     * PhoneAPI.cpp calls this the sentinel: it is what ends the stage. */
    ASSERT_TRUE(has_varint_field(
        reply.messages[5].data, reply.messages[5].len, FROMRADIO_FIELD_CONFIG_COMPLETE_ID, &value));
    ASSERT_EQ_INT(value, PHONE_NONCE_CONFIG);

    ASSERT_EQ_INT(handshake_stage(&h), HandshakeConfigRequested);
    ASSERT_TRUE(!handshake_is_complete(&h));
}

TEST(test_stage_two_returns_node_info_then_config_complete) {
    Handshake h;
    PhoneIdentity id = identity();
    HandshakeReply reply;
    uint8_t to_radio[16];
    uint64_t value = 0;

    handshake_init(&h, &id);
    size_t len = make_want_config(PHONE_NONCE_NODE_INFO, to_radio, sizeof(to_radio));

    ASSERT_TRUE(handshake_handle_to_radio(&h, to_radio, len, &reply));
    ASSERT_EQ_INT(reply.count, 2);
    ASSERT_EQ_INT(reply.messages[0].data[0] >> 3, FROMRADIO_FIELD_NODE_INFO);

    ASSERT_TRUE(has_varint_field(
        reply.messages[1].data, reply.messages[1].len, FROMRADIO_FIELD_CONFIG_COMPLETE_ID, &value));
    ASSERT_EQ_INT(value, PHONE_NONCE_NODE_INFO);

    ASSERT_TRUE(handshake_is_complete(&h));
}

TEST(test_full_two_stage_sequence) {
    Handshake h;
    PhoneIdentity id = identity();
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
    PhoneIdentity id = identity();
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
    PhoneIdentity id = identity();
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
    PhoneIdentity id = identity();
    HandshakeReply reply;
    const uint8_t truncated[] = {0x18, 0x80};

    handshake_init(&h, &id);
    ASSERT_TRUE(!handshake_handle_to_radio(&h, truncated, sizeof(truncated), &reply));
    ASSERT_EQ_INT(reply.count, 0);
}

TEST(test_reset_returns_to_idle) {
    Handshake h;
    PhoneIdentity id = identity();
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
    PhoneIdentity id = identity();
    HandshakeReply reply;
    uint8_t to_radio[16];
    size_t len = make_want_config(PHONE_NONCE_CONFIG, to_radio, sizeof(to_radio));

    handshake_init(&h, &id);
    ASSERT_TRUE(handshake_handle_to_radio(&h, to_radio, len, &reply));
    ASSERT_EQ_INT(reply.count, 6);
    ASSERT_TRUE(handshake_handle_to_radio(&h, to_radio, len, &reply));
    ASSERT_EQ_INT(reply.count, 6);
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
RUN_TEST(test_tolerates_null);
TEST_MAIN_END()
