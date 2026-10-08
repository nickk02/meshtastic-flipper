/* QUEUE_DEPTH against what the device actually queues.
 *
 * meshtastic_service.c is Flipper-only, so its queue cannot run here. The
 * iOS client model in ios_client.h keeps a queue of the same depth in front of
 * the real handshake, which is enough to check the arithmetic: stage one is
 * queued in one go, and anything else the device builds before the phone has
 * read it all lands behind it. */
#include "tinytest.h"

#include "ios_client.h"

/* The depth the service had before this change. */
#define OLD_QUEUE_DEPTH 40

/* Frames queued on top of stage one in the stress case, standing in for the
 * two admin replies the phone asks for mid-drain, a queueStatus per heartbeat
 * once that change lands, and a few received packets being forwarded. The
 * ideal transport reads between writes, so a full connect never piles them up
 * this way; a slow reader can. */
#define EXTRA_FRAMES 6

static MeshConfig config(void) {
    MeshConfig c;
    mesh_config_defaults(&c, 0x11223344);
    return c;
}

static int connect_with_depth(size_t depth) {
    IosClient c;
    MeshConfig cfg = config();
    ios_client_init(&c, &cfg, IosTransportIdeal);
    c.queue_depth = depth;
    ios_client_connect(&c);
    printf("  ideal transport, queue depth %u:\n", (unsigned)depth);
    ios_client_report(&c);
    return c.queue_refused;
}

/* Stage one queued in full, then EXTRA_FRAMES more before the phone reads
 * any of it. Returns the refusals; *stage_one gets the stage one frame count. */
static int stage_one_plus_extra(size_t depth, size_t* stage_one) {
    IosClient c;
    MeshConfig cfg = config();
    uint8_t buf[HANDSHAKE_MAX_MESSAGE];
    size_t n;

    ios_client_init(&c, &cfg, IosTransportIdeal);
    c.queue_depth = depth;

    n = ios_build_want_config(buf, sizeof(buf), IOS_NONCE_CONFIG);
    ios_device_write(&c, buf, n);
    *stage_one = c.q_pending;

    n = phone_encode_config_complete(0, buf, sizeof(buf));
    for(int i = 0; i < EXTRA_FRAMES; i++) {
        ios_device_inject(&c, buf, n);
    }
    printf(
        "  depth %u: stage one %u frames, %d extra, %u pending, %d refused\n",
        (unsigned)depth,
        (unsigned)*stage_one,
        EXTRA_FRAMES,
        (unsigned)c.q_pending,
        c.queue_refused);
    return c.queue_refused;
}

TEST(test_harness_default_is_the_new_depth) {
    ASSERT_EQ_INT(IOS_MODEL_QUEUE_DEPTH, 64);
}

TEST(test_ideal_connect_refuses_nothing_at_either_depth) {
    ASSERT_EQ_INT(connect_with_depth(OLD_QUEUE_DEPTH), 0);
    ASSERT_EQ_INT(connect_with_depth(IOS_MODEL_QUEUE_DEPTH), 0);
}

TEST(test_stage_one_is_36_frames) {
    size_t stage_one = 0;
    stage_one_plus_extra(IOS_MODEL_QUEUE_DEPTH, &stage_one);
    ASSERT_EQ_INT((int)stage_one, 36);
}

TEST(test_old_depth_refuses_a_burst_behind_stage_one) {
    size_t stage_one = 0;
    int refused = stage_one_plus_extra(OLD_QUEUE_DEPTH, &stage_one);
    ASSERT_TRUE(refused >= 1);
    ASSERT_EQ_INT(refused, (int)(stage_one + EXTRA_FRAMES - OLD_QUEUE_DEPTH));
}

TEST(test_new_depth_holds_a_burst_behind_stage_one) {
    size_t stage_one = 0;
    int refused = stage_one_plus_extra(IOS_MODEL_QUEUE_DEPTH, &stage_one);
    ASSERT_EQ_INT(refused, 0);
}

TEST_MAIN_BEGIN()
RUN_TEST(test_harness_default_is_the_new_depth);
RUN_TEST(test_ideal_connect_refuses_nothing_at_either_depth);
RUN_TEST(test_stage_one_is_36_frames);
RUN_TEST(test_old_depth_refuses_a_burst_behind_stage_one);
RUN_TEST(test_new_depth_holds_a_burst_behind_stage_one);
TEST_MAIN_END()
