/* Fixed-size table of heard nodes.
 *
 * Deliberately not a NodeDB. No persistence, no user records, no position, no
 * routing state. Just who was heard, how strongly, and how recently. The spec
 * draws the line here on purpose: NodeDB.cpp is 4,467 lines and is the
 * boundary between "receive and display" and "be a node".
 *
 * No Flipper dependencies. */
#ifndef NODE_ROSTER_H
#define NODE_ROSTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "src/model/mesh_event.h"
#include "src/proto/mesh_user.h"

#define NODE_ROSTER_CAPACITY 32

typedef struct {
    uint32_t node_num;
    int16_t rssi; /* most recent */
    int8_t snr; /* most recent */
    uint32_t packets;
    uint32_t last_seen_ms;

    /* Learned from NODEINFO_APP packets. Empty until one arrives, which is
     * why the UI falls back to the hex node number. A real device does the
     * same thing while a node is still unknown. */
    char long_name[MESH_USER_LONG_NAME_MAX];
    char short_name[MESH_USER_SHORT_NAME_MAX];
    bool has_name;

    /* hop_start minus hop_limit. Zero means a direct neighbour, which is the
     * only case where signal strength is meaningful: for anything relayed,
     * RSSI describes the last hop, not the originator. */
    uint8_t hops_away;
    bool has_hops;
} MeshNode;

typedef struct {
    MeshNode items[NODE_ROSTER_CAPACITY];
    size_t count;
} NodeRoster;

void node_roster_init(NodeRoster* roster);

/* Record that a node was heard.
 *
 * Updates the existing entry if the node is known, otherwise adds one. When
 * the table is full, the least recently heard node is evicted, since a roster
 * that stops updating once full is worse than useless.
 *
 * Accepts events that failed to decode: the sender is still worth listing,
 * and hearing an undecodable frame is exactly the sort of thing you want
 * visible. Events with no usable header (from == 0) are ignored.
 *
 * Returns true if this added a new node. */
bool node_roster_observe(NodeRoster* roster, const MeshEvent* event, uint32_t now_ms);

/* Attach a name learned from a NODEINFO_APP packet. Creates the entry if the
 * node has not been heard from yet. */
void node_roster_set_user(
    NodeRoster* roster,
    uint32_t node_num,
    const MeshUser* user,
    uint32_t now_ms);

/* What to show for a node: long name, else short name, else the low 4 hex
 * digits of its number. Never returns NULL. */
const char* node_roster_display_name(const MeshNode* node, char* scratch, size_t scratch_len);

size_t node_roster_count(const NodeRoster* roster);

/* Index 0 is the most recently heard node. Returns NULL if out of range. */
const MeshNode* node_roster_get(const NodeRoster* roster, size_t index);

#endif
