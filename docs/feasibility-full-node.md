# Can a Flipper Zero FAP be a full Meshtastic node?

Date: 2026-08-05. Memory figures updated 2026-08-06 with measured values.

The project owner asked for "an app on my Flipper that makes it a Meshtastic
node I can connect to my phone". The project brief puts the phone API, direct
messages, position, telemetry and routing out of scope. This document explains
which is achievable.

The answer is no, not as a FAP. Bluetooth is not the blocker. Protocol
complexity is not the blocker. A FAP runs from RAM, and Meshtastic is about
eight times too large to fit there.

Section 5 covers a path to a real Meshtastic node on Flipper hardware. That
path stops being a Flipper.

## 1. Where I was wrong

I first claimed that a Flipper app can barely reach the Bluetooth stack. That
is wrong. I am correcting it here.

`furi_hal_bt.h:86` declares:

```c
FuriHalBleProfileBase* furi_hal_bt_start_app(
    const FuriHalBleProfileTemplate* profile_template,
    FuriHalBleProfileParams params,
    ...);
```

It accepts a custom profile template. An app can therefore create its own GATT
service, with its own UUIDs and characteristics. The Meshtastic BLE service
needs three: a `ToRadio` write characteristic, a `FromRadio` read
characteristic, and a `FromNum` notify characteristic. An app can create all
three.

Bluetooth transport is not why this fails.

## 2. The blocker: a FAP runs from RAM

This is the whole argument. It is structural. No amount of optimization changes
it.

Real Meshtastic runs from flash. On ESP32 and nRF52 targets the firmware image
is about 1MB. It sits in flash and executes in place. Only its mutable data
uses RAM.

A Flipper FAP runs from RAM. The loader reads the `.fap` off the SD card,
relocates it, and places it in the heap. Its code, its constants and its data
all consume heap. There is no execute in place, and no way to request one.

The linker script fixes the available RAM,
`targets/f7/stm32wb55xx_flash.ld:8-12`:

| Region | Size | Purpose |
| --- | --- | --- |
| `RAM1` | `0x2FFF8`, 196,600 bytes | Everything the application core does |
| `RAM2A` | 10KB | BLE core mailbox and shared memory |
| `RAM2B` | 10KB | BLE core |

Measurement on hardware, 2026-08-06: total heap 187,448 bytes, free heap
128,728 bytes with an app running. See `docs/measurements.md`.

So the comparison is about 1MB of flash-resident code against about 128KB of
heap. That is a factor of eight. The gap is not in the features, so trimming
features does not close it.

## 3. How much code a full node is

Measured from a sparse checkout of `meshtastic/firmware`, `src/mesh` only:

| Component | Lines |
| --- | --- |
| `NodeDB.cpp` | 4,467 |
| `PhoneAPI.cpp` | 2,199 |
| `Router.cpp` | 1,566 |
| `MeshService.cpp` | 645 |
| `NextHopRouter.cpp` | 602 |
| `StreamAPI.cpp` | 284 |
| `ReliableRouter.cpp` | 207 |
| `aes-ccm.cpp` | 180 |
| `FloodingRouter.cpp` | 174 |
| All of `src/mesh` | 44,648 |

`src/mesh` is not the whole node. It excludes `src/modules`, which holds
position, telemetry, admin and canned messages. It excludes the generated
protobuf code for the full message set. It excludes the platform layers.

For scale: the receive and display slice this project builds is a few hundred
lines.

Compiled ARM Thumb-2 code from tens of thousands of lines of C++ runs to
hundreds of kilobytes, even on a generous estimate. The measured ceiling is
about 128KB of heap. The arithmetic does not work at any plausible code
density.

The brief drew its scope line at the right place, and named the reason:
`NodeDB.cpp` at 4,467 lines and `Router.cpp` at 1,566 lines separate "receive
and display" from "be a node".

## 4. What each capability needs

Set size aside for a moment. This is what each feature would still require.

| Capability | Requires |
| --- | --- |
| Appear in other nodes' node lists | A periodic `NodeInfo` broadcast. Protobuf encoding. Small. |
| Send text on the primary channel | A transmit path. Already planned for M4. |
| Receive direct messages | X25519 key exchange and AES-CCM. The `CryptoEngine` PKI paths, plus key storage. |
| Report position and telemetry | The module system, a GPS or a fixed position, and `NodeDB` entries. |
| Relay traffic for others | `Router`, plus `PacketHistory` to suppress duplicates. Real airtime and battery cost. |
| Connect to the phone app | `PhoneAPI` at 2,199 lines, the full config and admin protobuf set, `NodeDB` for the node list, and a custom BLE GATT service. |

The phone connection is the most demanding item, not the least. The phone app
does not only read messages. It reads and writes the node's whole
configuration and node database.

The handshake is also all or nothing. On connect the app sends a
`want_config_id`. It then expects a full sequence in reply: MyNodeInfo, the
node's own NodeInfo, every Config block, every ModuleConfig block, all
Channels, and a completion marker. A partial implementation does not half
work. The app fails to connect.

## 5. The one real path to a full node on Flipper hardware

Flash Meshtastic firmware onto the STM32WB55, in place of Flipper OS.

The Meshtastic repository now contains a `zephyr/` directory. Today it holds
one `prj.conf` and a single nRF54L15 board overlay, so it is embryonic. The
configuration is written to be shared across future Zephyr targets. Zephyr
supports the STM32WB55 natively.

If that port matures, running Meshtastic on Flipper hardware becomes a board
bring-up problem rather than a rewrite. The flash-resident constraint
disappears, because Meshtastic would be the firmware.

The cost is total. The device stops being a Flipper. No sub-GHz app, no NFC, no
infrared, no BadUSB, no app catalog. You would own a Meshtastic node in a
Flipper-shaped case. You would still need to add an SX1262, because the CC1101
cannot demodulate LoRa.

Check the state of that port before committing significant time past M3.

## 6. The comparison worth making

A Heltec V3 or similar costs about 30 USD. It is a full Meshtastic node out of
the box. It pairs with the phone app. It works today.

The Flipper version costs about 35 USD for the Electronic Cats add-on, plus an
antenna, plus a logic analyzer, plus weeks of work. It lands well short of
that.

This is not an argument against building it. It is an argument that the reason
to build it must be "I want the Flipper to do this". If the reason is "I need a
Meshtastic node", the answer is already on the desk.

## 7. Recommendation

Build the receiver the spec describes. It is achievable. It is useful. It makes
the Flipper do something it cannot do today.

If M3 lands and the size numbers allow, add transmit and a `NodeInfo`
broadcast. Other nodes would then list the Flipper as a minimal participant.
That is the realistic ceiling for a FAP, and it is worth aiming at.

Phone connectivity is not on that ladder. No amount of scoping puts it there.
