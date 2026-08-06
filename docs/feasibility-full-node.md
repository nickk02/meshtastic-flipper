# Can a Flipper Zero FAP be a full Meshtastic node?

Date: 2026-08-05

Written because the project's stated end goal ("an app on my Flipper that makes
it a Meshtastic node I can connect to my phone") conflicts with the project
brief, which puts the phone API, direct messages, position, telemetry and
routing explicitly out of scope.

Conclusion up front: **no, not as a FAP.** The blocker is not Bluetooth and not
protocol complexity. It is that a FAP is RAM-resident and Meshtastic is roughly
an order of magnitude too large to live in RAM.

There is a path to a real Meshtastic node on Flipper *hardware*, covered in
section 5, but it stops being a Flipper.

## 1. Where I was wrong

I initially claimed the Flipper's Bluetooth stack was "barely reachable from an
app." That is incorrect and I am correcting it on the record.

`furi_hal_bt.h:86` exposes:

```c
FuriHalBleProfileBase* furi_hal_bt_start_app(
    const FuriHalBleProfileTemplate* profile_template,
    FuriHalBleProfileParams params,
    ...);
```

It takes a **custom profile template**, so an application genuinely can stand up
its own GATT service with its own UUIDs and characteristics. Implementing the
Meshtastic BLE service (a `ToRadio` write characteristic, a `FromRadio` read
characteristic, and a `FromNum` notify characteristic) is possible in principle.

So Bluetooth transport is not the reason this fails. Something else is.

## 2. The actual blocker: FAPs live in RAM

This is the whole argument, and it is structural rather than a matter of
optimization.

**Real Meshtastic runs from flash.** On ESP32 and nRF52 targets the firmware
image is on the order of 1MB, stored in flash and executed in place. Only its
mutable data occupies RAM.

**A Flipper FAP runs from RAM.** The `.fap` is an ELF that the loader reads off
the SD card, relocates, and places in the heap at launch. Its code, its
read-only data and its mutable data all consume heap. There is no execute in
place, and no way to ask for one.

The RAM available is fixed by the linker script,
`targets/f7/stm32wb55xx_flash.ld:8-12`:

| Region | Size | Purpose |
| --- | --- | --- |
| `RAM1` | `0x2FFF8`, 196,600 bytes (192KB) | Everything the application core does |
| `RAM2A` | 10KB | BLE core mailbox and shared memory |
| `RAM2B` | 10KB | BLE core |

192KB is the ceiling for firmware code data, firmware stacks, the heap, and any
FAP loaded into it. Free heap with the system running is a fraction of that.
Task 1 of the M0 plan measures the real figure on the device.

So the comparison is roughly **1MB of flash-resident code versus something
under 192KB of shared RAM**, and realistically closer to 100KB of free heap.
That is a factor of ten. Nothing is recoverable by trimming features, because
the gap is not in the features.

## 3. How much code a full node actually is

Measured against the sparse checkout of `meshtastic/firmware`, `src/mesh` only:

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
| **All of `src/mesh`** | **44,648** |

And `src/mesh` is not the whole node. It excludes `src/modules` (position,
telemetry, admin, canned messages, and the rest), the generated nanopb protobuf
code for the full message set, and the platform layers.

For scale, the receive-and-display slice this project actually builds is a few
hundred lines. The brief drew its line at exactly the right place, and named the
reason: `NodeDB.cpp` at 4,467 lines and `Router.cpp` at 1,566 lines are the
boundary between "receive and display" and "be a node."

Even on a deliberately generous estimate of compiled ARM Thumb-2 density, tens
of thousands of lines of C++ produces hundreds of kilobytes of code. Set against
a ceiling around 100KB of free heap, the arithmetic does not work at any
plausible density. This is why no amount of careful engineering closes it.

## 4. What each capability would additionally require

Even setting size aside, for completeness:

| Capability | Requires |
| --- | --- |
| Appear in others' node lists | Periodic `NodeInfo` broadcast. Protobuf encode. Small. |
| Send text on primary channel | Transmit path. Already M4 in the plan. |
| Receive direct messages | X25519 key exchange and AES-CCM. `CryptoEngine` PKI paths plus key storage. |
| Position and telemetry | The module system, GPS or fake position, `NodeDB` entries. |
| Relay traffic for others | `Router` plus `PacketHistory` for duplicate suppression. Real airtime and battery cost. |
| Phone app connection | `PhoneAPI` (2,199 lines), the full config and admin protobuf set, `NodeDB` for the node list the app displays, and a custom BLE GATT service. |

The phone connection is the most demanding item on the list, not the least. The
phone app does not just read messages, it reads and writes the node's entire
configuration and node database.

## 5. The one real path to a full node on Flipper hardware

Flash Meshtastic firmware onto the STM32WB55 in place of Flipper OS.

Meshtastic's repository now contains a `zephyr/` directory. It is embryonic, one
`prj.conf` and a single nRF54L15 board overlay, but the configuration is written
to be shared across future Zephyr targets. Zephyr supports the STM32WB55
natively. If that port matures, running real Meshtastic on Flipper hardware
becomes a board bringup problem rather than a rewrite, and the flash-resident
constraint disappears because it would be the firmware.

The cost is total: the device stops being a Flipper. No sub-GHz app, no NFC, no
infrared, no BadUSB, no app catalog. You would own a Meshtastic node in a
Flipper-shaped case, and you would still need to add the SX1262, because the
CC1101 cannot do LoRa.

This is worth checking again before committing significant time past M3, which
is what the brief's watch item already says.

## 6. The comparison worth making

A Heltec V3 or similar is roughly 30 USD, is a full Meshtastic node out of the
box, pairs with the phone app, and works today.

The Flipper version costs about 35 USD for the Electronic Cats add-on, plus a
logic analyzer, plus weeks of work, and lands somewhere well short of that.

That is not an argument against building it. It is an argument that the reason
to build it has to be "I want the Flipper to do this" rather than "I need a
Meshtastic node," because for the second one the answer is already on the desk.

## 7. Recommendation

Build the receiver in the spec. It is achievable, it is genuinely useful, and it
makes the Flipper do something it cannot currently do.

If M3 lands and the size numbers come in better than expected, the honest
stretch is transmit plus `NodeInfo` broadcast, which would make the Flipper
visible to other nodes as a minimal participant. That is the realistic ceiling
for a FAP, and it is worth aiming at.

Phone connectivity is not on that ladder, and no amount of scoping gets it
there.
