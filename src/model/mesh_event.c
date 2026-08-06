#include "mesh_event.h"

#include <string.h>

void mesh_event_from_decoded(
    MeshEvent* out,
    const MeshDecoded* decoded,
    MeshDecodeResult result,
    int16_t rssi,
    int8_t snr) {
    if(out == NULL) return;

    memset(out, 0, sizeof(*out));
    out->result = result;
    out->rssi = rssi;
    out->snr = snr;

    if(decoded == NULL) return;

    /* Header fields are populated even on failure, so an undecodable frame
     * still tells the user who it came from. */
    out->from = decoded->header.from;
    out->to = decoded->header.to;
    out->id = decoded->header.id;
    out->hop_limit = mesh_header_hop_limit(&decoded->header);
    out->hop_start = mesh_header_hop_start(&decoded->header);

    out->portnum = decoded->data.portnum;

    if(result != MESH_OK || decoded->data.payload == NULL) return;
    if(decoded->data.portnum != MESH_PORTNUM_TEXT_MESSAGE_APP) return;

    size_t len = decoded->data.payload_len;
    if(len > MESH_TEXT_MAX) {
        len = MESH_TEXT_MAX;
        out->text_truncated = true;
    }
    memcpy(out->text, decoded->data.payload, len);
    out->text_len = (uint8_t)len;
}
