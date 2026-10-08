/* Direct message crypto: SHA-256, X25519, AES-256, CCM and the Meshtastic
 * composition over them.
 *
 * Every primitive is checked against a published vector, copied from the
 * cited document and cross-checked against OpenSSL through Python's
 * cryptography package before being written here. The composition is checked
 * against gen_vectors.py, which builds the payload with cryptography's X25519
 * and AESCCM. If a known answer fails, do not edit the expected bytes. */
#include "tinytest.h"
#include "mesh_aes256.h"
#include "mesh_ccm.h"
#include "mesh_pkc.h"
#include "mesh_sha256.h"
#include "mesh_x25519.h"
#include "vectors.h"

/* Hex keeps the published vectors readable and on one line each. */
static void unhex(const char* s, uint8_t* out) {
    for(size_t i = 0; s[2 * i]; i++) {
        unsigned v;
        sscanf(&s[2 * i], "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

#define ASSERT_HEX(got, hex)                            \
    do {                                                \
        uint8_t tt_want[64];                            \
        unhex((hex), tt_want);                          \
        ASSERT_EQ_MEM((got), tt_want, strlen(hex) / 2); \
    } while(0)

/* FIPS 180-4 examples: SHA256.pdf one block, two block, and the empty
   message (NIST CSRC example values). */
TEST(test_sha256_fips_vectors) {
    const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    uint8_t d[32];
    mesh_sha256((const uint8_t*)"abc", 3, d);
    ASSERT_HEX(d, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    mesh_sha256((const uint8_t*)two, strlen(two), d);
    ASSERT_HEX(d, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    mesh_sha256(NULL, 0, d);
    ASSERT_HEX(d, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

/* FIPS-197 appendix C.3, AES-256. */
TEST(test_aes256_fips197_c3) {
    uint8_t key[32], block[16];
    MeshAes256 ctx;
    unhex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key);
    unhex("00112233445566778899aabbccddeeff", block);
    mesh_aes256_init(&ctx, key);
    mesh_aes256_encrypt(&ctx, block, block);
    ASSERT_HEX(block, "8ea2b7ca516745bfeafc49904b496089");
}

/* RFC 7748 section 5.2, the two single-call vectors and the iterated one
   after 1 and 1000 iterations. */
TEST(test_x25519_rfc7748_5_2) {
    uint8_t k[32], u[32], out[32];
    unhex("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4", k);
    unhex("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c", u);
    mesh_x25519(out, k, u);
    ASSERT_HEX(out, "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552");

    unhex("4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d", k);
    unhex("e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493", u);
    mesh_x25519(out, k, u);
    ASSERT_HEX(out, "95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957");

    memset(k, 0, 32);
    k[0] = 9;
    memcpy(u, k, 32);
    for(int i = 1; i <= 1000; i++) {
        mesh_x25519(out, k, u);
        memcpy(u, k, 32);
        memcpy(k, out, 32);
        if(i == 1)
            ASSERT_HEX(k, "422c8e7a6227d7bca1350b3e2bb7279f7897b87bb6854b783c60e80311ae3079");
    }
    ASSERT_HEX(k, "684cf59ba83309552800ef566f2f4d3c1c3887c49360e3875f2eb94d99532c51");
}

/* RFC 7748 section 6.1, Alice and Bob. */
TEST(test_x25519_rfc7748_6_1) {
    uint8_t a[32], b[32], pa[32], pb[32], s1[32], s2[32];
    unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", a);
    unhex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", b);
    mesh_x25519_base(pa, a);
    mesh_x25519_base(pb, b);
    ASSERT_HEX(pa, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
    ASSERT_HEX(pb, "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
    mesh_x25519(s1, a, pb);
    mesh_x25519(s2, b, pa);
    ASSERT_HEX(s1, "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
    ASSERT_EQ_MEM(s1, s2, 32);
}

/* NIST CAVP ccmtestvectors.zip, VTT256.rsp, [Tlen = 8], Count = 20:
   AES-256, 13 byte nonce, 8 byte tag, 32 bytes of AD, 24 bytes of payload.
   There is no RFC 3610 equivalent: its vectors are AES-128 only. */
TEST(test_ccm_nist_vtt256) {
    uint8_t key[32], nonce[13], aad[32], pt[24], ct[24], tag[8], back[24];
    unhex("bae73483de27b581a7c13f178a6d7bda168c1b4a1cb9180512a13e3ab914eb61", key);
    unhex("daf54faef6e4fc7867624b76f2", nonce);
    unhex("7022eaa52c9da821da72d2edd98f6b91dfe474999b75b34699aeb38465f70c1c", aad);
    unhex("28ef408d57930086011b167ac04b866e5b58fe6690a0b9c3", pt);
    ASSERT_TRUE(mesh_ccm_encrypt(key, nonce, aad, 32, pt, 24, ct, tag, 8));
    ASSERT_HEX(ct, "356367c6cee4453658418d9517f7c6faddcd7c65aef46013");
    ASSERT_HEX(tag, "8cf050f48c505151");
    ASSERT_TRUE(mesh_ccm_decrypt(key, nonce, aad, 32, ct, 24, back, tag, 8));
    ASSERT_EQ_MEM(back, pt, 24);
}

TEST(test_ccm_python_vector) {
    uint8_t ct[CCM_PLAINTEXT_LEN], tag[CCM_TAG_LEN], back[CCM_PLAINTEXT_LEN];
    ASSERT_TRUE(mesh_ccm_encrypt(
        CCM_KEY, CCM_NONCE, CCM_AAD, CCM_AAD_LEN, CCM_PLAINTEXT, CCM_PLAINTEXT_LEN, ct, tag, 8));
    ASSERT_EQ_MEM(ct, CCM_CIPHERTEXT, CCM_PLAINTEXT_LEN);
    ASSERT_EQ_MEM(tag, CCM_TAG, CCM_TAG_LEN);
    ASSERT_TRUE(mesh_ccm_decrypt(
        CCM_KEY, CCM_NONCE, CCM_AAD, CCM_AAD_LEN, ct, CCM_PLAINTEXT_LEN, back, tag, 8));
    ASSERT_EQ_MEM(back, CCM_PLAINTEXT, CCM_PLAINTEXT_LEN);
    /* The AD is authenticated. */
    ASSERT_TRUE(!mesh_ccm_decrypt(
        CCM_KEY, CCM_NONCE, CCM_AAD, CCM_AAD_LEN - 1, ct, CCM_PLAINTEXT_LEN, back, tag, 8));
    ASSERT_TRUE(!mesh_ccm_encrypt(CCM_KEY, CCM_NONCE, NULL, 0, ct, 1, ct, tag, 7));
}

TEST(test_pkc_shared_key_both_directions) {
    uint8_t pub[32], k1[32], k2[32];
    mesh_x25519_base(pub, PKC_ALICE_PRIV);
    ASSERT_EQ_MEM(pub, PKC_ALICE_PUB, 32);
    ASSERT_TRUE(mesh_pkc_shared_key(PKC_ALICE_PRIV, PKC_BOB_PUB, k1));
    ASSERT_TRUE(mesh_pkc_shared_key(PKC_BOB_PRIV, PKC_ALICE_PUB, k2));
    ASSERT_EQ_MEM(k1, PKC_SHARED_KEY, 32);
    ASSERT_EQ_MEM(k2, PKC_SHARED_KEY, 32);
}

TEST(test_pkc_rejects_low_order_key) {
    /* u = 0 and u = 1 are low order points: X25519 of them is all zero. */
    uint8_t pub[32] = {0}, key[32];
    ASSERT_TRUE(!mesh_pkc_shared_key(PKC_ALICE_PRIV, pub, key));
    pub[0] = 1;
    ASSERT_TRUE(!mesh_pkc_shared_key(PKC_ALICE_PRIV, pub, key));
}

TEST(test_pkc_encrypt_matches_python) {
    uint8_t nonce[13], out[PKC_PAYLOAD_LEN];
    mesh_pkc_build_nonce(PKC_FROM_NODE, PKC_PACKET_ID, PKC_EXTRA_NONCE, nonce);
    ASSERT_EQ_MEM(nonce, PKC_NONCE, 13);
    ASSERT_TRUE(mesh_pkc_encrypt(
        PKC_SHARED_KEY,
        PKC_FROM_NODE,
        PKC_PACKET_ID,
        PKC_EXTRA_NONCE,
        PKC_PLAINTEXT,
        PKC_PLAINTEXT_LEN,
        out));
    ASSERT_EQ_MEM(out, PKC_PAYLOAD, PKC_PAYLOAD_LEN);
    ASSERT_EQ_INT(PKC_PAYLOAD_LEN - PKC_PLAINTEXT_LEN, MESH_PKC_OVERHEAD);
}

TEST(test_pkc_decrypt_python_payload) {
    uint8_t out[PKC_PLAINTEXT_LEN];
    ASSERT_TRUE(mesh_pkc_decrypt(
        PKC_SHARED_KEY, PKC_FROM_NODE, PKC_PACKET_ID, PKC_PAYLOAD, PKC_PAYLOAD_LEN, out));
    ASSERT_EQ_MEM(out, PKC_PLAINTEXT, PKC_PLAINTEXT_LEN);
}

TEST(test_pkc_round_trip) {
    const char* msg = "round trip";
    uint8_t key[32], wire[64], out[64];
    size_t n = strlen(msg);
    ASSERT_TRUE(mesh_pkc_shared_key(PKC_BOB_PRIV, PKC_ALICE_PUB, key));
    ASSERT_TRUE(mesh_pkc_encrypt(key, 0x55667788u, 42, 7, (const uint8_t*)msg, n, wire));
    ASSERT_TRUE(mesh_pkc_decrypt(key, 0x55667788u, 42, wire, n + MESH_PKC_OVERHEAD, out));
    ASSERT_EQ_MEM(out, msg, n);
}

TEST(test_pkc_tamper_and_wrong_key_fail) {
    uint8_t wire[PKC_PAYLOAD_LEN], out[PKC_PLAINTEXT_LEN], zero[PKC_PLAINTEXT_LEN] = {0};
    uint8_t key[32];

    memcpy(wire, PKC_PAYLOAD, PKC_PAYLOAD_LEN);
    wire[3] ^= 0x01;
    ASSERT_TRUE(!mesh_pkc_decrypt(
        PKC_SHARED_KEY, PKC_FROM_NODE, PKC_PACKET_ID, wire, PKC_PAYLOAD_LEN, out));
    /* No unauthenticated plaintext is left behind. */
    ASSERT_EQ_MEM(out, zero, PKC_PLAINTEXT_LEN);

    /* The extra nonce in the trailer is bound in through the nonce. */
    memcpy(wire, PKC_PAYLOAD, PKC_PAYLOAD_LEN);
    wire[PKC_PAYLOAD_LEN - 1] ^= 0x01;
    ASSERT_TRUE(!mesh_pkc_decrypt(
        PKC_SHARED_KEY, PKC_FROM_NODE, PKC_PACKET_ID, wire, PKC_PAYLOAD_LEN, out));

    /* So are the sender and the packet id. */
    ASSERT_TRUE(!mesh_pkc_decrypt(
        PKC_SHARED_KEY, PKC_FROM_NODE + 1, PKC_PACKET_ID, PKC_PAYLOAD, PKC_PAYLOAD_LEN, out));

    memcpy(key, PKC_SHARED_KEY, 32);
    key[0] ^= 0x80;
    ASSERT_TRUE(
        !mesh_pkc_decrypt(key, PKC_FROM_NODE, PKC_PACKET_ID, PKC_PAYLOAD, PKC_PAYLOAD_LEN, out));

    /* A payload no longer than the trailer is refused, as Router.cpp:953. */
    ASSERT_TRUE(!mesh_pkc_decrypt(
        PKC_SHARED_KEY, PKC_FROM_NODE, PKC_PACKET_ID, PKC_PAYLOAD, MESH_PKC_OVERHEAD, out));
}

TEST_MAIN_BEGIN()
RUN_TEST(test_sha256_fips_vectors);
RUN_TEST(test_aes256_fips197_c3);
RUN_TEST(test_x25519_rfc7748_5_2);
RUN_TEST(test_x25519_rfc7748_6_1);
RUN_TEST(test_ccm_nist_vtt256);
RUN_TEST(test_ccm_python_vector);
RUN_TEST(test_pkc_shared_key_both_directions);
RUN_TEST(test_pkc_rejects_low_order_key);
RUN_TEST(test_pkc_encrypt_matches_python);
RUN_TEST(test_pkc_decrypt_python_payload);
RUN_TEST(test_pkc_round_trip);
RUN_TEST(test_pkc_tamper_and_wrong_key_fail);
TEST_MAIN_END()
