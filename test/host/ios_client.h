/* Host model of the Meshtastic-Apple client's connect flow.
 *
 * This is the client this device actually talks to, read from the shipped
 * v2.7.21 source. Every rule below cites the file it came from. Nothing here
 * is inferred from the Android client or from behaviour.
 *
 *   AccessoryManager+Connect.swift   the eight connect steps and their timeouts
 *   AccessoryManager.swift           sendWantConfig, sendWantDatabase,
 *                                    processFromRadio, checkIsVersionSupported,
 *                                    NONCE_ONLY_CONFIG, NONCE_ONLY_DB, minimumVersion
 *   AccessoryManager+FromRadio.swift handleMyInfo, handleNodeInfo, handleConfig,
 *                                    handleModuleConfig, handleDeviceMetadata
 *   AccessoryManager+ToRadio.swift   sendHeartbeat, sendTime, getRingtone,
 *                                    getCannedMessageModuleMessages, saveTimeZone
 *   BLEConnection.swift              drainPendingPackets, didUpdateValueFor,
 *                                    send, read
 *   Connection.swift                 FromRadioDecoder.classify
 *
 * The client side is driven against the real handshake and encoder in this
 * repo, so a change to either is exercised the way the phone would exercise
 * it. Two transports are modelled:
 *
 *   IosTransportIdeal   every read consumes one queued frame. What a real
 *                       node with a per-read callback gives the phone.
 *   IosTransportPaced   what this app does: the FromRadio value is restated
 *                       every DRAIN_INTERVAL_MS and a read returns whatever
 *                       is published, so the phone sees each frame several
 *                       times and the batch takes count x interval to drain.
 *
 * Header only, static inline, so a test includes it and nothing else changes
 * in run_tests.sh. */
#ifndef IOS_CLIENT_H
#define IOS_CLIENT_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "src/ble/meshtastic_handshake.h"
#include "src/proto/pb_write.h"

/* ATT MTU the phone ends up with against this device, and the largest value
 * one read returns (MTU minus the one byte ATT opcode). A frame longer than
 * this comes back truncated and FromRadioDecoder.classify reports .failed,
 * which BLEConnection.drainPendingPackets answers by disconnecting. */
#define IOS_ATT_MTU  185
#define IOS_READ_MAX (IOS_ATT_MTU - 1)

/* The largest write without response: MTU minus the three byte ATT header,
 * what maximumWriteValueLength(for: .withoutResponse) returns. BLEConnection
 * .send() uses that write type whenever ToRadio offers it (line 533). */
#define IOS_WRITE_NO_RSP_MAX (IOS_ATT_MTU - 3)

/* AccessoryManager.swift:126-128. */
#define IOS_NONCE_CONFIG 69420
#define IOS_NONCE_DB     69421
#define IOS_MIN_FIRMWARE "2.5.18"

/* Paced transport timing. The first two mirror meshtastic_service.c; the read
 * round trip is a client-side figure, see ios_client_paced_defaults. */
#define IOS_MODEL_DRAIN_INTERVAL_MS 350
#define IOS_MODEL_WORKER_POLL_MS    50
#define IOS_MODEL_READ_RTT_MS       88
#define IOS_MODEL_QUEUE_DEPTH       64 /* mirrors QUEUE_DEPTH in meshtastic_service.c */

/* Step timeouts, AccessoryManager+Connect.swift. Step 5 is 10s with three
 * attempts; Step 3 is 30s; Step 5a is 120s. */
#define IOS_STEP3_TIMEOUT_MS  30000
#define IOS_STEP5_TIMEOUT_MS  10000
#define IOS_STEP5A_TIMEOUT_MS 120000

/* mesh.proto FromRadio payload_variant, from mesh.pb.h in the firmware. */
#define IOS_FR_ID              1
#define IOS_FR_PACKET          2
#define IOS_FR_MY_INFO         3
#define IOS_FR_NODE_INFO       4
#define IOS_FR_CONFIG          5
#define IOS_FR_LOG_RECORD      6
#define IOS_FR_CONFIG_COMPLETE 7
#define IOS_FR_REBOOTED        8
#define IOS_FR_MODULECONFIG    9
#define IOS_FR_CHANNEL         10
#define IOS_FR_QUEUE_STATUS    11
#define IOS_FR_XMODEM          12
#define IOS_FR_METADATA        13
#define IOS_FR_MQTT_PROXY      14
#define IOS_FR_FILE_INFO       15
#define IOS_FR_CLIENT_NOTIFY   16
#define IOS_FR_DEVICEUI        17
#define IOS_FR_LOCKDOWN        18
#define IOS_FR_REGION_PRESETS  19
#define IOS_FR_MAX             19

/* mesh.proto ToRadio. */
#define IOS_TR_PACKET         1
#define IOS_TR_WANT_CONFIG_ID 3
#define IOS_TR_HEARTBEAT      7

/* admin.proto AdminMessage, the requests the client sends during connect. */
#define IOS_ADMIN_GET_CANNED   10
#define IOS_ADMIN_GET_RINGTONE 14
#define IOS_ADMIN_SET_CONFIG   34
#define IOS_ADMIN_SET_TIME     43

/* config.proto and module_config.proto variants the client reacts to. */
#define IOS_CONFIG_DEVICE           1
#define IOS_DEVICECONFIG_TZDEF      11
#define IOS_MODULE_EXT_NOTIFICATION 3
#define IOS_MODULE_CANNED_MESSAGE   7

/* mesh.proto MeshPacket and Data, as read back from FromRadio.packet. */
#define IOS_MP_FROM           1
#define IOS_MP_TO             2
#define IOS_MP_CHANNEL        3
#define IOS_MP_DECODED        4
#define IOS_MP_ID             6
#define IOS_MP_RX_TIME        7
#define IOS_MP_RX_SNR         8
#define IOS_MP_HOP_LIMIT      9
#define IOS_MP_WANT_ACK       10
#define IOS_MP_PRIORITY       11
#define IOS_MP_RX_RSSI        12
#define IOS_MP_HOP_START      15
#define IOS_DATA_PORTNUM      1
#define IOS_DATA_PAYLOAD      2
#define IOS_DATA_WANT_RSP     3
#define IOS_DATA_REQ_ID       6
#define IOS_PORTNUM_ADMIN     6
#define IOS_PRIORITY_RELIABLE 70

#define IOS_QUEUE_CAP     128
#define IOS_LOG_LINES     64
#define IOS_LOG_LINE      128
#define IOS_VARIANT_TRACE 512
#define IOS_VERSION_MAX   48
#define IOS_DB_NODES_MAX  64

typedef enum {
    IosTransportIdeal,
    IosTransportPaced,
} IosTransport;

typedef enum {
    IosOutcomeNotRun = 0,
    IosOutcomeConnected,
    IosOutcomeFailed,
} IosOutcome;

