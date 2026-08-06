#include "mesh_crypto.h"

#include <string.h>

#include "aes.h"

void mesh_crypto_build_nonce(uint32_t packet_id, uint32_t from_node, uint8_t nonce[MESH_NONCE_LEN]) {
    memset(nonce, 0, MESH_NONCE_LEN);

    /* Written byte-wise rather than memcpy of a uint32_t so the wire layout is
       little endian regardless of host byte order. */
    nonce[0] = (uint8_t)(packet_id & 0xFF);
    nonce[1] = (uint8_t)((packet_id >> 8) & 0xFF);
    nonce[2] = (uint8_t)((packet_id >> 16) & 0xFF);
    nonce[3] = (uint8_t)((packet_id >> 24) & 0xFF);

    /* Bytes 4 to 7 stay zero: the packet id is 32 bits widened to 64. */

    nonce[8] = (uint8_t)(from_node & 0xFF);
    nonce[9] = (uint8_t)((from_node >> 8) & 0xFF);
    nonce[10] = (uint8_t)((from_node >> 16) & 0xFF);
    nonce[11] = (uint8_t)((from_node >> 24) & 0xFF);

    /* Bytes 12 to 15 are the block counter and start at zero. */
}

void mesh_crypto_xcrypt(
    const uint8_t key[MESH_PSK_LEN],
    uint32_t packet_id,
    uint32_t from_node,
    const uint8_t* in,
    size_t len,
    uint8_t* out) {
    uint8_t nonce[MESH_NONCE_LEN];
    struct AES_ctx ctx;

    if(len == 0) return;

    mesh_crypto_build_nonce(packet_id, from_node, nonce);

    /* tiny-AES-c transforms in place, so copy first. */
    memcpy(out, in, len);

    AES_init_ctx_iv(&ctx, key, nonce);
    AES_CTR_xcrypt_buffer(&ctx, out, len);
}
