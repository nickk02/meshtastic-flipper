/* Minimal protobuf writer.
 *
 * Enough to build the handful of messages this project sends. Not a general
 * encoder: it handles varint, length-delimited, and nested messages, which is
 * all the Meshtastic messages we produce need.
 *
 * Errors are sticky. Once a write fails the writer stays failed, so callers
 * can write a whole message and check once at the end instead of after every
 * field.
 *
 * No Flipper dependencies. No allocation. */
#ifndef PB_WRITE_H
#define PB_WRITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t* buf;
    size_t cap;
    size_t len;
    bool ok;
} PbWriter;

void pb_writer_init(PbWriter* w, uint8_t* buf, size_t cap);

/* True if every write so far succeeded. */
bool pb_writer_ok(const PbWriter* w);

/* Bytes written. Meaningless if pb_writer_ok is false. */
size_t pb_writer_len(const PbWriter* w);

/* Writes nothing when value is 0. proto3 omits zero-valued scalars, and
 * matching that keeps output byte-identical to a reference encoder. Use
 * pb_write_varint_field_always when the field must appear regardless. */
bool pb_write_varint_field(PbWriter* w, uint32_t field, uint64_t value);
bool pb_write_varint_field_always(PbWriter* w, uint32_t field, uint64_t value);

/* Writes nothing when len is 0, for the same reason. */
bool pb_write_bytes_field(PbWriter* w, uint32_t field, const uint8_t* data, size_t len);

/* Writes nothing for NULL or an empty string. */
bool pb_write_string_field(PbWriter* w, uint32_t field, const char* str);

/* Nested message, already encoded by the caller. Written even when len is 0,
 * because an empty submessage is distinguishable from an absent one and the
 * client may care. */
bool pb_write_submessage(PbWriter* w, uint32_t field, const uint8_t* data, size_t len);

#endif
