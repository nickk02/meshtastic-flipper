#include "phone_encode.h"

#include <stdio.h>
#include <string.h>

#include "pb_write.h"

/* MyNodeInfo field numbers. mesh.proto, message MyNodeInfo. */
#define MYNODEINFO_FIELD_MY_NODE_NUM     1
#define MYNODEINFO_FIELD_MIN_APP_VERSION 11
#define MYNODEINFO_FIELD_DEVICE_ID       12
#define MYNODEINFO_FIELD_PIO_ENV         13

/* NodeInfo field numbers. mesh.proto, message NodeInfo. */
#define NODEINFO_FIELD_NUM  1
#define NODEINFO_FIELD_USER 2

/* mesh.proto, message DeviceMetadata. */
#define METADATA_FIELD_FIRMWARE_VERSION     1
#define METADATA_FIELD_DEVICE_STATE_VERSION 2
#define METADATA_FIELD_HAS_BLUETOOTH        5
#define METADATA_FIELD_HW_MODEL             9

/* Reported to the app as this device's firmware version.
 *
 * The app parses it as a version string and uses it to decide whether the
 * device is supported. It is a claim about protocol compatibility, not about
 * this being Meshtastic firmware, and it is deliberately a version whose phone
 * protocol this app actually implements. */
#define PHONE_FIRMWARE_VERSION "2.5.0"

/* device_state_version tracks the on-device database layout. The app only
 * compares it, so any stable value works; this one matches what the 2.5 series
 * reports. */
#define PHONE_DEVICE_STATE_VERSION 23

/* User field numbers. mesh.proto, message User. Note that field 4 is retired,
 * so hw_model is 5 and not 4. */
#define USER_FIELD_ID         1
#define USER_FIELD_LONG_NAME  2
#define USER_FIELD_SHORT_NAME 3
#define USER_FIELD_HW_MODEL   5

/* The oldest app version the client should accept. The Android client stores
 * this and warns below its own floor, but does not refuse the connection. */
#define MIN_APP_VERSION 30200

/* Reported to the app as the build identity. Real firmware puts its PlatformIO
 * environment name here. Ours is not a PlatformIO build, so it says what it
 * actually is rather than impersonating a supported target. */
#define PIO_ENV "flipper-meshtastic"

size_t phone_encode_my_node_info(const PhoneIdentity* id, uint8_t* out, size_t out_len) {
    uint8_t inner[64];
    PbWriter body;
    PbWriter frame;

    if(id == NULL || out == NULL) return 0;

    pb_writer_init(&body, inner, sizeof(inner));
    pb_write_varint_field_always(&body, MYNODEINFO_FIELD_MY_NODE_NUM, id->node_num);
    pb_write_varint_field(&body, MYNODEINFO_FIELD_MIN_APP_VERSION, MIN_APP_VERSION);

    /* device_id is raw bytes, which the client hex-encodes for display. The
     * node number is the only stable hardware identity available here. */
    uint8_t device_id[4] = {
        (uint8_t)(id->node_num >> 24),
        (uint8_t)(id->node_num >> 16),
        (uint8_t)(id->node_num >> 8),
        (uint8_t)(id->node_num),
    };
    pb_write_bytes_field(&body, MYNODEINFO_FIELD_DEVICE_ID, device_id, sizeof(device_id));
    pb_write_string_field(&body, MYNODEINFO_FIELD_PIO_ENV, PIO_ENV);

    if(!pb_writer_ok(&body)) return 0;

    pb_writer_init(&frame, out, out_len);
    pb_write_submessage(&frame, FROMRADIO_FIELD_MY_INFO, inner, pb_writer_len(&body));

    return pb_writer_ok(&frame) ? pb_writer_len(&frame) : 0;
}

size_t phone_encode_node_info(const PhoneIdentity* id, uint8_t* out, size_t out_len) {
    uint8_t user[96];
    uint8_t inner[128];
    PbWriter user_writer;
    PbWriter body;
    PbWriter frame;

    if(id == NULL || out == NULL) return 0;

    pb_writer_init(&user_writer, user, sizeof(user));
    pb_write_string_field(&user_writer, USER_FIELD_ID, id->id);
    pb_write_string_field(&user_writer, USER_FIELD_LONG_NAME, id->long_name);
    pb_write_string_field(&user_writer, USER_FIELD_SHORT_NAME, id->short_name);
    pb_write_varint_field(&user_writer, USER_FIELD_HW_MODEL, id->hw_model);
    if(!pb_writer_ok(&user_writer)) return 0;

    pb_writer_init(&body, inner, sizeof(inner));
    pb_write_varint_field_always(&body, NODEINFO_FIELD_NUM, id->node_num);
    pb_write_submessage(&body, NODEINFO_FIELD_USER, user, pb_writer_len(&user_writer));
    if(!pb_writer_ok(&body)) return 0;

    pb_writer_init(&frame, out, out_len);
    pb_write_submessage(&frame, FROMRADIO_FIELD_NODE_INFO, inner, pb_writer_len(&body));

    return pb_writer_ok(&frame) ? pb_writer_len(&frame) : 0;
}

