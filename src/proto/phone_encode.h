/* FromRadio messages for the Meshtastic phone app.
 *
 * The app's connection handshake is two stages. Each stage ends when the
 * device sends config_complete_id with the nonce the app asked for.
 *
 * An earlier version of this comment claimed the app needs nothing but
 * MyNodeInfo and config_complete for stage 1, on the strength of the Android
 * client's own test suite. That is true of the handshake and false of the
 * result. A real phone completes the handshake and is then left holding a node
 * with no name, because the node's User record only ever arrived in stage 2.
 *
 * The firmware's real stage 1, PhoneAPI.cpp getFromRadio(), is
 *
 *   my_info, deviceuiConfig, own node_info, metadata, 8 channels,
 *   10 config variants, 13 module config variants, then config_complete
 *
 * What follows is a subset: my_info, own node_info, metadata, config_complete.
 * Channels and config are still missing, and need their field numbers taken
 * from channel.proto and config.proto before they can be written.
 *
 * Field numbers come from meshtastic/protobufs, mesh.proto. They are cited
 * next to each one because getting a field number wrong produces a message the
 * app silently ignores.
 *
 * No Flipper dependencies. No allocation. */
#ifndef PHONE_ENCODE_H
#define PHONE_ENCODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "src/model/mesh_config.h"

/* FromRadio field numbers. mesh.proto, message FromRadio. */
#define FROMRADIO_FIELD_PACKET             2
#define FROMRADIO_FIELD_MY_INFO            3
#define FROMRADIO_FIELD_NODE_INFO          4
#define FROMRADIO_FIELD_CONFIG_COMPLETE_ID 7
#define FROMRADIO_FIELD_CHANNEL            10
#define FROMRADIO_FIELD_CONFIG             5
#define FROMRADIO_FIELD_METADATA           13

/* ToRadio field numbers. mesh.proto, message ToRadio. */
#define TORADIO_FIELD_PACKET         1
#define TORADIO_FIELD_WANT_CONFIG_ID 3
#define TORADIO_FIELD_DISCONNECT     4
#define TORADIO_FIELD_HEARTBEAT      7

/* What the app sends in want_config_id to select a handshake stage, and what
 * it expects back in config_complete_id. Taken from the Android client,
 * HandshakeConstants. Stage 1 is config, stage 2 is the node list. */
#define PHONE_NONCE_CONFIG    69420
#define PHONE_NONCE_NODE_INFO 69421

/* Identity this device reports to the app.
 *
 * The strings are fixed arrays rather than pointers on purpose. The identity
 * is copied by value into the BLE profile, and a pointer would dangle as soon
 * as whatever built it went out of scope. Lengths follow mesh.proto: long_name
 * is capped at 40 bytes and short_name at 5. */
#define PHONE_ID_MAX         16
#define PHONE_LONG_NAME_MAX  40
#define PHONE_SHORT_NAME_MAX 8

typedef struct {
    uint32_t node_num;
    char id[PHONE_ID_MAX]; /* "!aabbccdd" by Meshtastic convention */
    char long_name[PHONE_LONG_NAME_MAX];
    char short_name[PHONE_SHORT_NAME_MAX];
    uint32_t hw_model;
} PhoneIdentity;

/* Fill an identity, truncating any oversized name rather than overflowing. */
/* Derive the wire-facing identity from the shared config record.
 *
 * PhoneIdentity is a view, not a source. The record in MeshConfig is the one
 * place this node's identity is defined, and everything on the wire is computed
 * from it so the two cannot drift. */
void phone_identity_from_config(const MeshConfig* config, PhoneIdentity* out);

void phone_identity_init(
    PhoneIdentity* out,
    uint32_t node_num,
    const char* long_name,
    const char* short_name);

/* FromRadio { my_info { ... } }.
 *
 * The client reads my_node_num, min_app_version, device_id and pio_env, and
 * tolerates the rest being absent. buildMyNodeInfo in
 * MeshConfigFlowManagerImpl.kt:463. */
size_t phone_encode_my_node_info(const PhoneIdentity* id, uint8_t* out, size_t out_len);

/* FromRadio { node_info { num, user { ... } } }. */
size_t phone_encode_node_info(const PhoneIdentity* id, uint8_t* out, size_t out_len);

/* FromRadio { config_complete_id }.
 *
 * Uses the always variant, because a nonce of 0 must still appear on the wire.
 * An omitted field would leave the app waiting. */
size_t phone_encode_config_complete(uint32_t nonce, uint8_t* out, size_t out_len);

/* FromRadio { metadata { ... } }.
 *
 * The app reads firmware_version to decide whether the device is supported at
 * all, so a node that never sends metadata is a node it cannot fully adopt.
 * Field numbers from mesh.proto, message DeviceMetadata. */
size_t phone_encode_device_metadata(const PhoneIdentity* id, uint8_t* out, size_t out_len);

/* FromRadio { channel { ... } } for the primary channel.
 *
 * psk_index is written as a one byte psk. Meshtastic reads a single byte psk as
 * a key index rather than a key, which is how the default channel is expressed.
 * channel.proto, message ChannelSettings.
 *
 * The name and index passed here must match what the radio is actually tuned
 * to. They are supplied by the caller rather than defined here so there is one
 * place to change when the shared config store lands. */
size_t
    phone_encode_primary_channel(const char* name, uint8_t psk_index, uint8_t* out, size_t out_len);

/* FromRadio { config { lora { ... } } }.
 *
 * Without this the app has a node with no region and no modem preset, so it
 * cannot show or change what the radio is doing. config.proto, message
 * LoRaConfig. */
size_t phone_encode_lora_config(uint32_t channel_num, uint8_t* out, size_t out_len);

/* FromRadio { packet { ... } }, wrapping an already-built MeshPacket. */
size_t phone_encode_packet(
    const uint8_t* mesh_packet,
    size_t packet_len,
    uint8_t* out,
    size_t out_len);

/* Read want_config_id out of a ToRadio the app wrote.
 *
 * Returns false when the message is malformed or carries no want_config_id.
 * Other ToRadio fields are skipped by wire type. */
bool phone_decode_want_config_id(const uint8_t* buf, size_t len, uint32_t* nonce);

#endif
