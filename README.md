# meshtastic-flipper

A Flipper Zero app that receives Meshtastic LoRa traffic through an external
SX1262 module and shows text messages and heard nodes on the screen.

## Status

Early. There is no working code yet.

The design and the first implementation plan are written and the protocol facts
have been verified against the Meshtastic source. The radio hardware has not
been ordered. Work is currently on M0, which builds and tests the decode path
on a PC without any radio attached.

Do not install this expecting it to do anything.

## Why this needs extra hardware

The Flipper's built-in sub-GHz radio is a CC1101. It can tune to 915MHz, but it
can only demodulate OOK, FSK, GFSK and MSK. LoRa is a different modulation,
Semtech's chirp spread spectrum, and decoding it needs a dedicated demodulator
in silicon. The CC1101 does not have one, which is why every Meshtastic device
has an SX1262 or SX1276 in it.

So the Flipper can see that something is transmitting at 915MHz. It cannot read
it. Adding an SX1262 is what fixes that.

## Hardware

- Flipper Zero
- [Electronic Cats Flipper Add-on Sub-GHz](https://electroniccats.com/store/flipper-lra-subghz/),
  which carries an SX1262 and connects through the GPIO header with no
  soldering
- A second Meshtastic node to transmit against while testing

Pin assignments are fixed by that board and are listed in the spec. Note that
BUSY is on PB7 (header pin 14), not the PB2 you might expect from the usual
SX1262 wiring examples.

## Scope

This is a partial reimplementation of the Meshtastic protocol, not a port of
the firmware.

Planned:

- Receive and decrypt packets on the primary channel
- Display text messages
- Track heard nodes with RSSI and SNR, in memory only
- Transmit text, if the binary size allows
- Announce itself so other nodes list it, if the binary size allows

Not planned, and not achievable as a Flipper app:

- Phone app connectivity
- Direct messages, position, telemetry, MQTT, routing for other nodes

The reasoning for that second list, including the memory arithmetic, is in
[docs/feasibility-full-node.md](docs/feasibility-full-node.md). Short version:
a Flipper app is loaded into RAM rather than run from flash, and Meshtastic
firmware is about ten times larger than the available heap.

## Layout

```
src/proto/     Protocol decode. No Flipper dependencies, compiles on a PC.
src/radio/     SX1262 driver and the receive thread. Flipper HAL only.
src/ui/        Views.
test/host/     Tests that run on a PC.
test/tools/    Test vector generator.
vendor/        Third party code, licenses retained.
docs/          Spec, plans, measurements.
```

The rule that makes this testable is that `src/proto/` cannot include a single
Flipper header. It takes byte buffers in and plain structs out, allocates
nothing, and knows nothing about SPI or threads. That is why the crypto and
decode path can be proven correct on a PC before any radio exists.

## Installing

Grab `meshtastic.fap` from the
[latest release](https://github.com/nickk02/meshtastic-flipper/releases) and
copy it to `apps/Tools/` on the SD card. It appears under Apps, Tools,
Meshtastic.

Releases are built against official firmware on the release channel. Custom
firmware such as Momentum, Unleashed or RogueMaster may reject the app with an
API mismatch, in which case it needs rebuilding against that SDK.

## Building

The app is built with [ufbt](https://github.com/flipperdevices/flipperzero-ufbt):

```
ufbt
ufbt launch
```

The artifact lands at `dist/meshtastic.fap`.

The PC-side tests need gcc and do not need a Flipper:

```
bash test/host/run_tests.sh
```

## CI

Every push and pull request runs four checks:

- the host test suite
- a regeneration of `test/host/vectors.h`, compared against the committed copy,
  so the vectors cannot silently drift from their generator
- a guard that `src/proto/` includes no Flipper headers and performs no
  allocation, since the whole testing approach depends on that boundary holding
- a FAP build, with the artifact attached to the run

Pushing a `v*` tag builds and publishes a release, but only after the host
suite passes on that commit.

Regenerating the test vectors additionally needs Python with `cryptography`
and `meshtastic` installed. The generated header is committed, so this is only
necessary when the vectors change.

## Third party code

- [tiny-AES-c](https://github.com/kokke/tiny-AES-c), public domain. AES128 in
  CTR mode. Used because the Flipper's own AES hardware acceleration is
  hardcoded to AES256 and Meshtastic's default channel is AES128.
- [ElectronicCats/flipper-SX1262-LoRa](https://github.com/ElectronicCats/flipper-SX1262-LoRa),
  MIT. The SX1262 driver is derived from this. Copyright 2024 ElectronicCats,
  notice retained in `vendor/`.

Protocol details were read out of
[meshtastic/firmware](https://github.com/meshtastic/firmware) and
[meshtastic/protobufs](https://github.com/meshtastic/protobufs) rather than
inferred. Constants in the source carry a citation to the file they came from.

## License

MIT. See [LICENSE](LICENSE).
