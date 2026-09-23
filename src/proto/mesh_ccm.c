#include "mesh_ccm.h"

#include <string.h>

/* L = 2: the length field is 2 bytes, so the nonce is 15 - L = 13 bytes. */
#define CCM_L 2

/* CBC-MAC state: the running block and how many bytes of it are filled. */
typedef struct {
    MeshAes256 aes;
    uint8_t x[MESH_AES_BLOCK_LEN];
    size_t pos;
} CcmMac;

static void mac_feed(CcmMac* m, const uint8_t* data, size_t len) {
    for(size_t i = 0; i < len; i++) {
        m->x[m->pos++] ^= data[i];
        if(m->pos == MESH_AES_BLOCK_LEN) {
            mesh_aes256_encrypt(&m->aes, m->x, m->x);
            m->pos = 0;
        }
    }
}

/* Zero pads the current block, which is what XOR with nothing does. */
static void mac_flush(CcmMac* m) {
    if(m->pos) {
        mesh_aes256_encrypt(&m->aes, m->x, m->x);
        m->pos = 0;
    }
}

static bool params_ok(size_t aad_len, size_t len, size_t tag_len) {
    return tag_len >= 4 && tag_len <= 16 && (tag_len % 2) == 0 && len <= 0xFFFF &&
           aad_len < 0xFF00;
}

/* A_i = flags (L - 1) | nonce | counter i, big endian. */
static void counter_block(const uint8_t nonce[MESH_CCM_NONCE_LEN], size_t i, uint8_t a[16]) {
    a[0] = CCM_L - 1;
    memcpy(&a[1], nonce, MESH_CCM_NONCE_LEN);
    a[14] = (uint8_t)(i >> 8);
    a[15] = (uint8_t)i;
}

/* Computes the CBC-MAC T over B_0, the associated data and the plaintext,
   leaving it in m->x. */
static void ccm_mac(
    CcmMac* m,
    const uint8_t nonce[MESH_CCM_NONCE_LEN],
    const uint8_t* aad,
    size_t aad_len,
    const uint8_t* plain,
    size_t len,
    size_t tag_len) {
    uint8_t b0[MESH_AES_BLOCK_LEN];

    b0[0] = (uint8_t)((aad_len ? 0x40 : 0) | (((tag_len - 2) / 2) << 3) | (CCM_L - 1));
    memcpy(&b0[1], nonce, MESH_CCM_NONCE_LEN);
    b0[14] = (uint8_t)(len >> 8);
    b0[15] = (uint8_t)len;
    mesh_aes256_encrypt(&m->aes, b0, m->x);
    m->pos = 0;

    if(aad_len) {
        uint8_t alen[2] = {(uint8_t)(aad_len >> 8), (uint8_t)aad_len};
        mac_feed(m, alen, 2);
        mac_feed(m, aad, aad_len);
        mac_flush(m);
    }
    mac_feed(m, plain, len);
    mac_flush(m);
}

/* out = in XOR S_1 | S_2 | ... ; in and out may alias. */
static void ccm_ctr(
    const MeshAes256* aes,
    const uint8_t nonce[MESH_CCM_NONCE_LEN],
    const uint8_t* in,
    size_t len,
    uint8_t* out) {
    uint8_t a[MESH_AES_BLOCK_LEN];
    uint8_t s[MESH_AES_BLOCK_LEN];

    for(size_t off = 0; off < len; off += MESH_AES_BLOCK_LEN) {
        size_t n = len - off < MESH_AES_BLOCK_LEN ? len - off : MESH_AES_BLOCK_LEN;
        counter_block(nonce, off / MESH_AES_BLOCK_LEN + 1, a);
        mesh_aes256_encrypt(aes, a, s);
        for(size_t i = 0; i < n; i++)
            out[off + i] = in[off + i] ^ s[i];
    }
}

/* U = T XOR S_0, the first tag_len bytes. */
static void ccm_tag(
    const CcmMac* m,
    const uint8_t nonce[MESH_CCM_NONCE_LEN],
    uint8_t* tag,
    size_t tag_len) {
    uint8_t a[MESH_AES_BLOCK_LEN];
    uint8_t s0[MESH_AES_BLOCK_LEN];

    counter_block(nonce, 0, a);
    mesh_aes256_encrypt(&m->aes, a, s0);
    for(size_t i = 0; i < tag_len; i++)
        tag[i] = m->x[i] ^ s0[i];
}

bool mesh_ccm_encrypt(
    const uint8_t key[MESH_AES256_KEY_LEN],
    const uint8_t nonce[MESH_CCM_NONCE_LEN],
    const uint8_t* aad,
    size_t aad_len,
    const uint8_t* in,
    size_t len,
    uint8_t* out,
    uint8_t* tag,
    size_t tag_len) {
    CcmMac m;

    if(!params_ok(aad_len, len, tag_len)) return false;
    mesh_aes256_init(&m.aes, key);
    /* MAC first: in and out may be the same buffer. */
    ccm_mac(&m, nonce, aad, aad_len, in, len, tag_len);
    ccm_tag(&m, nonce, tag, tag_len);
    ccm_ctr(&m.aes, nonce, in, len, out);
    return true;
}

bool mesh_ccm_decrypt(
    const uint8_t key[MESH_AES256_KEY_LEN],
    const uint8_t nonce[MESH_CCM_NONCE_LEN],
    const uint8_t* aad,
    size_t aad_len,
    const uint8_t* in,
    size_t len,
    uint8_t* out,
    const uint8_t* tag,
    size_t tag_len) {
    CcmMac m;
    uint8_t expect[16];
    uint8_t diff = 0;

    if(!params_ok(aad_len, len, tag_len)) return false;
    mesh_aes256_init(&m.aes, key);
    ccm_ctr(&m.aes, nonce, in, len, out);
    ccm_mac(&m, nonce, aad, aad_len, out, len, tag_len);
    ccm_tag(&m, nonce, expect, tag_len);

    /* Constant time, as the firmware's constant_time_compare
       (aes-ccm.cpp:20-36). */
    for(size_t i = 0; i < tag_len; i++)
        diff |= expect[i] ^ tag[i];
    if(diff != 0) {
        memset(out, 0, len);
        return false;
    }
    return true;
}
