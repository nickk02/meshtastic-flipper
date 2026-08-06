/* Meshtastic channel keys and the channel hash.
 *
 * No Flipper dependencies. Compiles and is tested on a PC. */
#ifndef MESH_CHANNEL_H
#define MESH_CHANNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The default channel PSK is AES128. Channels.h:153-154. */
#define MESH_PSK_LEN 16

extern const uint8_t mesh_default_psk[MESH_PSK_LEN];

/* Expand a short single byte PSK into full key bytes. Channels.cpp:254-267.
 *
 * Index 0 means encryption disabled and is rejected here, because a caller
 * wanting cleartext should not be asking for a key. Index 1 yields
 * mesh_default_psk unchanged. Higher indices add index - 1 to the last byte
 * only, with no carry.
 *
 * Returns false and leaves out untouched when index is 0. */
bool mesh_channel_expand_psk(uint8_t index, uint8_t out[MESH_PSK_LEN]);

/* Byte-wise XOR fold. Channels.cpp xorHash. */
uint8_t mesh_channel_xor_hash(const uint8_t* p, size_t len);

/* The channel hash carried in the header's channel byte as a decode hint.
 * xorHash(name) XOR xorHash(key). Channels::getHash.
 *
 * It is a hint and nothing more. One byte collides readily, so a match does
 * not prove the frame is ours, it only means decryption is worth attempting. */
uint8_t mesh_channel_hash(const char* name, const uint8_t* key, size_t key_len);

#endif
