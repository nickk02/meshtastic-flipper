#include "tinytest.h"

#include "mesh_channel.h"
#include "mesh_event.h"
#include "message_ring.h"
#include "node_roster.h"
#include "src/proto/mesh_user.h"
#include "vectors.h"

static void decode_vec0(MeshDecoded* out, MeshDecodeResult* result) {
    uint8_t key[MESH_PSK_LEN];
    mesh_channel_expand_psk(1, key);
    *result = mesh_decode_frame(VEC0_FRAME, VEC0_FRAME_LEN, key, VEC0_CHANNEL_HASH, out);
}

static MeshEvent make_event(uint32_t from, const char* text, int16_t rssi, int8_t snr) {
    MeshEvent e;
    memset(&e, 0, sizeof(e));
    e.from = from;
    e.result = MESH_OK;
    e.portnum = MESH_PORTNUM_TEXT_MESSAGE_APP;
    e.rssi = rssi;
    e.snr = snr;
    e.text_len = (uint8_t)strlen(text);
    memcpy(e.text, text, e.text_len);
    return e;
}

/* mesh_event */

TEST(test_event_from_real_decode) {
    MeshDecoded decoded;
    MeshDecodeResult result;
    MeshEvent e;

    decode_vec0(&decoded, &result);
    ASSERT_EQ_INT(result, MESH_OK);

    mesh_event_from_decoded(&e, &decoded, result, -95, 7);
    ASSERT_EQ_INT(e.from, VEC0_FROM_NODE);
    ASSERT_EQ_INT(e.to, VEC0_TO_NODE);
    ASSERT_EQ_INT(e.id, VEC0_PACKET_ID);
    ASSERT_EQ_INT(e.hop_limit, VEC0_HOP_LIMIT);
    ASSERT_EQ_INT(e.hop_start, VEC0_HOP_START);
    ASSERT_EQ_INT(e.rssi, -95);
    ASSERT_EQ_INT(e.snr, 7);
    ASSERT_EQ_INT(e.text_len, VEC0_TEXT_LEN);
    ASSERT_EQ_MEM(e.text, VEC0_TEXT, VEC0_TEXT_LEN);
    ASSERT_TRUE(!e.text_truncated);
}

TEST(test_event_keeps_header_on_failed_decode) {
    /* An undecodable frame should still say who sent it. */
    MeshDecoded decoded;
    MeshEvent e;
    uint8_t key[MESH_PSK_LEN];

    mesh_channel_expand_psk(1, key);
    MeshDecodeResult r = mesh_decode_frame(
        VEC0_FRAME, VEC0_FRAME_LEN, key, (uint8_t)(VEC0_CHANNEL_HASH ^ 0xFF), &decoded);
    ASSERT_EQ_INT(r, MESH_ERR_CHANNEL_MISMATCH);

    mesh_event_from_decoded(&e, &decoded, r, -110, -3);
    ASSERT_EQ_INT(e.from, VEC0_FROM_NODE);
    ASSERT_EQ_INT(e.result, MESH_ERR_CHANNEL_MISMATCH);
    ASSERT_EQ_INT(e.text_len, 0);
}

TEST(test_event_truncates_long_text) {
    MeshDecoded decoded;
    MeshDecodeResult result;
    MeshEvent e;
    uint8_t key[MESH_PSK_LEN];

    mesh_channel_expand_psk(1, key);
    result = mesh_decode_frame(VEC2_FRAME, VEC2_FRAME_LEN, key, VEC2_CHANNEL_HASH, &decoded);
    ASSERT_EQ_INT(result, MESH_OK);
    ASSERT_TRUE(VEC2_TEXT_LEN > MESH_TEXT_MAX);

    mesh_event_from_decoded(&e, &decoded, result, 0, 0);
    ASSERT_EQ_INT(e.text_len, MESH_TEXT_MAX);
    ASSERT_TRUE(e.text_truncated);
    ASSERT_EQ_MEM(e.text, VEC2_TEXT, MESH_TEXT_MAX);
}

