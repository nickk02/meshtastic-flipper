#include "mesh_user.h"

#include <string.h>

#define WIRE_VARINT  0
#define WIRE_FIXED64 1
#define WIRE_LEN     2
#define WIRE_FIXED32 5

#define USER_FIELD_ID         1
#define USER_FIELD_LONG_NAME  2
#define USER_FIELD_SHORT_NAME 3
#define USER_FIELD_HW_MODEL   5

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
    return false;
}

/* Copies at most cap - 1 bytes and always terminates. Non-printable bytes
 * become '.', because a name arriving from the air can contain anything and
 * control characters would corrupt the display. */
static void copy_name(char* dest, size_t cap, const uint8_t* src, size_t len) {
    size_t n = len < cap - 1 ? len : cap - 1;
    for(size_t i = 0; i < n; i++) {
        uint8_t c = src[i];
        dest[i] = (c >= 0x20 && c <= 0x7E) ? (char)c : '.';
    }
    dest[n] = '\0';
}

bool mesh_user_parse(const uint8_t* buf, size_t len, MeshUser* out) {
    size_t pos = 0;

    if(out == NULL) return false;
    if(buf == NULL && len > 0) return false;

    memset(out, 0, sizeof(*out));

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
            if(field == USER_FIELD_HW_MODEL) out->hw_model = (uint32_t)value;
            break;
        }
        case WIRE_LEN: {
            uint64_t size;
            if(!read_varint(buf, len, &pos, &size)) return false;
            if(size > (uint64_t)(len - pos)) return false;

            if(field == USER_FIELD_ID) {
                copy_name(out->id, sizeof(out->id), buf + pos, (size_t)size);
            } else if(field == USER_FIELD_LONG_NAME) {
                copy_name(out->long_name, sizeof(out->long_name), buf + pos, (size_t)size);
                out->has_long_name = size > 0;
            } else if(field == USER_FIELD_SHORT_NAME) {
                copy_name(out->short_name, sizeof(out->short_name), buf + pos, (size_t)size);
                out->has_short_name = size > 0;
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