/* Larger than PHONE_FRAME_MAX on purpose. The harness must be able to carry a
 * frame the device should have refused, so a test can show what the phone does
 * when one gets through. */
#define IOS_FRAME_MAX 256

typedef struct {
    uint8_t data[IOS_FRAME_MAX];
    size_t len;
} IosFrame;

/* The last FromRadio.packet the client decoded. Filled for any portnum. */
typedef struct {
    uint32_t from;
    uint32_t to;
    uint32_t id;
    uint32_t portnum;
    uint32_t request_id;
    uint32_t hop_limit;
    uint32_t hop_start;
    int32_t rx_rssi;
    float rx_snr;
    bool has_rx_snr;
    bool has_rx_rssi;
    bool has_hop_limit;
    bool has_hop_start;
    bool decoded;
    size_t payload_len;
    uint8_t payload[HANDSHAKE_MAX_MESSAGE];
} IosPacket;

typedef struct {
    /* Device side. The real handshake, driven the way the service drives it. */
    Handshake handshake;
    IosFrame queue[IOS_QUEUE_CAP];
    size_t q_tail;
    size_t q_pending;
    size_t queue_depth; /* modelled QUEUE_DEPTH; a full queue refuses */
    int queue_refused;
    int to_radio_writes;
    int to_radio_not_understood;
    size_t to_radio_max_write; /* one write's limit, IOS_WRITE_NO_RSP_MAX */
    int to_radio_oversize; /* writes longer than to_radio_max_write */

    /* Paced transport state. */
    IosTransport transport;
    uint32_t now_ms;
    uint32_t drain_due_ms;
    bool drain_active;
    bool doorbell_rung; /* once per batch, meshtastic_service.c publish_head */
    bool doorbell_pending; /* rung and not yet consumed by a drain */
    uint32_t doorbell_at_ms;
    IosFrame published; /* the FromRadio value the stack serves reads from */
    uint32_t drain_interval_ms;
    uint32_t worker_poll_ms;
    uint32_t read_rtt_ms;

    /* Client side, AccessoryManager state that the connect steps read. */
    bool refresh_active; /* activeAutomaticConfigRefresh != nil */
    bool have_my_info; /* device.num set, handleMyInfo */
    uint32_t my_node_num;
    bool have_long_name; /* device.longName set, handleNodeInfo for own num */
    bool have_firmware_version;
    char firmware_version[IOS_VERSION_MAX];
    bool in_database_stage; /* state == .retrievingDatabase */
    int node_count; /* .retrievingDatabase(nodeCount) */
    /* Distinct node numbers among those, since the client counts repeats. */
    int db_distinct_nodes;
    uint32_t db_node_nums[IOS_DB_NODES_MAX];
    bool db_first_node_seen;
    bool db_gate_open;
    int stage; /* 0 idle, 1 config, 2 database, 3 connected */

    /* Frame accounting. */
    int frames_total;
    int frames_stage[4];
    int frames_empty_reads;
    size_t max_frame_len;
    int oversize_frames;
    int decode_failures;
    int invalid_utf8;
    int unknown_variants;
    int oneof_multiple;
    int variant_count[IOS_FR_MAX + 1];
    uint8_t variant_trace[IOS_VARIANT_TRACE];
    int variant_trace_len;

    /* Rule violations the handlers would log and drop. */
    int metadata_dropped_no_device;
    int channel_dropped_no_device;
    int config_dropped_no_name;
    int moduleconfig_dropped_no_name;
    int nodeinfo_zero_num;
    int config_complete_ignored; /* 69420 with no refresh owner */
    int config_complete_unknown;
    int db_complete_count;

    /* Requests the client sent during connect, and what it got. */
    int sent_heartbeats;
    int sent_get_canned;
    int sent_get_ringtone;
    int sent_set_config_tzdef;
    int sent_set_time;
    int admin_responses;
    int routing_acks;
    int queue_status_frames;
    int packet_frames;
    IosPacket last_packet;

    /* Paced transport observations. */
    int duplicate_reads;
    uint32_t stage_started_ms[4];
    uint32_t stage_finished_ms[4];

    /* Outcome. */
    IosOutcome outcome;
    int failed_step;
    char failure[IOS_LOG_LINE];
    char log[IOS_LOG_LINES][IOS_LOG_LINE];
    int log_count;
    bool verbose;
} IosClient;

/* Logging */

static inline void ios_log(IosClient* c, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));

static inline void ios_log(IosClient* c, const char* fmt, ...) {
    va_list ap;
    char line[IOS_LOG_LINE];
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if(c->log_count < IOS_LOG_LINES) {
        memcpy(c->log[c->log_count], line, sizeof(line));
        c->log_count++;
    }
    if(c->verbose) printf("    [ios] %s\n", line);
}

static inline void ios_fail(IosClient* c, int step, const char* why) {
    if(c->outcome == IosOutcomeFailed) return;
    c->outcome = IosOutcomeFailed;
    c->failed_step = step;
    snprintf(c->failure, sizeof(c->failure), "%s", why);
    ios_log(c, "Step %d failed: %s", step, why);
}

/* Protobuf reading */

typedef struct {
    const uint8_t* buf;
    size_t len;
    size_t pos;
} IosReader;

static inline bool ios_read_varint(IosReader* r, uint64_t* out) {
    uint64_t value = 0;
    unsigned shift = 0;
    while(r->pos < r->len) {
        uint8_t b = r->buf[r->pos++];
        if(shift >= 64) return false;
        value |= (uint64_t)(b & 0x7F) << shift;
        if((b & 0x80) == 0) {
            *out = value;
            return true;
        }
        shift += 7;
    }
    return false;
}

/* One field. wire 0 and 5 report through val, wire 2 through ptr/plen.
 * Returns false at end of message or on a malformed field; *ok says which. */
static inline bool ios_next_field(
    IosReader* r,
    uint32_t* field,
    uint8_t* wire,
    uint64_t* val,
    const uint8_t** ptr,
    size_t* plen,
    bool* ok) {
    uint64_t tag;
    *ok = true;
    if(r->pos >= r->len) return false;
    if(!ios_read_varint(r, &tag)) {
        *ok = false;
        return false;
    }
    *field = (uint32_t)(tag >> 3);
    *wire = (uint8_t)(tag & 7);
    if(*field == 0) {
        *ok = false;
        return false;
    }
    switch(*wire) {
    case 0:
        if(!ios_read_varint(r, val)) {
            *ok = false;
            return false;
        }
        return true;
    case 1:
        if(r->len - r->pos < 8) {
            *ok = false;
            return false;
        }
        r->pos += 8;
        *val = 0;
        return true;
    case 2: {
        uint64_t size;
        if(!ios_read_varint(r, &size) || size > (uint64_t)(r->len - r->pos)) {
            *ok = false;
            return false;
        }
        *ptr = r->buf + r->pos;
        *plen = (size_t)size;
        r->pos += (size_t)size;
        return true;
    }
    case 5:
        if(r->len - r->pos < 4) {
            *ok = false;
            return false;
        }
        *val = (uint32_t)r->buf[r->pos] | ((uint32_t)r->buf[r->pos + 1] << 8) |
               ((uint32_t)r->buf[r->pos + 2] << 16) | ((uint32_t)r->buf[r->pos + 3] << 24);
        r->pos += 4;
        return true;
    default:
        *ok = false;
        return false;
    }
}

