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

**The watermark needed a second reading, and it came out fine.**
`memmgr_get_minimum_free_heap` is `xPortGetMinimumEverFreeHeapSize`
(`furi/core/memmgr.c:58-60`), the lowest free heap since boot, so it is
sensitive to whatever else happened in the session.

| Session | Free | Watermark |
| --- | --- | --- |
| After USB install | 128,728 | 2,784 |
| After a clean reboot | 129,792 | 114,408 |

The first reading suggested the device had come within 2.7KB of exhausting its
heap. The reboot shows that is not the steady state: on a clean boot, free heap
never drops below about 114KB. The 2,784 figure came from the install session
itself, most likely USB file transfer and the loader relocating a FAP.

Practical reading: **about 114KB is reliably available even at the worst moment
of a normal boot**, and 128KB is the idle figure. Heavy USB or file operations
can transiently consume far more, so the existing discipline still applies:
allocate nothing on the receive path, keep state static. But there is no
memory-pressure problem to design around.

Self test reported PASS in both sessions.

### Effect on the full-node feasibility argument

None. `docs/feasibility-full-node.md` argued that roughly 1MB of flash-resident
Meshtastic firmware cannot fit in a RAM-resident FAP. The measured ceiling is
128KB rather than the estimated 100KB, so the gap is about eightfold instead of
tenfold. The conclusion is unchanged.

## FAP size as the app grew

Measured on each release build. The ceiling is free heap, 128,728 bytes, since
a FAP is loaded into the heap rather than run from flash.

| Release | Contents | Bytes | Share of free heap |
| --- | --- | --- | --- |
| v0.1.0-m0 | protocol core, heap measurement, decode self test | 6,120 | 4.8% |
| v0.2.0-app | plus models, three views, threading, simulated source | 11,400 | 8.9% |
| v0.3.0-radio | plus SX1262 driver and LoRa configuration | 19,580 | 15.2% |

Binary size was named in the brief as the scarcest resource in the project.
Measurement says otherwise: the complete receiver, radio driver included, uses
about a seventh of the available heap. Nothing has had to be traded away for
size, and nothing looks likely to.

## Radio parameters, derived and tested

Not measured on hardware, but derived from the Meshtastic source and checked
against an independently known result, which is the strongest available check
short of a radio.

| | Value | Source |
| --- | --- | --- |
| Frequency | 906.875 MHz | derived, matches documented US LongFast |
| Frequency slot | 19, channel 20 | djb2("LongFast") mod 104 |
| Spreading factor | 11 | `MeshRadio.h:282-287` |
| Bandwidth | 250 kHz | `MeshRadio.h:282-287` |
| Coding rate | 4/5 | `MeshRadio.h:282-287` |
| Sync word registers | 0x0740 = 0x24, 0x0741 = 0xB4 | nibble split of 0x2b |
| Low data rate optimize | off | symbol 8.192ms, below the 16ms threshold |