TEST(test_event_handles_null_decoded) {
    MeshEvent e;
    mesh_event_from_decoded(&e, NULL, MESH_ERR_TOO_SHORT, -100, 0);
    ASSERT_EQ_INT(e.from, 0);
    ASSERT_EQ_INT(e.text_len, 0);
    ASSERT_EQ_INT(e.result, MESH_ERR_TOO_SHORT);
}

/* message_ring */

TEST(test_ring_starts_empty) {
    MessageRing ring;
    message_ring_init(&ring);
    ASSERT_EQ_INT(message_ring_count(&ring), 0);
    ASSERT_TRUE(message_ring_get(&ring, 0) == NULL);
}

TEST(test_ring_returns_newest_first) {
    MessageRing ring;
    message_ring_init(&ring);

    MeshEvent a = make_event(1, "first", -90, 5);
    MeshEvent b = make_event(2, "second", -80, 6);
    message_ring_push(&ring, &a);
    message_ring_push(&ring, &b);

    ASSERT_EQ_INT(message_ring_count(&ring), 2);
    ASSERT_EQ_INT(message_ring_get(&ring, 0)->from, 2);
    ASSERT_EQ_INT(message_ring_get(&ring, 1)->from, 1);
    ASSERT_TRUE(message_ring_get(&ring, 2) == NULL);
}

TEST(test_ring_preserves_payload) {
    MessageRing ring;
    message_ring_init(&ring);
    MeshEvent a = make_event(0xAABBCCDD, "hello mesh", -77, 9);
    message_ring_push(&ring, &a);

    const MeshMessage* m = message_ring_get(&ring, 0);
    ASSERT_EQ_INT(m->from, 0xAABBCCDD);
    ASSERT_EQ_INT(m->rssi, -77);
    ASSERT_EQ_INT(m->snr, 9);
    ASSERT_EQ_INT(m->text_len, 10);
    ASSERT_EQ_MEM(m->text, "hello mesh", 10);
}

TEST(test_ring_overwrites_oldest_when_full) {
    MessageRing ring;
    message_ring_init(&ring);

    for(uint32_t i = 0; i < MESSAGE_RING_CAPACITY + 3; i++) {
        MeshEvent e = make_event(i + 1, "x", 0, 0);
        message_ring_push(&ring, &e);
    }

    ASSERT_EQ_INT(message_ring_count(&ring), MESSAGE_RING_CAPACITY);
    /* Newest is the last pushed. */
    ASSERT_EQ_INT(message_ring_get(&ring, 0)->from, MESSAGE_RING_CAPACITY + 3);
    /* Oldest surviving is 4, since 1 through 3 were overwritten. */
    ASSERT_EQ_INT(message_ring_get(&ring, MESSAGE_RING_CAPACITY - 1)->from, 4);
    ASSERT_TRUE(message_ring_get(&ring, MESSAGE_RING_CAPACITY) == NULL);
}

TEST(test_ring_ignores_failed_decodes) {
    MessageRing ring;
    message_ring_init(&ring);

    MeshEvent bad = make_event(1, "should not appear", 0, 0);
    bad.result = MESH_ERR_BAD_PROTOBUF;
    message_ring_push(&ring, &bad);

    ASSERT_EQ_INT(message_ring_count(&ring), 0);
}

TEST(test_ring_tolerates_null) {
    MessageRing ring;
    message_ring_init(&ring);
    message_ring_push(&ring, NULL);
    message_ring_push(NULL, NULL);
    ASSERT_EQ_INT(message_ring_count(NULL), 0);
    ASSERT_TRUE(message_ring_get(NULL, 0) == NULL);
}

/* node_roster */

TEST(test_roster_starts_empty) {
    NodeRoster r;
    node_roster_init(&r);
    ASSERT_EQ_INT(node_roster_count(&r), 0);
    ASSERT_TRUE(node_roster_get(&r, 0) == NULL);
}