/* Whole-message wire validity, one level. What SwiftProtobuf would reject as
 * malformed framing at this level. Nested messages are checked where the
 * client actually decodes them. */
static inline bool ios_wire_valid(const uint8_t* buf, size_t len) {
    IosReader r = {buf, len, 0};
    uint32_t field;
    uint8_t wire;
    uint64_t val;
    const uint8_t* ptr;
    size_t plen;
    bool ok;
    while(ios_next_field(&r, &field, &wire, &val, &ptr, &plen, &ok)) {
    }
    return ok;
}

/* First occurrence of a field. Last-wins semantics are what protobuf uses,
 * but the encoders here never repeat a scalar, and the difference is only
 * visible for the oneof check below, which counts. */
static inline bool ios_find(
    const uint8_t* buf,
    size_t len,
    uint32_t want,
    uint64_t* val,
    const uint8_t** ptr,
    size_t* plen) {
    IosReader r = {buf, len, 0};
    uint32_t field;
    uint8_t wire;
    uint64_t v;
    const uint8_t* p;
    size_t pl;
    bool ok;
    bool found = false;
    while(ios_next_field(&r, &field, &wire, &v, &p, &pl, &ok)) {
        if(field != want) continue;
        if(wire == 2) {
            if(ptr) *ptr = p;
            if(plen) *plen = pl;
            found = true;
        } else {
            if(val) *val = v;
            found = true;
        }
    }
    return ok && found;
}

static inline bool ios_utf8_valid(const uint8_t* s, size_t n) {
    size_t i = 0;
    while(i < n) {
        uint8_t b = s[i];
        size_t extra;
        if(b < 0x80) {
            i++;
            continue;
        } else if((b & 0xE0) == 0xC0 && b >= 0xC2) {
            extra = 1;
        } else if((b & 0xF0) == 0xE0) {
            extra = 2;
        } else if((b & 0xF8) == 0xF0 && b <= 0xF4) {
            extra = 3;
        } else {
            return false;
        }
        if(i + extra >= n) return false;
        for(size_t k = 1; k <= extra; k++) {
            if(i + k >= n || (s[i + k] & 0xC0) != 0x80) return false;
        }
        i += extra + 1;
    }
    return true;
}

/* Version rule */

/* Foundation's String.compare(_:options: .numeric): digit runs compare as
 * numbers, everything else as characters. Returns <0, 0, >0. */
static inline int ios_numeric_compare(const char* a, const char* b) {
    size_t i = 0, j = 0;
    while(a[i] && b[j]) {
        bool da = a[i] >= '0' && a[i] <= '9';
        bool db = b[j] >= '0' && b[j] <= '9';
        if(da && db) {
            unsigned long long na = 0, nb = 0;
            while(a[i] >= '0' && a[i] <= '9')
                na = na * 10 + (unsigned)(a[i++] - '0');
            while(b[j] >= '0' && b[j] <= '9')
                nb = nb * 10 + (unsigned)(b[j++] - '0');
            if(na != nb) return na < nb ? -1 : 1;
        } else {
            if(a[i] != b[j]) return (unsigned char)a[i] < (unsigned char)b[j] ? -1 : 1;
            i++;
            j++;
        }
    }
    if(a[i] == 0 && b[j] == 0) return 0;
    return a[i] == 0 ? -1 : 1;
}

/* What Step 6 stores in UserDefaults.firmwareVersion: everything before the
 * last dot. "2.7.21.abc123" becomes "2.7.21"; "2.5.0" becomes "2.5". */
static inline void ios_version_strip(const char* fw, char* out, size_t cap) {
    const char* dot = strrchr(fw, '.');
    size_t n = dot ? (size_t)(dot - fw) : strlen(fw);
    if(n >= cap) n = cap - 1;
    memcpy(out, fw, n);
    out[n] = 0;
}

/* Step 6, AccessoryManager+Connect.swift, and checkIsVersionSupported in
 * AccessoryManager.swift:1230-1253.
 *
 * The version must contain a dot or the step throws versionMismatch. Then
 * minimumVersion "2.5.18" is compared, numerically, against the LIVE
 * firmware_version string the metadata frame carried, hash suffix and all.
 * The stripped copy only goes to UserDefaults and is consulted when the
 * live one is nil, which it never is here. The step passes when
 * "2.5.18" <= version. */
static inline bool ios_version_supported(const char* fw) {
    if(fw == NULL || strchr(fw, '.') == NULL) return false;
    return ios_numeric_compare(IOS_MIN_FIRMWARE, fw) <= 0;
}

/* Device model */

static inline void ios_device_queue_frame(IosClient* c, const uint8_t* data, size_t len) {
    if(len == 0 || len > IOS_FRAME_MAX) return;
    if(c->q_pending >= c->queue_depth || c->q_pending >= IOS_QUEUE_CAP) {
        c->queue_refused++;
        ios_log(c, "device: queue full, frame of %u bytes refused", (unsigned)len);
        return;
    }
    IosFrame* slot = &c->queue[(c->q_tail + c->q_pending) % IOS_QUEUE_CAP];
    memcpy(slot->data, data, len);
    slot->len = len;
    c->q_pending++;
    /* meshtastic_ble_service_queue: arm the drain, due now. The worker
     * notices within one poll. */
    c->drain_active = true;
    c->drain_due_ms = c->now_ms + c->worker_poll_ms;
}

/* Queue a frame the app built outside the handshake, such as a received
 * mesh packet forwarded to the phone. */
static inline void ios_device_inject(IosClient* c, const uint8_t* data, size_t len) {
    ios_device_queue_frame(c, data, len);
}

/* One drain_step from meshtastic_service.c: publish the head, then advance.
 * One step past empty publishes a zero-length value. */
static inline void ios_device_drain_step(IosClient* c) {
    bool had = c->q_pending > 0;
    if(had) {
        c->published = c->queue[c->q_tail];
        c->q_tail = (c->q_tail + 1) % IOS_QUEUE_CAP;
        c->q_pending--;
        if(!c->doorbell_rung) {
            c->doorbell_rung = true;
            c->doorbell_pending = true;
            c->doorbell_at_ms = c->drain_due_ms;
        }
    } else {
        c->published.len = 0;
    }
    c->drain_active = had || c->q_pending > 0;
    if(!c->drain_active) c->doorbell_rung = false;
    c->drain_due_ms += c->drain_interval_ms;
}

