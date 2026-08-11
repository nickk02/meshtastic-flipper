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
#define NODEINFO_FIELD_NUM       1
/* mesh.proto MeshPacket. from, to and id are fixed32, not varint. */
#define MESHPACKET_FIELD_FROM    1
#define MESHPACKET_FIELD_TO      2
#define MESHPACKET_FIELD_DECODED 4
#define MESHPACKET_FIELD_ID      6

/* mesh.proto Data. dest, source and request_id are fixed32. */
#define DATA_FIELD_PORTNUM       1
#define DATA_FIELD_PAYLOAD       2
#define DATA_FIELD_WANT_RESPONSE 3
#define DATA_FIELD_REQUEST_ID    6

/* portnums.proto. */
#define PORTNUM_ADMIN_APP 6

/* admin.proto AdminMessage. */
#define ADMIN_FIELD_GET_OWNER_REQUEST  3
#define ADMIN_FIELD_GET_OWNER_RESPONSE 4
#define ADMIN_FIELD_SESSION_PASSKEY    101

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

/* Finds one field in a message and stops at the end of it.
 *
 * Length delimited fields report through ptr and plen, varint and fixed32
 * through val. Passing NULL for the pair that does not apply means a field of
 * the wrong wire type is treated as absent rather than misread, which matters
 * because several Meshtastic fields that look like integers are fixed32. */
static bool scan_field(
    const uint8_t* buf,
    size_t len,
    uint32_t want,
    const uint8_t** ptr,
    size_t* plen,
    uint64_t* val) {
    size_t pos = 0;
    bool found = false;

    if(buf == NULL) return false;

    while(pos < len) {
        uint64_t tag;
        if(!read_varint(buf, len, &pos, &tag)) return false;

        uint32_t field = (uint32_t)(tag >> 3);
        uint8_t wire = (uint8_t)(tag & 0x07);
        if(field == 0) return false;

        switch(wire) {
        case 0: {
            uint64_t v;
            if(!read_varint(buf, len, &pos, &v)) return false;
            if(field == want && val != NULL) {
                *val = v;
                found = true;
            }
            break;
        }
        case 5:
            if(len - pos < 4) return false;
            if(field == want && val != NULL) {
                *val = (uint32_t)buf[pos] | ((uint32_t)buf[pos + 1] << 8) |
                       ((uint32_t)buf[pos + 2] << 16) | ((uint32_t)buf[pos + 3] << 24);
                found = true;
            }
            pos += 4;
            break;
        case 1:
            if(len - pos < 8) return false;
            pos += 8;
            break;
        case 2: {
            uint64_t size;
            if(!read_varint(buf, len, &pos, &size)) return false;
            if(size > (uint64_t)(len - pos)) return false;
            if(field == want && ptr != NULL) {
                *ptr = buf + pos;
                *plen = (size_t)size;
                found = true;
            }
            pos += (size_t)size;
            break;
        }
        default:
            return false;
        }
    }

    return found;
}

const char* phone_admin_reason_name(PhoneAdminReason reason) {
    switch(reason) {
    case PhoneAdminOk:
        return "ok";
    case PhoneAdminNoPacket:
        return "no packet";
    case PhoneAdminNoDecoded:
        return "no decoded data";
    case PhoneAdminNoPortnum:
        return "no portnum";
    case PhoneAdminWrongPortnum:
        return "not admin portnum";
    case PhoneAdminNoPayload:
        return "no payload";
    case PhoneAdminNotGetOwner:
        return "admin but not get_owner";
    }
    return "unknown";
}

/* The field number of the first field in a message.
 *
 * AdminMessage.payload_variant is a oneof, so the first field is the request.
 * Returns 0 for an empty or malformed message, and 0 is not a legal field
 * number, so it doubles as "nothing here". */
static uint32_t first_field(const uint8_t* buf, size_t len) {
    size_t pos = 0;
    uint64_t tag;

    if(buf == NULL || len == 0) return 0;
    if(!read_varint(buf, len, &pos, &tag)) return 0;
    return (uint32_t)(tag >> 3);
}

/* True for any admin message. The reason code from the get_owner specific walk
 * distinguishes "not an admin message at all" from "an admin message asking
 * something else", and only the first means there is nothing to answer. */
