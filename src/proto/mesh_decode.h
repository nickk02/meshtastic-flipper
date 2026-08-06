/* Full receive path for one frame off the air.
 *
 * This is the seam M3's radio thread calls into. No Flipper dependencies, so
 * the whole decode chain is proven on a PC before any radio exists. */
#ifndef MESH_DECODE_H
#define MESH_DECODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mesh_channel.h"
#include "mesh_data.h"
#include "mesh_header.h"

/* Failure reasons.
 *
 * These are the buckets the counters view tallies, so each one localizes the
 * fault to a single layer. That histogram is what substitutes for a logic
 * analyzer when the radio misbehaves. CRC failures are reported by the radio
 * and never reach here. */
typedef enum {
    MESH_OK = 0,
    MESH_ERR_TOO_SHORT,
    MESH_ERR_CHANNEL_MISMATCH,
    MESH_ERR_BAD_PROTOBUF,
    MESH_ERR_NOT_TEXT,
} MeshDecodeResult;

/* Number of values in MeshDecodeResult, for sizing counter arrays. Kept as a
 * define rather than an enum member so switch statements over the enum stay
 * exhaustive and the compiler keeps warning about unhandled cases. */
#define MESH_RESULT_COUNT 5

typedef struct {
    MeshHeader header;
    MeshData data;
    uint8_t plaintext[MESH_MAX_PAYLOAD];
    size_t plaintext_len;
} MeshDecoded;

/* Parse header, match channel hash, decrypt, decode Data.
 *
 * out->data.payload points into out->plaintext, so MeshDecoded must outlive
 * any use of the payload. Nothing is allocated.
 *
 * On any failure the header is still populated when it could be parsed, so
 * the caller can show who a frame came from even when its contents are not
 * readable. */
MeshDecodeResult mesh_decode_frame(
    const uint8_t* frame,
    size_t len,
    const uint8_t key[MESH_PSK_LEN],
    uint8_t expected_channel_hash,
    MeshDecoded* out);

/* Stable short label for display and logging. Never NULL. */
const char* mesh_decode_result_name(MeshDecodeResult r);

#endif
