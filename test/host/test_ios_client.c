/* The iOS client, run against this device's real handshake and encoder.
 *
 * Every earlier test checked frames against the protobuf spec. These check
 * them against what Meshtastic-Apple v2.7.21 does with them, step by step,
 * using the model in ios_client.h. Read that header first: each rule there
 * cites the Swift file it came from. */
#include "tinytest.h"

#include "ios_client.h"

static MeshConfig config(void) {
    MeshConfig c;
    mesh_config_defaults(&c, 0x11223344);
    return c;
}

/* Version rule, Step 6 */

TEST(test_numeric_compare_matches_foundation) {
    ASSERT_TRUE(ios_numeric_compare("2.5.18", "2.5.0") > 0);
    ASSERT_TRUE(ios_numeric_compare("2.5.18", "2.5.18") == 0);
    ASSERT_TRUE(ios_numeric_compare("2.5.18", "2.5.19") < 0);
    ASSERT_TRUE(ios_numeric_compare("2.5.18", "2.7.21.abc123") < 0);
    ASSERT_TRUE(ios_numeric_compare("2.5.18", "2.10.0") < 0);
    /* Digit runs compare as numbers, not characters: 10 > 9. */
    ASSERT_TRUE(ios_numeric_compare("2.9", "2.10") < 0);
    ASSERT_TRUE(ios_numeric_compare("2.5.18", "3.0.0") < 0);
    ASSERT_TRUE(ios_numeric_compare("2.5.18", "1.99.99") > 0);
}

/* The version this device reports today is "2.5.0". Step 6 compares
 * minimumVersion "2.5.18" against it and throws "Update Your Firmware".
 * The stepper's default failure behaviour is retryAll, and Step 0 of a
 * retry begins with closeConnection(): that is the HCI 0x13 the phone
 * sends, and with maxRetries 2 and retryDelay 2s it is the cycle. */
TEST(test_version_2_5_0_fails_step_6) {
    ASSERT_TRUE(!ios_version_supported("2.5.0"));
    /* The reduction Step 6 stores in UserDefaults, for the record. */
    char stripped[IOS_VERSION_MAX];
    ios_version_strip("2.5.0", stripped, sizeof(stripped));
    ASSERT_EQ_MEM(stripped, "2.5", 4);
}

TEST(test_version_rule_edges) {
    /* Exactly the floor passes: orderedSame is accepted. */
    ASSERT_TRUE(ios_version_supported("2.5.18"));
    ASSERT_TRUE(ios_version_supported("2.5.19"));
    ASSERT_TRUE(ios_version_supported("2.6.11.flipper"));
    ASSERT_TRUE(ios_version_supported("2.7.21.abc123"));
    ASSERT_TRUE(!ios_version_supported("2.5.17"));
    ASSERT_TRUE(!ios_version_supported("2.4.99"));
    /* No dot: Step 6 throws versionMismatch before the comparison. */
    ASSERT_TRUE(!ios_version_supported("3"));
    ASSERT_TRUE(!ios_version_supported(""));
    ASSERT_TRUE(!ios_version_supported(NULL));
    /* The live string is compared whole. A hash suffix does not matter. */
    char stripped[IOS_VERSION_MAX];
    ios_version_strip("2.7.21.abc123", stripped, sizeof(stripped));
    ASSERT_EQ_MEM(stripped, "2.7.21", 7);
}

/* The full flow against the real encoder, ideal transport */

