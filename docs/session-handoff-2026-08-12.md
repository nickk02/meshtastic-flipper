# Session handoff — 2026-08-12

Read this first, before touching code. It says exactly what is proven, what
is guessed, and what the next concrete step is.

## 1. Immediate action, before anything else

PR is open and needs merging:
**https://github.com/nickk02/meshtastic-flipper/pull/new/stage2-repeat-and-queue-fix**
(branch `stage2-repeat-and-queue-fix`, pushed but not merged — this session ran
out of budget before opening it as a real PR with CI. Open it, let CI run,
merge, tag a release the normal way.)

This branch contains real, hardware-verified fixes. Before it, the phone
never got past the config stage. After it, the phone reached "16 nodes"
before dropping — the furthest this project has gotten. Do not lose this
branch.

## 2. Where the project actually stands

**Working, verified against a real client (your iPhone) over several
sessions of live device testing:**
- BLE discovery and bonding
- Full stage 1 config stream (36 messages, correct order, correct content)
- Stage 2 (own node_info + config_complete)
- All admin requests iOS actually sends, answered correctly (`get_ringtone_request`
  field 14, `get_canned_message_module_messages_request` field 10, `set_config`
  field 34 — iOS asks for these after config, not `get_owner_request`, which
  it never sends)
- No dropped/refused messages by the device's own accounting
- Screen updates live, remote install/relaunch over USB works, device log is
  readable in real time

**The one remaining bug:** the phone completes the entire handshake — by
every measure on the device side, everything it asked for was sent and
"sent" — and then **deliberately disconnects** (BLE HCI reason 13, "Remote
User Terminated Connection" — this is the phone choosing to hang up, not a
radio dropout) roughly 15-20 seconds after starting, then immediately
reconnects and retries the whole thing. Every cycle looks identical.

This is the only thing standing between "protocol implemented" and "actually
works."

## 3. What is proven about the failure, and what is not

**Proven (do not re-derive these, they cost real time to establish):**

- The disconnect is not caused by missing or malformed protocol content.
  Every message iOS asks for arrives, in the right order, with `0 refused`
  on the device side, every single cycle.
- It is not a stall/timeout on the device side reading too slowly — a
  `DRAIN_INTERVAL_MS` of 350ms (up from 150ms, see the PR above) was tested
  live and made no difference to the disconnect happening, though it did
  measurably help data get further into the phone (16 nodes vs never
  progressing) — see the queue-depth bug below for why.
- **The Android client's source is irrelevant to this bug.** An earlier
  investigation this session read `Meshtastic-Android`'s
  `MeshConfigFlowManagerImpl.kt` in detail (its handshake state machine, its
  database-install try/catch, its two watchdog timeouts) before realizing
  the user has an iPhone, not an Android phone. That entire investigation
  does not apply. Do not redo it; do not trust anything in this doc's
  earlier drafts (if any survive) that cites Android/Kotlin files.
- **The real client is `Meshtastic-Apple`, and its actual mechanism is now
  understood from source** (cloned locally, see §5). Key facts, all
  read directly from current source, not summarized by a tool:
  - iOS starts draining `FromRadio` **immediately after the `ToRadio`
    write completes**, in `sendWantConfig()`/`sendWantDatabase()`
    (`AccessoryManager.swift`), via `connection.startDrainPendingPackets()`
    — it does **not** wait for a `FromNum` notification first for that
    initial burst.
  - `drainPendingPackets()` (`BLEConnection.swift`) is a tight
    `repeat { read(); if empty break } while true` loop with no delay
    between reads. It stops dead on the very first empty read.
  - `FromNum` value-change notifications additionally call
    `startDrainPendingPackets()` again (`didUpdateValueFor`,
    `case FROMNUM_UUID: try? startDrainPendingPackets()`), which is
    coalesced (a `needsDrain` flag) rather than spawning a second
    concurrent drain loop.
  - iOS's own app-level timeouts for the connect sequence are generous and
    **do not match the observed timing**: stage 1 (`wantConfig`) has a 30s
    budget, stage 2 (`wantDatabase`) has a 120s budget
    (`AccessoryManager+Connect.swift`, the `SequentialSteps` stepper). We
    are disconnecting in 15-20s, well under both. This rules out a simple
    "iOS gave up waiting" timeout as the direct cause.
  - `FromRadio` does not need `CHAR_PROP_NOTIFY` — iOS subscribes to notify
    on it defensively but handles the "not supported" error as benign and
    does not gate connection readiness on it (only `FromNum`'s notify
    subscription gates readiness). This is not a bug on our side.

**Not proven — this is the actual gap:**