static inline void ios_device_advance(IosClient* c, uint32_t t) {
    while(c->drain_active && (int32_t)(c->drain_due_ms - t) <= 0) {
        ios_device_drain_step(c);
    }
    if((int32_t)(t - c->now_ms) > 0) c->now_ms = t;
}

/* A ToRadio write. The real handshake answers it; its frames go on the
 * modelled queue exactly as handle_to_radio would queue them. */
static inline void ios_device_write(IosClient* c, const uint8_t* data, size_t len) {
    HandshakeReply reply;
    c->to_radio_writes++;
    if(len > c->to_radio_max_write) c->to_radio_oversize++;
    if(c->transport == IosTransportPaced) ios_device_advance(c, c->now_ms);

    /* handle_to_radio resets the queue on a fresh stage one request. The
     * reply to the Step 2 heartbeat is discarded with everything else. */
    uint32_t nonce = 0;
    if(phone_decode_want_config_id(data, len, &nonce) && nonce == PHONE_NONCE_CONFIG) {
        c->q_tail = 0;
        c->q_pending = 0;
        c->drain_active = false;
        c->doorbell_rung = false;
    }

    bool understood = handshake_handle_to_radio(&c->handshake, data, len, &reply);
    if(!understood) {
        /* The service also accepts a bare heartbeat without a reply. */
        if(!(len > 0 && (data[0] >> 3) == IOS_TR_HEARTBEAT)) {
            c->to_radio_not_understood++;
            ios_log(
                c,
                "device: ToRadio not understood, tag=%u len=%u",
                len ? data[0] >> 3 : 0,
                (unsigned)len);
        }
        return;
    }
    for(size_t i = 0; i < reply.count; i++) {
        ios_device_queue_frame(c, reply.messages[i].data, reply.messages[i].len);
    }
}

/* BLEConnection.read(): one readValue, one response, at most IOS_READ_MAX
 * bytes of it. out must hold IOS_READ_MAX. */
static inline size_t ios_device_read(IosClient* c, uint8_t* out, bool* truncated) {
    size_t len = 0;
    *truncated = false;
    if(c->transport == IosTransportIdeal) {
        if(c->q_pending > 0) {
            IosFrame* f = &c->queue[c->q_tail];
            len = f->len;
            memcpy(out, f->data, len > IOS_READ_MAX ? IOS_READ_MAX : len);
            c->q_tail = (c->q_tail + 1) % IOS_QUEUE_CAP;
            c->q_pending--;
        }
    } else {
        ios_device_advance(c, c->now_ms);
        len = c->published.len;
        memcpy(out, c->published.data, len > IOS_READ_MAX ? IOS_READ_MAX : len);
        c->now_ms += c->read_rtt_ms;
    }
    if(len > IOS_READ_MAX) {
        *truncated = true;
        len = IOS_READ_MAX;
    }
    return len;
}

/* Client side ToRadio builders, AccessoryManager+ToRadio.swift. SwiftProtobuf
 * writes fields in number order, which is what these reproduce. */

static inline size_t ios_build_heartbeat(uint8_t* buf, size_t cap, uint32_t nonce) {
    uint8_t hb[8];
    PbWriter w;
    pb_writer_init(&w, hb, sizeof(hb));
    pb_write_varint_field(&w, 1, nonce);
    size_t hb_len = pb_writer_len(&w);
    pb_writer_init(&w, buf, cap);
    pb_write_submessage(&w, IOS_TR_HEARTBEAT, hb, hb_len);
    return pb_writer_ok(&w) ? pb_writer_len(&w) : 0;
}

static inline size_t ios_build_want_config(uint8_t* buf, size_t cap, uint32_t nonce) {
    PbWriter w;
    pb_writer_init(&w, buf, cap);
    pb_write_varint_field_always(&w, IOS_TR_WANT_CONFIG_ID, nonce);
    return pb_writer_ok(&w) ? pb_writer_len(&w) : 0;
}

/* ToRadio.packet carrying an AdminMessage, the shape every request in
 * AccessoryManager+ToRadio.swift takes: to, from, id, priority RELIABLE,
 * want_ack, decoded { portnum ADMIN_APP, payload, want_response }. */
static inline size_t ios_build_admin(
    uint8_t* buf,
    size_t cap,
    uint32_t to,
    uint32_t from,
    uint32_t id,
    const uint8_t* admin,
    size_t admin_len,
    bool want_response) {
    uint8_t data[160];
    uint8_t packet[192];
    PbWriter w;

    pb_writer_init(&w, data, sizeof(data));
    pb_write_varint_field_always(&w, IOS_DATA_PORTNUM, IOS_PORTNUM_ADMIN);
    pb_write_bytes_field(&w, IOS_DATA_PAYLOAD, admin, admin_len);
    if(want_response) pb_write_varint_field_always(&w, IOS_DATA_WANT_RSP, 1);
    size_t data_len = pb_writer_len(&w);

    pb_writer_init(&w, packet, sizeof(packet));
    pb_write_fixed32_field(&w, IOS_MP_FROM, from);
    pb_write_fixed32_field(&w, IOS_MP_TO, to);
    pb_write_submessage(&w, IOS_MP_DECODED, data, data_len);
    pb_write_fixed32_field(&w, IOS_MP_ID, id);
    pb_write_varint_field_always(&w, IOS_MP_WANT_ACK, 1);
    pb_write_varint_field_always(&w, IOS_MP_PRIORITY, IOS_PRIORITY_RELIABLE);
    size_t packet_len = pb_writer_len(&w);

    pb_writer_init(&w, buf, cap);
    pb_write_submessage(&w, IOS_TR_PACKET, packet, packet_len);
    return pb_writer_ok(&w) ? pb_writer_len(&w) : 0;
}

static inline uint32_t ios_next_packet_id(IosClient* c) {
    /* UInt32.random(in: 255..<UInt32.max). Deterministic here. */
    return 0x40000000u + (uint32_t)c->to_radio_writes * 7919u + 255u;
}

static inline void ios_send_admin_bool(IosClient* c, uint32_t field, bool want_response) {
    uint8_t admin[8];
    uint8_t buf[192];
    PbWriter w;
    pb_writer_init(&w, admin, sizeof(admin));
    pb_write_varint_field_always(&w, field, 1);
    size_t n = ios_build_admin(
        buf,
        sizeof(buf),
        c->my_node_num,
        c->my_node_num,
        ios_next_packet_id(c),
        admin,
        pb_writer_len(&w),
        want_response);
    ios_device_write(c, buf, n);
}

