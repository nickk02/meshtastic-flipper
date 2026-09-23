/* SHA-256, FIPS 180-4. One-shot, no allocation.
 *
 * Meshtastic direct messages key AES-256-CCM with SHA-256 of the raw X25519
 * output. CryptoEngine::hash (firmware src/mesh/CryptoEngine.cpp:351-365)
 * feeds the 32 byte secret in 16 byte pieces, which is the same digest as
 * hashing it in one call. */
#ifndef MESH_SHA256_H
#define MESH_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define MESH_SHA256_LEN 32

void mesh_sha256(const uint8_t* data, size_t len, uint8_t out[MESH_SHA256_LEN]);

#endif