bool phone_decode_admin_request(const uint8_t* buf, size_t len, PhoneAdminRequest* out) {
    PhoneAdminReason reason = PhoneAdminOk;

    if(phone_decode_get_owner_request_why(buf, len, out, &reason)) return true;
    return reason == PhoneAdminNotGetOwner && out->admin_field != 0;
}

bool phone_decode_get_owner_request(const uint8_t* buf, size_t len, PhoneAdminRequest* out) {
    PhoneAdminReason ignored;
    return phone_decode_get_owner_request_why(buf, len, out, &ignored);
}

bool phone_decode_get_owner_request_why(
    const uint8_t* buf,
    size_t len,
    PhoneAdminRequest* out,
    PhoneAdminReason* reason) {
    const uint8_t* packet = NULL;
    const uint8_t* data = NULL;
    const uint8_t* payload = NULL;
    size_t packet_len = 0;
    size_t data_len = 0;
    size_t payload_len = 0;
    uint64_t value = 0;

    if(out == NULL || reason == NULL) return false;
    memset(out, 0, sizeof(*out));
    *reason = PhoneAdminOk;

    /* ToRadio.packet -> MeshPacket.decoded -> Data. An admin request that is
     * encrypted rather than decoded is not for us to answer. */
    if(!scan_field(buf, len, TORADIO_FIELD_PACKET, &packet, &packet_len, NULL)) {
        *reason = PhoneAdminNoPacket;
        return false;
    }
    if(!scan_field(packet, packet_len, MESHPACKET_FIELD_DECODED, &data, &data_len, NULL)) {
        *reason = PhoneAdminNoDecoded;
        return false;
    }

    if(!scan_field(data, data_len, DATA_FIELD_PORTNUM, NULL, NULL, &value)) {
        *reason = PhoneAdminNoPortnum;
        return false;
    }
    if(value != PORTNUM_ADMIN_APP) {
        *reason = PhoneAdminWrongPortnum;
        return false;
    }

    if(!scan_field(data, data_len, DATA_FIELD_PAYLOAD, &payload, &payload_len, NULL)) {
        *reason = PhoneAdminNoPayload;
        return false;
    }

    /* Whichever admin field this carries. The first one wins: payload_variant
     * is a oneof, so there is only ever one. */
    out->admin_field = first_field(payload, payload_len);
    if(scan_field(data, data_len, DATA_FIELD_WANT_RESPONSE, NULL, NULL, &value)) {
        out->want_response = value != 0;
    }

    if(out->admin_field != ADMIN_GET_OWNER_REQUEST) {
        *reason = PhoneAdminNotGetOwner;
        /* Still fill in the addressing, because the caller that wants any admin
         * message rather than this one specifically needs it. */
        if(scan_field(packet, packet_len, MESHPACKET_FIELD_ID, NULL, NULL, &value)) {
            out->packet_id = (uint32_t)value;
        }
        if(scan_field(packet, packet_len, MESHPACKET_FIELD_FROM, NULL, NULL, &value)) {
            out->from = (uint32_t)value;
        }
        return false;
    }

    if(scan_field(packet, packet_len, MESHPACKET_FIELD_ID, NULL, NULL, &value)) {
        out->packet_id = (uint32_t)value;
    }
    if(scan_field(packet, packet_len, MESHPACKET_FIELD_FROM, NULL, NULL, &value)) {
        out->from = (uint32_t)value;
    }

    return true;
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

/* channel.proto, message Channel and message ChannelSettings. */
#define CHANNEL_FIELD_INDEX         1
#define CHANNEL_FIELD_SETTINGS      2
#define CHANNEL_FIELD_ROLE          3
#define CHANNEL_SETTINGS_FIELD_PSK  2
#define CHANNEL_SETTINGS_FIELD_NAME 3
#define CHANNEL_ROLE_PRIMARY        1

/* config.proto: Config.lora is 6, and the LoRaConfig fields below. */
#define CONFIG_FIELD_LORA       6
#define LORA_FIELD_USE_PRESET   1
#define LORA_FIELD_MODEM_PRESET 2
#define LORA_FIELD_REGION       7
#define LORA_FIELD_TX_ENABLED   9
#define LORA_FIELD_CHANNEL_NUM  11

#define LORA_REGION_US              1
#define LORA_MODEM_PRESET_LONG_FAST 0

size_t phone_encode_primary_channel(
    const char* name,
    uint8_t psk_index,
    uint8_t* out,
    size_t out_len) {
    uint8_t settings[64];
    uint8_t body[96];
    PbWriter set_writer;
    PbWriter body_writer;
    PbWriter msg;

    if(name == NULL || out == NULL) return 0;

    pb_writer_init(&set_writer, settings, sizeof(settings));
    pb_write_bytes_field(&set_writer, CHANNEL_SETTINGS_FIELD_PSK, &psk_index, 1);
    pb_write_string_field(&set_writer, CHANNEL_SETTINGS_FIELD_NAME, name);
    if(!pb_writer_ok(&set_writer)) return 0;

    pb_writer_init(&body_writer, body, sizeof(body));
    /* Channel.index is left out. The primary channel is index 0, and protobuf
     * omits zero-valued scalars, so absent and zero are the same thing here. */
    pb_write_submessage(
        &body_writer, CHANNEL_FIELD_SETTINGS, settings, pb_writer_len(&set_writer));
    pb_write_varint_field_always(&body_writer, CHANNEL_FIELD_ROLE, CHANNEL_ROLE_PRIMARY);
    if(!pb_writer_ok(&body_writer)) return 0;

    pb_writer_init(&msg, out, out_len);
    pb_write_submessage(&msg, FROMRADIO_FIELD_CHANNEL, body, pb_writer_len(&body_writer));
    if(!pb_writer_ok(&msg)) return 0;

    return pb_writer_len(&msg);
}

static size_t encode_variant(
    uint32_t outer,
    uint32_t variant,
    const uint8_t* body,
    size_t body_len,
    uint8_t* out,
    size_t out_len) {
    uint8_t wrapper[96];
    PbWriter w;
    PbWriter msg;
    static const uint8_t nothing = 0;

    if(out == NULL) return 0;
    if(body == NULL) {
        body = &nothing;
        body_len = 0;
    }

    pb_writer_init(&w, wrapper, sizeof(wrapper));
    pb_write_submessage(&w, variant, body, body_len);
    if(!pb_writer_ok(&w)) return 0;

    pb_writer_init(&msg, out, out_len);
    pb_write_submessage(&msg, outer, wrapper, pb_writer_len(&w));
    if(!pb_writer_ok(&msg)) return 0;

    return pb_writer_len(&msg);
}

size_t phone_encode_config_variant(
    uint32_t variant,
    const uint8_t* body,
    size_t body_len,
    uint8_t* out,
    size_t out_len) {
    return encode_variant(FROMRADIO_FIELD_CONFIG, variant, body, body_len, out, out_len);
}

size_t phone_encode_moduleconfig_variant(uint32_t variant, uint8_t* out, size_t out_len) {
    return encode_variant(FROMRADIO_FIELD_MODULECONFIG, variant, NULL, 0, out, out_len);
}

size_t phone_encode_empty_channel(uint32_t index, uint8_t* out, size_t out_len) {
    uint8_t body[32];
    PbWriter body_writer;
    PbWriter msg;

    if(out == NULL) return 0;

    pb_writer_init(&body_writer, body, sizeof(body));
    pb_write_varint_field(&body_writer, CHANNEL_FIELD_INDEX, index);
    if(!pb_writer_ok(&body_writer)) return 0;

    pb_writer_init(&msg, out, out_len);
    pb_write_submessage(&msg, FROMRADIO_FIELD_CHANNEL, body, pb_writer_len(&body_writer));
    if(!pb_writer_ok(&msg)) return 0;

    return pb_writer_len(&msg);
}

size_t phone_encode_device_ui(uint8_t* out, size_t out_len) {
    static const uint8_t nothing = 0;
    PbWriter msg;

    if(out == NULL) return 0;

    pb_writer_init(&msg, out, out_len);
    pb_write_submessage(&msg, FROMRADIO_FIELD_DEVICEUI, &nothing, 0);
    if(!pb_writer_ok(&msg)) return 0;

    return pb_writer_len(&msg);
}

size_t phone_encode_lora_config(uint32_t channel_num, uint8_t* out, size_t out_len) {
    uint8_t lora[64];
    uint8_t body[96];
    PbWriter lora_writer;
    PbWriter body_writer;
    PbWriter msg;

    if(out == NULL) return 0;

    pb_writer_init(&lora_writer, lora, sizeof(lora));
    pb_write_varint_field_always(&lora_writer, LORA_FIELD_USE_PRESET, 1);
    /* LONG_FAST is 0, which a default-omitting writer would drop. Absent and
     * LONG_FAST happen to mean the same thing, but relying on that would make
     * the message say nothing about the preset, so it is written out. */
    pb_write_varint_field_always(
        &lora_writer, LORA_FIELD_MODEM_PRESET, LORA_MODEM_PRESET_LONG_FAST);
    pb_write_varint_field_always(&lora_writer, LORA_FIELD_REGION, LORA_REGION_US);
    /* Written as false on purpose. This build has no transmit path, and letting
     * the app believe otherwise invites it to queue messages that never go out. */
    pb_write_varint_field_always(&lora_writer, LORA_FIELD_TX_ENABLED, 0);
    pb_write_varint_field(&lora_writer, LORA_FIELD_CHANNEL_NUM, channel_num);
    if(!pb_writer_ok(&lora_writer)) return 0;

    pb_writer_init(&body_writer, body, sizeof(body));
    pb_write_submessage(&body_writer, CONFIG_FIELD_LORA, lora, pb_writer_len(&lora_writer));
    if(!pb_writer_ok(&body_writer)) return 0;

    pb_writer_init(&msg, out, out_len);
    pb_write_submessage(&msg, FROMRADIO_FIELD_CONFIG, body, pb_writer_len(&body_writer));
    if(!pb_writer_ok(&msg)) return 0;

    return pb_writer_len(&msg);
}

size_t phone_encode_get_owner_response(
    const PhoneIdentity* id,
    const PhoneAdminRequest* request,
    const uint8_t* passkey,
    uint8_t* out,
    size_t out_len) {
    uint8_t user[96];
    uint8_t admin[128];
    uint8_t data[160];
    uint8_t packet[192];
    PbWriter w;

    if(id == NULL || request == NULL || passkey == NULL || out == NULL) return 0;

    /* The User this device reports as its owner. Same shape as the one inside
     * NodeInfo, but AdminMessage carries it bare. */
    pb_writer_init(&w, user, sizeof(user));
    pb_write_string_field(&w, USER_FIELD_ID, id->id);
    pb_write_string_field(&w, USER_FIELD_LONG_NAME, id->long_name);
    pb_write_string_field(&w, USER_FIELD_SHORT_NAME, id->short_name);
    pb_write_varint_field(&w, USER_FIELD_HW_MODEL, id->hw_model);
    if(!pb_writer_ok(&w)) return 0;
    size_t user_len = pb_writer_len(&w);

    pb_writer_init(&w, admin, sizeof(admin));
    pb_write_submessage(&w, ADMIN_FIELD_GET_OWNER_RESPONSE, user, user_len);
    pb_write_bytes_field(&w, ADMIN_FIELD_SESSION_PASSKEY, passkey, PHONE_SESSION_PASSKEY_LEN);
    if(!pb_writer_ok(&w)) return 0;
    size_t admin_len = pb_writer_len(&w);

    pb_writer_init(&w, data, sizeof(data));
    pb_write_varint_field_always(&w, DATA_FIELD_PORTNUM, PORTNUM_ADMIN_APP);
    pb_write_bytes_field(&w, DATA_FIELD_PAYLOAD, admin, admin_len);
    /* request_id is what lets the client match this to the request it sent.
     * fixed32, so a varint here would be silently discarded. */
    pb_write_fixed32_field(&w, DATA_FIELD_REQUEST_ID, request->packet_id);
    if(!pb_writer_ok(&w)) return 0;
    size_t data_len = pb_writer_len(&w);

    pb_writer_init(&w, packet, sizeof(packet));
    pb_write_fixed32_field_always(&w, MESHPACKET_FIELD_FROM, id->node_num);
    pb_write_fixed32_field(&w, MESHPACKET_FIELD_TO, request->from);
    pb_write_submessage(&w, MESHPACKET_FIELD_DECODED, data, data_len);
    /* Reusing the request id as the packet id keeps this device from having to
     * invent one. The client matches on request_id, not on this. */
    pb_write_fixed32_field(&w, MESHPACKET_FIELD_ID, request->packet_id);
    if(!pb_writer_ok(&w)) return 0;

    return phone_encode_packet(packet, pb_writer_len(&w), out, out_len);
}

/* Wraps an already-built Data into FromRadio.packet, addressed at the sender. */
static size_t wrap_admin_packet(
    const PhoneIdentity* id,
    const PhoneAdminRequest* request,
    const uint8_t* data,
    size_t data_len,
    uint8_t* out,
    size_t out_len) {
    uint8_t packet[192];
    PbWriter w;

    pb_writer_init(&w, packet, sizeof(packet));
    pb_write_fixed32_field_always(&w, MESHPACKET_FIELD_FROM, id->node_num);
    pb_write_fixed32_field(&w, MESHPACKET_FIELD_TO, request->from);
    pb_write_submessage(&w, MESHPACKET_FIELD_DECODED, data, data_len);
    pb_write_fixed32_field(&w, MESHPACKET_FIELD_ID, request->packet_id);
    if(!pb_writer_ok(&w)) return 0;

    return phone_encode_packet(packet, pb_writer_len(&w), out, out_len);
}

size_t phone_encode_admin_reply(
    const PhoneIdentity* id,
    const PhoneAdminRequest* request,
    const uint8_t* passkey,
    uint8_t* out,
    size_t out_len) {
    uint8_t admin[128];
    uint8_t data[160];
    PbWriter w;
    size_t admin_len = 0;
    uint32_t portnum = PORTNUM_ADMIN_APP;

    if(id == NULL || request == NULL || out == NULL) return 0;
    if(!request->want_response) return 0;

    switch(request->admin_field) {
    case ADMIN_GET_OWNER_REQUEST:
        return phone_encode_get_owner_response(id, request, passkey, out, out_len);

    case ADMIN_GET_CANNED_REQUEST:
    case ADMIN_GET_RINGTONE_REQUEST: {
        /* Both responses are strings and both are legitimately empty here: this
         * device has no canned messages and no ringtone. An empty string is an
         * answer, silence is not, and the client waits on the difference. */
        uint32_t field = request->admin_field == ADMIN_GET_CANNED_REQUEST ?
                             ADMIN_GET_CANNED_RESPONSE :
                             ADMIN_GET_RINGTONE_RESPONSE;
        pb_writer_init(&w, admin, sizeof(admin));
        pb_write_string_field(&w, field, "");
        if(passkey != NULL) {
            pb_write_bytes_field(
                &w, ADMIN_FIELD_SESSION_PASSKEY, passkey, PHONE_SESSION_PASSKEY_LEN);
        }
        if(!pb_writer_ok(&w)) return 0;
        admin_len = pb_writer_len(&w);
        break;
    }

    default:
        /* Everything else, setters included, is acknowledged on the routing
         * port. Routing with no error_reason is an ACK, and error_reason NONE
         * is 0, so the message is deliberately empty. */
        portnum = PORTNUM_ROUTING_APP;
        admin_len = 0;
        break;
    }

    pb_writer_init(&w, data, sizeof(data));
    pb_write_varint_field_always(&w, DATA_FIELD_PORTNUM, portnum);
    if(admin_len > 0) pb_write_bytes_field(&w, DATA_FIELD_PAYLOAD, admin, admin_len);
    pb_write_fixed32_field(&w, DATA_FIELD_REQUEST_ID, request->packet_id);
    if(!pb_writer_ok(&w)) return 0;

    return wrap_admin_packet(id, request, data, pb_writer_len(&w), out, out_len);
}

void phone_identity_from_config(const MeshConfig* config, PhoneIdentity* out) {
    if(out == NULL) return;
    if(config == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }

    /* hw_model stays 0, HW_UNSET. Claiming a hardware model this is not would
     * make the app show a device picture and capabilities that do not exist. */
    phone_identity_init(
        out, config->owner.node_num, config->owner.long_name, config->owner.short_name);
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
