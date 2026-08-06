#include "pb_write.h"

#include <string.h>

#define WIRE_VARINT 0
#define WIRE_LEN    2

void pb_writer_init(PbWriter* w, uint8_t* buf, size_t cap) {
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
    w->ok = (buf != NULL);
}

bool pb_writer_ok(const PbWriter* w) {
    return w != NULL && w->ok;
}

size_t pb_writer_len(const PbWriter* w) {
    return w == NULL ? 0 : w->len;
}

static bool raw_varint(PbWriter* w, uint64_t value) {
    if(!w->ok) return false;

    do {
        if(w->len >= w->cap) {
            w->ok = false;
            return false;
        }
        uint8_t byte = (uint8_t)(value & 0x7F);
        value >>= 7;
        if(value != 0) byte |= 0x80;
        w->buf[w->len++] = byte;
    } while(value != 0);

    return true;
}

static bool raw_tag(PbWriter* w, uint32_t field, uint8_t wire) {
    return raw_varint(w, ((uint64_t)field << 3) | wire);
}

static bool raw_bytes(PbWriter* w, const uint8_t* data, size_t len) {
    if(!w->ok) return false;
    if(len > w->cap - w->len) {
        w->ok = false;
        return false;
    }
    if(len > 0) memcpy(w->buf + w->len, data, len);
    w->len += len;
    return true;
}

bool pb_write_varint_field_always(PbWriter* w, uint32_t field, uint64_t value) {
    if(!raw_tag(w, field, WIRE_VARINT)) return false;
    return raw_varint(w, value);
}

bool pb_write_varint_field(PbWriter* w, uint32_t field, uint64_t value) {
    if(value == 0) return w->ok;
    return pb_write_varint_field_always(w, field, value);
}

bool pb_write_bytes_field(PbWriter* w, uint32_t field, const uint8_t* data, size_t len) {
    if(len == 0) return w->ok;
    if(data == NULL) {
        w->ok = false;
        return false;
    }
    if(!raw_tag(w, field, WIRE_LEN)) return false;
    if(!raw_varint(w, len)) return false;
    return raw_bytes(w, data, len);
}

bool pb_write_string_field(PbWriter* w, uint32_t field, const char* str) {
    if(str == NULL) return w->ok;
    return pb_write_bytes_field(w, field, (const uint8_t*)str, strlen(str));
}

bool pb_write_submessage(PbWriter* w, uint32_t field, const uint8_t* data, size_t len) {
    if(len > 0 && data == NULL) {
        w->ok = false;
        return false;
    }
    if(!raw_tag(w, field, WIRE_LEN)) return false;
    if(!raw_varint(w, len)) return false;
    return raw_bytes(w, data, len);
}