/* saveTimeZone: set_config { device { tzdef } }, no want_response. */
static inline void ios_send_set_tzdef(IosClient* c) {
    uint8_t device[64];
    uint8_t config[72];
    uint8_t admin[80];
    uint8_t buf[192];
    PbWriter w;
    pb_writer_init(&w, device, sizeof(device));
    pb_write_string_field(&w, IOS_DEVICECONFIG_TZDEF, "EST5EDT,M3.2.0,M11.1.0");
    size_t dl = pb_writer_len(&w);
    pb_writer_init(&w, config, sizeof(config));
    pb_write_submessage(&w, IOS_CONFIG_DEVICE, device, dl);
    size_t cl = pb_writer_len(&w);
    pb_writer_init(&w, admin, sizeof(admin));
    pb_write_submessage(&w, IOS_ADMIN_SET_CONFIG, config, cl);
    size_t n = ios_build_admin(
        buf,
        sizeof(buf),
        c->my_node_num,
        c->my_node_num,
        ios_next_packet_id(c),
        admin,
        pb_writer_len(&w),
        false);
    c->sent_set_config_tzdef++;
    ios_device_write(c, buf, n);
}

/* sendTime: set_time_only, no want_response. */
static inline void ios_send_time(IosClient* c) {
    uint8_t admin[8];
    uint8_t buf[192];
    PbWriter w;
    pb_writer_init(&w, admin, sizeof(admin));
    pb_write_varint_field_always(&w, IOS_ADMIN_SET_TIME, 1758600000u);
    size_t n = ios_build_admin(
        buf,
        sizeof(buf),
        c->my_node_num,
        c->my_node_num,
        ios_next_packet_id(c),
        admin,
        pb_writer_len(&w),
        false);
    c->sent_set_time++;
    ios_device_write(c, buf, n);
}

static inline void ios_send_heartbeat(IosClient* c) {
    uint8_t buf[16];
    size_t n = ios_build_heartbeat(buf, sizeof(buf), 0x1234567u + (uint32_t)c->sent_heartbeats);
    c->sent_heartbeats++;
    ios_device_write(c, buf, n);
}

/* FromRadio handlers, AccessoryManager+FromRadio.swift */

static inline void ios_handle_my_info(IosClient* c, const uint8_t* m, size_t n) {
    uint64_t num = 0;
    if(!ios_wire_valid(m, n)) {
        c->decode_failures++;
        return;
    }
    ios_find(m, n, 1, &num, NULL, NULL);
    c->have_my_info = true;
    c->my_node_num = (uint32_t)num;
}

static inline void ios_handle_node_info(IosClient* c, const uint8_t* m, size_t n) {
    uint64_t num = 0;
    const uint8_t* user = NULL;
    size_t user_len = 0;
    if(!ios_wire_valid(m, n)) {
        c->decode_failures++;
        return;
    }
    /* firstDatabaseNodeInfoContinuation resumes before the num guard. */
    if(c->in_database_stage) c->db_first_node_seen = true;

    ios_find(m, n, 1, &num, NULL, NULL);
    if(num == 0) {
        c->nodeinfo_zero_num++;
        ios_log(c, "handleNodeInfo: NodeInfo packet with a zero nodeNum");
        return;
    }
    if(ios_find(m, n, 2, NULL, &user, &user_len)) {
        const uint8_t* s = NULL;
        size_t sl = 0;
        if(!ios_wire_valid(user, user_len)) {
            c->decode_failures++;
            return;
        }
        for(uint32_t f = 1; f <= 3; f++) {
            if(ios_find(user, user_len, f, NULL, &s, &sl) && !ios_utf8_valid(s, sl)) {
                c->invalid_utf8++;
            }
        }
        if(c->have_my_info && (uint32_t)num == c->my_node_num) c->have_long_name = true;
    }
    if(c->in_database_stage) {
        c->node_count++;
        bool seen = false;
        for(int i = 0; i < c->db_distinct_nodes; i++) {
            if(c->db_node_nums[i] == (uint32_t)num) seen = true;
        }
        if(!seen && c->db_distinct_nodes < IOS_DB_NODES_MAX) {
            c->db_node_nums[c->db_distinct_nodes++] = (uint32_t)num;
        }
    }
}

static inline void ios_handle_metadata(IosClient* c, const uint8_t* m, size_t n) {
    const uint8_t* s = NULL;
    size_t sl = 0;
    if(!ios_wire_valid(m, n)) {
        c->decode_failures++;
        return;
    }
    if(!c->have_my_info) {
        /* guard let device = activeConnection?.device, let deviceNum = device.num */
        c->metadata_dropped_no_device++;
        ios_log(c, "handleDeviceMetadata: dropped, no device num yet (metadata before my_info)");
        return;
    }
    if(ios_find(m, n, 1, NULL, &s, &sl)) {
        if(!ios_utf8_valid(s, sl)) c->invalid_utf8++;
        if(sl >= IOS_VERSION_MAX) sl = IOS_VERSION_MAX - 1;
        memcpy(c->firmware_version, s, sl);
        c->firmware_version[sl] = 0;
    } else {
        c->firmware_version[0] = 0;
    }
    c->have_firmware_version = true;
}

static inline void ios_handle_channel(IosClient* c, const uint8_t* m, size_t n) {
    if(!ios_wire_valid(m, n)) {
        c->decode_failures++;
        return;
    }
    if(!c->have_my_info) c->channel_dropped_no_device++;
}

static inline void ios_handle_config(IosClient* c, const uint8_t* m, size_t n) {
    IosReader r = {m, n, 0};
    uint32_t field;
    uint8_t wire;
    uint64_t val;
    const uint8_t* ptr = NULL;
    size_t plen = 0;
    bool ok;
    uint32_t variant = 0;
    if(!ios_wire_valid(m, n)) {
        c->decode_failures++;
        return;
    }
    /* guard let device, deviceNum, longName */
    if(!c->have_my_info || !c->have_long_name) {
        c->config_dropped_no_name++;
        ios_log(c, "handleConfig: dropped, device has no longName yet");
        return;
    }
    while(ios_next_field(&r, &field, &wire, &val, &ptr, &plen, &ok)) {
        if(wire == 2) variant = field;
    }
    if(variant == IOS_CONFIG_DEVICE) {
        const uint8_t* tz = NULL;
        size_t tzl = 0;
        bool has_tz = ios_find(ptr, plen, IOS_DEVICECONFIG_TZDEF, NULL, &tz, &tzl) && tzl > 0;
        if(!has_tz) {
            ios_log(c, "handleConfig: device.tzdef empty, sending set_config");
            ios_send_set_tzdef(c);
        }
    }
}

