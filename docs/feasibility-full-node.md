# Can a Flipper Zero FAP be a full Meshtastic node?

Date: 2026-08-05. Memory figures updated 2026-08-06 with measured values.

The project owner asked for "an app on my Flipper that makes it a Meshtastic
node I can connect to my phone". This document works out which parts of that
are achievable.

It answers two separate questions, and I originally merged them into one. That
was a mistake, corrected in section 1.

1. Can a FAP be a full Meshtastic node? No. Section 2 explains why.
2. Can a FAP connect to the Meshtastic phone app? Probably yes. Section 4
   explains what that needs. It is much less work than I first claimed.

## 1. Two corrections I owe

### Bluetooth is available to apps

I first said a Flipper app can barely reach the Bluetooth stack. That is wrong.

`furi_hal_bt.h:86` declares `furi_hal_bt_start_app`. It accepts a custom
profile template, and `FuriHalBleProfileTemplate` is a fully defined struct
with `start`, `stop` and `get_gap_config` hooks. `profile_interface.h` and
`gatt.h` are both in the FAP-visible SDK.

The firmware also publishes an API table listing every symbol an app may call.
It marks 2,444 symbols as exported and 1,087 as blocked.
`furi_hal_bt_start_app` is exported. So an app can create its own GATT service.
This is confirmed, not inferred.

### The phone handshake is four messages, not forty

I then said the phone app needs about 40 messages before it will connect, and
that this made phone support impractical. That is also wrong.

The error was in my method. I read the firmware, saw it send 40 messages during
the config handshake, and assumed the client required all of them. The firmware
sends everything because it has everything. Reading one side and inferring the
other proves nothing about the other.

The Android client's own test suite settles it
(`MeshConfigFlowManagerImplTest.kt`):

```kotlin
fun `Stage 1 complete without metadata still succeeds with null firmware version`() {
    handleMyInfo(protoMyNodeInfo)
    manager.handleConfigComplete(HandshakeConstants.CONFIG_NONCE)
    verify { connectionManager.onRadioConfigLoaded() }
}
```

That is the whole of Stage 1. Two messages. No Config blocks, no ModuleConfig
blocks, no Channels, no DeviceMetadata, no region presets, no file manifest.

`buildMyNodeInfo` in `MeshConfigFlowManagerImpl.kt:463` reads only four fields
from MyNodeInfo: `my_node_num`, `min_app_version`, `device_id` and `pio_env`.
Its `metadata` parameter is nullable throughout. It returns null only if an
exception is thrown.

Nothing in the client tracks which config blocks arrived. There is no
"required config" set to satisfy.

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

## 4. What phone connectivity actually needs

Revised after reading the Android client.

### The BLE service

ESP32 nodes expose one service with three characteristics.
`src/BluetoothCommon.h:9-14`:

| Characteristic | UUID | Role |
| --- | --- | --- |
| Service | `6ba1b218-15a8-461f-9fa8-5dcae273eafd` | What the app scans for |
| ToRadio | `f75c76d2-129e-4dad-a1dd-7866124401e7` | The phone writes a ToRadio protobuf |
| FromRadio | `2c55e69e-4993-11ed-b878-0242ac120002` | The phone reads one FromRadio message per read |
| FromNum | `ed9da18c-a800-4f66-a670-aa7547e34453` | The device notifies. A doorbell, carrying no data |

The flow is simple. The device notifies FromNum when data is waiting. The phone
then reads FromRadio repeatedly until a read returns empty.

`NimbleBluetooth.cpp` is 1,085 lines, but most of that handles synchronization
between the NimBLE FreeRTOS task and the main task. The protocol itself is
small.

### The handshake

Four messages:

1. MyNodeInfo. The client reads `my_node_num`, `min_app_version`, `device_id`
   and `pio_env`.
2. `config_complete_id` carrying the config nonce. Stage 1 ends.
3. Our own NodeInfo. An empty node set is valid.
4. `config_complete_id` carrying the node-info nonce. Stage 2 ends and the
   connection reports Complete.

After that, received frames stream to the phone as MeshPacket inside FromRadio.

### The remaining capabilities

| Capability | Requires |
| --- | --- |
| Appear in other nodes' node lists | A periodic `NodeInfo` broadcast over the air. Small. |
| Send text on the primary channel | A transmit path. Planned for M4. |
| Phone app connects and shows messages | The BLE service above, four protobuf messages, and MeshPacket encoding. |
| Receive direct messages | X25519 and AES-CCM. The PKI paths, plus key storage. |
| Report position and telemetry | The module system, a position source, and NodeDB entries. |
| Relay traffic for others | `Router` plus `PacketHistory`. Real airtime and battery cost. |

The first three are achievable. The last three are not, and they are what make
a *full node* impossible.

### Why this is still not scheduled

Not because it cannot work. Because of sequencing.

The radio has never received a packet. The SX1262 driver has never run. Adding
a second unproven system on top of an unproven first one is how a project
stalls with two broken halves and no way to tell which is at fault.

It also depends on the phone app's behaviour, which we do not control. A client
update can change what the handshake accepts.

M4 already delivers the visible outcome, by a different route: transmit plus a
NodeInfo broadcast makes the Flipper appear in the phone's node list, through
the node the phone is already paired with.

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

## 7. Ready-made hardware, if none of this appeals

If the goal is simply a Meshtastic node that works with a Flipper, that product
exists. [Nibble Zero](https://retia.io/products/nibble-zero-kit) is 75 USD. It
is a real Meshtastic node with Flipper Zero support, built around a Seeed
WIO-SX1262 radio programmable for 868MHz or 915MHz. It runs Meshtastic firmware
itself, has Wi-Fi and Bluetooth, and works standalone. A Flipper is not
required. The kit includes a case, a BMP280 sensor and an antenna.

It also swaps between Meshtastic and MeshCore by reflashing.

That is the honest last-resort answer. It costs more than the Electronic Cats
add-on and it does not make the Flipper itself a receiver, but it needs no
software work and it pairs with the phone app today.

## 8. Recommendation

Build the receiver the spec describes. It is achievable, it is useful, and it
makes the Flipper do something it cannot do today.

Then add transmit and a `NodeInfo` broadcast. Other nodes will list the Flipper
as a minimal participant, and it will appear in the phone's node list through
the node the phone is already paired with.

Phone connectivity is a real option after that, not before it. The order
matters: prove the radio receives, then prove it transmits, then consider a
second protocol surface. Doing it the other way around leaves two unproven
systems and no way to isolate a fault between them.

A full node remains out of reach. Direct messages, position, telemetry and
routing need `NodeDB`, `Router` and the module system, and those do not fit.
