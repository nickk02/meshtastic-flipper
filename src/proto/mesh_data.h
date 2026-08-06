/* Protobuf field walker for the Meshtastic Data message.
 *
 * Only the two fields this project reads are extracted. Everything else is
 * skipped by wire type. This replaces nanopb for the receive path: generating
 * nanopb code for mesh.proto and portnums.proto costs several kilobytes plus a
 * protoc and codegen step in the build, to read two fields.
 *
 * No Flipper dependencies. */
#ifndef MESH_DATA_H
#define MESH_DATA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Confirmed from protobufs/meshtastic/portnums.proto via the meshtastic
   Python package: PortNum.TEXT_MESSAGE_APP == 1. */
#define MESH_PORTNUM_TEXT_MESSAGE_APP 1

/* Decoded subset of Data.
 *
 * payload points into the caller's buffer. Nothing is copied and nothing is
 * allocated, so the source buffer must outlive this struct. */
typedef struct {
    uint32_t portnum;
    const uint8_t* payload;
    size_t payload_len;
} MeshData;

/* Returns false on malformed input, leaving out zeroed.
 *
 * Rejecting malformed input is load bearing rather than hygiene: a failed
 * decryption produces effectively random bytes, and those must not surface as
 * a message. */
bool mesh_data_parse(const uint8_t* buf, size_t len, MeshData* out);

#endif