- What specifically makes iOS decide to disconnect. `Reason: 13` means the
  phone's own Bluetooth stack sent the disconnect, which happens when
  **app code, or CoreBluetooth on its behalf, decides to hang up** — not a
  passive link-layer timeout (that would show a different HCI reason). But
  which code path decides this, and why, has not been identified. Static
  source reading found the mechanism but not the trigger.
- Leading hypothesis, unconfirmed: iOS's very first drain (fired
  immediately on write-ack, before our device has even started queuing
  replies — there is real dispatch latency: GATT write → message queue →
  worker thread wake, up to `WORKER_POLL_MS` after the write) can race
  ahead of us and read empty, terminating its first pass with zero
  messages. It should recover on the `FromNum`-triggered second pass — and
  usually does, per "16 nodes" being reached — but if that second pass
  *also* loses a race under real RF conditions (weaker signal, retries,
  slower connection interval negotiation than tested locally), the whole
  cycle could come up short in a way that leaves an iOS-side continuation
  unresolved. This is a plausible mechanism, not a confirmed one. It does
  not, by itself, explain a *deliberate* disconnect (HCI 13) rather than a
  hang — something has to actively decide to give up and call disconnect.
- The device install into iOS's local SwiftData/CoreData store
  (`AccessoryManager+FromRadio.swift`, `handleNodeInfo` /
  `installAndPublishNodeDatabase`-equivalent path — not fully traced this
  session, this is where the Android investigation found the actual "any
  exception here restarts the transport" pattern, and iOS likely has an
  analogous path that was not located before time ran out) is the next
  most promising place to look, structurally, but was not confirmed.

## 4. What you need to do: capture real iOS logs

This is the same move that cracked every other bug in this project: stop
guessing from source, read what actually happened. On the firmware side that
was the Flipper's serial CLI; on the phone side it is the Mac's Console app
or Xcode's device console. You have a Mac, so this is very doable.

### Steps

1. **Connect the iPhone to the Mac** with a cable (or over the same Wi-Fi
   network for wireless debugging, if already paired for that — cable is
   simpler for a first attempt).

2. **Open Console.app** (Applications → Utilities → Console, or Spotlight
   search "Console"). In the sidebar under Devices, select your iPhone. You
   may need to unlock the phone and tap "Trust This Computer" if this is the
   first connection.

3. **Filter to the Meshtastic app.** In the search field, filter by process
   name. The app's log lines all go through Apple's unified logging
   (`Logger.transport`, `Logger.services`, `Logger.data` in the source), and
   most lines in the relevant files carry a recognizable emoji tag —
   search for `🔗👟` (the `[Connect]` step markers) or `🛜` (the `[BLE]`
   transport markers) to cut noise. If Console's search doesn't support
   emoji well, filter by process = "Meshtastic" instead and read visually.

4. **Start capturing before you connect the Flipper.** Console shows a live
   stream; get it running and visible first.

5. **Connect from the Meshtastic app to the Flipper**, same as every other
   test this session. Let it run through its full cycle: config, admin
   exchange, then the disconnect.

6. **Look for these specific things, in order of how useful they'd be:**
   - Any line containing `error`, `Error`, `throw`, or a Swift stack trace,
     especially anything near/after `"✅ [Accessory] Notifying completions
     that have completed for configCompleteID"` or after the last
     `"NONCE_ONLY_DB"` line.
   - The `🔗👟 [Connect] Step N` lines — these tell you exactly which step
     of the `SequentialSteps` sequence (see `AccessoryManager+Connect.swift`
     in the reference checkout, §5 below) was executing right before the
     drop. Step 7 ("Update UI and status to connected") and Step 8
     ("Initialize MQTT and Location Provider") are the ones after the
     handshake proper — if the log shows it reaching Step 6 or 7 and then
     stopping, that narrows it a lot.
   - Anything from `Logger.data` around `context.save()` (SwiftData/Core
     Data writes) — a save failure there is the iOS-side equivalent of the
     Android bug this session initially (wrongly) chased.
   - The literal text `"Recovering from post-handshake"` or similar — if
     iOS has a directly analogous recovery path to Android's, it would say
     something like this near the disconnect.
   - Any CoreBluetooth-level error (`CBError`, `CBATTError`) right before
     the disconnect line.

7. **Copy the relevant window of log lines** (a minute or two around one
   full connect-to-disconnect cycle is plenty — don't need the whole
   session) and bring that back to the next Claude Code session. That's the
   one piece of evidence needed to actually fix this rather than guess
   again.

### If Console.app filtering is painful

Xcode's device console (Window → Devices and Simulators, select the phone,
"Open Console") is the alternative and behaves similarly. Either works; use
whichever is less annoying.

