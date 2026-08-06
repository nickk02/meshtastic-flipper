# meshtastic-flipper

A Flipper Zero app. It receives Meshtastic LoRa traffic through an external
SX1262 module. It shows text messages and heard nodes on the screen.

## Status

The app builds and runs. It cannot receive yet. The radio hardware has not
arrived.

I verified everything that does not need an SX1262: the decode path, the
channel keys and crypto, the models, and the LoRa parameters. That is 369 host
tests plus a self test that runs on the device.

Install it today and you get a working receiver. A simulated frame source
drives it. The simulation uses the same decode and display path that a radio
will use.

One piece is unverified: the SX1262 driver. I wrote it from the datasheet and
from a working reference for this exact board. It has never run against
hardware. Treat it as a hypothesis.

If no radio answers, the app switches to simulation. The Stats page then labels
the source "(sim)". You can always tell whether the frames are real.

## Why this needs extra hardware

The Flipper's built-in sub-GHz radio is a CC1101. It can tune to 915MHz. It can
demodulate OOK, FSK, GFSK and MSK.

LoRa is a different modulation. Semtech calls it chirp spread spectrum, and
decoding it needs a dedicated demodulator in silicon. The CC1101 does not have
one. That is why every Meshtastic device contains an SX1262 or an SX1276.

A stock Flipper can detect that something transmits at 915MHz. It cannot read
it. An SX1262 fixes that.

## Hardware

You need three things:

1. A Flipper Zero.
2. An [Electronic Cats Flipper Add-on Sub-GHz](https://electroniccats.com/store/flipper-lra-subghz/).
   It carries an SX1262. It seats on the GPIO header. No soldering.
3. A 915MHz antenna with a U.FL lead, or a U.FL to SMA pigtail and an SMA
   antenna. The board ships without one. Its PCB antenna footprints are marked
   do-not-populate.

A second Meshtastic node is also needed, to transmit against during testing.

The board fixes the pin assignments. The spec lists them. Two are easy to get
wrong:

- BUSY is on PB7, header pin 14. It is not on PB2, which the usual SX1262
  wiring examples suggest.
- The SX1262 chip select is PC0, header pin 16. Header pin 4 selects the
  board's second CC1101, despite being named NSS0.

## Scope

This reimplements part of the Meshtastic protocol. It is not a port of the
firmware.

Planned:

- Receive and decrypt packets on the primary channel
- Display text messages
- Track heard nodes with RSSI and SNR, in memory only
- Transmit text
- Announce itself, so other nodes list it

Not planned, and not possible in a Flipper app:

- Phone app connectivity
- Direct messages, position, telemetry, MQTT, routing for other nodes

[docs/feasibility-full-node.md](docs/feasibility-full-node.md) explains the
second list, with the memory arithmetic. In short: a Flipper app loads into
RAM, it does not run from flash, and Meshtastic firmware is about eight times
larger than the heap an app gets.

## Layout

```
src/proto/     Protocol encode and decode. No Flipper headers. Compiles on a PC.
src/model/     Message ring and node roster. No Flipper headers.
src/radio/     SX1262 driver, LoRa parameters, frame sources.
src/ui/        Views. Drawing only.
test/host/     Tests that run on a PC.
test/tools/    Test vector generator.
lib/           Third party code. Licenses retained.
docs/          Spec, plans, measurements.
```

One rule makes this testable: `src/proto/` must not include a Flipper header.
It takes byte buffers in. It returns plain structs. It allocates nothing. It
knows nothing about SPI or threads. That is why the crypto and the decode path
were proven on a PC before any radio existed. CI enforces the rule.

## Installing

Download `meshtastic.fap` from the
[latest release](https://github.com/nickk02/meshtastic-flipper/releases). Copy
it to `apps/Tools/` on the SD card. It appears under Apps, Tools, Meshtastic.

Releases build against official firmware on the release channel. Custom
firmware such as Momentum, Unleashed or RogueMaster may reject the app with an
API mismatch. Rebuild against that SDK if it does.

## Building

The app builds with [ufbt](https://github.com/flipperdevices/flipperzero-ufbt):

```
ufbt
ufbt launch
```

The artifact lands at `dist/meshtastic.fap`.

The PC tests need gcc. They do not need a Flipper:

```
bash test/host/run_tests.sh
```

To regenerate the test vectors you also need Python, with `cryptography` and
`meshtastic` installed. The generated headers are committed, so you only need
this when the vectors change.

## CI

Every push and pull request runs five checks:

1. The host test suite.
2. A regeneration of the test vectors and the simulated frames, compared
   against the committed copies. The generated files cannot drift from their
   generator.
3. A guard that `src/proto/` includes no Flipper headers and calls no
   allocator.
4. A FAP build, with the artifact attached to the run.
5. A lint pass against the SDK's clang-format rules.

A `v*` tag builds and publishes a release. The host suite must pass on that
commit first.

## Third party code

- [tiny-AES-c](https://github.com/kokke/tiny-AES-c), public domain. AES128 in
  CTR mode. The Flipper's own AES acceleration is hardcoded to AES256, and the
  Meshtastic default channel uses AES128.
- [ElectronicCats/flipper-SX1262-LoRa](https://github.com/ElectronicCats/flipper-SX1262-LoRa),
  MIT. The SX1262 driver derives from this. Copyright 2024 ElectronicCats.
  Notice retained in `lib/`.

I read the protocol details out of
[meshtastic/firmware](https://github.com/meshtastic/firmware) and
[meshtastic/protobufs](https://github.com/meshtastic/protobufs). I did not
infer them. Each constant in the source cites the file it came from.

## License

MIT. See [LICENSE](LICENSE).
