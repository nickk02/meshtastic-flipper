# Meshtastic Receiver for Flipper Zero: Design

Date: 2026-08-05
Status: approved, ready for implementation planning

## 1. Objective

A Flipper Zero FAP that drives an external SX1262 over SPI, receives Meshtastic
LoRa packets, decrypts them with the primary channel key, decodes the protobuf
payload, and displays text messages and heard nodes on the Flipper screen.

This is a partial reimplementation of the Meshtastic protocol against FuriHAL.
It is not a port of the Meshtastic firmware.

### Delivery target: FAP, confirmed

Ships as a `.fap` installed from the app catalog, not as a custom firmware
build. Decided deliberately after weighing the alternative.

An app compiled into the firmware would execute from flash instead of the heap,
which raises the size ceiling. The ElectronicCats LoRa project does exactly that
via `applications_user/`. It was rejected because it requires the user to flash
custom firmware and give up official updates, against a one-tap install. The
receive path is small enough that the FAP ceiling is not binding.

Consequence to design against: **a FAP is RAM-resident.** Every section,
including `.text`, is heap-allocated by the loader at
`lib/flipper_application/elf/elf_file.c:488`, and the FAP linker script
`targets/f7/application_ext.ld` links at address zero with everything
relocatable. Code size competes directly with runtime heap. This is why binary
size is the scarcest resource in the project.

### Out of scope

PKI direct messages, position, telemetry, MQTT, store and forward, admin
messages, config over radio, canned messages, the phone API. If a task appears
to require any of these, stop and ask rather than expanding scope.

**Phone connectivity is confirmed out of scope**, accepted by the project owner
rather than merely inherited from the brief. The full reasoning, including
measured line counts and the RAM versus flash argument, is in
`docs/feasibility-full-node.md`. Summary: real Meshtastic firmware is roughly
1MB and flash-resident, a FAP gets about 128KB of heap (measured, see
`docs/measurements.md`), and the phone
API additionally needs `PhoneAPI.cpp` (2,199 lines), `NodeDB.cpp` (4,467 lines)
and the full config and admin protobuf set. Bluetooth itself is not the
obstacle: `furi_hal_bt_start_app` accepts a custom profile template, so a FAP
can register its own GATT service.

Transmit is a stretch goal for M4 only. M0 through M3 are receive only.

### Rejected alternative: ZeroMesh and the UART client architecture

Considered three times. Rejected three times. Recorded here in full so it is
not raised a fourth.

