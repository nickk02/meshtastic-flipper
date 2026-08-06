# Meshtastic Receiver for Flipper Zero: Design

Date: 2026-08-05
Status: approved, ready for implementation planning

## 1. Objective

A Flipper Zero FAP that drives an external SX1262 over SPI, receives Meshtastic
LoRa packets, decrypts them with the primary channel key, decodes the protobuf
payload, and displays text messages and heard nodes on the Flipper screen.

This is a partial reimplementation of the Meshtastic protocol against FuriHAL.
It is not a port of the Meshtastic firmware.

### Out of scope

PKI direct messages, position, telemetry, MQTT, store and forward, admin
messages, config over radio, canned messages, the phone API. If a task appears
to require any of these, stop and ask rather than expanding scope.

Transmit is a stretch goal for M4 only. M0 through M3 are receive only.

### Explicitly rejected alternative

A UART client architecture, where the Flipper acts as a display for a separate
Meshtastic node (the ZeroMesh approach), was considered and rejected. It is
substantially less work, but it does not make the Flipper a Meshtastic
receiver, which is the point of the project.

## 2. Verified facts

Every item below was read out of a checkout, not recalled. Citations are file
and line. Re-verify against the checkout rather than trusting this document.

### Meshtastic protocol

Source: `meshtastic/firmware`, sparse checkout of `src/mesh`.

| Fact | Value | Citation |
| --- | --- | --- |
| Header length | 16 bytes | `RadioInterface.h:21` |
| Max LoRa payload | 255 | `RadioInterface.h:20` |
| PKC overhead | 12 | `RadioInterface.h:22` |
| Header layout | `to`, `from` (NodeNum), `id` (PacketId), then single bytes `flags`, `channel`, `next_hop`, `relay_node` | `RadioInterface.h:36-53` |
| Hop limit mask | `0x07` | `RadioInterface.h:24` |
| Want ack mask | `0x08` | `RadioInterface.h:25` |
| Via MQTT mask | `0x10` | `RadioInterface.h:26` |
| Hop start mask, shift | `0xE0`, 5 | `RadioInterface.h:27-28` |
| Sync word | `0x2b` | `RadioLibInterface.h:84` |
| US region | 902.0 to 928.0 MHz, 100 percent duty cycle, 30 dBm limit | `RadioInterface.cpp:112` |

The `RDEF` macro signature is at `RadioInterface.cpp:100`. Note that in
`RDEF(US, 902.0f, 928.0f, 100, 30, ...)` the 100 is duty cycle percent and the
30 is the power limit in dBm. Neither is a channel count.

### Channel key derivation

The default primary channel sets `psk.size = 1` and `psk.bytes[0] = 1`
(`Channels.cpp:157-159`). `Channels::getKey` expands a single byte PSK: index 0
disables encryption, otherwise the 16 byte `defaultpsk` is copied and its last
byte incremented by `index - 1`, so index 1 means `defaultpsk` verbatim
(`Channels.cpp:254-267`).

`defaultpsk`, 16 bytes, AES128 (`Channels.h:153-154`):

```
d4 f1 bb 3a 20 29 07 59 f0 bc ff ab cf 4e 69 01
```

The channel hash is `xorHash(name) XOR xorHash(key bytes)`, where `xorHash` is a
plain byte-wise XOR fold. The hash is what the `channel` header byte carries as
a decode hint.

### Crypto

`CryptoEngine::encryptAESCtr` uses AES-CTR with a 16 byte IV and
`setCounterSize(4)`, selecting AES128 or AES256 purely on key length.

The nonce is built by `CryptoEngine::initNonce`: 8 bytes of packet id in little
endian, then 4 bytes of source node number in little endian, then a 4 byte block
counter starting at zero. For the channel path `extraNonce` is zero and is not
written.

Because the packet id is a 32 bit value widened to 64 bits, bytes 4 through 7 of
the nonce are zero in practice.

### Flipper HAL

Source: `flipperdevices/flipperzero-firmware`.

The correct SPI handle for external GPIO SPI is
`furi_hal_spi_bus_handle_external` (`furi_hal_spi_config.c:344-351`), with MISO
on PA6, MOSI on PA7, SCK on PB3 and CS on PA4.

It sits on `furi_hal_spi_bus_r`, shared with the `_subghz` (CC1101) and `_nfc`
handles. The bus takes a mutex on every acquire
(`furi_hal_spi_config.c:108-110`), so access is serialized rather than
corrupting. Because the Flipper runs one foreground app, the conflict named in
the brief is theoretical.

