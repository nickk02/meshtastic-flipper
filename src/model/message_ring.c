#include "message_ring.h"

#include <string.h>

void message_ring_init(MessageRing* ring) {
    if(ring == NULL) return;
    memset(ring, 0, sizeof(*ring));
}

void message_ring_push(MessageRing* ring, const MeshEvent* event) {
    if(ring == NULL || event == NULL) return;
    if(event->result != MESH_OK) return;

    MeshMessage* slot = &ring->items[ring->head];
    memset(slot, 0, sizeof(*slot));

    slot->from = event->from;
    slot->id = event->id;
    slot->rssi = event->rssi;
    slot->snr = event->snr;
    slot->text_len = event->text_len;
    slot->text_truncated = event->text_truncated;
    memcpy(slot->text, event->text, event->text_len);

    ring->head = (ring->head + 1) % MESSAGE_RING_CAPACITY;
    if(ring->count < MESSAGE_RING_CAPACITY) ring->count++;
}

size_t message_ring_count(const MessageRing* ring) {
    return ring == NULL ? 0 : ring->count;
}

const MeshMessage* message_ring_get(const MessageRing* ring, size_t index) {
    if(ring == NULL || index >= ring->count) return NULL;

    /* head points at the next write, so head - 1 is the newest. Add capacity
     * before the subtraction so the unsigned arithmetic cannot wrap. */
    size_t slot = (ring->head + MESSAGE_RING_CAPACITY - 1 - index) % MESSAGE_RING_CAPACITY;
    return &ring->items[slot];
}