size_t phone_encode_config_complete(uint32_t nonce, uint8_t* out, size_t out_len) {
    PbWriter frame;

    if(out == NULL) return 0;

    pb_writer_init(&frame, out, out_len);
    pb_write_varint_field_always(&frame, FROMRADIO_FIELD_CONFIG_COMPLETE_ID, nonce);

    return pb_writer_ok(&frame) ? pb_writer_len(&frame) : 0;
}

size_t phone_encode_packet(
    const uint8_t* mesh_packet,
    size_t packet_len,
    uint8_t* out,
    size_t out_len) {
    PbWriter frame;

    if(out == NULL || (mesh_packet == NULL && packet_len > 0)) return 0;

    pb_writer_init(&frame, out, out_len);
    pb_write_submessage(&frame, FROMRADIO_FIELD_PACKET, mesh_packet, packet_len);

    return pb_writer_ok(&frame) ? pb_writer_len(&frame) : 0;
}

/* Reads a base 128 varint and advances *pos. */
static bool read_varint(const uint8_t* buf, size_t len, size_t* pos, uint64_t* out) {
    uint64_t value = 0;
    unsigned shift = 0;

    while(*pos < len) {
        uint8_t byte = buf[*pos];
        (*pos)++;

        if(shift >= 64) return false;
        value |= (uint64_t)(byte & 0x7F) << shift;

        if((byte & 0x80) == 0) {
            *out = value;
            return true;
        }
        shift += 7;
    }
    return false;
}

bool phone_decode_want_config_id(const uint8_t* buf, size_t len, uint32_t* nonce) {
    size_t pos = 0;
    bool found = false;

    if(nonce == NULL) return false;
    if(buf == NULL && len > 0) return false;

    while(pos < len) {
        uint64_t tag;
        uint32_t field;
        uint8_t wire;

        if(!read_varint(buf, len, &pos, &tag)) return false;

        field = (uint32_t)(tag >> 3);
        wire = (uint8_t)(tag & 0x07);
        if(field == 0) return false;

        switch(wire) {
        case 0: { /* varint */
            uint64_t value;
            if(!read_varint(buf, len, &pos, &value)) return false;
            if(field == TORADIO_FIELD_WANT_CONFIG_ID) {
                *nonce = (uint32_t)value;
                found = true;
            }
            break;
        }
        case 2: { /* length delimited */
            uint64_t size;
            if(!read_varint(buf, len, &pos, &size)) return false;
            if(size > (uint64_t)(len - pos)) return false;
            pos += (size_t)size;
            break;
        }
        case 5: /* fixed32 */
            if(len - pos < 4) return false;
            pos += 4;
            break;
        case 1: /* fixed64 */
            if(len - pos < 8) return false;
            pos += 8;
            break;
        default:
            return false;
        }
    }

    return found;
}

size_t phone_encode_device_metadata(const PhoneIdentity* id, uint8_t* out, size_t out_len) {
    uint8_t body[64];
    PbWriter meta;
    PbWriter msg;

    if(id == NULL || out == NULL) return 0;

    pb_writer_init(&meta, body, sizeof(body));
    pb_write_string_field(&meta, METADATA_FIELD_FIRMWARE_VERSION, PHONE_FIRMWARE_VERSION);
    pb_write_varint_field(&meta, METADATA_FIELD_DEVICE_STATE_VERSION, PHONE_DEVICE_STATE_VERSION);
    /* Written even though it is true, because the default-omit rule would drop
     * it and the app treats a missing hasBluetooth as false. */
    pb_write_varint_field_always(&meta, METADATA_FIELD_HAS_BLUETOOTH, 1);
    pb_write_varint_field(&meta, METADATA_FIELD_HW_MODEL, id->hw_model);
    if(!pb_writer_ok(&meta)) return 0;

    pb_writer_init(&msg, out, out_len);
    pb_write_submessage(&msg, FROMRADIO_FIELD_METADATA, body, pb_writer_len(&meta));
    if(!pb_writer_ok(&msg)) return 0;

    return pb_writer_len(&msg);
}

void phone_identity_init(
    PhoneIdentity* out,
    uint32_t node_num,
    const char* long_name,
    const char* short_name) {
    if(out == NULL) return;

    memset(out, 0, sizeof(*out));
    out->node_num = node_num;
    out->hw_model = 0;

    /* Meshtastic writes the node id as "!" followed by eight lowercase hex
     * digits. The app displays it verbatim. */
    snprintf(out->id, sizeof(out->id), "!%08lx", (unsigned long)node_num);

    if(long_name) {
        strncpy(out->long_name, long_name, sizeof(out->long_name) - 1);
    }
    if(short_name) {
        strncpy(out->short_name, short_name, sizeof(out->short_name) - 1);
    }
}
