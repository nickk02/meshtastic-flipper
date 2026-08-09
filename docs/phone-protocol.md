# The Meshtastic phone protocol, as this project implements it

Ground truth for the BLE bridge. Every field number and constant here was read
from the source named beside it. Nothing in this file is inferred from
behaviour, and nothing is remembered from a previous session.

Sources:

- `meshtastic/protobufs`, `mesh.proto`, `channel.proto`, `config.proto`, `admin.proto`
- `meshtastic/firmware`, `src/mesh/PhoneAPI.cpp` and `PhoneAPI.h`
- `meshtastic/meshtastic-sdk`, `docs/protocol.md` and `docs/architecture/handshake-fsm.md`

## 1. Transport

One GATT service, three characteristics. `BluetoothCommon.h:9-14`.

| Purpose | UUID | Properties |
| --- | --- | --- |
| Service | `6ba1b218-15a8-461f-9fa8-5dcae273eafd` | |
| ToRadio | `f75c76d2-129e-4dad-a1dd-7866124401e7` | write |
| FromRadio | `2c55e69e-4993-11ed-b878-0242ac120002` | read |
| FromNum | `ed9da18c-a800-4f66-a670-aa7547e34453` | read, notify |

The phone reads FromRadio repeatedly until a read returns empty. FromNum is a
doorbell whose value only has to change.

A Flipper app cannot answer a read individually. No firmware exports any
`aci_gatt_*` function to applications, including `aci_gatt_allow_read`, so
there is no per-read callback. This app instead publishes one queued message at
a time and advances on a timer, which is a workaround for the sandbox and not
how a real node behaves.

## 2. Handshake, three phases

```
Phone -> ToRadio.want_config_id = 69420          stage 1 begins
Device -> the stage 1 stream, ending in config_complete_id = 69420

Phone waits ~100ms
Phone -> ToRadio.heartbeat, nonce = ++counter    settle
Phone waits ~100ms

Phone -> ToRadio.want_config_id = 69421          stage 2 begins
Device -> node_info x N, config_complete_id = 69421

Phone -> AdminMessage.get_owner_request, want_response = true
Device -> AdminMessage.get_owner_response, session_passkey
                                                 only now: Connected
```

The nonces are `#define SPECIAL_NONCE_ONLY_CONFIG 69420` and
`#define SPECIAL_NONCE_ONLY_NODES 69421`, from `PhoneAPI.h`.

The settle heartbeat is sent by the phone, not the device. Its purpose is to
defeat the firmware's per-connection memcmp dedup so stage 2's `want_config_id`
is not treated as a duplicate of stage 1's. The firmware does not echo the
nonce back, so the device's correct response to a heartbeat is to send nothing.

The phone does not report Connected until `get_owner_response` lands and a
`session_passkey` is latched. A device that completes stage 2 and stops will
leave the phone waiting.

## 3. Stage 1 stream

From `PhoneAPI.cpp` `getFromRadio()`, cross-checked against `protocol.md` §6.
The firmware's own comment on this sequence is "the client apps ASSUME THIS
SEQUENCE, DO NOT CHANGE IT", so order matters as much as presence.

| # | State | FromRadio field | Field no. | Under 69420 |
| --- | --- | --- | --- | --- |
| 1 | `STATE_SEND_MY_INFO` | `my_info` | 3 | mandatory |
| 2 | `STATE_SEND_UIDATA` | `deviceuiConfig` | 17 | optional |
| 3 | `STATE_SEND_OWN_NODEINFO` | `node_info` | 4 | yes |
| 4 | `STATE_SEND_METADATA` | `metadata` | 13 | yes |
| 5 | `STATE_SEND_CHANNELS` | `channel` x N | 10 | yes |
| 6 | `STATE_SEND_CONFIG` | `config` x N | 5 | yes |
| 7 | `STATE_SEND_MODULECONFIG` | `moduleConfig` x N | 9 | yes |
| 8 | `STATE_SEND_OTHER_NODEINFOS` | `node_info` x N | 4 | skipped |
| 9 | `STATE_SEND_FILEMANIFEST` | `fileInfo` x N | 15 | yes |
| 10 | `STATE_SEND_COMPLETE_ID` | `config_complete_id` | 7 | sentinel |

Under nonce 69421 the firmware starts at `STATE_SEND_OWN_NODEINFO`, jumps to
`STATE_SEND_OTHER_NODEINFOS`, then completes. That is why stage 2 is short.

### What this app sends today

Stage 1: `my_info`, `node_info`, `metadata`, `config_complete`.
Stage 2: `node_info`, `config_complete`.

Missing from stage 1: channels, config, module config, file manifest. The
handshake completes without them, which is what made this look finished when it
was not. A phone reaches Connected and holds a node with no channel and no
region.

## 4. Message field numbers

