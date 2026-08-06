# Measurements

Numbers I observed. Anything I estimated instead says so.

## M0 acceptance, 2026-08-05

Synthesized encrypted frames decode to byte-exact original text. The chain runs
header parse, channel hash match, AES128-CTR decrypt, then `Data` protobuf
decode.

I covered five cases: a short message, an empty payload, a 180 byte payload
that spans several AES blocks, a non-default PSK index, and a multi-byte UTF-8
payload.

Toolchain: gcc 15.2.0, and Python 3.10.11 with `cryptography` 48.0.0 and the
`meshtastic` package for vector generation.

### Results worth calling out

AES-CTR matches. The nonce layout and the counter behaviour agree with OpenSSL
byte for byte, through `cryptography`. The encrypt direction reproduces the
generator's ciphertext exactly, which is a stronger check than decrypting
alone. This cleared M0's kill condition.

The protobuf walker rejects garbage. Of 1000 pseudo-random 64 byte buffers,
1 parsed as valid protobuf. A wrong key produces effectively random plaintext,
so a 0.1 percent false accept rate means bogus messages almost never reach the
screen. Above roughly 20 percent would have meant the parser was too
permissive.

The NIST vector is independent. The AES128-CTR known-answer test uses NIST SP
800-38A F.5.1. I reproduced it with OpenSSL before writing it into the test. I
did not transcribe it from memory.

### Dependency check

`src/proto/` includes only `stdbool.h`, `stddef.h`, `stdint.h`, `string.h`,
`aes.h`, and its own headers. No Flipper headers. No allocation.

## Device measurements, 2026-08-06

Taken on hardware. These replace the earlier estimates.

| | Bytes |
| --- | --- |
| Total heap | 187,448 |
| Free heap, app running | 128,728 |
| `meshtastic.fap` at the time | 6,120 |

ufbt SDK target: 1.4.3, release channel, f7, API 87.1.

The decode self test reported PASS. The full chain produced the expected text
on the device. The protocol layer is therefore correct on ARM as well as on
x86. Only the radio remains unproven.

### Size is not the constraint the brief expected

The brief called binary size "the scarcest resource in this project, ahead of
CPU". The measurements disagree.

`RAM1` is 196,600 bytes (`stm32wb55xx_flash.ld:10`). Of that, 187,448 is heap.
The rest holds the firmware's static data and stacks. With the system idle and
this app running, firmware and services use about 59KB of the heap.

That leaves about 128KB for an app. The first build used 6,120 bytes of it.

### The heap watermark needed a second reading

`memmgr_get_minimum_free_heap` is `xPortGetMinimumEverFreeHeapSize`
(`furi/core/memmgr.c:58-60`). It reports the lowest free heap since boot, so
whatever else happened in the session affects it.

| Session | Free | Watermark |
| --- | --- | --- |
| After a USB install | 128,728 | 2,784 |
| After a clean reboot | 129,792 | 114,408 |

The first reading suggested the device came within 2.7KB of exhausting its
heap. The reboot shows that is not the steady state. On a clean boot, free heap
never drops below about 114KB. The 2,784 figure came from the install session
itself, most likely the USB file transfer and the loader relocating a FAP.

So about 114KB is reliably available, even at the worst moment of a normal
boot. 128KB is the idle figure. Heavy USB or file operations can consume far
more for a short time.

The existing discipline still applies: allocate nothing on the receive path,
and keep state static. There is no memory pressure to design around.

The self test reported PASS in both sessions.

### Effect on the full-node argument

None. `docs/feasibility-full-node.md` argues that about 1MB of flash-resident
Meshtastic firmware cannot fit in a RAM-resident FAP. The measured ceiling is
128KB rather than the estimated 100KB. The gap is eightfold instead of
tenfold. The conclusion does not change.

## FAP size as the app grew

Measured on each release build. Free heap of 128,728 bytes is the ceiling,
because a FAP loads into the heap rather than running from flash.

| Release | Contents | Bytes | Share of free heap |
| --- | --- | --- | --- |
| v0.1.0-m0 | protocol core, heap measurement, decode self test | 6,120 | 4.8% |
| v0.2.0-app | plus models, three views, threading, simulated source | 11,400 | 8.9% |
| v0.3.0-radio | plus SX1262 driver and LoRa configuration | 19,580 | 15.2% |

The complete receiver, radio driver included, uses about a seventh of the
available heap. Nothing has been traded away for size. Nothing looks likely to
need to be.

The transmit encoder added 0 bytes. Nothing calls it yet, so the linker drops
it.

## Radio parameters

I derived these from the Meshtastic source and checked them against an
independently known result. I did not measure them on hardware.

| | Value | Source |
| --- | --- | --- |
| Frequency | 906.875 MHz | derived, matches the documented US LongFast frequency |
| Frequency slot | 19, channel 20 | djb2("LongFast") mod 104 |
| Spreading factor | 11 | `MeshRadio.h:282-287` |
| Bandwidth | 250 kHz | `MeshRadio.h:282-287` |
| Coding rate | 4/5 | `MeshRadio.h:282-287` |
| Sync word registers | 0x0740 = 0x24, 0x0741 = 0xB4 | nibble split of 0x2b |
| Low data rate optimize | off | symbol is 8.192ms, below the 16ms threshold |

Two routes reached 906.875 MHz. I derived it from region and preset constants
in the firmware source. It also matches the frequency the Meshtastic community
documents. That agreement is what makes the value trustworthy.