static inline void ios_handle_module_config(IosClient* c, const uint8_t* m, size_t n) {
    IosReader r = {m, n, 0};
    uint32_t field;
    uint8_t wire;
    uint64_t val;
    const uint8_t* ptr;
    size_t plen;
    bool ok;
    uint32_t variant = 0;
    if(!ios_wire_valid(m, n)) {
        c->decode_failures++;
        return;
    }
    if(!c->have_my_info || !c->have_long_name) {
        c->moduleconfig_dropped_no_name++;
        return;
    }
    while(ios_next_field(&r, &field, &wire, &val, &ptr, &plen, &ok)) {
        if(wire == 2) variant = field;
    }
    if(variant == IOS_MODULE_CANNED_MESSAGE) {
        c->sent_get_canned++;
        ios_send_admin_bool(c, IOS_ADMIN_GET_CANNED, true);
    }
    if(variant == IOS_MODULE_EXT_NOTIFICATION) {
        c->sent_get_ringtone++;
        ios_send_admin_bool(c, IOS_ADMIN_GET_RINGTONE, true);
    }
}

static inline void ios_handle_packet(IosClient* c, const uint8_t* m, size_t n) {
    IosPacket* p = &c->last_packet;
    uint64_t v;
    const uint8_t* data = NULL;
    size_t data_len = 0;
    if(!ios_wire_valid(m, n)) {
        c->decode_failures++;
        return;
    }
    memset(p, 0, sizeof(*p));
    if(ios_find(m, n, IOS_MP_FROM, &v, NULL, NULL)) p->from = (uint32_t)v;
    if(ios_find(m, n, IOS_MP_TO, &v, NULL, NULL)) p->to = (uint32_t)v;
    if(ios_find(m, n, IOS_MP_ID, &v, NULL, NULL)) p->id = (uint32_t)v;
    if(ios_find(m, n, IOS_MP_RX_SNR, &v, NULL, NULL)) {
        uint32_t bits = (uint32_t)v;
        memcpy(&p->rx_snr, &bits, sizeof(bits));
        p->has_rx_snr = true;
    }
    if(ios_find(m, n, IOS_MP_RX_RSSI, &v, NULL, NULL)) {
        p->rx_rssi = (int32_t)(int64_t)v;
        p->has_rx_rssi = true;
    }
    if(ios_find(m, n, IOS_MP_HOP_LIMIT, &v, NULL, NULL)) {
        p->hop_limit = (uint32_t)v;
        p->has_hop_limit = true;
    }
    if(ios_find(m, n, IOS_MP_HOP_START, &v, NULL, NULL)) {
        p->hop_start = (uint32_t)v;
        p->has_hop_start = true;
    }
    if(ios_find(m, n, IOS_MP_DECODED, NULL, &data, &data_len)) {
        const uint8_t* payload = NULL;
        size_t payload_len = 0;
        if(!ios_wire_valid(data, data_len)) {
            c->decode_failures++;
            return;
        }
        p->decoded = true;
        if(ios_find(data, data_len, IOS_DATA_PORTNUM, &v, NULL, NULL)) p->portnum = (uint32_t)v;
        if(ios_find(data, data_len, IOS_DATA_REQ_ID, &v, NULL, NULL)) p->request_id = (uint32_t)v;
        if(ios_find(data, data_len, IOS_DATA_PAYLOAD, NULL, &payload, &payload_len)) {
            if(payload_len > sizeof(p->payload)) payload_len = sizeof(p->payload);
            memcpy(p->payload, payload, payload_len);
            p->payload_len = payload_len;
        }
        if(p->portnum == IOS_PORTNUM_ADMIN) c->admin_responses++;
        if(p->portnum == 5) c->routing_acks++;
    }
    c->packet_frames++;
}

/* BLEConnection.drainPendingPackets body plus processFromRadio. */
static inline void ios_process_frame(IosClient* c, const uint8_t* buf, size_t len) {
    IosReader r = {buf, len, 0};
    uint32_t field;
    uint8_t wire;
    uint64_t val = 0;
    const uint8_t* ptr = NULL;
    size_t plen = 0;
    bool ok;
    uint32_t variant = 0;
    uint64_t scalar = 0;
    const uint8_t* body = NULL;
    size_t body_len = 0;
    int payload_fields = 0;

    c->frames_total++;
    if(c->stage >= 0 && c->stage <= 3) c->frames_stage[c->stage]++;
    if(len > c->max_frame_len) c->max_frame_len = len;

    while(ios_next_field(&r, &field, &wire, &val, &ptr, &plen, &ok)) {
        if(field == IOS_FR_ID) continue;
        if(field > IOS_FR_MAX) {
            /* Unknown fields are kept by SwiftProtobuf, not an error, but the
             * payloadVariant stays .none and processFromRadio logs it. */
            continue;
        }
        payload_fields++;
        variant = field;
        scalar = val;
        body = ptr;
        body_len = plen;
    }
    if(!ok) {
        c->decode_failures++;
        ios_log(
            c, "FromRadioDecoder: .failed on a %u byte frame, client disconnects", (unsigned)len);
        ios_fail(c, c->stage == 2 ? 5 : 3, "FromRadio decode failed, BLEConnection disconnects");
        return;
    }
    if(payload_fields > 1) c->oneof_multiple++;
    if(c->variant_trace_len < IOS_VARIANT_TRACE)
        c->variant_trace[c->variant_trace_len++] = (uint8_t)variant;
    if(variant <= IOS_FR_MAX) c->variant_count[variant]++;

    switch(variant) {
    case IOS_FR_MY_INFO:
        ios_handle_my_info(c, body, body_len);
        break;
    case IOS_FR_NODE_INFO:
        ios_handle_node_info(c, body, body_len);
        break;
    case IOS_FR_METADATA:
        ios_handle_metadata(c, body, body_len);
        break;
    case IOS_FR_CHANNEL:
        ios_handle_channel(c, body, body_len);
        break;
    case IOS_FR_CONFIG:
        ios_handle_config(c, body, body_len);
        break;
    case IOS_FR_MODULECONFIG:
        ios_handle_module_config(c, body, body_len);
        break;
    case IOS_FR_PACKET:
        ios_handle_packet(c, body, body_len);
        break;
    case IOS_FR_QUEUE_STATUS:
        c->queue_status_frames++;
        break;
    case IOS_FR_DEVICEUI:
    case IOS_FR_FILE_INFO:
    case IOS_FR_LOG_RECORD:
    case IOS_FR_REBOOTED:
        break;
    case IOS_FR_CONFIG_COMPLETE:
        if(scalar == IOS_NONCE_CONFIG) {
            if(c->refresh_active) {
                c->refresh_active = false;
                c->stage_finished_ms[1] = c->now_ms;
            } else {
                c->config_complete_ignored++;
            }
        } else if(scalar == IOS_NONCE_DB) {
            c->db_complete_count++;
            c->db_gate_open = true;
            c->db_first_node_seen = true;
            if(c->stage_finished_ms[2] == 0) c->stage_finished_ms[2] = c->now_ms;
        } else {
            c->config_complete_unknown++;
        }
        break;
    default:
        c->unknown_variants++;
        ios_log(c, "processFromRadio: Unknown FromRadio variant %u", variant);
        break;
    }
}

