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

## Device measurements, 2026-08-06

Taken on hardware. These replace the estimates used earlier.

| | Bytes | |
| --- | --- | --- |
| Total heap | 187,448 | ~183KB |
| Free heap, app running | 128,728 | ~126KB |
| Minimum ever free since boot | 2,784 | ~2.7KB |
| `meshtastic.fap` | 6,120 | ~6KB |

ufbt SDK target: **1.4.3**, release channel, f7, API 87.1.

**Decode core self test: PASS.** The full chain, header parse through channel
key expansion, AES128-CTR decrypt and protobuf decode, produces the exact
expected text on the device. The protocol layer is confirmed correct on ARM as
well as x86, so only the radio remains unproven.

### What the numbers mean

**Size is not the constraint I expected.** The FAP is 6,120 bytes against
128,728 free, which is under 5 percent. My working estimate had been roughly
100KB free; the real figure is about 28 percent higher. There is ample room for
the SX1262 driver, the RX thread and the UI.

For context on where the RAM goes: `RAM1` is 196,600 bytes
(`stm32wb55xx_flash.ld:10`), of which 187,448 is heap. The remainder is the
firmware's static data and stacks. With the system idle and this app running,
firmware and services occupy roughly 59KB of that heap.

**The watermark deserves attention.** `memmgr_get_minimum_free_heap` is
`xPortGetMinimumEverFreeHeapSize` (`furi/core/memmgr.c:58-60`), the lowest free
heap since boot. A reading of 2,784 bytes means the device came within about
2.7KB of exhausting its heap at some point in that session.

This does not threaten the current design, which allocates nothing in the
receive path and keeps under 5KB of static state. It does mean **128KB is the
quiet-state figure, not a guarantee**. Something on the device, most likely the
app loader relocating a FAP or a service loading assets, can transiently
consume nearly all of it.

Practical consequence: keep allocating nothing on the hot path, and do not size
future buffers against 128KB as though it were always available. Worth
re-reading the watermark after a clean reboot to establish a baseline, since
this reading includes whatever happened during install over USB.

### Effect on the full-node feasibility argument

None. `docs/feasibility-full-node.md` argued that roughly 1MB of flash-resident
Meshtastic firmware cannot fit in a RAM-resident FAP. The measured ceiling is
128KB rather than the estimated 100KB, so the gap is about eightfold instead of
tenfold. The conclusion is unchanged.