TEST(test_ideal_flow_against_real_encoder) {
    IosClient c;
    MeshConfig cfg = config();
    ios_client_init(&c, &cfg, IosTransportIdeal);
    IosOutcome outcome = ios_client_connect(&c);
    printf("  ideal transport, current encoder:\n");
    ios_client_report(&c);

    /* Everything the device sent decoded, whatever the outcome. */
    ASSERT_EQ_INT(c.decode_failures, 0);
    ASSERT_EQ_INT(c.invalid_utf8, 0);
    ASSERT_EQ_INT(c.unknown_variants, 0);
    ASSERT_EQ_INT(c.oneof_multiple, 0);
    ASSERT_EQ_INT(c.to_radio_not_understood, 0);

    /* Ordering rules the handlers enforce with a guard and a dropped frame. */
    ASSERT_EQ_INT(c.metadata_dropped_no_device, 0);
    ASSERT_EQ_INT(c.channel_dropped_no_device, 0);
    ASSERT_EQ_INT(c.config_dropped_no_name, 0);
    ASSERT_EQ_INT(c.moduleconfig_dropped_no_name, 0);
    ASSERT_EQ_INT(c.nodeinfo_zero_num, 0);

    /* Stages 1 and 2 both completed: the flow got as far as Step 6. */
    ASSERT_TRUE(c.have_my_info);
    ASSERT_TRUE(c.have_long_name);
    ASSERT_TRUE(c.have_firmware_version);
    ASSERT_TRUE(c.db_gate_open);
    ASSERT_TRUE(c.db_first_node_seen);
    ASSERT_TRUE(c.failed_step == 0 || c.failed_step >= 6);

    /* Step 6 is decided by the version string in the metadata frame and
     * nothing else. This holds today, when it rejects "2.5.0", and it holds
     * after the one line fix, when it accepts. */
    bool supported = ios_version_supported(c.firmware_version);
    if(supported) {
        ASSERT_EQ_INT(outcome, IosOutcomeConnected);
    } else {
        ASSERT_EQ_INT(outcome, IosOutcomeFailed);
        ASSERT_EQ_INT(c.failed_step, 6);
    }
    printf(
        "  Step 6 with reported version \"%s\": %s\n",
        c.firmware_version,
        supported ? "passes" : "throws Update Your Firmware, client disconnects and retries");
}

TEST(test_stage_one_order_as_the_client_reads_it) {
    IosClient c;
    MeshConfig cfg = config();
    ios_client_init(&c, &cfg, IosTransportIdeal);
    ios_client_connect(&c);

    /* my_info first, then the client has a device num. */
    ASSERT_EQ_INT(c.variant_trace[0], IOS_FR_MY_INFO);
    /* The node's own NodeInfo, carrying the name, precedes metadata and
     * every config frame, since handleConfig drops frames until it has one. */
    int own_node_info = -1, first_config = -1, metadata = -1;
    for(int i = 0; i < c.variant_trace_len && i < 40; i++) {
        if(c.variant_trace[i] == IOS_FR_NODE_INFO && own_node_info < 0) own_node_info = i;
        if(c.variant_trace[i] == IOS_FR_CONFIG && first_config < 0) first_config = i;
        if(c.variant_trace[i] == IOS_FR_METADATA && metadata < 0) metadata = i;
    }
    ASSERT_TRUE(own_node_info > 0);
    ASSERT_TRUE(metadata > own_node_info);
    ASSERT_TRUE(first_config > own_node_info);
    /* Stage one is the firmware's 36 frames. */
    ASSERT_EQ_INT(c.variant_count[IOS_FR_MY_INFO], 1);
    ASSERT_EQ_INT(c.variant_count[IOS_FR_METADATA], 1);
    ASSERT_EQ_INT(c.variant_count[IOS_FR_CHANNEL], PHONE_CHANNEL_SLOTS);
    ASSERT_EQ_INT(c.variant_count[IOS_FR_CONFIG], PHONE_CONFIG_VARIANTS);
    ASSERT_EQ_INT(c.variant_count[IOS_FR_MODULECONFIG], PHONE_MODULECONFIG_VARIANTS);
}

/* handleModuleConfig sends get_canned_message_module_messages_request on
 * the canned_message variant and get_ringtone_request on the
 * external_notification variant, both want_response. handleConfig sends
 * set_config with a tzdef when device.tzdef is empty. All three land during
 * stage one, and the device must answer the two that want a response. */
