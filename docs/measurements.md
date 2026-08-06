# Measurements

Numbers observed rather than estimated. Anything not measured yet says so.

## M0 acceptance, 2026-08-05

Synthesized encrypted frames decode to byte-exact original text through the
full chain: header parse, channel hash match, AES128-CTR decrypt, `Data`
protobuf decode.

Covered cases: a short message, an empty payload, a 180 byte payload spanning
multiple AES blocks, a non-default PSK index, and a multi-byte UTF-8 payload.

Host suite: **170 checks, 0 failures**, under `-std=c99 -Wall -Wextra -Werror`.

| Test binary | Checks |
| --- | --- |
| `test_aes` | 5 |
| `test_channel` | 18 |
| `test_crypto` | 22 |
| `test_data` | 43 |
| `test_decode` | 40 |
| `test_harness` | 3 |
| `test_header` | 39 |

Toolchain: gcc 15.2.0, Python 3.10.11 with `cryptography` 48.0.0 and the
`meshtastic` package for vector generation.

### Notable results

**AES-CTR semantics match.** The nonce layout and counter behaviour agree
byte-for-byte with OpenSSL through `cryptography`. The encrypt direction
reproduces the generator's ciphertext exactly, which is a stronger check than
decrypting alone. This clears M0's kill condition.

**The protobuf walker rejects garbage.** Of 1000 pseudo-random 64 byte buffers,
**1** parsed as valid protobuf. A wrong key produces effectively random
plaintext, so a 0.1% false-accept rate means bogus messages will essentially
never reach the screen. Anything above roughly 20% would have meant the parser
was too permissive.

**NIST vector confirmed independently.** The AES128-CTR known-answer test uses
NIST SP 800-38A F.5.1, reproduced with OpenSSL before being written into the
test rather than transcribed from memory.

### Layer sizes

| | Lines |
| --- | --- |
| `src/proto/` | 494 |
| `test/host/test_*.c` | 748 |

More test than implementation, which is the intended ratio for the layer where
silent wrongness is hardest to diagnose.

### Dependency check

`src/proto/` includes only `stdbool.h`, `stddef.h`, `stdint.h`, `string.h`,
`aes.h`, and its own headers. No Flipper headers, no allocation.

## Device measurements

**Not yet taken.** Task 1 of the M0 plan requires a Flipper connected by USB and
has not been run. Pending numbers:

- ufbt SDK target version, to pin the build
- Empty FAP size
- Free heap with the app running
- Total heap

Free heap is the practical ceiling for everything the app allocates at runtime,
and since a FAP is loaded into the heap, code size competes with it directly.
The figure of roughly 100KB used in `feasibility-full-node.md` is an estimate
until this is run.
