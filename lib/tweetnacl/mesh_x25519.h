/* X25519 (RFC 7748 section 5), vendored from TweetNaCl 20140427.
 * See mesh_x25519.c for origin and license. */
#ifndef MESH_X25519_H
#define MESH_X25519_H

#define MESH_X25519_LEN 32

/* q = X25519(n, p). n is the 32 byte scalar (clamped internally), p the
 * 32 byte u coordinate (top bit ignored). Always returns 0; it does not
 * reject low order points, so callers check for an all zero result. */
int mesh_x25519(unsigned char* q, const unsigned char* n, const unsigned char* p);

/* q = X25519(n, 9): the public key for private key n. */
int mesh_x25519_base(unsigned char* q, const unsigned char* n);

#endif