TEST(test_requests_the_client_makes_during_stage_one) {
    IosClient c;
    MeshConfig cfg = config();
    ios_client_init(&c, &cfg, IosTransportIdeal);
    ios_client_connect(&c);

    ASSERT_EQ_INT(c.sent_get_canned, 1);
    ASSERT_EQ_INT(c.sent_get_ringtone, 1);
    ASSERT_EQ_INT(c.admin_responses, 2);
    /* iOS does not set want_response on set_config or set_time, so the
     * device owes nothing for them. */
    ASSERT_EQ_INT(c.routing_acks, 0);
    ASSERT_EQ_INT(c.to_radio_not_understood, 0);
    /* Every request the client sent was one the device recognised. */
    ASSERT_EQ_INT(
        c.to_radio_writes,
        2 /* heartbeats */ + 2 /* want_config */ + c.sent_get_canned + c.sent_get_ringtone +
            c.sent_set_config_tzdef + c.sent_set_time);
}

/* Every frame must fit one ATT read. */
TEST(test_every_frame_fits_one_read) {
    IosClient c;
    MeshConfig cfg = config();
    /* Longest names the record allows, so the biggest frames are measured. */
    mesh_config_set_long_name(&cfg, "0123456789012345678901234567890123456789");
    mesh_config_set_short_name(&cfg, "WXYZ");
    ios_client_init(&c, &cfg, IosTransportIdeal);
    ios_client_connect(&c);
    printf("  longest frame with maximal names: %u bytes\n", (unsigned)c.max_frame_len);
    ASSERT_TRUE(c.max_frame_len <= IOS_READ_MAX);
    ASSERT_EQ_INT(c.oversize_frames, 0);
}

/* A frame past the read cap is what BLEConnection turns into a disconnect. */
TEST(test_oversize_frame_disconnects_the_client) {
    IosClient c;
    MeshConfig cfg = config();
    /* Local and past PHONE_FRAME_MAX: this frame is one the device refuses to
     * queue, so it cannot be built in a buffer the device's cap sizes. */
    uint8_t big[256];
    ios_client_init(&c, &cfg, IosTransportIdeal);
    ios_client_connect(&c);
    /* Whatever Step 6 said, the link is up for this test. */
    c.outcome = IosOutcomeNotRun;
    c.stage = 3;
    /* A syntactically valid FromRadio.packet of 189 bytes: tag, two byte
     * length, 186 byte payload. */
    {
        uint8_t payload[186];
        memset(payload, 'x', sizeof(payload));
        size_t n = phone_encode_packet(payload, sizeof(payload), big, sizeof(big));
        ASSERT_EQ_INT(n, 189);
        ASSERT_TRUE(n > PHONE_FRAME_MAX);
        ASSERT_TRUE(n > IOS_READ_MAX);
        ios_device_inject(&c, big, n);
    }
    ios_drain(&c);
    ASSERT_EQ_INT(c.oversize_frames, 1);
    ASSERT_EQ_INT(c.outcome, IosOutcomeFailed);
    ASSERT_EQ_INT(c.decode_failures, 1);
}

/* The paced transport: what the phone actually sees from this app */

