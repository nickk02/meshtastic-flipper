# meshtastic-flipper

A Flipper Zero app that receives Meshtastic LoRa traffic. It needs an external
SX1262 module. It shows text messages and heard nodes on the Flipper screen.

## Status

The app builds and runs. It does not receive yet. The radio hardware has not
arrived.

Current release: v0.3.0-radio, 19,576 bytes.

Verified without hardware, across 369 host tests and a self test on the device:

- The decode path: header, channel keys, AES128-CTR, protobuf
- The transmit encoder, checked byte for byte against the `meshtastic` Python
  package
- The message ring and node roster
- The US LongFast frequency and modem parameters

Not verified: the SX1262 driver. It was written from the datasheet and from a
working reference for the same board. It has not run against hardware.

The app selects a frame source at startup. If no radio answers, it uses a
simulated source and the Stats page shows "(sim)".

## Why an external module is required

The Flipper's sub-GHz radio is a CC1101. It tunes to 915MHz. It demodulates
OOK, FSK, GFSK and MSK.

LoRa uses chirp spread spectrum. Demodulating it requires dedicated silicon.
The CC1101 has none, so a stock Flipper cannot decode LoRa at any frequency.
Meshtastic devices all contain an SX1262 or an SX1276 for this reason.

## Hardware

Required:

1. A Flipper Zero.
2. An [Electronic Cats Flipper Add-on Sub-GHz](https://electroniccats.com/store/flipper-lra-subghz/).
   It carries an SX1262 and seats on the GPIO header. No soldering.
3. A 915MHz antenna with a U.FL lead, or a U.FL to SMA pigtail with an SMA
   antenna. The board ships without an antenna. Its PCB antenna footprints are
   marked do-not-populate.

Testing also requires a second Meshtastic node to transmit against.

The board fixes the pin assignments. `docs/superpowers/specs/` lists them all.
Two differ from the common examples:

- BUSY is PB7, header pin 14. It is not PB2.
- The SX1262 chip select is PC0, header pin 16. Header pin 4 is named NSS0 but
  selects the board's second CC1101.

## Scope

This implements part of the Meshtastic protocol. It is not a port of the
firmware.

Implemented or planned:

- Receive and decrypt on the primary channel
- Display text messages
- Track heard nodes with RSSI and SNR, in memory only
- Transmit text
- Broadcast NodeInfo, so other nodes list this device

Possible, not scheduled:

- Meshtastic phone app connectivity. The app's handshake needs four messages,
  not the forty the firmware sends. The Flipper SDK exports the BLE and GATT
  calls required.

Out of reach in a Flipper app:

- Direct messages, position, telemetry, MQTT, relaying for other nodes

[docs/feasibility-full-node.md](docs/feasibility-full-node.md) covers all
three lists and the memory arithmetic behind the third.

## Layout

```
src/proto/     Encode and decode. No Flipper headers. Builds on a PC.
src/model/     Message ring, node roster. No Flipper headers.
src/radio/     SX1262 driver, LoRa parameters, frame sources.
src/ui/        Drawing.
test/host/     PC tests.
test/tools/    Test vector generator.
lib/           Third party code.
docs/          Feasibility analysis, measurements, spec and plans.
```

`src/proto/` must not include a Flipper header, and must not allocate. CI
enforces both. This is what allows the decode path to be tested on a PC.

## Installing

Download `meshtastic.fap` from the
[latest release](https://github.com/nickk02/meshtastic-flipper/releases). Copy
it to `apps/Tools/` on the SD card. It appears under Apps, Tools, Meshtastic.

Releases build against official firmware, release channel, API 87.1. Custom
firmware such as Momentum, Unleashed or RogueMaster may report an API mismatch.
Rebuild against that SDK if it does.

## Building

```
ufbt
ufbt launch
```

Output: `dist/meshtastic.fap`.

PC tests need gcc. They do not need a Flipper:

```
bash test/host/run_tests.sh
```

Regenerating the test vectors needs Python with `cryptography` and
`meshtastic`. The generated headers are committed, so this is only required
when the vectors change.

## CI

Each push and pull request runs five checks:

1. The host test suite.
2. Regeneration of the test vectors and simulated frames, compared against the
   committed copies.
3. A check that `src/proto/` includes no Flipper headers and calls no
   allocator.
4. A FAP build, with the artifact attached to the run.
5. clang-format, using the SDK's rules.

A `v*` tag publishes a release after the host suite passes on that commit.

## Third party code

- [tiny-AES-c](https://github.com/kokke/tiny-AES-c), public domain. AES128-CTR.
  The Flipper's AES acceleration is hardcoded to AES256; the Meshtastic default
  channel uses AES128.
- [ElectronicCats/flipper-SX1262-LoRa](https://github.com/ElectronicCats/flipper-SX1262-LoRa),
  MIT. Source of the SX1262 driver. Copyright 2024 ElectronicCats. Notice
  retained in `lib/`.

Protocol constants come from
[meshtastic/firmware](https://github.com/meshtastic/firmware) and
[meshtastic/protobufs](https://github.com/meshtastic/protobufs). Each constant
in the source cites its origin file and line.

## License

MIT. See [LICENSE](LICENSE).