GPIO header pin numbering is authoritative in `gpio_pins[]`
(`furi_hal_resources.c`): pin 2 PA7, pin 3 PA6, pin 4 PA4, pin 5 PB3, pin 6 PB2,
pin 7 PC3, pin 15 PC1, pin 16 PC0, pin 8 GND, pin 9 3V3.

### Correction to the brief: crypto acceleration is unusable

The brief instructs using `furi_hal_crypto` rather than a software AES. That is
not possible for this protocol.

Every raw key path in `furi_hal_crypto` hardcodes `CRYPTO_KEYSIZE_256B`
(`furi_hal_crypto.c:203` and `furi_hal_crypto.c:400`). `furi_hal_crypto_ctr`
therefore accepts only a 32 byte key. The Meshtastic default channel is AES128
with a 16 byte key. The STM32WB AES block supports AES128, but the Flipper HAL
never exposes it. The only AES128 aware path is
`furi_hal_crypto_enclave_store_key` (`furi_hal_crypto.c:181`), and the enclave is
sequential append only and explicitly off limits to public applications.

Consequence: M3 uses a software AES128. CTR mode needs only the forward cipher,
so the inverse S-box and inverse mix-columns are omitted. Expected cost is
roughly 1.2KB.

One helpful alignment: `furi_hal_crypto_ctr` models a 12 byte IV plus a 4 byte
counter, which is exactly the shape Meshtastic uses. The IV construction is
therefore correct even though the primitive is not usable.

## 3. Hardware

### Board

