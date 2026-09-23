/* The receive path, end to end: packets heard on the air reach the phone as
 * FromRadio.packet, and heard nodes reach it in stage two as NodeInfo.
 *
 * Driven through the iOS client model in ios_client.h, so each frame is
 * checked against what Meshtastic-Apple v2.7.21 does with it, and through the
 * real decode chain on over-the-air frames from gen_vectors.py. */
#include "tinytest.h"

#include "ios_client.h"
#include "mesh_channel.h"
#include "mesh_decode.h"
#include "mesh_user.h"
#include "vectors.h"

#define OWN_NODE 0x11223344u

static MeshConfig config(void) {
    MeshConfig c;
    mesh_config_defaults(&c, OWN_NODE);
    return c;
}

/* A node as the radio thread would record it: heard once, hop fields from
 * the header. */
static void hear(NodeRoster* r, uint32_t from, int8_t snr, uint32_t now_ms) {
    MeshEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.from = from;
    ev.to = 0xFFFFFFFFu;
    ev.hop_start = 3;
    ev.hop_limit = 2;
    ev.snr = snr;
    ev.rssi = -90;
    node_roster_observe(r, &ev, now_ms);
}

static void name(NodeRoster* r, uint32_t num, const char* long_name, const char* short_name) {
    MeshUser u;
    memset(&u, 0, sizeof(u));
    strncpy(u.long_name, long_name, sizeof(u.long_name) - 1);
    strncpy(u.short_name, short_name, sizeof(u.short_name) - 1);
    u.has_long_name = true;
    u.has_short_name = true;
    node_roster_set_user(r, num, &u, 5000);
}

/* Three heard nodes, one of them named. */
static void seed_three(NodeRoster* r) {
    node_roster_init(r);
    hear(r, 0xA1A1A1A1u, 6, 1000);
    hear(r, 0xB2B2B2B2u, -3, 2000);
    hear(r, 0xC3C3C3C3u, 0, 3000);
    name(r, 0xA1A1A1A1u, "Alpha Node", "ALPH");
}

/* The node_info body out of a FromRadio frame, or NULL. */
static const uint8_t* node_info_body(const uint8_t* frame, size_t len, size_t* body_len) {
    const uint8_t* body = NULL;
    if(!ios_find(frame, len, IOS_FR_NODE_INFO, NULL, &body, body_len)) return NULL;
    return body;
}

/* phone_encode_other_node_info */

TEST(test_other_node_info_named) {
    NodeRoster r;
    uint8_t out[HANDSHAKE_MAX_MESSAGE];
    const uint8_t* body;
    const uint8_t* user = NULL;
    const uint8_t* s = NULL;
    size_t body_len = 0, user_len = 0, sl = 0;
    uint64_t v = 0;

    seed_three(&r);
    const MeshNode* alpha = NULL;
    for(size_t i = 0; i < node_roster_count(&r); i++) {
        if(node_roster_get(&r, i)->node_num == 0xA1A1A1A1u) alpha = node_roster_get(&r, i);
    }
    ASSERT_TRUE(alpha != NULL);

    size_t n = phone_encode_other_node_info(alpha, 0, out, sizeof(out));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(n <= PHONE_READ_MAX);
    body = node_info_body(out, n, &body_len);
    ASSERT_TRUE(body != NULL);

    ASSERT_TRUE(ios_find(body, body_len, 1, &v, NULL, NULL));
    ASSERT_EQ_INT(v, 0xA1A1A1A1u);
    ASSERT_TRUE(ios_find(body, body_len, 2, NULL, &user, &user_len));
    ASSERT_TRUE(ios_find(user, user_len, 1, NULL, &s, &sl));
    ASSERT_EQ_INT(sl, 9);
    ASSERT_EQ_MEM(s, "!a1a1a1a1", 9);
    ASSERT_TRUE(ios_find(user, user_len, 2, NULL, &s, &sl));
    ASSERT_EQ_INT(sl, 10);
    ASSERT_EQ_MEM(s, "Alpha Node", 10);
    ASSERT_TRUE(ios_find(user, user_len, 3, NULL, &s, &sl));
    ASSERT_EQ_INT(sl, 4);
    ASSERT_EQ_MEM(s, "ALPH", 4);

    /* snr is a float on wire type 5. */
    ASSERT_TRUE(ios_find(body, body_len, 4, &v, NULL, NULL));
    float snr;
    uint32_t bits = (uint32_t)v;
    memcpy(&snr, &bits, sizeof(snr));
    ASSERT_TRUE(snr == 6.0f);
    /* last_heard 0 is omitted: the phone uses its own clock for it. */
    ASSERT_TRUE(!ios_find(body, body_len, 5, &v, NULL, NULL));
    /* hops_away is start minus limit, 3 - 2. */
    ASSERT_TRUE(ios_find(body, body_len, 9, &v, NULL, NULL));
    ASSERT_EQ_INT(v, 1);
}

