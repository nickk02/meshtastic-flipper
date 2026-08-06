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

#define NODE_ROSTER_CAPACITY 32

typedef struct {
    uint32_t node_num;
    int16_t rssi; /* most recent */
    int8_t snr; /* most recent */
    uint32_t packets;
    uint32_t last_seen_ms;
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

size_t node_roster_count(const NodeRoster* roster);

/* Index 0 is the most recently heard node. Returns NULL if out of range. */
const MeshNode* node_roster_get(const NodeRoster* roster, size_t index);

#endif