[Electronic Cats Flipper Add-on Sub-GHz](https://electroniccats.com/store/flipper-lra-subghz/),
35 USD. Carries both a CC1101 and an SX1262, connects through the GPIO header,
no soldering required.

Chosen over a bare module because there is no soldering setup available, and
because an MIT licensed driver already exists for this exact board.

### Pin map, as fixed by the board

Read from `ElectronicCats/flipper-SX1262-LoRa`, `lora.c:31-36`.

| Signal | MCU pin | Header pin |
| --- | --- | --- |
| MOSI | PA7 | 2 |
| MISO | PA6 | 3 |
| CS (NSS0, SX1262) | PA4 | 4 |
| SCK | PB3 | 5 |
| DIO1 | PC3 | 7 |
| NRST | PC1 | 15 |
| CS (NSS1, second device) | PC0 | 16 |
| BUSY | PB7 | 14 |
| GND | - | 8 |
| 3V3 | - | 9 |

This differs from the brief in one place. The brief proposes BUSY on PB2, header
pin 6. The board routes BUSY to `gpio_usart_rx`, which is PB7 on header pin 14.
The board layout wins.

The board also needs two chip selects. The driver handles this by copying
`furi_hal_spi_bus_handle_external` into a mutable handle and overriding the CS
pin per device (`lora.c:1014-1019`).

### Electrical

GPIO is 3.3V logic, driven by an STM32WB55 which is a 3.3V part throughout.

Pins are 5V tolerant **in input mode only**. Once a pin is configured as an
output it is no longer 5V tolerant and applying 5V damages it. This is not a
concern for this build, since the SX1262 board is 3.3V on every line, but it is
the usual way a Flipper GPIO pin gets destroyed.

The bottom pogo pins are the 1-Wire / iButton interface on PB14, header pin 17.
Not used by this project.

### Power

3V3 from pin 9 only. Never hot-plug. Never supply the module from USB or a
battery while it is also connected to a Flipper power pin. TX peaks around
120mA, so the radio is idled whenever the RX screen is not in front.

Per pin and per rail current limits are **not recorded here**. `docs.flipper.net`
returns HTTP 403 to automated fetches, and the secondary sources found were
mutually inconsistent, including one that described pin 1 as a 3.3V rail when it
is 5V. Read the limits off the official GPIO documentation by hand before M4,
where transmit makes them load bearing. They do not matter for M0 through M3,
which are receive only.

### Unresolved before purchase

- Antenna connector type, and whether a 915MHz antenna is included. Not
  confirmed by any source read so far. Confirm with the vendor.
- Whether the SX1262 RF front end is matched for 915 rather than 868. The
  driver exposes US915 and EU868 menu options, which suggests 915 is intended,
  but that is software rather than an RF matching claim.
- One third party retailer lists the chip as an SX1272. Both the Electronic Cats
  store and the driver source say SX1262, and the driver is SX126x throughout.
  The retailer listing is treated as a typo, not as a conflict.

## 4. Architecture

The organizing constraint: **`src/proto/` must not include a single Flipper
header.** It compiles under plain gcc on a PC, takes byte buffers in and plain
structs out, and allocates nothing. That boundary is what makes M0 possible and
what makes the highest risk code testable before any radio exists.

```
meshtastic-flipper/
  application.fam
  meshtastic_flipper.c        entry, view dispatcher, scene manager
  src/
    proto/                    ZERO Flipper deps, host compilable
      aes128.c/.h             encrypt only AES128 core
      mesh_crypto.c/.h        nonce construction plus CTR wrapper
      mesh_header.c/.h        16 byte PacketHeader plus flag decode
      mesh_data.c/.h          Data field walker: portnum, payload
      mesh_channel.c/.h       defaultpsk expansion, channel hash
    radio/                    Flipper HAL only, zero Meshtastic knowledge
      sx126x_regs.h           every constant with a datasheet citation
      sx126x.c/.h             opcodes, register access, BUSY handshake
      lora_config.c/.h        LongFast US params, frequency slot math
      radio_thread.c/.h       FuriThread: config, RX loop, decode, queue
    ui/                       views only, no protocol logic
  test/host/                  gcc, runs on a PC
  test/tools/gen_vectors.py   ground truth generator
  vendor/                     third party sources, licenses retained
```

### Dependency rules

- `proto` depends on nothing. Not on FuriHAL, not on the radio layer.
- `radio/sx126x` depends only on `furi_hal_spi` and `furi_hal_gpio`, and holds
  no Meshtastic concepts. It is a reusable SX1262 driver that happens to live
  here.
- `radio_thread` is the single place where the two meet.
- `ui` reads finished results and renders. It contains no protocol logic.

### Radio layer provenance

The radio layer is derived from `ElectronicCats/flipper-SX1262-LoRa` (MIT,
Copyright 2024 ElectronicCats). The MIT notice is retained in `vendor/`. The
driver is stripped to the receive path, then retuned to Meshtastic parameters.

This satisfies the brief's rule that no register address, opcode or timing
constant be invented, and satisfies it better than hand transcription would.
The driver already carries datasheet citations in its comments.

The concrete example of why this matters: the SX1262 sync word register at
`0x0740` is 16 bits, and RadioLib splits a one byte sync word across nibbles
before writing it. `lora.c:576-586` implements exactly that split. For
Meshtastic's `0x2b` with RadioLib's default control bits of `0x44`, the register
pair is `0x0740 = 0x24` and `0x0741 = 0xB4`. Writing `0x2b` directly would
produce a radio that receives nothing, silently, with no diagnostic. That is the
M2 failure mode with no exit given no logic analyzer.

## 5. Data flow

The radio thread owns everything from the wire to a finished result:

1. Poll IRQ status.
2. On RxDone, read the radio buffer, capture RSSI and SNR.
3. Run the frame through `proto`: parse header, match channel hash, decrypt,
   decode `Data`.
4. Push one `MeshFrame` onto a `FuriMessageQueue`.

`MeshFrame` carries the raw bytes, the radio metadata, and either a decoded
message or a specific failure reason. The GUI thread only pops and renders.

Decode runs on the radio thread rather than the GUI thread so that the UI stays
purely presentational. A full 255 byte frame is 16 AES blocks, which on a 64MHz
Cortex-M4 with a table-free AES128 lands on the order of one millisecond. That
is negligible against LongFast airtimes measured in hundreds of milliseconds, so
it does not risk the RX loop. Measure it at M3 rather than trusting the
estimate.

Because `MeshFrame` carries both raw and decoded content, the M2 hex dump view
and the M3 message view read the same queue item. The debug view is not
throwaway code.

## 6. Memory budget

All state is static. No allocation in the RX path.

| Item | Size |
| --- | --- |
| Frame queue, depth 8 at ~290 bytes | ~2.3KB |
| Message ring, 16 entries | ~1.5KB |
| Node roster, 32 entries | 512B |

Under 5KB of state, leaving the FAP budget to code.

## 7. Error handling and observability

There is no logic analyzer available. Built in observability substitutes for it.

Every dropped frame is counted and attributed, never silently discarded.
Distinct reasons:

- CRC failure
- Implausible header (nonsensical `to`/`from`, hop limit out of range)
- Channel hash mismatch
- Decrypt produced non-protobuf output
- Portnum is not a text message

A counters view shows the tally per reason. When M2 or M3 misbehaves, that
histogram localizes the fault to a layer, which is most of what the instrument
would have provided.

## 8. Milestones

Each milestone has a kill condition. If one is hit, stop and report rather than
working around it.

### M0: protocol core, no radio hardware

Front loaded deliberately because the radio is not ordered. Converts the
shipping wait into the highest risk work.

- Install ufbt, pin the firmware version, build and launch an empty FAP on the
  Flipper.
- Record actual FAP size and actual free heap on the device. This answers the
  brief's open question 2 with a measurement rather than an estimate.
- Build the host test harness (gcc, no Flipper deps).
- Write `test/tools/gen_vectors.py`: construct a `Data` protobuf, encrypt with
  `defaultpsk` and the documented nonce layout, emit expected ciphertext. This
  is an independent implementation of the same specification, which is what a
  test requires.
- Implement `aes128`, `mesh_crypto`, `mesh_header`, `mesh_channel`, `mesh_data`
  against those vectors.

Vectors are synthesized rather than captured off the air. Capture depends on
plumbing that does not exist yet and would make M0 wait on setup. The existing
node validates the real thing at M2, which is where it belongs.

- Acceptance: host tests take a synthesized encrypted frame and produce the
  exact original text, byte for byte.
- Kill: AES-CTR nonce or counter semantics cannot be made to match. Nothing
  downstream works without this.

### M1: SPI handshake

Vendor the ElectronicCats driver, strip to the receive path, bring up on the
board.

- Acceptance: `GetStatus` plus a register write then read roundtrip, correct and
  repeatable.
- Kill: no clean response after wiring is confirmed. Do not proceed to modem
  configuration on a flaky bus.

### M2: raw RX, project go/no-go

Configure for US LongFast, sync word `0x2b` written as the nibble split pair,
and render received packets as hex. Reference node transmitting alongside.

- Acceptance: repeated captures whose first 16 bytes parse as a plausible header
  (sane `to` and `from`, hop limit in range).
- Kill: no plausible frames after parameters are confirmed against the reference
  node's own CLI.

No UI beyond the hex dump is built before this passes.

### M3: decode

Wire M0's already proven `proto` layer into the RX path.

- Acceptance: a message typed into the reference node's app appears as readable
  text on the Flipper.
- Report FAP binary size and peak heap here.

### M4: roster, and TX if size allows

Track heard nodes with SNR and RSSI in a fixed 32 entry in-memory array. No
persistence, no NodeDB. Broadcast text to the primary channel.

- Acceptance: the reference node receives a message sent from the Flipper.

## 9. Testing strategy

Real automated tests exist only for `proto`, run on a PC via gcc. The radio
layer is verified manually on hardware, because there is nothing meaningful to
mock about an SX1262.

The asymmetry is deliberate. The layer that can be tested properly is the layer
where silent wrongness is most likely and hardest to diagnose.

## 10. Open items

Carried forward into the implementation plan. None block starting M0.

1. LongFast bandwidth, spreading factor and coding rate. Not found in the files
   read. `RadioInterface.h:90-92` carries only the 125/9/5 defaults. Derive from
   the `PRESET` macro and `applyModemConfig`, then cross check against the
   reference node's CLI. Do not assume values.
2. The frequency slot calculation. `RadioInterface::getFreq` only returns
   `savedFreq`. The real computation is around `RadioInterface.cpp:1302-1340`.
3. Antenna connector and 915 RF matching on the board (see section 3).
4. Whether the Meshtastic `zephyr/` port matures enough to change the calculus.
   Check before committing significant time past M3. As of this writing it is
   one `prj.conf` and a single nRF54L15 board overlay.

### Resolved by this document

- Open question 1, SPI bus handle: `furi_hal_spi_bus_handle_external`, shared
  bus, mutex protected. See section 2.
- Open question 3, DIO1 interrupt versus polling: poll. At LongFast timings a
  10ms poll interval is negligible, and the vendored driver already polls. Add
  an interrupt only if measurement demands it.
- Open question 4, channel key derivation: see section 2.
- Open question 5, RF switch control lines: not needed. The board's SX1262
  drives its RF switch from DIO2 internally.

## 11. Departures from the brief

Recorded so the reasoning survives.

| Brief says | Design does | Why |
| --- | --- | --- |
| Use `furi_hal_crypto` for AES | Software AES128 | HAL is AES256 only, hardcoded. Section 2. |
| Generate nanopb code for `mesh.proto` and `portnums.proto` | Hand written field walker | Only `portnum` and `payload` are ever read. Roughly 60 lines and a few hundred bytes against 4 to 6KB plus protoc and nanopb codegen in the build. Parser risk is covered by M0 vector tests. |
| BUSY on PB2, header pin 6 | PB7, header pin 14 | Fixed by the board layout. |
| M1 through M4, starting at SPI handshake | M0 added ahead of M1 | Radio is not ordered. M0 needs no radio and removes the highest risk work from the hardware phase. |
| Write the SX1262 driver from the datasheet | Vendor the ElectronicCats MIT driver | Proven on this exact board, already carries datasheet citations, and already solves the sync word nibble split that would otherwise cause a silent M2 failure. |