/* One drainPendingPackets pass: read until an empty read. In paced mode the
 * same published frame is seen on consecutive reads; those are counted. */
static inline void ios_drain(IosClient* c) {
    uint8_t buf[IOS_READ_MAX];
    uint8_t last[IOS_READ_MAX];
    size_t last_len = 0;
    bool truncated;
    c->doorbell_pending = false;
    for(;;) {
        size_t len = ios_device_read(c, buf, &truncated);
        if(len == 0) {
            c->frames_empty_reads++;
            break;
        }
        if(truncated) {
            c->oversize_frames++;
            ios_log(c, "read returned %u of a longer frame: FromRadio truncated", (unsigned)len);
        }
        if(c->transport == IosTransportPaced && len == last_len && memcmp(buf, last, len) == 0) {
            c->duplicate_reads++;
        }
        memcpy(last, buf, len);
        last_len = len;
        ios_process_frame(c, buf, len);
        if(c->outcome == IosOutcomeFailed) return;
        /* A doorbell during the drain is coalesced into needsDrain; the loop
         * here simply keeps reading, which is the same thing. */
        if(c->transport == IosTransportPaced && c->doorbell_pending &&
           (int32_t)(c->doorbell_at_ms - c->now_ms) <= 0) {
            c->doorbell_pending = false;
        }
    }
}

/* Drain, then keep draining on doorbells until done() or the step budget is
 * spent. done() is checked after every pass. */
typedef bool (*IosCondition)(const IosClient* c);

static inline bool
    ios_wait(IosClient* c, IosCondition done, uint32_t budget_ms, int step, const char* what) {
    uint32_t start = c->now_ms;
    for(;;) {
        ios_drain(c);
        if(c->outcome == IosOutcomeFailed) return false;
        if(done(c)) return true;
        if(c->transport == IosTransportIdeal) {
            char why[IOS_LOG_LINE];
            snprintf(why, sizeof(why), "queue empty before %s", what);
            ios_fail(c, step, why);
            return false;
        }
        /* Idle until the next doorbell. Nothing else wakes the client. */
        if(c->doorbell_pending) {
            if((int32_t)(c->doorbell_at_ms - c->now_ms) > 0) c->now_ms = c->doorbell_at_ms;
        } else if(c->drain_active) {
            /* The device is still publishing but has already rung for this
             * batch. Advance to the next publish and read again: the
             * FromNum value changes on every publish only when a doorbell
             * is rung, so this models the phone polling on its own timer
             * would be wrong. Instead this is a stall the step timeout ends. */
            if(c->now_ms - start > budget_ms) {
                char why[IOS_LOG_LINE];
                snprintf(
                    why,
                    sizeof(why),
                    "timeout waiting for %s, device still publishing but no doorbell",
                    what);
                ios_fail(c, step, why);
                return false;
            }
            c->now_ms = c->drain_due_ms;
            continue;
        } else {
            char why[IOS_LOG_LINE];
            snprintf(why, sizeof(why), "timeout waiting for %s, device idle", what);
            ios_fail(c, step, why);
            return false;
        }
        if(c->now_ms - start > budget_ms) {
            char why[IOS_LOG_LINE];
            snprintf(why, sizeof(why), "timeout waiting for %s", what);
            ios_fail(c, step, why);
            return false;
        }
    }
}

static inline bool ios_cond_config_done(const IosClient* c) {
    return !c->refresh_active;
}
static inline bool ios_cond_first_db_node(const IosClient* c) {
    return c->db_first_node_seen;
}
static inline bool ios_cond_db_gate(const IosClient* c) {
    return c->db_gate_open;
}

/* Setup */

static inline void
    ios_client_init(IosClient* c, const MeshConfig* config, IosTransport transport) {
    memset(c, 0, sizeof(*c));
    handshake_init(&c->handshake, config);
    {
        const uint8_t passkey[PHONE_SESSION_PASSKEY_LEN] = {0xA5, 1, 2, 3, 4, 5, 6, 7};
        handshake_set_session_passkey(&c->handshake, passkey);
    }
    c->transport = transport;
    c->queue_depth = IOS_MODEL_QUEUE_DEPTH;
    c->to_radio_max_write = IOS_WRITE_NO_RSP_MAX;
    c->drain_interval_ms = IOS_MODEL_DRAIN_INTERVAL_MS;
    c->worker_poll_ms = IOS_MODEL_WORKER_POLL_MS;
    c->read_rtt_ms = IOS_MODEL_READ_RTT_MS;
}

/* Model the GAP link dropping, client side only. The device keeps its queue
 * and its published value until ios_device_reset is called, so a test can
 * show the device with and without its disconnect hook. The client forgets
 * everything, because AccessoryManager.connect() clears its state on every
 * attempt. */
static inline void ios_client_disconnect(IosClient* c) {
    c->refresh_active = false;
    c->have_my_info = false;
    c->my_node_num = 0;
    c->have_long_name = false;
    c->have_firmware_version = false;
    c->firmware_version[0] = 0;
    c->in_database_stage = false;
    c->node_count = 0;
    c->db_distinct_nodes = 0;
    c->db_first_node_seen = false;
    c->db_gate_open = false;
    c->stage = 0;
    c->outcome = IosOutcomeNotRun;
    c->failed_step = 0;
    c->failure[0] = 0;
    c->doorbell_pending = false;
    /* The handshake stage is left as it was; ios_device_reset clears it. */
}

/* Model the device's disconnect hook: the queue and the published value are
 * cleared and the handshake returns to idle. This is reset_session in
 * meshtastic_service.c, reached through meshtastic_ble_service_on_disconnect
 * from the Bt status callback. */
static inline void ios_device_reset(IosClient* c) {
    c->q_tail = 0;
    c->q_pending = 0;
    c->drain_active = false;
    c->doorbell_rung = false;
    c->doorbell_pending = false;
    c->published.len = 0;
    handshake_reset(&c->handshake);
}

/* The connect flow, AccessoryManager+Connect.swift, Steps 2 through 8. Step
 * 0 and 1 are the GAP connection and characteristic discovery, which the
 * harness has nothing to say about. Returns the outcome; the client struct
 * carries every count. */
