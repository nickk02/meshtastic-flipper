/* AES-256 forward cipher, FIPS-197. Encrypt only.
 *
 * Meshtastic direct messages use AES-256-CCM (CryptoEngine::aesSetKey picks
 * AES256 for a 32 byte key, firmware src/mesh/CryptoEngine.cpp:376-378), and
 * CCM only ever runs the block cipher forward. tiny-AES-c cannot supply this:
 * it is built for AES128 for the channel path, and its key size is a compile
 * time choice with shared symbol names, so it cannot be linked twice. */
#ifndef MESH_AES256_H
#define MESH_AES256_H

#include <stdint.h>

#define MESH_AES256_KEY_LEN 32
#define MESH_AES_BLOCK_LEN  16

/* Expanded key schedule: 15 round keys of 16 bytes. Caller owned, so the
 * module never allocates. */
typedef struct {
    uint8_t round_key[240];
} MeshAes256;

void mesh_aes256_init(MeshAes256* ctx, const uint8_t key[MESH_AES256_KEY_LEN]);

/* in and out may be the same buffer. */
void mesh_aes256_encrypt(
    const MeshAes256* ctx,
    const uint8_t in[MESH_AES_BLOCK_LEN],
    uint8_t out[MESH_AES_BLOCK_LEN]);

#endif