[ZeroMesh](https://lab.flipper.net/apps/zeromesh) is a free FAP in the official
catalog. It already implements the UART client architecture. It connects to a
separate Meshtastic node over serial, on header pins 13 and 14 plus ground, at
115200 baud. The node does all the LoRa modulation, the protocol handling and
the crypto. ZeroMesh draws what the node sends it.

Two points are easy to get backwards:

1. ZeroMesh cannot drive a LoRa board. It contains no SX1262 driver and no
   protocol code. Attach an SX1262 add-on to a Flipper running ZeroMesh and
   nothing happens. There is nothing in it for a radio to talk to.
2. ZeroMesh is not a stepping stone to this project. The two share almost no
   code. Its only value here is as a reference for FAP structure, view dispatch
   and settings persistence, which is how the brief lists it.

Effort is not the reason for the rejection. ZeroMesh is free and it works
today. If the goal were to see mesh traffic on the Flipper screen, ZeroMesh
would be the right answer and this project would be unnecessary.

The reason is that ZeroMesh needs the node as well. The Flipper becomes a
display for a device you already carry. This project exists to make the Flipper
itself hear Meshtastic, and that needs LoRa silicon.

A stock Flipper cannot receive LoRa under any circumstances. Its sub-GHz chip
is a CC1101. The CC1101 demodulates FSK, GFSK, MSK, OOK and ASK. LoRa is
Semtech's chirp spread spectrum modulation, implemented in silicon, and the
CC1101 has no demodulator for it. No firmware, app or antenna changes that.

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

Read from `ElectronicCats/flipper-SX1262-LoRa` `lora.c:30-36` and cross-checked
against the board's own KiCad schematic and BOM in
`ElectronicCats/flipper-shields`, `FLIPPER_Subg/`.

| Signal | MCU pin | Header pin | Notes |
| --- | --- | --- | --- |
| MOSI | PA7 | 2 | |
| MISO | PA6 | 3 | |
| CS, NSS0 | PA4 | 4 | **Not the SX1262.** See below. |
| SCK | PB3 | 5 | |
| DIO1 | PC3 | 7 | SX1262 interrupt |
| ANT\_SW | PB6 | 13 | Board antenna switch. See below. |
| BUSY | PB7 | 14 | |
| NRST | PC1 | 15 | |
| **CS, NSS1, SX1262** | **PC0** | **16** | The SX1262 chip select |
| GND | - | 8 | |
| 3V3 | - | 9 | |

Two departures from the brief's proposed wiring, both fixed by the PCB:

**BUSY is PB7, header pin 14**, not PB2 on pin 6. The board routes it to
`gpio_usart_tx`'s neighbour `gpio_usart_rx`.

**The SX1262 chip select is PC0, header pin 16, not PA4.** This is the trap. The
brief and an obvious reading of the pin names both suggest NSS0 on PA4 is the
LoRa radio. It is not. In the reference driver `pin_nss1` (PC0) appears 56 times
and wraps every single SX1262 transaction, while `pin_nss0` (PA4) appears three
times: declared, initialised as an output, and written low once at startup
(`lora.c:1029-1034`). PA4 belongs to the board's second CC1101.

Driving PA4 as the SX1262 select would talk to the wrong chip and M1 would fail
with no useful diagnostic.

The board needs two chip selects on one bus. The driver handles this by copying
`furi_hal_spi_bus_handle_external` into a mutable handle and overriding the CS
pin per device (`lora.c:1014-1019`).

### Two RF switches, only one of which is automatic

The board has a Peregrine PE42421 SPDT RF switch (BOM item 27) on the net
`ANT_SW`, wired to PB6, header pin 13. It selects which radio reaches the
antenna.

The reference driver **declares `pin_ant_sw` and never drives it**
(`lora.c:34`, one occurrence in the whole file). So either the switch defaults
to the SX1262 path when the line floats, or their receive path works by luck of
the default state. This is unresolved and is an M1 task: drive PB6 both ways
and record which state lets packets through. It is a prime candidate for a
silent M2 failure.

Separately, the SX1262's own transmit and receive switching is handled by the
chip. The driver issues `SetDIO2AsRfSwitchCtrl`, opcode `0x9D`, with enable
(`lora.c:348`). That resolves the brief's open question 5: no external TXEN and
RXEN lines are needed, and no GPIO is spent on them.

PA14 (header pin 10, SWCLK) is used by the reference driver as a beacon LED
output. Not needed here.

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

### Resolved from the board design files

All settled by reading `ElectronicCats/flipper-shields`, `FLIPPER_Subg/`, which
is the published KiCad project and BOM. Product pages and reseller listings were
not treated as authoritative, and were wrong in two places.

**The chip is an SX1262.** BOM item 23 is `U1 = SX1262IMLTRT`. Electronic Cats'
own `FLIPPER_Subg/README.md` says "SX1272", and at least one reseller repeats
it. That is a documentation typo in the vendor's README, contradicted by their
own BOM, schematic symbol and driver, all of which are SX126x.

**The antenna connector is U.FL, and there are two of them.** BOM items 2 and 3,
`AE3` and `AE4`, both Hirose `U.FL-R-SMT-180`. Items 1, `AE1` and `AE2`, are PCB
trace antenna footprints using the Texas SWRA416 868/915MHz reference design,
and both are marked **DNP**, do not populate.

Consequence for ordering: the board ships with no usable antenna of its own. It
needs either a 915MHz antenna terminated in U.FL, or a U.FL to SMA pigtail plus
an SMA antenna. U.FL is a small snap-fit coax connector rated for very few
mating cycles, so a pigtail is the kinder option if the antenna will come on and
off.

**The RF front end is matched for 915, not 868.** BOM item 26 is a Johanson
`0915BM15A0001E` balun, which is the 915MHz part, and item 25 is a Johanson
`0900FM15D0039E` 900MHz band filter. That is a hardware answer, unlike the
driver's US915 and EU868 menu entries, which are only software.

**No soldering.** `J1` is a 1x10 socket and `J2` a 1x08 socket, both 2.54mm
vertical (BOM items 13 and 14). The board seats on the Flipper GPIO header.

### Still to buy alongside the board

- A 915MHz antenna with a U.FL lead, or a U.FL to SMA pigtail and an SMA
  antenna.
- A USB logic analyzer. Any of the cheap 8 channel clones works with PulseView.
  Without one, M1's kill condition cannot be evaluated: a silent bus and a
  correct bus with wrong driver code look identical from software.

## 4. Architecture

One rule organizes the whole design: `src/proto/` must not include a Flipper
header. It compiles under plain gcc on a PC. It takes byte buffers in. It
returns plain structs. It allocates nothing.

That boundary makes M0 possible. It lets the highest risk code be tested before
any radio exists. CI enforces it.

```
meshtastic-flipper/
  application.fam
  meshtastic_flipper.c        entry point
  src/
    app.c/.h                  state, radio thread, main loop
    proto/                    No Flipper headers. Compiles on a PC.
      mesh_channel.c/.h       defaultpsk expansion, channel hash
      mesh_crypto.c/.h        nonce construction, AES-CTR wrapper
      mesh_header.c/.h        16 byte PacketHeader, flag decode
      mesh_data.c/.h          Data field walker: portnum, payload
      mesh_decode.c/.h        frame to result, failure reasons
      mesh_encode.c/.h        transmit frame construction
    model/                    No Flipper headers. Compiles on a PC.
      mesh_event.c/.h         one received frame, reduced for the UI
      message_ring.c/.h       fixed ring of received messages
      node_roster.c/.h        fixed table of heard nodes
    radio/                    Flipper HAL. No Meshtastic knowledge.
      lora_config.c/.h        US LongFast parameters, frequency slot math
      sx126x_regs.h           opcodes, each with a datasheet citation
      sx126x.c/.h             driver: commands, registers, BUSY handshake
      frame_source.h          the interface a frame producer implements
      source_radio.c/.h       FrameSource backed by the SX1262
      source_sim.c/.h         FrameSource that replays known frames
    ui/
      app_view.c/.h           drawing only, no protocol logic
  lib/                        Third party sources. Licenses retained.
  test/host/                  gcc, runs on a PC
  test/tools/gen_vectors.py   generates test vectors and sim frames
```

`lora_config.c` sits in `src/radio/` but uses no Flipper HAL, so the host build
compiles it too. The frequency and modem parameters are therefore tested, even
though the driver around them is not.

### Dependency rules

- `proto` depends on nothing. Not on FuriHAL, not on the radio layer.
- `radio/sx126x` depends only on `furi_hal_spi` and `furi_hal_gpio`, and holds
  no Meshtastic concepts. It is a reusable SX1262 driver that happens to live
  here.
- `radio_thread` is the single place where the two meet.
- `ui` reads finished results and renders. It contains no protocol logic.

### Radio layer provenance

The radio layer is derived from `ElectronicCats/flipper-SX1262-LoRa` (MIT,
Copyright 2024 ElectronicCats). The MIT notice is retained in `lib/`. The
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

No logic analyzer is available. Built-in observability replaces it.

The app counts every dropped frame and records why. It never discards one
silently. The reasons are:

1. CRC failure.
2. Implausible header: nonsensical `to` or `from`, or a hop limit out of range.
3. Channel hash mismatch.
4. Decryption produced something that is not protobuf.
5. The portnum is not a text message.

A counters view shows the tally for each reason. When M2 or M3 misbehaves, that
tally points at one layer. A logic analyzer would tell you much the same thing.

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

### M4: roster, TX, and minimal node presence if size allows

Track heard nodes with SNR and RSSI in a fixed 32 entry in-memory array. No
persistence, no NodeDB. Broadcast text to the primary channel.

- Acceptance: the reference node receives a message sent from the Flipper.

**Stretch, only if the M3 size report leaves room:** periodic `NodeInfo`
broadcast so the Flipper appears in other nodes' node lists rather than being a
silent listener that occasionally speaks. This requires protobuf *encoding*,
which M0 through M3 do not build, since the receive path only decodes.

That is the realistic ceiling for a FAP. It makes the Flipper a genuine if
minimal mesh participant: it hears, it speaks, and it announces itself.
Everything past that point (direct messages, position, telemetry, relaying for
others, phone connectivity) is ruled out on size. See
`docs/feasibility-full-node.md`.

## 9. Testing strategy

Automated tests cover `proto`, `model`, and the LoRa parameters. They run on a
PC under gcc.

The driver is verified by hand, on hardware. There is nothing useful to mock
about an SX1262: a mock would only confirm that the driver sends the bytes the
mock expects, which is the same assumption twice.

The split is deliberate. The layers that can be tested properly are the layers
where a silent error is most likely and hardest to find.

## 10. Open items

Carried forward into the implementation plan. None block starting M0.

1. Antenna connector and 915 RF matching on the board. **Resolved**, see
   section 3: two U.FL connectors, no antenna included, Johanson 915MHz balun.
2. Per pin and per rail GPIO current limits, needed before M4 transmit. Still
   open. `docs.flipper.net` returns 403 to automated fetches, so read them by
   hand. Not needed for receive.
3. Whether the Meshtastic `zephyr/` port matures enough to change the calculus.
   Check before committing significant time past M3.

### Resolved since this document was written

**LongFast parameters**, previously open question 1. SF11, bandwidth 250kHz,
coding rate 4/5, from `MeshRadio.h:282-287` with the named defaults at
`MeshRadio.h:99`, `:104` and `:106`. Implemented and tested in
`src/radio/lora_config.c`.

**Frequency slot calculation**, previously open question 2. From
`RadioInterface.cpp:1307-1348`:

```
slotWidth   = spacing + padding * 2 + bw / 1000
numSlots    = round((freqEnd - freqStart + spacing) / slotWidth)
slot        = djb2(channelName) % numSlots
frequency   = freqStart + bw / 2000 + padding + slot * slotWidth
```

For US LongFast that gives 104 slots, slot 19, and **906.875 MHz**, which is
channel 20 in the 1-based numbering nodes display. That independently
reproduces the frequency the Meshtastic community documents, which is what
makes it trustworthy rather than merely derived.

Note that this uses djb2 (`RadioInterface.cpp:943`), a different function from
the XOR fold used for the channel hash. Confusing the two puts the radio on the
wrong frequency.

**Low data rate optimization.** Not previously identified as a risk, and it
should have been. The reference driver comments that LDRO is required for SF11
and SF12, which holds only at narrow bandwidths. RadioLib, and therefore
Meshtastic, enables it when symbol duration reaches 16ms. US LongFast is SF11
at 250kHz, so 8.192ms, so LDRO stays **off**. Tested in
`test/host/test_lora_config.c`.

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
