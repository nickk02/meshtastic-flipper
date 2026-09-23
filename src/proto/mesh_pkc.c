#include "mesh_pkc.h"

#include <string.h>

#include "mesh_sha256.h"
#include "mesh_x25519.h"

static void put_le32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

bool mesh_pkc_shared_key(
    const uint8_t our_priv[MESH_PKC_KEY_LEN],
    const uint8_t their_pub[MESH_PKC_KEY_LEN],
    uint8_t out[MESH_PKC_KEY_LEN]) {
    uint8_t secret[MESH_X25519_LEN];
    uint8_t any = 0;

    mesh_x25519(secret, our_priv, their_pub);
    for(size_t i = 0; i < sizeof(secret); i++)
        any |= secret[i];
    if(any == 0) {
        memset(out, 0, MESH_PKC_KEY_LEN);
        return false;
    }
    mesh_sha256(secret, sizeof(secret), out);
    memset(secret, 0, sizeof(secret));
    return true;
}

void mesh_pkc_build_nonce(
    uint32_t from_node,
    uint32_t packet_id,
    uint32_t extra_nonce,
    uint8_t nonce[MESH_CCM_NONCE_LEN]) {
    put_le32(&nonce[0], packet_id);
    put_le32(&nonce[4], extra_nonce);
    put_le32(&nonce[8], from_node);
    nonce[12] = 0;
}

bool mesh_pkc_encrypt(
    const uint8_t key[MESH_PKC_KEY_LEN],
    uint32_t from_node,
    uint32_t packet_id,
    uint32_t extra_nonce,
    const uint8_t* plain,
    size_t len,
    uint8_t* out) {
    uint8_t nonce[MESH_CCM_NONCE_LEN];

    mesh_pkc_build_nonce(from_node, packet_id, extra_nonce, nonce);
    if(!mesh_ccm_encrypt(key, nonce, NULL, 0, plain, len, out, out + len, MESH_PKC_TAG_LEN)) {
        return false;
    }
    put_le32(out + len + MESH_PKC_TAG_LEN, extra_nonce);
    return true;
}

bool mesh_pkc_decrypt(
    const uint8_t key[MESH_PKC_KEY_LEN],
    uint32_t from_node,
    uint32_t packet_id,
    const uint8_t* in,
    size_t in_len,
    uint8_t* out) {
    uint8_t nonce[MESH_CCM_NONCE_LEN];
    size_t len;
    const uint8_t* trailer;

    if(in_len <= MESH_PKC_OVERHEAD) return false;
    len = in_len - MESH_PKC_OVERHEAD;
    trailer = in + len;

    /* The trailer's last 4 bytes are the extra nonce exactly as the sender's
       nonce held them, so they are copied, not decoded. */
    mesh_pkc_build_nonce(from_node, packet_id, 0, nonce);
    memcpy(&nonce[4], trailer + MESH_PKC_TAG_LEN, MESH_PKC_EXTRA_NONCE_LEN);
    return mesh_ccm_decrypt(key, nonce, NULL, 0, in, len, out, trailer, MESH_PKC_TAG_LEN);
}