TEST(test_paced_flow_reproduces_the_cycle) {
    IosClient c;
    MeshConfig cfg = config();
    ios_client_init(&c, &cfg, IosTransportPaced);
    IosOutcome outcome = ios_client_connect(&c);
    printf("  paced transport, current encoder:\n");
    ios_client_report(&c);

    ASSERT_EQ_INT(c.decode_failures, 0);
    ASSERT_TRUE(c.db_gate_open);
    ASSERT_TRUE(c.failed_step == 0 || c.failed_step >= 6);
    bool supported = ios_version_supported(c.firmware_version);
    ASSERT_EQ_INT(outcome, supported ? IosOutcomeConnected : IosOutcomeFailed);

    /* The value is restated every 350ms and read every 88ms, so the phone
     * sees each frame more than once. Every duplicate is a handler call. */
    ASSERT_TRUE(c.duplicate_reads > c.frames_stage[1] / 2);

    /* Stage one lasts about 36 frames' worth of intervals. The admin replies
     * the client asks for mid batch re-arm the drain and pull a few steps
     * early, so the bound is loose. That, not the phone's own timeouts, is
     * what sets the length of a cycle. */
    uint32_t stage1 = c.stage_finished_ms[1] - c.stage_started_ms[1];
    ASSERT_TRUE(stage1 >= 30u * IOS_MODEL_DRAIN_INTERVAL_MS);
    ASSERT_TRUE(stage1 < 42u * IOS_MODEL_DRAIN_INTERVAL_MS);

    /* Every duplicate read of config.device is another handleConfig call,
     * and each one sends set_config. The phone writes the timezone several
     * times per connect because of the transport, not because it wants to. */
    printf("  set_config(tzdef) writes caused by duplicate reads: %d\n", c.sent_set_config_tzdef);
    ASSERT_TRUE(c.sent_set_config_tzdef > 1);

    /* Stage two with STAGE_TWO_REPEATS copies of NodeInfo, each read about
     * four times, is where the phone's "16 nodes" came from: it was this
     * node, counted once per read. */
    printf(
        "  stage two node count as the phone displays it: %d (from %d NodeInfo frames queued)\n",
        c.node_count,
        c.variant_count[IOS_FR_NODE_INFO] - 1);
    ASSERT_TRUE(c.node_count > 1);
}

/* A link that drops mid batch leaves the published value in place. On the
 * next connect the phone's first drain reads it before the new stage one
 * has queued anything, and processes a frame from the old session. */
TEST(test_stale_value_survives_a_reconnect_today) {
    IosClient c;
    MeshConfig cfg = config();
    uint8_t buf[16];
    ios_client_init(&c, &cfg, IosTransportPaced);

    /* Start stage one, let a few frames publish, then drop the link. */
    size_t n = ios_build_want_config(buf, sizeof(buf), IOS_NONCE_CONFIG);
    c.refresh_active = true;
    c.stage = 1;
    ios_device_write(&c, buf, n);
    ios_device_advance(&c, c.now_ms + 5 * IOS_MODEL_DRAIN_INTERVAL_MS);
    ASSERT_TRUE(c.published.len > 0);
    ASSERT_TRUE(c.q_pending > 0);
    ios_client_disconnect(&c);

    /* Time passes with no reader; the device keeps stepping until it runs
     * dry and parks the last frame it published... */
    ios_device_advance(&c, c.now_ms + 60u * IOS_MODEL_DRAIN_INTERVAL_MS);
    /* ...which, one step past empty, is a zero-length value. So a batch that
     * finishes unread is harmless. The stale case is a reconnect that comes
     * while the old batch is still stepping. */
    ASSERT_EQ_INT(c.published.len, 0);

    ios_client_init(&c, &cfg, IosTransportPaced);
    c.refresh_active = true;
    c.stage = 1;
    ios_device_write(&c, buf, n);
    ios_device_advance(&c, c.now_ms + 5 * IOS_MODEL_DRAIN_INTERVAL_MS);
    ios_client_disconnect(&c);
    /* Reconnect 2s later, retryDelay, with the old batch mid stream. */
    ios_device_advance(&c, c.now_ms + 2000);
    ASSERT_TRUE(c.q_pending > 0);
    size_t stale_len = c.published.len;
    ASSERT_TRUE(stale_len > 0);

    /* The new session's Step 2 heartbeat drain reads whatever is published. */
    int before = c.frames_total;
    ios_send_heartbeat(&c);
    ios_drain(&c);
    ASSERT_TRUE(c.frames_total > before);
    printf(
        "  frames from the previous session read by the new one: %d\n", c.frames_total - before);

    /* With the device reset on disconnect, the same reconnect reads empty. */
    ios_client_init(&c, &cfg, IosTransportPaced);
    c.refresh_active = true;
    c.stage = 1;
    ios_device_write(&c, buf, n);
    ios_device_advance(&c, c.now_ms + 5 * IOS_MODEL_DRAIN_INTERVAL_MS);
    ios_client_disconnect(&c);
    ios_device_reset(&c);
    ios_device_advance(&c, c.now_ms + 2000);
    before = c.frames_total;
    ios_send_heartbeat(&c);
    ios_drain(&c);
    ASSERT_EQ_INT(c.frames_total, before);
}

