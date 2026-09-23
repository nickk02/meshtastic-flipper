#!/usr/bin/env python3
"""Generate host test vectors for the Meshtastic receive path.

Independence is the point. AES comes from OpenSSL via `cryptography`, and the
Data protobuf comes from the real meshtastic package, so a shared
misunderstanding between the generator and the C code cannot quietly pass.

Run from the repo root:
    python test/tools/gen_vectors.py > test/host/vectors.h

Output is pure ASCII regardless of the payload contents, since every byte is
emitted in hex.
"""

import hashlib
import struct
import sys

from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.ciphers.aead import AESCCM
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

try:
    from meshtastic.protobuf import mesh_pb2, portnums_pb2
except ImportError:  # older meshtastic package layout
    from meshtastic import mesh_pb2, portnums_pb2

# Channels.h:153-154 in meshtastic/firmware.
DEFAULT_PSK = bytes([
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
    0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01,
])


def expand_psk(index: int) -> bytes:
    """Channels.cpp:254-267. Index 1 means defaultpsk unchanged."""
    if index == 0:
        raise ValueError("index 0 means encryption disabled")
    key = bytearray(DEFAULT_PSK)
    key[-1] = (key[-1] + index - 1) & 0xFF
    return bytes(key)


def build_nonce(packet_id: int, from_node: int) -> bytes:
    """CryptoEngine::initNonce.

    8 bytes packet id little endian, 4 bytes source node little endian, then a
    4 byte block counter starting at zero.
    """
    return struct.pack("<QII", packet_id, from_node, 0)


def aes_ctr(key: bytes, nonce: bytes, data: bytes) -> bytes:
    encryptor = Cipher(algorithms.AES(key), modes.CTR(nonce)).encryptor()
    return encryptor.update(data) + encryptor.finalize()


def build_data(text: str) -> bytes:
    data = mesh_pb2.Data()
    data.portnum = portnums_pb2.PortNum.TEXT_MESSAGE_APP
    data.payload = text.encode("utf-8")
    return data.SerializeToString()


def xor_hash(b: bytes) -> int:
    h = 0
    for x in b:
        h ^= x
    return h


def channel_hash(name: str, key: bytes) -> int:
    """Channels::getHash. xorHash(name) XOR xorHash(key)."""
    return xor_hash(name.encode("utf-8")) ^ xor_hash(key)


def build_header(to, frm, packet_id, flags, chan_hash, next_hop, relay):
    """RadioInterface.h:36-53. Exactly 16 bytes, little endian."""
    header = struct.pack("<IIIBBBB", to, frm, packet_id, flags, chan_hash,
                         next_hop, relay)
    assert len(header) == 16, "PacketHeader must be exactly 16 bytes"
    return header


def c_bytes(name, data):
    """Emit a byte array.

    Empty arrays are emitted as a single zero because a zero-length array is
    not valid C99. The matching _LEN define carries the real length.
    """
    payload = data if data else b"\x00"
    body = ", ".join("0x%02x" % b for b in payload)
    return "static const uint8_t %s[] VEC_UNUSED = {%s};\n" % (name, body)


CASES = [
    # label, text, to, from, packet_id, hop_limit, hop_start, psk_index, channel
    ("simple", "hello mesh", 0xFFFFFFFF, 0x11223344, 0x0A0B0C0D, 3, 3, 1, "LongFast"),
    ("empty", "", 0xFFFFFFFF, 0x00000001, 0x00000001, 0, 0, 1, "LongFast"),
    ("long", "x" * 180, 0xFFFFFFFF, 0xDEADBEEF, 0xCAFEBABE, 7, 7, 1, "LongFast"),
    ("psk2", "second key", 0xFFFFFFFF, 0x0000ABCD, 0x00001234, 2, 3, 2, "LongFast"),
    ("utf8", "cafe ✓ éè", 0xFFFFFFFF, 0x55667788, 0x99AABBCC, 3, 3, 1, "LongFast"),
]