## 5. Local reference checkouts (already done, reuse them)

Two client repos are cloned locally for direct source reading, **outside**
this repo per repo-boundary discipline:

```
C:\Users\Nick\Documents\nickk02\meshtastic-refs\Meshtastic-Android\
C:\Users\Nick\Documents\nickk02\meshtastic-refs\Meshtastic-Apple\
```

The Apple one is the one that matters now. Key files already read this
session, worth going straight to rather than re-searching:

- `Meshtastic/Accessory/Accessory Manager/AccessoryManager+Connect.swift` —
  the whole connect sequence, `SequentialSteps`, every step's timeout
- `Meshtastic/Accessory/Accessory Manager/AccessoryManager+FromRadio.swift`
  — `handleMyInfo`, `handleNodeInfo`, `handleConfig`, `handleModuleConfig`
  (this is where the `getRingtone`/`getCannedMessageModuleMessages`/
  `set_config(tzdef)` admin requests originate — confirms the field 14/10/34
  behavior observed on the wire is genuinely this client's normal behavior,
  not a bug)
- `Meshtastic/Accessory/Transports/Bluetooth Low Energy/BLEConnection.swift`
  — the actual GATT read/write/notify plumbing, `drainPendingPackets`,
  `didUpdateValueFor`
- `Meshtastic/Accessory/Accessory Manager/AccessoryManager.swift` around
  line 996 — the `configCompleteID` switch, `NONCE_ONLY_CONFIG` /
  `NONCE_ONLY_DB` handling

These are shallow clones (`--depth 1`) of `main`. If genuinely stuck, `git
log`/blame won't have history — that's fine, this is for reading current
behavior, not archaeology. Delete and re-clone if they go stale; they're
disposable reference material, not part of this project's repo.

## 6. Standing facts worth not re-deriving (platform traps already paid for)

These cost a release each to find earlier this session/project. Do not
reintroduce them:

- `ble_gatt_characteristic_update` returns **true on failure** (`result !=
  BLE_STATUS_SUCCESS`); `ble_gatt_service_add` returns **true on success**.
  Opposite polarity, same file. Never assume they agree.
- `ble_gatt_characteristic_init` probes the length callback with **both**
  `context == NULL` and `data == NULL`. A callback that checks context
  before checking `data == NULL` silently registers a zero-length
  characteristic that can never carry data, with no error anywhere.
- `pb_write_string_field` (and `pb_write_bytes_field`) **omit the field
  entirely when the value is empty** — correct for a plain protobuf default,
  **wrong when the field is a `oneof` member**, because an empty response
  needs to still assert which oneof case it is. Use
  `pb_write_string_field_always` for those.
- `FuriTimer` callbacks run on the shared FreeRTOS timer service task —
  never acquire a mutex with `FuriWaitForever` or call into the BLE stack
  from one; it can deadlock the whole device.
- The ViewPort draw callback runs on the GUI thread, a shared system
  service — never block it either, for the same reason.
- `furi_hal_crypto` is AES-256 only; this project needs AES-128-CTR, hence
  the vendored tiny-AES-c.
- The BLE advertising packet is 31 bytes total; flags (3) + the 128-bit
  service UUID (18) leaves 10 bytes for the name AD structure. An
  eight-character name fills that with **zero margin** and made the stack
  refuse the whole advertisement (`set_discoverable failed 146`) on real
  hardware. The name is `"Mesh"` (4 chars) and should stay short until
  there's a real reason and a tested budget to grow it.
- A stale BLE bond can wedge advertising with that same
  `set_discoverable failed` symptom, and survives a plain reboot on both
  ends. Fix: `storage remove /int/.bt.keys` over the Flipper CLI, then
  reboot.
- The device has **one** serial CLI session. `ufbt launch`/`ufbt cli` and
  any log-tailing script fight over `COM3`; only run one at a time, and
  qFlipper grabs the port on launch if it's running — close it first.
- The app previously had **no periodic screen redraw** once the radio
  thread stopped polling absent hardware (a real regression, since fixed);
  if counters ever look frozen again on a live connection, check
  `mesh_app_run`'s main loop still wakes on a timeout, not just on input.
- `ufbt` builds against whichever SDK index it was last pointed at.
  Building without checking can silently produce a binary for the wrong
  firmware (official vs Unleashed vs Momentum), which manifests as "App
  Too old"/"App too new" on install.

## 7. Full protocol reference

`docs/phone-protocol.md` (already in this repo) has the field-number-level
reference for the whole handshake: message order, field numbers for every
message type touched, the admin exchange, and this same trap list in
shorter form. Read that before this file if you need the wire format rather
than the current bug.