TEST(test_roster_adds_and_updates) {
    NodeRoster r;
    node_roster_init(&r);

    MeshEvent a = make_event(0x1111, "x", -90, 5);
    ASSERT_TRUE(node_roster_observe(&r, &a, 1000));
    ASSERT_EQ_INT(node_roster_count(&r), 1);

    /* Same node again updates rather than duplicating. */
    MeshEvent b = make_event(0x1111, "y", -70, 8);
    ASSERT_TRUE(!node_roster_observe(&r, &b, 2000));
    ASSERT_EQ_INT(node_roster_count(&r), 1);

    const MeshNode* n = node_roster_get(&r, 0);
    ASSERT_EQ_INT(n->node_num, 0x1111);
    ASSERT_EQ_INT(n->packets, 2);
    ASSERT_EQ_INT(n->rssi, -70);
    ASSERT_EQ_INT(n->snr, 8);
    ASSERT_EQ_INT(n->last_seen_ms, 2000);
}

TEST(test_roster_orders_most_recent_first) {
    NodeRoster r;
    node_roster_init(&r);

    MeshEvent a = make_event(0xAAA, "x", 0, 0);
    MeshEvent b = make_event(0xBBB, "x", 0, 0);
    MeshEvent c = make_event(0xCCC, "x", 0, 0);
    node_roster_observe(&r, &a, 100);
    node_roster_observe(&r, &b, 300);
    node_roster_observe(&r, &c, 200);

    ASSERT_EQ_INT(node_roster_get(&r, 0)->node_num, 0xBBB);
    ASSERT_EQ_INT(node_roster_get(&r, 1)->node_num, 0xCCC);
    ASSERT_EQ_INT(node_roster_get(&r, 2)->node_num, 0xAAA);
    ASSERT_TRUE(node_roster_get(&r, 3) == NULL);
}

TEST(test_roster_reorders_after_update) {
    NodeRoster r;
    node_roster_init(&r);

    MeshEvent a = make_event(0xAAA, "x", 0, 0);
    MeshEvent b = make_event(0xBBB, "x", 0, 0);
    node_roster_observe(&r, &a, 100);
    node_roster_observe(&r, &b, 200);
    ASSERT_EQ_INT(node_roster_get(&r, 0)->node_num, 0xBBB);

    node_roster_observe(&r, &a, 300);
    ASSERT_EQ_INT(node_roster_get(&r, 0)->node_num, 0xAAA);
    ASSERT_EQ_INT(node_roster_get(&r, 1)->node_num, 0xBBB);
}

TEST(test_roster_every_index_is_reachable_and_unique) {
    /* Guards the ranking logic: with equal timestamps the order must still be
       total, with no duplicates and no gaps. */
    NodeRoster r;
    node_roster_init(&r);

    for(uint32_t i = 0; i < 5; i++) {
        MeshEvent e = make_event(0x100 + i, "x", 0, 0);
        node_roster_observe(&r, &e, 500); /* all identical timestamps */
    }

    uint32_t seen[5] = {0};
    for(size_t i = 0; i < 5; i++) {
        const MeshNode* n = node_roster_get(&r, i);
        ASSERT_TRUE(n != NULL);
        for(size_t j = 0; j < i; j++)
            ASSERT_TRUE(seen[j] != n->node_num);
        seen[i] = n->node_num;
    }
}

TEST(test_roster_evicts_least_recently_heard_when_full) {
    NodeRoster r;
    node_roster_init(&r);

    for(uint32_t i = 0; i < NODE_ROSTER_CAPACITY; i++) {
        MeshEvent e = make_event(0x1000 + i, "x", 0, 0);
        node_roster_observe(&r, &e, 1000 + i);
    }
    ASSERT_EQ_INT(node_roster_count(&r), NODE_ROSTER_CAPACITY);

    /* 0x1000 was heard longest ago, so it should be the one to go. */
    MeshEvent fresh = make_event(0xDEAD, "x", 0, 0);
    ASSERT_TRUE(node_roster_observe(&r, &fresh, 9999));
    ASSERT_EQ_INT(node_roster_count(&r), NODE_ROSTER_CAPACITY);
    ASSERT_EQ_INT(node_roster_get(&r, 0)->node_num, 0xDEAD);

    for(size_t i = 0; i < node_roster_count(&r); i++) {
        ASSERT_TRUE(node_roster_get(&r, i)->node_num != 0x1000);
    }
}