def build_case(case):
    """Compute every derived value for one case."""
    (label, text, to, frm, pid, hop_limit, hop_start, psk_index, chan_name) = case
    key = expand_psk(psk_index)
    nonce = build_nonce(pid, frm)
    plaintext = build_data(text)
    ciphertext = aes_ctr(key, nonce, plaintext)
    chash = channel_hash(chan_name, key)
    flags = (hop_limit & 0x07) | ((hop_start & 0x07) << 5)
    header = build_header(to, frm, pid, flags, chash, 0, 0)
    return {
        "label": label,
        "text": text,
        "raw": text.encode("utf-8"),
        "key": key,
        "nonce": nonce,
        "plaintext": plaintext,
        "ciphertext": ciphertext,
        "chash": chash,
        "flags": flags,
        "frame": header + ciphertext,
        "to": to,
        "from": frm,
        "pid": pid,
        "hop_limit": hop_limit,
        "hop_start": hop_start,
        "psk_index": psk_index,
    }


def pkc_nonce(packet_id: int, from_node: int, extra_nonce: int) -> bytes:
    """CryptoEngine::initNonce with an extra nonce, cut to CCM's 13 bytes.

    16 bytes zeroed, the 64 bit packet id at 0, the from node at 8, then the
    extra nonce written over bytes 4 to 7 when it is non-zero. aes_ccm_ae uses
    the first 15 - L = 13 bytes (aes-ccm.cpp:60, L = 2).
    """
    nonce = bytearray(16)
    nonce[0:8] = struct.pack("<Q", packet_id)
    nonce[8:12] = struct.pack("<I", from_node)
    if extra_nonce:
        nonce[4:8] = struct.pack("<I", extra_nonce)
    return bytes(nonce[:13])


def x25519_keypair(seed: bytes):
    """A fixed keypair: the private key is SHA-256 of a label."""
    priv = hashlib.sha256(seed).digest()
    key = X25519PrivateKey.from_private_bytes(priv)
    pub = key.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
    return key, priv, pub


def write_ccm_vector(out):
    """A plain AES-256-CCM vector, 13 byte nonce, 8 byte tag, 8 bytes of AD.

    40 bytes of plaintext, so the last block is partial.
    """
    key = bytes(range(0x40, 0x60))
    nonce = bytes(range(0x10, 0x1d))
    aad = struct.pack("<II", 0x11223344, 0x55667788)
    plaintext = bytes(range(40))
    sealed = AESCCM(key, tag_length=8).encrypt(nonce, plaintext, aad)
    out.write("/* AES-256-CCM, tag 8, nonce 13, from cryptography's AESCCM */\n")
    out.write(c_bytes("CCM_KEY", key))
    out.write(c_bytes("CCM_NONCE", nonce))
    out.write(c_bytes("CCM_AAD", aad))
    out.write("#define CCM_AAD_LEN %d\n" % len(aad))
    out.write(c_bytes("CCM_PLAINTEXT", plaintext))
    out.write("#define CCM_PLAINTEXT_LEN %d\n" % len(plaintext))
    out.write(c_bytes("CCM_CIPHERTEXT", sealed[:-8]))
    out.write(c_bytes("CCM_TAG", sealed[-8:]))
    out.write("#define CCM_TAG_LEN 8\n\n")


def write_pkc_vector(out):
    """A Meshtastic direct message from Alice to Bob.

    CryptoEngine::encryptCurve25519 (CryptoEngine.cpp:223-251): key is
    SHA-256 of the X25519 secret, AES-256-CCM with an 8 byte tag and no AD,
    and the payload is ciphertext, tag, then the 4 byte extra nonce.
    """
    alice, alice_priv, alice_pub = x25519_keypair(b"meshtastic-flipper pkc alice")
    bob, bob_priv, bob_pub = x25519_keypair(b"meshtastic-flipper pkc bob")
    secret = alice.exchange(bob.public_key())
    assert secret == bob.exchange(alice.public_key())
    key = hashlib.sha256(secret).digest()

    frm, to, pid, extra = 0x11223344, 0x55667788, 0x0A0B0C0D, 0xA1B2C3D4
    nonce = pkc_nonce(pid, frm, extra)
    plaintext = build_data("hello bob, this is a direct message")
    sealed = AESCCM(key, tag_length=8).encrypt(nonce, plaintext, None)
    payload = sealed + struct.pack("<I", extra)

    out.write("/* PKC direct message, Alice to Bob */\n")
    out.write(c_bytes("PKC_ALICE_PRIV", alice_priv))
    out.write(c_bytes("PKC_ALICE_PUB", alice_pub))
    out.write(c_bytes("PKC_BOB_PRIV", bob_priv))
    out.write(c_bytes("PKC_BOB_PUB", bob_pub))
    out.write(c_bytes("PKC_SHARED_KEY", key))
    out.write("#define PKC_FROM_NODE 0x%08xu\n" % frm)
    out.write("#define PKC_TO_NODE 0x%08xu\n" % to)
    out.write("#define PKC_PACKET_ID 0x%08xu\n" % pid)
    out.write("#define PKC_EXTRA_NONCE 0x%08xu\n" % extra)
    out.write(c_bytes("PKC_NONCE", nonce))
    out.write(c_bytes("PKC_PLAINTEXT", plaintext))
    out.write("#define PKC_PLAINTEXT_LEN %d\n" % len(plaintext))
    out.write(c_bytes("PKC_PAYLOAD", payload))
    out.write("#define PKC_PAYLOAD_LEN %d\n\n" % len(payload))


