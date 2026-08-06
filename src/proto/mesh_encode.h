/* Building a frame to transmit.
 *
 * The mirror of mesh_decode. Pure, no Flipper dependencies, no allocation, so
 * the entire transmit path except the radio itself is provable on a PC.
 *
 * This is M4 work. Nothing here is wired into the app yet, and it is not used
 * by the receive path. It exists now because it can be tested now: the
 * strongest available check is that encoding a frame reproduces, byte for
 * byte, a frame that Python generated independently. If those agree, our
 * transmit format is right, and only keying the radio remains unproven. */
#ifndef MESH_ENCODE_H
#define MESH_ENCODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mesh_channel.h"
#include "mesh_header.h"

typedef struct {
    uint32_t to; /* 0xFFFFFFFF for a broadcast */
    uint32_t from;
    uint32_t id;
    uint8_t hop_limit;
    uint8_t hop_start;
    bool want_ack;
    uint8_t channel_hash;
    const uint8_t* key; /* MESH_PSK_LEN bytes */
    const uint8_t* text;
    size_t text_len;
} MeshTxParams;

/* Encode a Data protobuf carrying a text message.
 *
 * Writes only portnum and payload, matching what the decoder reads, and emits
 * fields in ascending field number, which is what protobuf encoders
 * conventionally do and what makes byte comparison against a reference
 * possible at all.
 *
 * Returns the number of bytes written, or 0 if the buffer is too small. */
size_t mesh_encode_data(
    uint32_t portnum,
    const uint8_t* payload,
    size_t payload_len,
    uint8_t* out,
    size_t out_len);

/* Encode the 16 byte header. Returns bytes written, or 0 if out is too small. */
size_t mesh_encode_header(const MeshTxParams* params, uint8_t* out, size_t out_len);

/* Build a complete frame: header, then the AES-CTR encrypted Data protobuf.
 *
 * Returns the total frame length, or 0 on failure. A frame that would exceed
 * MESH_MAX_PAYLOAD is a failure rather than a truncation, since a truncated
 * ciphertext decrypts to garbage at the far end. */
size_t mesh_encode_frame(const MeshTxParams* params, uint8_t* out, size_t out_len);

/* Largest text that still fits in a frame, given the header and protobuf
 * overhead. Callers should use this rather than discovering the limit by
 * getting 0 back from mesh_encode_frame. */
size_t mesh_encode_max_text_len(void);

#endif