TEST(test_other_node_info_unnamed_still_has_user_id) {
    MeshNode node;
    uint8_t out[HANDSHAKE_MAX_MESSAGE];
    const uint8_t* body;
    const uint8_t* user = NULL;
    const uint8_t* s = NULL;
    size_t body_len = 0, user_len = 0, sl = 0;
    uint64_t v = 0;

    memset(&node, 0, sizeof(node));
    node.node_num = 0x0000BEEFu;
    node.has_hops = true;
    node.hops_away = 0;

    size_t n = phone_encode_other_node_info(&node, 1758600000u, out, sizeof(out));
    ASSERT_TRUE(n > 0);
    body = node_info_body(out, n, &body_len);
    ASSERT_TRUE(body != NULL);
    ASSERT_TRUE(ios_find(body, body_len, 2, NULL, &user, &user_len));
    ASSERT_TRUE(ios_find(user, user_len, 1, NULL, &s, &sl));
    ASSERT_EQ_MEM(s, "!0000beef", 9);
    ASSERT_TRUE(!ios_find(user, user_len, 2, NULL, &s, &sl));
    ASSERT_TRUE(!ios_find(user, user_len, 3, NULL, &s, &sl));
    /* A direct neighbour: hops_away 0 is written, since it is known. */
    ASSERT_TRUE(ios_find(body, body_len, 9, &v, NULL, NULL));
    ASSERT_EQ_INT(v, 0);
    ASSERT_TRUE(ios_find(body, body_len, 5, &v, NULL, NULL));
    ASSERT_EQ_INT(v, 1758600000u);

    node.node_num = 0;
    ASSERT_EQ_INT(phone_encode_other_node_info(&node, 0, out, sizeof(out)), 0);
}

/* Stage one: no other nodes. The firmware skips STATE_SEND_OTHER_NODEINFOS
 * for 69420. */

TEST(test_stage_one_sends_only_own_node_info) {
    Handshake h;
    HandshakeReply reply;
    NodeRoster r;
    MeshConfig cfg = config();
    uint8_t buf[16];
    int node_infos = 0;

    seed_three(&r);
    handshake_init(&h, &cfg);
    handshake_set_roster(&h, &r);
    size_t n = ios_build_want_config(buf, sizeof(buf), IOS_NONCE_CONFIG);
    ASSERT_TRUE(handshake_handle_to_radio(&h, buf, n, &reply));
    for(size_t i = 0; i < reply.count; i++) {
        size_t bl;
        if(node_info_body(reply.messages[i].data, reply.messages[i].len, &bl)) node_infos++;
    }
    ASSERT_EQ_INT(node_infos, 1);
}

/* Stage two through the iOS model: own node plus three heard. */

TEST(test_stage_two_sends_heard_nodes) {
    IosClient c;
    NodeRoster r;
    MeshConfig cfg = config();

    seed_three(&r);
    ios_client_init(&c, &cfg, IosTransportIdeal);
    handshake_set_roster(&c.handshake, &r);
    ios_client_connect(&c);
    printf("  stage two with three heard nodes, ideal transport:\n");
    ios_client_report(&c);

    /* The client counts every NodeInfo it is handed, repeats included
     * (handleNodeInfo bumps .retrievingDatabase(nodeCount) per frame). The
     * own NodeInfo goes HANDSHAKE_STAGE_TWO_REPEATS times, each heard node
     * once. Distinct, that is the own node plus three. */
    ASSERT_EQ_INT(c.node_count, HANDSHAKE_STAGE_TWO_REPEATS + 3);
    ASSERT_EQ_INT(c.db_distinct_nodes, 4);
    ASSERT_EQ_INT(c.nodeinfo_zero_num, 0);
    ASSERT_EQ_INT(c.decode_failures, 0);
    ASSERT_EQ_INT(c.invalid_utf8, 0);
    ASSERT_EQ_INT(c.oversize_frames, 0);
    ASSERT_TRUE(c.db_gate_open);
}