TEST(test_roster_records_failed_decodes) {
    /* Hearing a node you cannot decode is worth showing. */
    NodeRoster r;
    node_roster_init(&r);

    MeshEvent e = make_event(0x2222, "", -100, 2);
    e.result = MESH_ERR_CHANNEL_MISMATCH;
    ASSERT_TRUE(node_roster_observe(&r, &e, 50));
    ASSERT_EQ_INT(node_roster_count(&r), 1);
}

TEST(test_roster_ignores_unparseable_sender) {
    NodeRoster r;
    node_roster_init(&r);

    MeshEvent e = make_event(0, "", 0, 0);
    e.result = MESH_ERR_TOO_SHORT;
    ASSERT_TRUE(!node_roster_observe(&r, &e, 10));
    ASSERT_EQ_INT(node_roster_count(&r), 0);
}

TEST(test_roster_tolerates_null) {
    ASSERT_TRUE(!node_roster_observe(NULL, NULL, 0));
    ASSERT_EQ_INT(node_roster_count(NULL), 0);
    ASSERT_TRUE(node_roster_get(NULL, 0) == NULL);
}

/* NODEINFO routing */

TEST(test_ring_ignores_nodeinfo) {
    /* A NODEINFO decodes cleanly but is not a chat message. Without the
       portnum check it would appear in the message list as raw protobuf. */
    MessageRing ring;
    message_ring_init(&ring);

    MeshEvent e = make_event(1, "not a message", 0, 0);
    e.portnum = MESH_PORTNUM_NODEINFO_APP;
    message_ring_push(&ring, &e);

    ASSERT_EQ_INT(message_ring_count(&ring), 0);
}

TEST(test_roster_learns_names_from_user) {
    NodeRoster r;
    MeshUser u;
    node_roster_init(&r);
    memset(&u, 0, sizeof(u));
    strcpy(u.long_name, "Base Station");
    strcpy(u.short_name, "BASE");
    u.has_long_name = true;
    u.has_short_name = true;

    node_roster_set_user(&r, 0x1234, &u, 500);
    ASSERT_EQ_INT(node_roster_count(&r), 1);

    const MeshNode* n = node_roster_get(&r, 0);
    ASSERT_TRUE(n->has_name);
    ASSERT_EQ_MEM(n->long_name, "Base Station", 12);
    ASSERT_EQ_MEM(n->short_name, "BASE", 4);
}

TEST(test_roster_creates_entry_for_unheard_node) {
    /* A NODEINFO can arrive for a node we have not otherwise heard. It should
       still be listed. */
    NodeRoster r;
    MeshUser u;
    node_roster_init(&r);
    memset(&u, 0, sizeof(u));
    strcpy(u.short_name, "NEW");
    u.has_short_name = true;

    node_roster_set_user(&r, 0xABCD, &u, 100);
    ASSERT_EQ_INT(node_roster_count(&r), 1);
    ASSERT_EQ_INT(node_roster_get(&r, 0)->node_num, 0xABCD);
}

