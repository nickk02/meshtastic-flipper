/* Meshtastic packet header, the first 16 bytes of every frame off the air.
 *
 * No Flipper dependencies. */
#ifndef MESH_HEADER_H
#define MESH_HEADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* RadioInterface.h:20-21. */
#define MESH_HEADER_LEN 16
#define MESH_MAX_PAYLOAD 255

/* RadioInterface.h:24-28. */
#define MESH_FLAG_HOP_LIMIT_MASK 0x07
#define MESH_FLAG_WANT_ACK_MASK 0x08
#define MESH_FLAG_VIA_MQTT_MASK 0x10
#define MESH_FLAG_HOP_START_MASK 0xE0
#define MESH_FLAG_HOP_START_SHIFT 5

/* Wire layout of PacketHeader. RadioInterface.h:36-53.
 *
 * Multi-byte fields are little endian on the wire. This struct is the decoded
 * form and is deliberately not memcpy'd over the wire bytes, so alignment and
 * host byte order cannot bite. */
typedef struct {
    uint32_t to;
    uint32_t from;
    uint32_t id;
    uint8_t flags;
    uint8_t channel; /* channel hash, a decode hint only */
    uint8_t next_hop;
    uint8_t relay_node;
} MeshHeader;

/* Returns false if buf or out is NULL, or len is under MESH_HEADER_LEN. */
bool mesh_header_parse(const uint8_t* buf, size_t len, MeshHeader* out);

uint8_t mesh_header_hop_limit(const MeshHeader* h);
uint8_t mesh_header_hop_start(const MeshHeader* h);
bool mesh_header_want_ack(const MeshHeader* h);
bool mesh_header_via_mqtt(const MeshHeader* h);

#endif
