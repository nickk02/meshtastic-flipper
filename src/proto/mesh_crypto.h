/* Meshtastic channel encryption: AES128 in CTR mode.
 *
 * No Flipper dependencies. The Flipper's AES accelerator cannot be used here,
 * because every raw key path in furi_hal_crypto hardcodes CRYPTO_KEYSIZE_256B
 * (furi_hal_crypto.c:203 and :400) and the default channel is AES128. See
 * vendor/tiny-AES-c/README-vendoring.md. */
#ifndef MESH_CRYPTO_H
#define MESH_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#include "mesh_channel.h"

/* CryptoEngine::encryptAESCtr calls setIV(nonce, 16). */
#define MESH_NONCE_LEN 16

/* Build the per-packet nonce. CryptoEngine::initNonce.
 *
 *   bytes 0-7    packet id, little endian, a 32 bit value widened to 64
 *   bytes 8-11   source node number, little endian
 *   bytes 12-15  block counter, starts at zero
 *
 * Bytes 4 to 7 are therefore always zero in practice. */
void mesh_crypto_build_nonce(
    uint32_t packet_id,
    uint32_t from_node,
    uint8_t nonce[MESH_NONCE_LEN]);

/* AES128-CTR over len bytes.
 *
 * CTR is symmetric, so this both encrypts and decrypts. The receive path only
 * decrypts; the encrypt direction exists so tests can reproduce the
 * generator's ciphertext, which is a stronger check than decrypting alone.
 *
 * in and out must not overlap. out must hold at least len bytes. */
void mesh_crypto_xcrypt(
    const uint8_t key[MESH_PSK_LEN],
    uint32_t packet_id,
    uint32_t from_node,
    const uint8_t* in,
    size_t len,
    uint8_t* out);

#endif