/* A received mesh packet forwarded after connect is drained on its doorbell
 * and decoded with its radio metadata. This is the shape Track B produces;
 * the encoder for it does not exist yet, so the frame is built here. */
TEST(test_forwarded_packet_is_decoded_with_radio_metadata) {
    IosClient c;
    MeshConfig cfg = config();
    uint8_t packet[128];
    uint8_t frame[160];
    PbWriter w;
    ios_client_init(&c, &cfg, IosTransportIdeal);
    ios_client_connect(&c);

    pb_writer_init(&w, packet, sizeof(packet));
    pb_write_fixed32_field_always(&w, IOS_MP_FROM, 0xAABBCCDD);
    pb_write_fixed32_field_always(&w, IOS_MP_TO, 0xFFFFFFFF);
    {
        uint8_t data[32];
        PbWriter d;
        pb_writer_init(&d, data, sizeof(data));
        pb_write_varint_field_always(&d, IOS_DATA_PORTNUM, 1);
        pb_write_string_field(&d, IOS_DATA_PAYLOAD, "hi");
        pb_write_submessage(&w, IOS_MP_DECODED, data, pb_writer_len(&d));
    }
    pb_write_fixed32_field_always(&w, IOS_MP_ID, 0x01020304);
    {
        float snr = -7.25f;
        uint32_t bits;
        memcpy(&bits, &snr, sizeof(bits));
        pb_write_fixed32_field_always(&w, IOS_MP_RX_SNR, bits);
    }
    pb_write_varint_field_always(&w, IOS_MP_HOP_LIMIT, 3);
    pb_write_varint_field_always(&w, IOS_MP_RX_RSSI, (uint64_t)(int64_t)-98);
    pb_write_varint_field_always(&w, IOS_MP_HOP_START, 5);
    size_t n = phone_encode_packet(packet, pb_writer_len(&w), frame, sizeof(frame));
    ASSERT_TRUE(n > 0);

    int before = c.packet_frames;
    ios_device_inject(&c, frame, n);
    ios_drain(&c);
    ASSERT_EQ_INT(c.packet_frames, before + 1);
    ASSERT_EQ_INT(c.last_packet.from, 0xAABBCCDD);
    ASSERT_EQ_INT(c.last_packet.portnum, 1);
    ASSERT_EQ_INT(c.last_packet.payload_len, 2);
    ASSERT_TRUE(c.last_packet.has_rx_snr && c.last_packet.rx_snr == -7.25f);
    ASSERT_TRUE(c.last_packet.has_rx_rssi && c.last_packet.rx_rssi == -98);
    ASSERT_TRUE(c.last_packet.has_hop_limit && c.last_packet.hop_limit == 3);
    ASSERT_TRUE(c.last_packet.has_hop_start && c.last_packet.hop_start == 5);
}

TEST_MAIN_BEGIN()
RUN_TEST(test_numeric_compare_matches_foundation);
RUN_TEST(test_version_2_5_0_fails_step_6);
RUN_TEST(test_version_rule_edges);
RUN_TEST(test_ideal_flow_against_real_encoder);
RUN_TEST(test_stage_one_order_as_the_client_reads_it);
RUN_TEST(test_requests_the_client_makes_during_stage_one);
RUN_TEST(test_every_frame_fits_one_read);
RUN_TEST(test_oversize_frame_disconnects_the_client);
RUN_TEST(test_paced_flow_reproduces_the_cycle);
RUN_TEST(test_stale_value_survives_a_reconnect_today);
RUN_TEST(test_forwarded_packet_is_decoded_with_radio_metadata);
TEST_MAIN_END()
