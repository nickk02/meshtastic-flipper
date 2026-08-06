#include "mesh_data.h"

/* Protobuf wire types. Types 3 and 4 are the deprecated group markers and 6 is
   not assigned; none appear in Data, so all are treated as malformed. */
#define WIRE_VARINT  0
#define WIRE_FIXED64 1
#define WIRE_LEN     2
#define WIRE_FIXED32 5

#define FIELD_PORTNUM 1
#define FIELD_PAYLOAD 2

/* Read a base 128 varint and advance *pos.
 *
 * Returns false if the buffer ends mid-varint or the value would exceed 64
 * bits. Both indicate corrupt input rather than a value we should truncate. */
static bool read_varint(const uint8_t* buf, size_t len, size_t* pos, uint64_t* out) {
    uint64_t value = 0;
    unsigned shift = 0;

    while(*pos < len) {
        uint8_t byte = buf[*pos];
        (*pos)++;

        if(shift >= 64) return false;
        value |= (uint64_t)(byte & 0x7F) << shift;

        if((byte & 0x80) == 0) {
            *out = value;
            return true;
        }
        shift += 7;
    }
    return false; /* ran off the end mid-varint */
}

bool mesh_data_parse(const uint8_t* buf, size_t len, MeshData* out) {
    size_t pos = 0;

    if(out == NULL) return false;
    if(buf == NULL && len > 0) return false;

    out->portnum = 0;
    out->payload = NULL;
    out->payload_len = 0;

    while(pos < len) {
        uint64_t tag;
        uint32_t field;
        uint8_t wire;

        if(!read_varint(buf, len, &pos, &tag)) return false;

        field = (uint32_t)(tag >> 3);
        wire = (uint8_t)(tag & 0x07);

        if(field == 0) return false;

        switch(wire) {
        case WIRE_VARINT: {
            uint64_t value;
            if(!read_varint(buf, len, &pos, &value)) return false;
            if(field == FIELD_PORTNUM) out->portnum = (uint32_t)value;
            break;
        }
        case WIRE_LEN: {
            uint64_t size;
            if(!read_varint(buf, len, &pos, &size)) return false;
            if(size > (uint64_t)(len - pos)) return false;
            if(field == FIELD_PAYLOAD) {
                out->payload = buf + pos;
                out->payload_len = (size_t)size;
            }
            pos += (size_t)size;
            break;
        }
        case WIRE_FIXED32:
            if(len - pos < 4) return false;
            pos += 4;
            break;
        case WIRE_FIXED64:
            if(len - pos < 8) return false;
            pos += 8;
            break;
        default:
            return false;
        }
    }
    return true;
}
