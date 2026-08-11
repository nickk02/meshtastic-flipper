# Reading what the device actually did

Every bug on this project so far has been the same shape: the phone sent
something, the device silently did nothing useful with it, and the only visible
symptom was a phone that said "connecting". Diagnosing that from a four line
status screen costs a full build, release, install and test cycle per guess.

The device already writes logs. Read them.

## Serial log

Connect the Flipper by USB and open its CLI, then:

```
log
```

Set the level first if you want the detail:

```
log debug
```

`Ctrl-C` stops the stream. With `ufbt` on the path:

```bash
python -m ufbt cli
```

## What the BLE code logs

Tag `MeshBLE`. During a phone connection you should see, in order:

```
ToRadio want_config_id nonce=69420
queueing 36 replies
batch drained, 36 sent, 0 refused
ToRadio heartbeat, no reply expected
ToRadio want_config_id nonce=69421
queueing 2 replies
batch drained, 38 sent, 0 refused
```

then the admin exchange.

The line that matters most is this one:

```
ToRadio not understood: tag=N len=M aa bb cc ...
```

A ToRadio nobody recognised is the shape of every stall here, and the on screen
counters cannot show it. They say a write arrived, not that it went unanswered.
The hex is there so a message ignored on purpose can be told apart from one that
failed to parse.

`queue full, reply N of M dropped` means a sequence was truncated. The client
assumes the shape of the config sequence, so a dropped reply is never harmless.

## Testing against the Python client instead of the phone app

The phone app is the worst available debugger: it reports "connecting" whether
the device is silent, malformed or slow. The Meshtastic Python CLI speaks the
same protocol over BLE and says what it received.

```bash
pip install meshtastic
meshtastic --ble-scan
meshtastic --ble <name> --info
```

`--info` prints the node it built from the config stream, so a missing field
shows up as a missing value rather than a spinner. Add `--debug` for the packet
level view.

This is the difference between "it did not work" and "it stopped after
STATE_SEND_CHANNELS".

## On screen counters

The Phone page is a summary, not a substitute. Current fields:

- `W` ToRadio writes, `P` publishes attempted
- `Fr` `Fn` FromRadio and FromNum updates the stack refused, separately, because
  only `Fr` costs the phone its data
- `Stage` and `N`, the handshake stage and last want_config nonce
- `Q` `Dr` `Now` queued, drained and pending
- `Wk` worker loop iterations, proof of life. Static while wedged means the
  worker died; climbing means it did not
- `Db` FromNum notifications, about one per batch
- `W<tag>/<len>` the first tag and length of the last ToRadio write: 1 packet,
  3 want_config_id, 4 disconnect, 7 heartbeat
