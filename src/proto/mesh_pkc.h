/* Meshtastic direct message (PKC) encryption.
 *
 * The composition in the firmware's CryptoEngine::encryptCurve25519 and
 * decryptCurve25519 (src/mesh/CryptoEngine.cpp:223-288):
 *
 *   key   = SHA-256(X25519(our private, their public))       :238-241, :389-401
 *   nonce = 13 bytes, see mesh_pkc_build_nonce                :242, :536-545
 *   AES-256-CCM, 8 byte tag, no associated data               :247, :287
 *   wire  = ciphertext | tag (8) | extra nonce (4, as in nonce bytes 4-7)
 *                                                              :232, :248-249
 *
 * The 12 byte trailer is MESHTASTIC_PKC_OVERHEAD (RadioInterface.h:22).
 * Nothing here allocates; every buffer belongs to the caller. */
#ifndef MESH_PKC_H
#define MESH_PKC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mesh_ccm.h"

#define MESH_PKC_KEY_LEN         32
#define MESH_PKC_TAG_LEN         8
#define MESH_PKC_EXTRA_NONCE_LEN 4
#define MESH_PKC_OVERHEAD        (MESH_PKC_TAG_LEN + MESH_PKC_EXTRA_NONCE_LEN)

/* Shared key for one peer: SHA-256 of the X25519 output.
 * Returns false, with out zeroed, if the X25519 output is all zero, which
 * is what every low order public key produces. The firmware rejects those
 * too, inside Curve25519::dh2 (CryptoEngine.cpp:394-399). */
bool mesh_pkc_shared_key(
    const uint8_t our_priv[MESH_PKC_KEY_LEN],
    const uint8_t their_pub[MESH_PKC_KEY_LEN],
    uint8_t out[MESH_PKC_KEY_LEN]);

/* CryptoEngine::initNonce(fromNode, packetId, extraNonce):
 *
 *   bytes 0-3    packet id, little endian
 *   bytes 4-7    extra nonce, little endian (the high half of the 64 bit
 *                packet number, always zero for a 32 bit MeshPacket.id)
 *   bytes 8-11   from node, little endian
 *   byte  12     zero (the first byte of the unused block counter)
 *
 * The firmware only writes the extra nonce when it is non-zero
 * (CryptoEngine.cpp:543), which gives the same bytes. */
void mesh_pkc_build_nonce(
    uint32_t from_node,
    uint32_t packet_id,
    uint32_t extra_nonce,
    uint8_t nonce[MESH_CCM_NONCE_LEN]);

/* Encrypts len bytes of plain into out, which must hold
 * len + MESH_PKC_OVERHEAD bytes. The firmware draws extra_nonce from the
 * hardware RNG per packet (CryptoEngine.cpp:229-231); the caller must do the
 * same. Returns false if len exceeds 0xFFFF. */
bool mesh_pkc_encrypt(
    const uint8_t key[MESH_PKC_KEY_LEN],
    uint32_t from_node,
    uint32_t packet_id,
    uint32_t extra_nonce,
    const uint8_t* plain,
    size_t len,
    uint8_t* out);

/* Decrypts a PKC payload of in_len bytes, trailer included, into out, which
 * must hold in_len - MESH_PKC_OVERHEAD bytes. The extra nonce is read from
 * the trailer. Returns false if in_len is not greater than the overhead
 * (Router.cpp:953 requires rawSize > MESHTASTIC_PKC_OVERHEAD) or if the tag
 * does not verify, in which case out is zeroed. */
bool mesh_pkc_decrypt(
    const uint8_t key[MESH_PKC_KEY_LEN],
    uint32_t from_node,
    uint32_t packet_id,
    const uint8_t* in,
    size_t in_len,
    uint8_t* out);

#endif
