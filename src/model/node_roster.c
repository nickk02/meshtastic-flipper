#include "node_roster.h"

#include <string.h>

void node_roster_init(NodeRoster* roster) {
    if(roster == NULL) return;
    memset(roster, 0, sizeof(*roster));
}

static MeshNode* find(NodeRoster* roster, uint32_t node_num) {
    for(size_t i = 0; i < roster->count; i++) {
        if(roster->items[i].node_num == node_num) return &roster->items[i];
    }
    return NULL;
}

static MeshNode* oldest(NodeRoster* roster) {
    MeshNode* found = &roster->items[0];
    for(size_t i = 1; i < roster->count; i++) {
        if(roster->items[i].last_seen_ms < found->last_seen_ms) {
            found = &roster->items[i];
        }
    }
    return found;
}

bool node_roster_observe(NodeRoster* roster, const MeshEvent* event, uint32_t now_ms) {
    if(roster == NULL || event == NULL) return false;
    /* Node number 0 is not a valid sender and shows up when the header could
     * not be parsed at all. */
    if(event->from == 0) return false;

    MeshNode* node = find(roster, event->from);
    bool added = false;

    if(node == NULL) {
        if(roster->count < NODE_ROSTER_CAPACITY) {
            node = &roster->items[roster->count];
            roster->count++;
        } else {
            node = oldest(roster);
        }
        memset(node, 0, sizeof(*node));
        node->node_num = event->from;
        added = true;
    }

    node->rssi = event->rssi;
    node->snr = event->snr;
    node->last_seen_ms = now_ms;
    node->packets++;
    return added;
}

size_t node_roster_count(const NodeRoster* roster) {
    return roster == NULL ? 0 : roster->count;
}

const MeshNode* node_roster_get(const NodeRoster* roster, size_t index) {
    if(roster == NULL || index >= roster->count) return NULL;

    /* Sorted on read rather than kept sorted on write, because eviction and
     * update both disturb the order and maintaining it through those is more
     * code than this.
     *
     * Ranking is by last_seen_ms descending, ties broken by array index so the
     * order is total and stable. For each candidate, count how many entries
     * rank ahead of it; the one with exactly `index` ahead is the answer.
     *
     * Quadratic, but the table holds at most 32 entries, so this is about a
     * thousand integer comparisons per redraw. Being obviously correct is
     * worth more here than being clever. */
    for(size_t i = 0; i < roster->count; i++) {
        size_t ahead = 0;
        for(size_t j = 0; j < roster->count; j++) {
            if(j == i) continue;
            bool j_ranks_first =
                roster->items[j].last_seen_ms > roster->items[i].last_seen_ms ||
                (roster->items[j].last_seen_ms == roster->items[i].last_seen_ms && j < i);
            if(j_ranks_first) ahead++;
        }
        if(ahead == index) return &roster->items[i];
    }
    return NULL;
}
