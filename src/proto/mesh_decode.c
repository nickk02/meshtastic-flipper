#include "mesh_decode.h"

#include <string.h>

#include "mesh_crypto.h"

MeshDecodeResult mesh_decode_frame(
    const uint8_t* frame,
    size_t len,
    const uint8_t key[MESH_PSK_LEN],
    uint8_t expected_channel_hash,
    MeshDecoded* out) {
    size_t payload_len;

    if(out == NULL || key == NULL) return MESH_ERR_TOO_SHORT;

    memset(&out->header, 0, sizeof(out->header));
    out->plaintext_len = 0;
    out->data.portnum = 0;
    out->data.payload = NULL;
    out->data.payload_len = 0;

    if(!mesh_header_parse(frame, len, &out->header)) return MESH_ERR_TOO_SHORT;

    payload_len = len - MESH_HEADER_LEN;
    if(payload_len > MESH_MAX_PAYLOAD) return MESH_ERR_TOO_SHORT;

    /* The channel byte is a one byte hint, so this rejects cheaply but does
       not prove ownership. A collision still has to survive decryption and
       protobuf parsing. */
    if(out->header.channel != expected_channel_hash) {
        return MESH_ERR_CHANNEL_MISMATCH;
    }

    mesh_crypto_xcrypt(
        key,
        out->header.id,
        out->header.from,
        frame + MESH_HEADER_LEN,
        payload_len,
        out->plaintext);
    out->plaintext_len = payload_len;

    if(!mesh_data_parse(out->plaintext, out->plaintext_len, &out->data)) {
        return MESH_ERR_BAD_PROTOBUF;
    }

    /* Two portnums are understood. Everything else decoded cleanly but is not
     * something this app renders, which is a different outcome from a decode
     * failure and is counted separately. */
    if(out->data.portnum != MESH_PORTNUM_TEXT_MESSAGE_APP &&
       out->data.portnum != MESH_PORTNUM_NODEINFO_APP) {
        return MESH_ERR_NOT_TEXT;
    }

    return MESH_OK;
}

const char* mesh_decode_result_name(MeshDecodeResult r) {
    switch(r) {
    case MESH_OK:
        return "ok";
    case MESH_ERR_TOO_SHORT:
        return "short";
    case MESH_ERR_CHANNEL_MISMATCH:
        return "chan";
    case MESH_ERR_BAD_PROTOBUF:
        return "proto";
    case MESH_ERR_NOT_TEXT:
        return "notxt";
    }
    return "unknown";
}