def main():
    out = sys.stdout
    out.write("/* Generated by test/tools/gen_vectors.py. Do not hand-edit. */\n")
    # Keep clang-format away from generated output. Without this the SDK
    # formatter rewrites the file and CI's regenerate-and-diff check fails
    # permanently, since the generator would never reproduce the formatted form.
    out.write("/* clang-format off */\n")
    out.write("#ifndef VECTORS_H\n#define VECTORS_H\n\n")
    out.write("#include <stdint.h>\n\n")
    out.write("/* Not every test uses every vector, and -Werror would reject\n"
              "   the unused ones. */\n")
    out.write("#if defined(__GNUC__)\n"
              "#define VEC_UNUSED __attribute__((unused))\n"
              "#else\n"
              "#define VEC_UNUSED\n"
              "#endif\n\n")
    out.write("#define VECTOR_COUNT %d\n\n" % len(CASES))

    for i, (label, text, to, frm, pid, hop_limit, hop_start, psk_index,
            chan_name) in enumerate(CASES):
        key = expand_psk(psk_index)
        nonce = build_nonce(pid, frm)
        plaintext = build_data(text)
        ciphertext = aes_ctr(key, nonce, plaintext)
        chash = channel_hash(chan_name, key)
        flags = (hop_limit & 0x07) | ((hop_start & 0x07) << 5)
        header = build_header(to, frm, pid, flags, chash, 0, 0)
        frame = header + ciphertext
        raw = text.encode("utf-8")

        p = "VEC%d_" % i
        out.write("/* case %d: %s */\n" % (i, label))
        out.write(c_bytes(p + "KEY", key))
        out.write("#define %sPACKET_ID 0x%08xu\n" % (p, pid))
        out.write("#define %sFROM_NODE 0x%08xu\n" % (p, frm))
        out.write("#define %sTO_NODE 0x%08xu\n" % (p, to))
        out.write("#define %sFLAGS 0x%02xu\n" % (p, flags))
        out.write("#define %sHOP_LIMIT %d\n" % (p, hop_limit))
        out.write("#define %sHOP_START %d\n" % (p, hop_start))
        out.write("#define %sCHANNEL_HASH 0x%02xu\n" % (p, chash))
        out.write("#define %sPORTNUM %d\n"
                  % (p, portnums_pb2.PortNum.TEXT_MESSAGE_APP))
        out.write(c_bytes(p + "NONCE", nonce))
        out.write(c_bytes(p + "PLAINTEXT", plaintext))
        out.write("#define %sPLAINTEXT_LEN %d\n" % (p, len(plaintext)))
        out.write(c_bytes(p + "CIPHERTEXT", ciphertext))
        out.write("#define %sCIPHERTEXT_LEN %d\n" % (p, len(ciphertext)))
        out.write(c_bytes(p + "FRAME", frame))
        out.write("#define %sFRAME_LEN %d\n" % (p, len(frame)))
        out.write(c_bytes(p + "TEXT", raw))
        out.write("#define %sTEXT_LEN %d\n\n" % (p, len(raw)))

    write_ccm_vector(out)
    write_pkc_vector(out)
    out.write("#endif\n")


if __name__ == "__main__":
    main()
