/* AES-256-CCM, NIST SP 800-38C / RFC 3610, with a 13 byte nonce (L = 2).
 *
 * Produces the same bytes as the firmware's aes_ccm_ae and aes_ccm_ad
 * (src/mesh/aes-ccm.cpp:142-179): L fixed at 2, so a 13 byte nonce and a
 * 16 bit length, tag length M a parameter. Meshtastic direct messages pass
 * M = 8 and no associated data (CryptoEngine.cpp:247 and :287).
 *
 * One difference, a superset: the firmware refuses associated data over 30
 * bytes (aes-ccm.cpp:147), this accepts any length below 0xFF00, which is
 * what lets the NIST CAVP VTT256 vector (32 bytes of AD) run against it. */
#ifndef MESH_CCM_H
#define MESH_CCM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mesh_aes256.h"

#define MESH_CCM_NONCE_LEN 13

/* Encrypts len bytes of in to out and writes tag_len bytes of tag.
 * in and out may be the same buffer. Returns false, writing nothing, if
 * tag_len is not an even number from 4 to 16, len exceeds 0xFFFF or
 * aad_len is 0xFF00 or more. */
bool mesh_ccm_encrypt(
    const uint8_t key[MESH_AES256_KEY_LEN],
    const uint8_t nonce[MESH_CCM_NONCE_LEN],
    const uint8_t* aad,
    size_t aad_len,
    const uint8_t* in,
    size_t len,
    uint8_t* out,
    uint8_t* tag,
    size_t tag_len);

/* Decrypts len bytes of in to out and checks tag. Returns true only if the
 * tag verifies. On failure out is zeroed, so unauthenticated plaintext never
 * reaches the caller (the firmware leaves it in the buffer). */
bool mesh_ccm_decrypt(
    const uint8_t key[MESH_AES256_KEY_LEN],
    const uint8_t nonce[MESH_CCM_NONCE_LEN],
    const uint8_t* aad,
    size_t aad_len,
    const uint8_t* in,
    size_t len,
    uint8_t* out,
    const uint8_t* tag,
    size_t tag_len);

#endif