Only fields this project reads or writes are listed. All from `mesh.proto`
unless noted.

### FromRadio

`packet` 2, `my_info` 3, `node_info` 4, `config` 5, `config_complete_id` 7,
`moduleConfig` 9, `channel` 10, `metadata` 13, `fileInfo` 15,
`deviceuiConfig` 17.

Note `config_complete_id` is 7, not 8. Field 8 is `rebooted`.

### ToRadio

`packet` 1, `want_config_id` 3, `disconnect` 4, `heartbeat` 7.

### User

`id` 1, `long_name` 2, `short_name` 3, `hw_model` 5, `role` 7, `public_key` 8.

Field 4 is `macaddr`, deprecated. Reading `hw_model` as 4 silently yields zero.

### NodeInfo

`num` 1, `user` 2, `position` 3, `snr` 4, `last_heard` 5 (fixed32),
`device_metrics` 6, `channel` 7, `hops_away` 9, `is_favorite` 10.

### DeviceMetadata

`firmware_version` 1 (string), `device_state_version` 2, `canShutdown` 3,
`hasWifi` 4, `hasBluetooth` 5, `hasEthernet` 6, `role` 7, `position_flags` 8,
`hw_model` 9, `hasRemoteHardware` 10, `hasPKC` 11.

The app reads `firmware_version` to decide whether a device is supported.

### Channel, from `channel.proto`

`Channel`: `index` 1, `settings` 2, `role` 3.

`ChannelSettings`: `channel_num` 1, `psk` 2 (bytes), `name` 3, `id` 4 (fixed32),
`uplink_enabled` 5, `downlink_enabled` 6, `module_settings` 7.

`Channel.Role`: `DISABLED` 0, `PRIMARY` 1, `SECONDARY` 2.

A `psk` of one byte is a key index rather than a key. That is how the default
channel is expressed.

### Config, from `config.proto`

`Config` oneof: `device` 1, `position` 2, `power` 3, `network` 4, `display` 5,
`lora` 6, `bluetooth` 7, `security` 8, `sessionkey` 9, `device_ui` 10.

`LoRaConfig`: `use_preset` 1, `modem_preset` 2, `bandwidth` 3,
`spread_factor` 4, `coding_rate` 5, `frequency_offset` 6, `region` 7,
`hop_limit` 8, `tx_enabled` 9, `tx_power` 10, `channel_num` 11,
`override_duty_cycle` 12, `sx126x_rx_boosted_gain` 13, `override_frequency` 14.

`RegionCode.US` is 1. `ModemPreset.LONG_FAST` is 0.

`LONG_FAST` being 0 matters: protobuf omits zero-valued scalars by default, so
writing the modem preset with a default-omitting writer emits nothing. Absence
and LONG_FAST are the same encoding, which is correct but only by accident.
Anything that must be distinguishable from absent has to be written
unconditionally.

### AdminMessage, from `admin.proto`

`get_owner_request` 3 (bool), `get_owner_response` 4 (User),
`session_passkey` 101 (bytes).

`session_passkey` is 8 bytes, valid for roughly 300 seconds and regenerated at
about 150. Every state-changing admin request must carry it. A stale one is
answered with `Routing.error_reason = ADMIN_BAD_SESSION_KEY`.

Admin messages travel inside a `MeshPacket`, not directly in FromRadio.

## 5. Traps already paid for

Each of these cost a release to find.

`ble_gatt_characteristic_update` returns `result != BLE_STATUS_SUCCESS`, so a
`true` return means it **failed**. `ble_gatt_service_add`, in the same file,
returns `result == BLE_STATUS_SUCCESS`. Never assume the two agree.

`ble_gatt_characteristic_init` probes the value-length callback with both
`context == NULL` and `data == NULL`. A callback that tests the context before
answering the length returns 0, and the characteristic is registered with
`Char_Value_Length = 0`. It gets a valid handle and can be read, and it can
never carry data. That was FromRadio for four releases.

`FuriTimer` callbacks run on the shared FreeRTOS timer service task. Taking a
mutex with `FuriWaitForever` there, or calling into the BLE stack, freezes the
device.

The ViewPort draw callback runs on the GUI thread, which is a shared system
service. Any blocking acquire there is a system freeze rather than a dropped
frame.

`furi_hal_crypto` is AES256 only. `furi_hal_crypto.c` hardcodes
`CRYPTO_KEYSIZE_256B` at both the key-load and operation sites. Meshtastic
channel crypto is AES128-CTR, so this project vendors tiny-AES-c. Any note
saying to use `furi_hal_crypto` for it is wrong.

## 6. Known gaps

- channels, config, module config and file manifest are absent from stage 1
- no `get_owner` exchange, so the phone may never report Connected
- there is no persistent identity, channel or config store, so everything
  above is currently hardcoded