TEST(test_stage_two_skips_own_node_in_roster) {
    IosClient c;
    NodeRoster r;
    MeshConfig cfg = config();

    /* The radio can hear this node's own number, from a relay echoing it. It
     * must not come back as a second, different own node. */
    seed_three(&r);
    hear(&r, OWN_NODE, 9, 4000);
    ios_client_init(&c, &cfg, IosTransportIdeal);
    handshake_set_roster(&c.handshake, &r);
    ios_client_connect(&c);
    ASSERT_EQ_INT(c.node_count, HANDSHAKE_STAGE_TWO_REPEATS + 3);
    ASSERT_EQ_INT(c.db_distinct_nodes, 4);
}

/* A full roster, paced like the real drain. The cap must keep stage two and
 * what stage one leaves behind inside the 40 slot queue. */
TEST(test_stage_two_full_roster_fits_queue) {
    IosClient c;
    NodeRoster r;
    MeshConfig cfg = config();

    node_roster_init(&r);
    for(uint32_t i = 0; i < NODE_ROSTER_CAPACITY; i++) {
        hear(&r, 0x20000000u + i, (int8_t)i, 1000 + i);
    }
    ASSERT_EQ_INT(node_roster_count(&r), NODE_ROSTER_CAPACITY);

    ios_client_init(&c, &cfg, IosTransportPaced);
    handshake_set_roster(&c.handshake, &r);
    ios_client_connect(&c);
    printf("  stage two with a full roster, paced transport:\n");
    ios_client_report(&c);

    ASSERT_EQ_INT(c.queue_refused, 0);
    ASSERT_EQ_INT(c.decode_failures, 0);
    ASSERT_TRUE(c.db_gate_open);
    ASSERT_EQ_INT(c.db_distinct_nodes, 1 + HANDSHAKE_MAX_OTHER_NODES);
}

/* phone_encode_rx_packet */

TEST(test_rx_packet_negative_rssi_is_ten_byte_varint) {
    const uint8_t payload[] = {'h', 'i'};
    PhoneRxPacket p;
    uint8_t out[PHONE_READ_MAX];
    const uint8_t* packet = NULL;
    size_t packet_len = 0;

    memset(&p, 0, sizeof(p));
    p.from = 1;
    p.to = 0xFFFFFFFFu;
    p.portnum = 1;
    p.payload = payload;
    p.payload_len = sizeof(payload);
    p.rx_rssi = -1;

    size_t n = phone_encode_rx_packet(&p, out, sizeof(out));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(ios_find(out, n, IOS_FR_PACKET, NULL, &packet, &packet_len));
    /* Tag 12 wire 0 is 0x60, then ten bytes: nine 0xff and a 0x01. */
    const uint8_t minus_one[] = {0x60, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01};
    bool found = false;
    for(size_t i = 0; i + sizeof(minus_one) <= packet_len; i++) {
        if(memcmp(packet + i, minus_one, sizeof(minus_one)) == 0) found = true;
    }
    ASSERT_TRUE(found);
    /* A zero id is still written, fixed32: tag 6 wire 5 is 0x35. */
    uint64_t v = 1;
    ASSERT_TRUE(ios_find(packet, packet_len, IOS_MP_ID, &v, NULL, NULL));
    ASSERT_EQ_INT(v, 0);
}

