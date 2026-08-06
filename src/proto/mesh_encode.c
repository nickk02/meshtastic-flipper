#include "mesh_encode.h"

#include <string.h>

#include "mesh_crypto.h"
#include "mesh_data.h"

#define WIRE_VARINT 0
#define WIRE_LEN    2

#define FIELD_PORTNUM 1
#define FIELD_PAYLOAD 2

/* Base 128 varint. Returns bytes written, or 0 if it would not fit. */
static size_t write_varint(uint64_t value, uint8_t* out, size_t out_len) {
    size_t written = 0;

    do {
        if(written >= out_len) return 0;
        uint8_t byte = (uint8_t)(value & 0x7F);
        value >>= 7;
        if(value != 0) byte |= 0x80;
        out[written++] = byte;
    } while(value != 0);

    return written;
}

static size_t write_tag(uint32_t field, uint8_t wire, uint8_t* out, size_t out_len) {
    return write_varint(((uint64_t)field << 3) | wire, out, out_len);
}

size_t mesh_encode_data(
    uint32_t portnum,
    const uint8_t* payload,
    size_t payload_len,
    uint8_t* out,
    size_t out_len) {
    size_t pos = 0;
    size_t n;

    if(out == NULL) return 0;
    if(payload == NULL && payload_len > 0) return 0;

    /* Field 1, portnum. proto3 omits zero-valued scalars, and matching that is
     * what lets the output be compared byte for byte against a reference
     * encoder. Meshtastic never sends portnum 0 in practice. */
    if(portnum != 0) {
        n = write_tag(FIELD_PORTNUM, WIRE_VARINT, out + pos, out_len - pos);
        if(n == 0) return 0;
        pos += n;

        n = write_varint(portnum, out + pos, out_len - pos);
        if(n == 0) return 0;
        pos += n;
    }

    /* Field 2, payload. Also omitted when empty, again matching proto3. */
    if(payload_len > 0) {
        n = write_tag(FIELD_PAYLOAD, WIRE_LEN, out + pos, out_len - pos);
        if(n == 0) return 0;
        pos += n;

        n = write_varint(payload_len, out + pos, out_len - pos);
        if(n == 0) return 0;
        pos += n;

        if(payload_len > out_len - pos) return 0;
        memcpy(out + pos, payload, payload_len);
        pos += payload_len;
    }

    return pos;
}

static void write_u32_le(uint8_t* p, uint32_t value) {
    p[0] = (uint8_t)(value & 0xFF);
    p[1] = (uint8_t)((value >> 8) & 0xFF);
    p[2] = (uint8_t)((value >> 16) & 0xFF);
    p[3] = (uint8_t)((value >> 24) & 0xFF);
}

size_t mesh_encode_header(const MeshTxParams* params, uint8_t* out, size_t out_len) {
    if(params == NULL || out == NULL) return 0;
    if(out_len < MESH_HEADER_LEN) return 0;

    write_u32_le(out + 0, params->to);
    write_u32_le(out + 4, params->from);
    write_u32_le(out + 8, params->id);

    uint8_t flags = (uint8_t)(params->hop_limit & MESH_FLAG_HOP_LIMIT_MASK);
    flags |=
        (uint8_t)((params->hop_start << MESH_FLAG_HOP_START_SHIFT) & MESH_FLAG_HOP_START_MASK);
    if(params->want_ack) flags |= MESH_FLAG_WANT_ACK_MASK;

    out[12] = flags;
    out[13] = params->channel_hash;
    /* next_hop and relay_node are zero for a packet we originate. */
    out[14] = 0;
    out[15] = 0;

    return MESH_HEADER_LEN;
}

size_t mesh_encode_max_text_len(void) {
    /* A frame is the 16 byte header plus the encrypted Data protobuf, and the
     * whole thing has to fit in MESH_MAX_PAYLOAD.
     *
     * Protobuf overhead for the largest case: 2 bytes for the portnum field,
     * 1 tag byte for the payload, and up to 2 bytes for its length varint once
     * the payload exceeds 127. */
    return MESH_MAX_PAYLOAD - MESH_HEADER_LEN - 5;
}

size_t mesh_encode_frame(const MeshTxParams* params, uint8_t* out, size_t out_len) {
    uint8_t plaintext[MESH_MAX_PAYLOAD];
    size_t plaintext_len;
    size_t total;

    if(params == NULL || out == NULL || params->key == NULL) return 0;
    if(params->text == NULL && params->text_len > 0) return 0;

    plaintext_len = mesh_encode_data(
        MESH_PORTNUM_TEXT_MESSAGE_APP,
        params->text,
        params->text_len,
        plaintext,
        sizeof(plaintext));
    if(plaintext_len == 0 && params->text_len > 0) return 0;

    total = MESH_HEADER_LEN + plaintext_len;
    if(total > MESH_MAX_PAYLOAD) return 0;
    if(total > out_len) return 0;

    if(mesh_encode_header(params, out, out_len) != MESH_HEADER_LEN) return 0;

    /* Same nonce construction as receive, and CTR is symmetric, so this is the
     * decrypt function used in the other direction. */
    mesh_crypto_xcrypt(
        params->key, params->id, params->from, plaintext, plaintext_len, out + MESH_HEADER_LEN);

    return total;
}
