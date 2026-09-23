/* The heartbeat, as the iOS client sends it during connect.
 *
 * AccessoryManager+Connect.swift sends one at Step 2 and one at Step 4. A real
 * node answers each with a queueStatus (PhoneAPI.cpp, heartbeatReceived). Here
 * the Step 2 answer is discarded by the stage one queue reset that follows
 * it, which the phone does not mind: on BLE nothing waits for a queueStatus
 * (BLETransport.requiresPeriodicHeartbeat is false). The Step 4 answer is
 * read during stage two. */
#include "tinytest.h"

#include "ios_client.h"

static MeshConfig config(void) {
    MeshConfig c;
    mesh_config_defaults(&c, 0x11223344);
    return c;
}

TEST(test_each_connect_heartbeat_gets_a_queue_status) {
    IosClient c;
    MeshConfig cfg = config();
    ios_client_init(&c, &cfg, IosTransportIdeal);
    ios_client_connect(&c);
    printf("  ideal transport, heartbeat answered:\n");
    ios_client_report(&c);

    ASSERT_EQ_INT(c.sent_heartbeats, 2);
    ASSERT_TRUE(c.queue_status_frames >= 1);
    ASSERT_EQ_INT(c.to_radio_not_understood, 0);
    ASSERT_EQ_INT(c.decode_failures, 0);
    ASSERT_EQ_INT(c.unknown_variants, 0);
    /* The flow still gets past both stages. */
    ASSERT_TRUE(c.failed_step == 0 || c.failed_step >= 6);
}

TEST(test_nonce_one_heartbeat_gets_nothing) {
    IosClient c;
    MeshConfig cfg = config();
    uint8_t buf[16];
    ios_client_init(&c, &cfg, IosTransportIdeal);

    size_t n = ios_build_heartbeat(buf, sizeof(buf), 1);
    ios_device_write(&c, buf, n);
    ios_drain(&c);
    ASSERT_EQ_INT(c.queue_status_frames, 0);
    ASSERT_EQ_INT(c.frames_total, 0);
    ASSERT_EQ_INT(c.to_radio_not_understood, 0);

    n = ios_build_heartbeat(buf, sizeof(buf), 2);
    ios_device_write(&c, buf, n);
    ios_drain(&c);
    ASSERT_EQ_INT(c.queue_status_frames, 1);
}

TEST_MAIN_BEGIN()
RUN_TEST(test_each_connect_heartbeat_gets_a_queue_status);
RUN_TEST(test_nonce_one_heartbeat_gets_nothing);
TEST_MAIN_END()