TEST(test_rx_packet_over_one_read_is_refused) {
    uint8_t payload[200];
    PhoneRxPacket p;
    uint8_t out[256];

    memset(payload, 'x', sizeof(payload));
    memset(&p, 0, sizeof(p));
    p.from = 0xDEADBEEFu;
    p.to = 0xFFFFFFFFu;
    p.id = 7;
    p.portnum = 1;
    p.payload = payload;
    p.rx_rssi = -98;
    p.rx_snr = -7.25f;
    p.hop_limit = 3;
    p.hop_start = 3;

    /* Find the largest payload that still fits one read, and check one byte
     * more is refused even with a large output buffer. */
    size_t fits = 0;
    size_t longest = 0;
    for(size_t len = 0; len <= sizeof(payload); len++) {
        p.payload_len = len;
        size_t n = phone_encode_rx_packet(&p, out, sizeof(out));
        if(n == 0) break;
        if(n > longest) longest = n;
        fits = len;
    }
    printf(
        "  largest payload that fits one phone read: %u bytes, frame %u bytes\n",
        (unsigned)fits,
        (unsigned)longest);
    ASSERT_TRUE(longest <= PHONE_READ_MAX);
    ASSERT_TRUE(fits > 100);
    ASSERT_TRUE(fits < sizeof(payload));
    p.payload_len = fits + 1;
    ASSERT_EQ_INT(phone_encode_rx_packet(&p, out, sizeof(out)), 0);
}

TEST(test_rx_packet_filter) {
    MeshDecoded d;
    PhoneRxPacket p;

    memset(&d, 0, sizeof(d));
    d.header.from = 5;
    d.data.portnum = MESH_PORTNUM_POSITION_APP;
    /* A clean decode of a portnum the app does not render still goes. */
    ASSERT_TRUE(phone_rx_packet_from_decoded(&d, MESH_ERR_NOT_TEXT, 0, 0, &p));
    ASSERT_EQ_INT(p.portnum, MESH_PORTNUM_POSITION_APP);
    /* Anything that did not decrypt to Data does not. */
    ASSERT_TRUE(!phone_rx_packet_from_decoded(&d, MESH_ERR_BAD_PROTOBUF, 0, 0, &p));
    ASSERT_TRUE(!phone_rx_packet_from_decoded(&d, MESH_ERR_CHANNEL_MISMATCH, 0, 0, &p));
    ASSERT_TRUE(!phone_rx_packet_from_decoded(&d, MESH_ERR_TOO_SHORT, 0, 0, &p));
    /* Portnum 0 is the wrong-key sign Router.cpp perhapsDecode rejects. */
    d.data.portnum = 0;
    ASSERT_TRUE(!phone_rx_packet_from_decoded(&d, MESH_ERR_NOT_TEXT, 0, 0, &p));
}

/* Real LongFast frames, through the real decode chain, to the phone. */

typedef struct {
    const char* label;
    const uint8_t* frame;
    size_t frame_len;
    uint32_t id;
    uint32_t portnum;
    const uint8_t* payload;
    size_t payload_len;
    uint32_t hop_limit;
    uint32_t hop_start;
} RxVector;

static void check_vector(const RxVector* v) {
    uint8_t key[MESH_PSK_LEN];
    MeshDecoded d;
    PhoneRxPacket p;
    uint8_t out[PHONE_READ_MAX];
    IosClient c;
    MeshConfig cfg = config();

    ASSERT_TRUE(v->frame_len <= PHONE_READ_MAX);

    /* The radio thread's own steps: expand the PSK, hash the channel name,
     * decode. */
    ASSERT_TRUE(mesh_channel_expand_psk(1, key));
    uint8_t hash = mesh_channel_hash("LongFast", key, MESH_PSK_LEN);
    MeshDecodeResult result = mesh_decode_frame(v->frame, v->frame_len, key, hash, &d);
    ASSERT_EQ_INT(result, MESH_OK);

    ASSERT_TRUE(phone_rx_packet_from_decoded(&d, result, -7.25f, -98, &p));
    size_t n = phone_encode_rx_packet(&p, out, sizeof(out));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(n <= PHONE_READ_MAX);
    printf(
        "  %s: over the air %u bytes, FromRadio.packet %u bytes\n",
        v->label,
        (unsigned)v->frame_len,
        (unsigned)n);

    ios_client_init(&c, &cfg, IosTransportIdeal);
    ios_client_connect(&c);
    int packets_before = c.packet_frames;
    ios_device_inject(&c, out, n);
    ios_drain(&c);

    ASSERT_EQ_INT(c.packet_frames, packets_before + 1);
    ASSERT_EQ_INT(c.decode_failures, 0);
    ASSERT_EQ_INT(c.oversize_frames, 0);
    ASSERT_TRUE(c.last_packet.decoded);
    ASSERT_EQ_INT(c.last_packet.from, 0xDEADBEEFu);
    ASSERT_EQ_INT(c.last_packet.to, 0xFFFFFFFFu);
    ASSERT_EQ_INT(c.last_packet.id, v->id);
    ASSERT_EQ_INT(c.last_packet.portnum, v->portnum);
    ASSERT_EQ_INT(c.last_packet.payload_len, v->payload_len);
    ASSERT_EQ_MEM(c.last_packet.payload, v->payload, v->payload_len);
    ASSERT_TRUE(c.last_packet.has_hop_limit);
    ASSERT_EQ_INT(c.last_packet.hop_limit, v->hop_limit);
    ASSERT_TRUE(c.last_packet.has_hop_start);
    ASSERT_EQ_INT(c.last_packet.hop_start, v->hop_start);
    ASSERT_TRUE(c.last_packet.has_rx_snr);
    ASSERT_TRUE(c.last_packet.rx_snr == -7.25f);
    ASSERT_TRUE(c.last_packet.has_rx_rssi);
    ASSERT_EQ_INT(c.last_packet.rx_rssi, -98);
}

