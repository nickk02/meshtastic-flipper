#include "mesh_channel.h"

#include <string.h>

/* Channels.h:153-154. The well-known public default channel key. */
const uint8_t mesh_default_psk[MESH_PSK_LEN] = {
    0xd4,
    0xf1,
    0xbb,
    0x3a,
    0x20,
    0x29,
    0x07,
    0x59,
    0xf0,
    0xbc,
    0xff,
    0xab,
    0xcf,
    0x4e,
    0x69,
    0x01,
};

bool mesh_channel_expand_psk(uint8_t index, uint8_t out[MESH_PSK_LEN]) {
    if(index == 0) return false;

    memcpy(out, mesh_default_psk, MESH_PSK_LEN);
    /* Deliberately wraps within the last byte. Channels.cpp does the same, so
       matching it matters more than the arithmetic being tidy. */
    out[MESH_PSK_LEN - 1] = (uint8_t)(out[MESH_PSK_LEN - 1] + index - 1);
    return true;
}

uint8_t mesh_channel_xor_hash(const uint8_t* p, size_t len) {
    uint8_t code = 0;
    for(size_t i = 0; i < len; i++)
        code ^= p[i];
    return code;
}

uint8_t mesh_channel_hash(const char* name, const uint8_t* key, size_t key_len) {
    uint8_t h = mesh_channel_xor_hash((const uint8_t*)name, strlen(name));
    h ^= mesh_channel_xor_hash(key, key_len);
    return h;
}
