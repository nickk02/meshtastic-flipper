#include "mesh_header.h"

static uint32_t read_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

bool mesh_header_parse(const uint8_t* buf, size_t len, MeshHeader* out) {
    if(buf == NULL || out == NULL) return false;
    if(len < MESH_HEADER_LEN) return false;

    out->to = read_u32_le(buf + 0);
    out->from = read_u32_le(buf + 4);
    out->id = read_u32_le(buf + 8);
    out->flags = buf[12];
    out->channel = buf[13];
    out->next_hop = buf[14];
    out->relay_node = buf[15];
    return true;
}

uint8_t mesh_header_hop_limit(const MeshHeader* h) {
    return (uint8_t)(h->flags & MESH_FLAG_HOP_LIMIT_MASK);
}

uint8_t mesh_header_hop_start(const MeshHeader* h) {
    return (uint8_t)((h->flags & MESH_FLAG_HOP_START_MASK) >> MESH_FLAG_HOP_START_SHIFT);
}

bool mesh_header_want_ack(const MeshHeader* h) {
    return (h->flags & MESH_FLAG_WANT_ACK_MASK) != 0;
}

bool mesh_header_via_mqtt(const MeshHeader* h) {
    return (h->flags & MESH_FLAG_VIA_MQTT_MASK) != 0;
}
