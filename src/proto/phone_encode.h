/* FromRadio messages for the Meshtastic phone app.
 *
 * The app's connection handshake is two stages. Each stage ends when the
 * device sends config_complete_id with the nonce the app asked for. The app
 * does not require the config, module config, channel or metadata blocks that
 * real firmware sends: its own test suite completes Stage 1 from MyNodeInfo
 * plus config_complete alone. See docs/feasibility-full-node.md.
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

/* FromRadio field numbers. mesh.proto, message FromRadio. */
#define FROMRADIO_FIELD_PACKET             2
#define FROMRADIO_FIELD_MY_INFO            3
#define FROMRADIO_FIELD_NODE_INFO          4
#define FROMRADIO_FIELD_CONFIG_COMPLETE_ID 7

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
