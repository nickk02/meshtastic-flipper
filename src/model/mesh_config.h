/* The one record of what this node is.
 *
 * Three things need the same answers and must not each keep their own copy:
 *
 *   the phone handshake  needs the owner, the channel and the LoRa settings to
 *                        answer want_config_id and get_owner_request
 *   the radio            needs the channel key and the LoRa settings to tune
 *                        and to decrypt
 *   the settings screen  edits all of it
 *
 * This is that record. It holds no Flipper types and does no I/O, so it is
 * host-testable and the storage layer can change without touching it.
 *
 * Field values follow the Meshtastic schema, so region and modem_preset are the
 * numbers from config.proto rather than an invention of this project. See
 * docs/phone-protocol.md. */
#ifndef MESH_CONFIG_H
#define MESH_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* mesh.proto caps long_name at 40 bytes and short_name at 5. One extra byte
 * each for the terminator. The channel name limit is Meshtastic's own. */
#define MESH_CONFIG_LONG_NAME_MAX    41
#define MESH_CONFIG_SHORT_NAME_MAX   6
#define MESH_CONFIG_CHANNEL_NAME_MAX 12

/* Bumped whenever the layout changes. A stored record with a different version
 * is discarded rather than reinterpreted, because reading old bytes as a new
 * layout is worse than losing a name. */
#define MESH_CONFIG_VERSION 1

/* config.proto, LoRaConfig.RegionCode and LoRaConfig.ModemPreset. */
#define MESH_REGION_UNSET       0
#define MESH_REGION_US          1
#define MESH_PRESET_LONG_FAST   0
#define MESH_PRESET_MEDIUM_FAST 4
#define MESH_PRESET_SHORT_FAST  6

typedef struct {
    uint32_t node_num;
    char long_name[MESH_CONFIG_LONG_NAME_MAX];
    char short_name[MESH_CONFIG_SHORT_NAME_MAX];
} MeshOwner;

typedef struct {
    char name[MESH_CONFIG_CHANNEL_NAME_MAX];
    /* A one byte psk is a key index rather than a key. Index 1 is the default
     * channel key. channel.proto, ChannelSettings.psk. */
    uint8_t psk_index;
} MeshChannelConfig;

typedef struct {
    uint8_t region;
    uint8_t modem_preset;
    uint32_t channel_num;
    /* False on this build: there is no transmit path yet. Telling the phone
     * otherwise invites it to queue messages that never leave. */
    bool tx_enabled;
} MeshLoraConfig;

typedef struct {
    uint32_t version;
    MeshOwner owner;
    MeshChannelConfig channel;
    MeshLoraConfig lora;
} MeshConfig;

/* US LongFast on the default channel, with a name derived from node_num.
 *
 * node_num should come from the BLE MAC so two Flippers on the same mesh do not
 * claim the same node number. */
void mesh_config_defaults(MeshConfig* out, uint32_t node_num);

/* True when the record is self-consistent and safe to use.
 *
 * A record that fails this is replaced with defaults rather than repaired,
 * because a half-valid identity on a mesh is worse than a fresh one. */
bool mesh_config_valid(const MeshConfig* config);

/* Both truncate rather than reject. A name too long for the wire is still a
 * name the user meant, and silently refusing an edit is worse than shortening
 * it. Empty names are refused, since a node with no name is what this project
 * spent four releases fixing. */
bool mesh_config_set_long_name(MeshConfig* config, const char* name);
bool mesh_config_set_short_name(MeshConfig* config, const char* name);

/* "!aabbccdd", the form the phone app displays verbatim. */
void mesh_config_node_id(const MeshConfig* config, char* out, size_t out_len);

#endif
