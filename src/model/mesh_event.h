/* One received frame, reduced to what the UI needs.
 *
 * This is the unit that crosses from the radio thread to the GUI thread, so it
 * is deliberately compact. A MeshDecoded carries a full 255 byte plaintext
 * buffer, which at a queue depth of 8 would cost several kilobytes for data
 * nobody displays. The raw frame is kept once in app state for the hex view
 * instead of once per queued item.
 *
 * No Flipper dependencies. */
#ifndef MESH_EVENT_H
#define MESH_EVENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mesh_decode.h"

/* Meshtastic allows longer text, but the Flipper screen is 128px wide and this
 * is a receiver, not an archive. Longer messages are truncated for display and
 * flagged, rather than silently cut. */
#define MESH_TEXT_MAX 64

typedef struct {
    uint32_t from;
    uint32_t to;
    uint32_t id;
    uint8_t hop_limit;
    uint8_t hop_start;

    /* Radio metadata. RSSI in dBm, SNR in dB. Both are signed and both are
     * meaningful only when the frame actually came off the air, so a
     * simulated source leaves them at 0. */
    int16_t rssi;
    int8_t snr;

    MeshDecodeResult result;

    uint8_t text[MESH_TEXT_MAX];
    uint8_t text_len;
    bool text_truncated;
} MeshEvent;

/* Fill a MeshEvent from a decode result. Copies at most MESH_TEXT_MAX bytes of
 * payload and sets text_truncated when there was more.
 *
 * Safe to call for a failed decode: the header fields are still populated when
 * the header parsed, so the UI can show who sent an undecodable frame. */
void mesh_event_from_decoded(
    MeshEvent* out,
    const MeshDecoded* decoded,
    MeshDecodeResult result,
    int16_t rssi,
    int8_t snr);

#endif