TEST(test_real_longfast_text_reaches_phone) {
    RxVector v = {
        "text",
        RXVEC_TEXT_FRAME,
        RXVEC_TEXT_FRAME_LEN,
        RXVEC_TEXT_PACKET_ID,
        RXVEC_TEXT_PORTNUM,
        RXVEC_TEXT_PAYLOAD,
        RXVEC_TEXT_PAYLOAD_LEN,
        RXVEC_TEXT_HOP_LIMIT,
        RXVEC_TEXT_HOP_START,
    };
    ASSERT_EQ_INT(RXVEC_TEXT_FROM_NODE, 0xDEADBEEFu);
    ASSERT_EQ_INT(RXVEC_TEXT_PORTNUM, MESH_PORTNUM_TEXT_MESSAGE_APP);
    check_vector(&v);
}

TEST(test_real_longfast_nodeinfo_reaches_phone) {
    RxVector v = {
        "nodeinfo",
        RXVEC_NODEINFO_FRAME,
        RXVEC_NODEINFO_FRAME_LEN,
        RXVEC_NODEINFO_PACKET_ID,
        RXVEC_NODEINFO_PORTNUM,
        RXVEC_NODEINFO_PAYLOAD,
        RXVEC_NODEINFO_PAYLOAD_LEN,
        RXVEC_NODEINFO_HOP_LIMIT,
        RXVEC_NODEINFO_HOP_START,
    };
    ASSERT_EQ_INT(RXVEC_NODEINFO_PORTNUM, MESH_PORTNUM_NODEINFO_APP);
    check_vector(&v);

    /* The same payload is what names the node in this app's own roster. */
    MeshUser u;
    ASSERT_TRUE(mesh_user_parse(RXVEC_NODEINFO_PAYLOAD, RXVEC_NODEINFO_PAYLOAD_LEN, &u));
    ASSERT_EQ_MEM(u.long_name, "Vector Node", 12);
    ASSERT_EQ_MEM(u.short_name, "VECT", 5);
}

TEST_MAIN_BEGIN()
RUN_TEST(test_other_node_info_named);
RUN_TEST(test_other_node_info_unnamed_still_has_user_id);
RUN_TEST(test_stage_one_sends_only_own_node_info);
RUN_TEST(test_stage_two_sends_heard_nodes);
RUN_TEST(test_stage_two_skips_own_node_in_roster);
RUN_TEST(test_stage_two_full_roster_fits_queue);
RUN_TEST(test_rx_packet_negative_rssi_is_ten_byte_varint);
RUN_TEST(test_rx_packet_over_one_read_is_refused);
RUN_TEST(test_rx_packet_filter);
RUN_TEST(test_real_longfast_text_reaches_phone);
RUN_TEST(test_real_longfast_nodeinfo_reaches_phone);
TEST_MAIN_END()
