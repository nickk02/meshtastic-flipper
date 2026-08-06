/* The User message, carried by NODEINFO_APP packets.
 *
 * This is where node names come from. Without it a node list can only show hex
 * node numbers, which is what a real Meshtastic device never does.
 *
 * portnums.proto:57-61 says NODEINFO_APP's payload is a User message.
 * Field numbers from mesh.proto, message User. Field 4 is retired, so hw_model
 * is 5.
 *
 * No Flipper dependencies. No allocation. */
#ifndef MESH_USER_H
#define MESH_USER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* mesh.proto caps long_name at 40 bytes and short_name at 5. One extra byte
 * each for the terminator. */
#define MESH_USER_ID_MAX         16
#define MESH_USER_LONG_NAME_MAX  41
#define MESH_USER_SHORT_NAME_MAX 6

typedef struct {
    char id[MESH_USER_ID_MAX];
    char long_name[MESH_USER_LONG_NAME_MAX];
    char short_name[MESH_USER_SHORT_NAME_MAX];
    uint32_t hw_model;
    bool has_long_name;
    bool has_short_name;
} MeshUser;

/* Parse a User message. Strings are copied and terminated, so the result does
 * not alias the input.
 *
 * Over-long names are truncated rather than rejected. A node with a 60
 * character name is still worth listing.
 *
 * Returns false only on malformed protobuf. */
bool mesh_user_parse(const uint8_t* buf, size_t len, MeshUser* out);

#endif