static inline IosOutcome ios_client_connect(IosClient* c) {
    uint8_t buf[16];
    size_t n;

    c->outcome = IosOutcomeNotRun;
    c->failed_step = 0;
    c->stage = 0;

    /* Step 2: heartbeat. sendHeartbeat writes and resets a response timer
     * that only exists on transports with requiresPeriodicHeartbeat, which
     * BLE is not (BLETransport.swift:62). No reply is required. */
    ios_send_heartbeat(c);
    if(c->transport == IosTransportPaced) ios_drain(c);

    /* Step 3: wantConfig. runAutomaticConfigRefresh sends want_config_id
     * 69420 and starts draining at once, then waits for the completion the
     * configCompleteID handler delivers. */
    c->stage = 1;
    c->stage_started_ms[1] = c->now_ms;
    c->refresh_active = true;
    n = ios_build_want_config(buf, sizeof(buf), IOS_NONCE_CONFIG);
    ios_device_write(c, buf, n);
    if(!ios_wait(c, ios_cond_config_done, IOS_STEP3_TIMEOUT_MS, 3, "config_complete_id 69420")) {
        return c->outcome;
    }

    /* Step 4: heartbeat. */
    ios_send_heartbeat(c);
    if(c->transport == IosTransportPaced) ios_drain(c);

    /* Step 5: wantDatabase. State becomes .retrievingDatabase(0), then the
     * step waits for the first NodeInfo (or the DB completion). */
    c->stage = 2;
    c->stage_started_ms[2] = c->now_ms;
    c->in_database_stage = true;
    c->node_count = 0;
    c->db_distinct_nodes = 0;
    n = ios_build_want_config(buf, sizeof(buf), IOS_NONCE_DB);
    ios_device_write(c, buf, n);
    if(!ios_wait(
           c, ios_cond_first_db_node, IOS_STEP5_TIMEOUT_MS * 3, 5, "first stage two NodeInfo")) {
        return c->outcome;
    }

    /* Step 5a: wait for config_complete_id 69421. */
    if(!ios_wait(c, ios_cond_db_gate, IOS_STEP5A_TIMEOUT_MS, 5, "config_complete_id 69421")) {
        return c->outcome;
    }

    /* Step 6: version check. */
    if(!c->have_firmware_version) {
        ios_fail(c, 6, "Firmware version not available");
        return c->outcome;
    }
    if(strchr(c->firmware_version, '.') == NULL) {
        ios_fail(c, 6, "versionMismatch: Update Your Firmware (no dot)");
        return c->outcome;
    }
    if(!ios_version_supported(c->firmware_version)) {
        char why[IOS_LOG_LINE];
        snprintf(
            why,
            sizeof(why),
            "Update Your Firmware: \"%s\" < minimumVersion %s",
            c->firmware_version,
            IOS_MIN_FIRMWARE);
        ios_fail(c, 6, why);
        return c->outcome;
    }

    /* Step 7: sendTime, then state .subscribed. */
    c->stage = 3;
    c->in_database_stage = false;
    ios_send_time(c);
    if(c->transport == IosTransportPaced) ios_drain(c);

    /* Step 8: no periodic heartbeat on BLE. Connected. */
    c->outcome = IosOutcomeConnected;
    c->stage_finished_ms[3] = c->now_ms;
    return c->outcome;
}

/* Print what the flow saw. For the test log. */
static inline void ios_client_report(const IosClient* c) {
    static const char* names[IOS_FR_MAX + 1] = {
        "none",
        "id",
        "packet",
        "my_info",
        "node_info",
        "config",
        "log_record",
        "config_complete_id",
        "rebooted",
        "moduleConfig",
        "channel",
        "queueStatus",
        "xmodem",
        "metadata",
        "mqtt_proxy",
        "fileInfo",
        "clientNotification",
        "deviceuiConfig",
        "lockdown_status",
        "region_presets"};
    printf(
        "  outcome: %s%s",
        c->outcome == IosOutcomeConnected ? "connected" :
        c->outcome == IosOutcomeFailed    ? "FAILED" :
                                            "not run",
        c->outcome == IosOutcomeFailed ? " at Step " : "");
    if(c->outcome == IosOutcomeFailed) printf("%d: %s", c->failed_step, c->failure);
    printf("\n");
    printf(
        "  transport: %s, frames %d (stage1 %d, stage2 %d), empty reads %d, duplicates %d\n",
        c->transport == IosTransportIdeal ? "ideal" : "paced",
        c->frames_total,
        c->frames_stage[1],
        c->frames_stage[2],
        c->frames_empty_reads,
        c->duplicate_reads);
    printf(
        "  device: writes %d (over %u bytes %d), not understood %d, queue refused %d\n",
        c->to_radio_writes,
        (unsigned)c->to_radio_max_write,
        c->to_radio_oversize,
        c->to_radio_not_understood,
        c->queue_refused);
    printf(
        "  version: \"%s\" -> %s\n",
        c->have_firmware_version ? c->firmware_version : "(none)",
        c->have_firmware_version ?
            (ios_version_supported(c->firmware_version) ? "supported" : "REJECTED") :
            "n/a");
    printf(
        "  frames: max %u bytes (read cap %d), oversize %d, decode failures %d, unknown variants %d\n",
        (unsigned)c->max_frame_len,
        IOS_READ_MAX,
        c->oversize_frames,
        c->decode_failures,
        c->unknown_variants);
    printf(
        "  drops: metadata/no-device %d, channel/no-device %d, config/no-name %d, moduleConfig/no-name %d, zero-num NodeInfo %d\n",
        c->metadata_dropped_no_device,
        c->channel_dropped_no_device,
        c->config_dropped_no_name,
        c->moduleconfig_dropped_no_name,
        c->nodeinfo_zero_num);
    printf(
        "  completions: 69420 ignored %d, 69421 seen %d, unknown %d; stage2 node count %d (%d distinct)\n",
        c->config_complete_ignored,
        c->db_complete_count,
        c->config_complete_unknown,
        c->node_count,
        c->db_distinct_nodes);
    printf(
        "  client sent: heartbeats %d, get_canned %d, get_ringtone %d, set_config(tzdef) %d, set_time %d\n",
        c->sent_heartbeats,
        c->sent_get_canned,
        c->sent_get_ringtone,
        c->sent_set_config_tzdef,
        c->sent_set_time);
    printf(
        "  client got: admin responses %d, routing acks %d, queueStatus %d, packets %d\n",
        c->admin_responses,
        c->routing_acks,
        c->queue_status_frames,
        c->packet_frames);
    if(c->transport == IosTransportPaced) {
        printf(
            "  timing: stage1 %u ms, stage2 %u ms, total %u ms\n",
            c->stage_finished_ms[1] - c->stage_started_ms[1],
            c->stage_finished_ms[2] - c->stage_started_ms[2],
            c->now_ms);
    }
    printf("  variants:");
    for(int v = 1; v <= IOS_FR_MAX; v++) {
        if(c->variant_count[v]) printf(" %s=%d", names[v], c->variant_count[v]);
    }
    printf("\n");
    for(int i = 0; i < c->log_count; i++)
        printf("  log: %s\n", c->log[i]);
}

#endif