TEST(test_display_name_prefers_long_then_short_then_hex) {
    NodeRoster r;
    MeshUser u;
    char scratch[16];
    node_roster_init(&r);

    /* No name yet: falls back to the low hex digits. */
    MeshEvent e = make_event(0xDEAD1234, "x", 0, 0);
    node_roster_observe(&r, &e, 10);
    ASSERT_EQ_MEM(
        node_roster_display_name(node_roster_get(&r, 0), scratch, sizeof(scratch)), "1234", 4);

    /* Short name only. */
    memset(&u, 0, sizeof(u));
    strcpy(u.short_name, "SHRT");
    u.has_short_name = true;
    node_roster_set_user(&r, 0xDEAD1234, &u, 20);
    ASSERT_EQ_MEM(
        node_roster_display_name(node_roster_get(&r, 0), scratch, sizeof(scratch)), "SHRT", 4);

    /* Long name wins once it arrives. */
    memset(&u, 0, sizeof(u));
    strcpy(u.long_name, "Long Name Here");
    u.has_long_name = true;
    node_roster_set_user(&r, 0xDEAD1234, &u, 30);
    ASSERT_EQ_MEM(
        node_roster_display_name(node_roster_get(&r, 0), scratch, sizeof(scratch)),
        "Long Name Here",
        14);
}

TEST(test_roster_records_hops_away) {
    /* hop_start minus hop_limit is how many relays a frame crossed. */
    NodeRoster r;
    node_roster_init(&r);

    MeshEvent direct = make_event(0x111, "x", 0, 0);
    direct.hop_start = 3;
    direct.hop_limit = 3;
    node_roster_observe(&r, &direct, 10);

    MeshEvent relayed = make_event(0x222, "x", 0, 0);
    relayed.hop_start = 3;
    relayed.hop_limit = 1;
    node_roster_observe(&r, &relayed, 20);

    const MeshNode* a = NULL;
    const MeshNode* b = NULL;
    for(size_t i = 0; i < node_roster_count(&r); i++) {
        const MeshNode* n = node_roster_get(&r, i);
        if(n->node_num == 0x111) a = n;
        if(n->node_num == 0x222) b = n;
    }
    ASSERT_TRUE(a != NULL && b != NULL);
    ASSERT_TRUE(a->has_hops);
    ASSERT_EQ_INT(a->hops_away, 0);
    ASSERT_EQ_INT(b->hops_away, 2);
}

TEST(test_roster_hops_absent_when_header_carried_none) {
    /* hop_start of 0 means the frame never carried the field, which is not
       the same as a confirmed direct neighbour. */
    NodeRoster r;
    node_roster_init(&r);
    MeshEvent e = make_event(0x333, "x", 0, 0);
    e.hop_start = 0;
    e.hop_limit = 0;
    node_roster_observe(&r, &e, 10);
    ASSERT_TRUE(!node_roster_get(&r, 0)->has_hops);
}

TEST_MAIN_BEGIN()
RUN_TEST(test_event_from_real_decode);
RUN_TEST(test_event_keeps_header_on_failed_decode);
RUN_TEST(test_event_truncates_long_text);
RUN_TEST(test_event_handles_null_decoded);
RUN_TEST(test_ring_starts_empty);
RUN_TEST(test_ring_returns_newest_first);
RUN_TEST(test_ring_preserves_payload);
RUN_TEST(test_ring_overwrites_oldest_when_full);
RUN_TEST(test_ring_ignores_failed_decodes);
RUN_TEST(test_ring_tolerates_null);
RUN_TEST(test_roster_starts_empty);
RUN_TEST(test_roster_adds_and_updates);
RUN_TEST(test_roster_orders_most_recent_first);
RUN_TEST(test_roster_reorders_after_update);
RUN_TEST(test_roster_every_index_is_reachable_and_unique);
RUN_TEST(test_roster_evicts_least_recently_heard_when_full);
RUN_TEST(test_roster_records_failed_decodes);
RUN_TEST(test_roster_ignores_unparseable_sender);
RUN_TEST(test_roster_tolerates_null);
RUN_TEST(test_ring_ignores_nodeinfo);
RUN_TEST(test_roster_learns_names_from_user);
RUN_TEST(test_roster_creates_entry_for_unheard_node);
RUN_TEST(test_display_name_prefers_long_then_short_then_hex);
RUN_TEST(test_roster_records_hops_away);
RUN_TEST(test_roster_hops_absent_when_header_carried_none);
TEST_MAIN_END()
