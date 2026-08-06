/* Fixed-size ring of received text messages.
 *
 * No persistence and no allocation, per the spec: this is a receiver, not a
 * message store. When it fills, the oldest message is overwritten.
 *
 * No Flipper dependencies. */
#ifndef MESSAGE_RING_H
#define MESSAGE_RING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mesh_event.h"

#define MESSAGE_RING_CAPACITY 16

typedef struct {
    uint32_t from;
    uint32_t id;
    int16_t rssi;
    int8_t snr;
    uint8_t text[MESH_TEXT_MAX];
    uint8_t text_len;
    bool text_truncated;
} MeshMessage;

typedef struct {
    MeshMessage items[MESSAGE_RING_CAPACITY];
    size_t count; /* number of valid entries, capped at capacity */
    size_t head; /* index the next write goes to */
} MessageRing;

void message_ring_init(MessageRing* ring);

/* Stores the event's text. Events that are not successfully decoded text are
 * ignored, so callers can push everything without filtering. */
void message_ring_push(MessageRing* ring, const MeshEvent* event);

size_t message_ring_count(const MessageRing* ring);

/* Index 0 is the newest message. Returns NULL if index is out of range.
 *
 * Newest-first because that is the order the UI shows them, and doing the
 * arithmetic here keeps the view free of ring buffer reasoning. */
const MeshMessage* message_ring_get(const MessageRing* ring, size_t index);

#endif
