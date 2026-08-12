#include "src/ble/meshtastic_service.h"

#include <ble/core/auto/ble_types.h>
#include <ble/core/ble_std.h>
#include <furi_ble/event_dispatcher.h>
#include <furi_ble/gatt.h>
#include <furi_ble/profile_interface.h>
#include <furi_hal_bt.h>
#include <furi_hal_random.h>
#include <furi_hal_version.h>
#include <string.h>

#include "src/ble/meshtastic_handshake.h"

#define TAG "MeshBLE"

/* Service and characteristic UUIDs, from the Meshtastic firmware,
 * src/BluetoothCommon.h:9-14. The phone app scans for the service UUID, so
 * these must match exactly or the Flipper never appears in its list.
 *
 * BLE carries 128 bit UUIDs least significant byte first, so each array below
 * is the textual UUID reversed. */
#define UUID_REVERSED_MESH_SERVICE \
    {0xfd, 0xea, 0x73, 0xe2, 0xca, 0x5d, 0xa8, 0x9f, 0x1f, 0x46, 0xa8, 0x15, 0x18, 0xb2, 0xa1, 0x6b}
#define UUID_REVERSED_TORADIO \
    {0xe7, 0x01, 0x44, 0x12, 0x66, 0x78, 0xdd, 0xa1, 0xad, 0x4d, 0x9e, 0x12, 0xd2, 0x76, 0x5c, 0xf7}
#define UUID_REVERSED_FROMRADIO \
    {0x02, 0x00, 0x12, 0xac, 0x42, 0x02, 0x78, 0xb8, 0xed, 0x11, 0x93, 0x49, 0x9e, 0xe6, 0x55, 0x2c}
#define UUID_REVERSED_FROMNUM \
    {0x53, 0x44, 0xe3, 0x47, 0x75, 0xaa, 0x70, 0xa6, 0x66, 0x4f, 0x00, 0xa8, 0x8c, 0xa1, 0x9d, 0xed}

/* Queue depth. The phone drains this by reading until empty, so a few slots
 * absorb a burst without blocking the radio thread. Dropping beats blocking:
 * a missed frame costs one message, a blocked radio thread costs every
 * subsequent one. */
/* Deep enough to hold a whole stage one reply. The handshake queues the entire
 * sequence at once, and a queue that drops the tail sends the client a
 * truncated sequence it is documented to assume the shape of. */
#define QUEUE_DEPTH 40

/* Declared value length for FromRadio, and the largest message the queue will
 * accept. The callback's length probe becomes Char_Value_Length in
 * aci_gatt_add_char, per the comment in furi_ble/gatt.h, and the stack carves
 * that out of a fixed ATT value array shared with every other service on the
 * device. Asking for more than we use is not free.
 *
 * 192 is HANDSHAKE_MAX_MESSAGE. Nothing we send can exceed it, and the queue
 * rejects anything larger so we never hold a message we cannot publish. */
#define QUEUE_MESSAGE_MAX 192

/* Declared value length for ToRadio. The phone's handshake writes are a few
 * bytes; this is headroom, not a target. */
#define TO_RADIO_VALUE_MAX 128

/* If a handshake reply could ever exceed the declared FromRadio length, the
 * queue would silently refuse it and the phone would wait forever for a message
 * that was built correctly and then dropped. Catch that at compile time. */
_Static_assert(
    QUEUE_MESSAGE_MAX >= HANDSHAKE_MAX_MESSAGE,
    "FromRadio value length must cover the largest handshake reply");

typedef struct {
    uint8_t data[QUEUE_MESSAGE_MAX];
    size_t len;
} QueuedMessage;

/* A ToRadio write, copied out of the BLE callback so the stack thread can
 * return immediately. */
typedef struct {
    uint8_t data[QUEUE_MESSAGE_MAX];
    size_t len;
} PendingWrite;

#define WRITE_QUEUE_DEPTH 4

struct MeshtasticBleService {
    FuriHalBleProfileBase base;

    uint16_t svc_handle;
    BleGattCharacteristicInstance to_radio;
    BleGattCharacteristicInstance from_radio;
    BleGattCharacteristicInstance from_num;

    FuriMutex* mutex;
    QueuedMessage queue[QUEUE_DEPTH];
    size_t head;
    size_t tail;
    size_t pending;

    /* Pacing for the queue walk, since reads are invisible to us. Both fields
     * belong to the worker thread. There is deliberately no FuriTimer here: see
     * drain_step. */
    bool drain_active;
    uint32_t drain_due_tick;

    /* Whether the doorbell has been rung for the batch being drained. One
     * notification per batch is what the protocol wants: the client drains
     * until empty on its own after that. */
    bool doorbell_rung;

    uint32_t stat_writes;
    uint32_t stat_last_nonce;
    uint32_t stat_queued;
    uint32_t stat_drained;
    uint32_t stat_publishes;
    uint32_t stat_doorbells;
    /* Field number and length of the most recent ToRadio write. A write count
     * alone cannot say what the phone asked for, which left the difference
     * between a heartbeat and an unparsed want_config invisible. */
    uint8_t stat_last_write_field;
    uint8_t stat_last_write_len;
    uint32_t stat_worker_ticks;
    uint32_t stat_fail_radio;
    uint32_t stat_fail_num;
    uint32_t stat_events;
    uint32_t stat_vendor_events;
    uint16_t stat_last_attr_handle;

    /* ToRadio writes arrive on the BLE stack's thread. They are copied here
     * and handled on a thread of our own.
     *
     * The Meshtastic firmware warns about exactly this: "CAUTION: This
     * callback runs in the NimBLE task!!! Don't do anything except communicate
     * with the main task's runOnce." Calling ble_gatt_characteristic_update
     * from inside a GATT event handler re-enters the stack from its own
     * callback, which locks the device up.
     */
    FuriMessageQueue* write_queue;
    FuriThread* worker;
    volatile bool worker_running;

    /* Staging for one inbound write. Owned here for the same reason as reply
     * below: the event handler runs on the BLE stack's thread and this struct
     * is 260 bytes, too much to put on a stack we do not size. */
    PendingWrite inbound;

    Handshake handshake;
    MeshConfig config;

    /* Owned here rather than declared in the event handler. That handler runs
     * on the BLE stack's thread, whose stack we neither size nor control, and
     * HandshakeReply is 408 bytes. A local that large on a foreign thread is
     * how BLE fails silently or faults. */
    HandshakeReply reply;

    /* The buffer a FromRadio read is served from. It was a function-static,
     * which is shared by every service instance and outlives the app. One per
     * service is both smaller in intent and safe to free. */
    uint8_t read_scratch[QUEUE_MESSAGE_MAX];
    uint32_t from_num_value;
    GapSvcEventHandler* event_handler;

    /* Last stats read, kept so the draw callback has something to show when
     * the lock is busy. Written only under the mutex. */
    MeshBleStats snapshot;

    MeshtasticBleToRadioCallback callback;
    void* callback_context;
};

static MeshtasticBleService* active_service = NULL;

/* Characteristic data callbacks. Called by the stack when a characteristic is
 * created, to learn the maximum length, and again on update. A NULL data
 * pointer means the stack only wants the length. */

static bool to_radio_data(const void* context, const uint8_t** data, uint16_t* data_len) {
    UNUSED(context);
    *data_len = TO_RADIO_VALUE_MAX;
    if(data) *data = NULL;
    return false;
}

/* Supplies whatever ble_gatt_characteristic_update should store as the
 * characteristic's value.
 *
 * This is NOT a per-read callback. The Flipper exports no aci_gatt_* function
 * to applications, including aci_gatt_allow_read, so an app cannot answer a
 * read request individually. The phone's reads are served by the stack from
 * the last value we stored.
 *
 * The Meshtastic protocol wants read-until-empty: each read returns the next
 * packet. We cannot see reads, so instead a timer walks the queue and restates
 * the value, notifying FromNum each time. Repeats are possible if the phone
 * reads faster than the timer advances. The client guards against duplicate
 * config_complete signals (MeshConfigFlowManagerImpl.kt:75), which is the
 * reason this is worth trying at all. */
static bool from_radio_data(const void* context, const uint8_t** data, uint16_t* data_len) {
    MeshtasticBleService* service = (MeshtasticBleService*)context;

    /* The length probe is answered first, and must not consult the context.
     *
     * ble_gatt_characteristic_init calls this with data == NULL to learn the
     * value length, and passes the params' stored context, which is NULL here
     * because these params are const and shared. Checking the context first
     * meant the probe took the NULL branch and reported 0. That 0 became
     * Char_Value_Length in aci_gatt_add_char, so FromRadio was registered as a
     * characteristic that can hold nothing at all.
     *
     * The characteristic still appeared, with a valid handle, and the phone
     * could still read it. It just always read empty, and every update carrying
     * real data was refused. */
    if(data == NULL) {
        *data_len = QUEUE_MESSAGE_MAX;
        return false;
    }

    if(service == NULL) {
        *data_len = 0;
        *data = NULL;
        return false;
    }

    furi_mutex_acquire(service->mutex, FuriWaitForever);

    /* The head is restated, not consumed. Advancing happens in the timer, so
     * the value stays readable for as long as the phone might read it. */
    if(service->pending == 0) {
        *data_len = 0;
        *data = service->read_scratch;
    } else {
        QueuedMessage* msg = &service->queue[service->tail];
        memcpy(service->read_scratch, msg->data, msg->len);
        *data_len = (uint16_t)msg->len;
        *data = service->read_scratch;
    }

    furi_mutex_release(service->mutex);
    return false;
}

static bool from_num_data(const void* context, const uint8_t** data, uint16_t* data_len) {
    MeshtasticBleService* service = (MeshtasticBleService*)context;

    /* FromNum is a doorbell. Its value only has to change to wake the phone,
     * which then reads FromRadio until empty.
     *
     * The length is answered without consulting the context, so the probe at
     * creation time gets the right number. This one was already correct, but
     * only because sizeof does not evaluate its operand, so the NULL context
     * was harmless. The probe branch is now written out rather than left to
     * that detail, which is what FromRadio got wrong. */
    *data_len = sizeof(service->from_num_value);

    if(data == NULL) return false;

    if(service == NULL) {
        *data = NULL;
        return false;
    }

    service->from_num_value = (uint32_t)service->pending;
    *data = (const uint8_t*)&service->from_num_value;
    return false;
}

static const BleGattCharacteristicParams to_radio_params = {
    .name = "ToRadio",
    .data_prop_type = FlipperGattCharacteristicDataCallback,
    .data.callback.fn = to_radio_data,
    .data.callback.context = NULL,
    .uuid.Char_UUID_128 = UUID_REVERSED_TORADIO,
    .uuid_type = UUID_TYPE_128,
    .char_properties = CHAR_PROP_WRITE | CHAR_PROP_WRITE_WITHOUT_RESP,
    .security_permissions = ATTR_PERMISSION_NONE,
    .gatt_evt_mask = GATT_NOTIFY_ATTRIBUTE_WRITE,
    .is_variable = CHAR_VALUE_LEN_VARIABLE,
};

static const BleGattCharacteristicParams from_radio_params = {
    .name = "FromRadio",
    .data_prop_type = FlipperGattCharacteristicDataCallback,
    .data.callback.fn = from_radio_data,
    .data.callback.context = NULL,
    .uuid.Char_UUID_128 = UUID_REVERSED_FROMRADIO,
    .uuid_type = UUID_TYPE_128,
    .char_properties = CHAR_PROP_READ,
    .security_permissions = ATTR_PERMISSION_NONE,
    .gatt_evt_mask = GATT_DONT_NOTIFY_EVENTS,
    .is_variable = CHAR_VALUE_LEN_VARIABLE,
};

static const BleGattCharacteristicParams from_num_params = {
    .name = "FromNum",
    .data_prop_type = FlipperGattCharacteristicDataCallback,
    .data.callback.fn = from_num_data,
    .data.callback.context = NULL,
    .uuid.Char_UUID_128 = UUID_REVERSED_FROMNUM,
    .uuid_type = UUID_TYPE_128,
    .char_properties = CHAR_PROP_READ | CHAR_PROP_NOTIFY,
    .security_permissions = ATTR_PERMISSION_NONE,
    .gatt_evt_mask = GATT_DONT_NOTIFY_EVENTS,
    .is_variable = CHAR_VALUE_LEN_CONSTANT,
};

/* Publishes the current head as the FromRadio value and rings the doorbell.
 *
 * Note the sense of the test. ble_gatt_characteristic_update ends with
 *
 *     return result != BLE_STATUS_SUCCESS;
 *
 * so a true return means the update FAILED. A bool named nothing in particular,
 * returned from a function called "update", reads as success, and I wrote these
 * counters that way. They shipped inverted and reported every success as a
 * failure. Do not simplify this to !update(...).
 *
 * ble_gatt_service_add, in the same file, returns the opposite:
 * "return result == BLE_STATUS_SUCCESS;". The two cannot be assumed to match.
 * Source: targets/f7/ble_glue/furi_ble/gatt.c.
 *
 * The two failures are counted separately because they mean different things.
 * FromRadio failing costs the phone all of its data. FromNum failing costs only
 * the doorbell, and the phone polls regardless. */
static void publish_head(MeshtasticBleService* service, bool ring_doorbell) {
    if(ble_gatt_characteristic_update(service->svc_handle, &service->from_radio, service)) {
        service->stat_fail_radio++;
    }

    /* The doorbell is rung once per batch, not once per message.
     *
     * KableMeshtasticRadioProfile.kt reacts to a FromNum notification by
     * reading FromRadio until empty, so one notification already causes the
     * client to collect everything. Ringing on every drain step made a
     * 28 message stage one fire 28 notifications in under a second, and each
     * one started another full read loop. That is a notification storm against
     * the stack rather than a protocol requirement. */
    if(ring_doorbell) {
        if(ble_gatt_characteristic_update(service->svc_handle, &service->from_num, service)) {
            service->stat_fail_num++;
        }
        service->stat_doorbells++;
    }

    service->stat_publishes++;
}

/* Publishes the head, then advances past it so the next step carries the next
 * message. This is the part that would be a read callback on any platform that
 * exposes one.
 *
 * Runs on our own worker thread, and it has to. This was a FuriTimer callback,
 * which was wrong twice over. Timer callbacks run on the shared FreeRTOS timer
 * service task, so acquiring a mutex with FuriWaitForever there stalls every
 * timer in the system, and calling into the BLE stack there deadlocks against
 * the stack's own timers. That froze the device. It also meant the blocking
 * update never returned, which is why the queue never advanced.
 *
 * The order matters: publish before advancing. Advancing first would skip the
 * first message. */
static void drain_step(MeshtasticBleService* service) {
    bool had_message;
    bool ring;

    furi_mutex_acquire(service->mutex, FuriWaitForever);
    ring = !service->doorbell_rung && service->pending > 0;
    if(ring) service->doorbell_rung = true;
    furi_mutex_release(service->mutex);

    publish_head(service, ring);

    furi_mutex_acquire(service->mutex, FuriWaitForever);
    had_message = service->pending > 0;
    if(had_message) {
        service->tail = (service->tail + 1) % QUEUE_DEPTH;
        service->pending--;
        service->stat_drained++;
    }
    /* Run one step past empty so the drained queue is published as a
     * zero-length read. That empty read is how the phone learns to stop. */
    service->drain_active = had_message || service->pending > 0;
    if(!service->drain_active) {
        FURI_LOG_I(
            TAG,
            "batch drained, %lu sent, %lu refused",
            (unsigned long)service->stat_drained,
            (unsigned long)service->stat_fail_radio);
    }
    /* Armed again for the next batch once this one is fully drained. */
    if(!service->drain_active) service->doorbell_rung = false;
    furi_mutex_release(service->mutex);
}

bool meshtastic_ble_service_queue(MeshtasticBleService* service, const uint8_t* data, size_t len) {
    if(service == NULL || data == NULL) return false;
    if(len == 0 || len > QUEUE_MESSAGE_MAX) return false;

    furi_mutex_acquire(service->mutex, FuriWaitForever);

    if(service->pending >= QUEUE_DEPTH) {
        furi_mutex_release(service->mutex);
        return false;
    }

    QueuedMessage* slot = &service->queue[service->head];
    memcpy(slot->data, data, len);
    slot->len = len;
    service->head = (service->head + 1) % QUEUE_DEPTH;
    service->pending++;
    service->stat_queued++;

    /* Arm the worker and let it do every publish. Nothing on this path may
     * touch the BLE stack: queue() is reached from handle_to_radio, and this
     * function must stay callable from the radio thread too. */
    service->drain_active = true;
    service->drain_due_tick = furi_get_tick();

    furi_mutex_release(service->mutex);
    return true;
}

/* Non-blocking for the same reason as meshtastic_ble_service_stats: any UI
 * caller runs on the GUI thread. size_t reads are atomic on this target, so the
 * unlocked read is a stale value at worst, never a torn one. */
size_t meshtastic_ble_service_pending(MeshtasticBleService* service) {
    if(service == NULL) return 0;
    return service->pending;
}

bool meshtastic_ble_service_is_connected(MeshtasticBleService* service) {
    return service != NULL && handshake_is_complete(&service->handshake);
}

void meshtastic_ble_service_set_callback(
    MeshtasticBleService* service,
    MeshtasticBleToRadioCallback callback,
    void* context) {
    if(service == NULL) return;
    service->callback = callback;
    service->callback_context = context;
}

/* Hex of the first bytes of a message, for the log.
 *
 * Every failure on this project so far has been "the phone sent something and
 * the device silently did nothing with it". The bytes are the only thing that
 * distinguishes a message we ignored on purpose from one we failed to parse.
 *
 * 40 bytes rather than 16. Sixteen stopped inside the MeshPacket header, one
 * byte short of the Data payload, which is exactly where the interesting part
 * of an admin request begins. */
static void hex_prefix(const uint8_t* data, size_t len, char* out, size_t out_len) {
    static const char digits[] = "0123456789abcdef";
    size_t n = 0;

    while(n + 3 < out_len && n / 3 < len) {
        uint8_t byte = data[n / 3];
        out[n++] = digits[byte >> 4];
        out[n++] = digits[byte & 0x0F];
        out[n++] = ' ';
    }
    out[n] = 0;
}

static void handle_to_radio(MeshtasticBleService* service, const uint8_t* data, size_t len) {
    uint32_t nonce = 0;
    bool understood = false;
    char hex[3 * 40 + 1];

    hex_prefix(data, len, hex, sizeof(hex));

    if(phone_decode_want_config_id(data, len, &nonce)) {
        service->stat_last_nonce = nonce;
        understood = true;
        FURI_LOG_I(TAG, "ToRadio want_config_id nonce=%lu", (unsigned long)nonce);

        if(nonce == PHONE_NONCE_CONFIG) {
            /* A fresh stage one request is the only reliable signal that a new
             * handshake is starting, so it is also the only reliable point to
             * discard whatever is left from a previous one. Without this,
             * items a prior connection never got to (because it disconnected
             * mid drain) stay in the queue and consume capacity meant for the
             * new session, which is exactly what turned into "queue full,
             * reply 31 of 36 dropped" on real hardware. */
            furi_mutex_acquire(service->mutex, FuriWaitForever);
            service->head = 0;
            service->tail = 0;
            service->pending = 0;
            service->drain_active = false;
            service->doorbell_rung = false;
            furi_mutex_release(service->mutex);
        }
    } else if(len > 0 && (data[0] >> 3) == TORADIO_FIELD_HEARTBEAT) {
        /* The settle heartbeat between stages. The firmware does not echo it,
         * so sending nothing back is correct, and this line is here so a silent
         * device can be told apart from a deaf one. */
        understood = true;
        FURI_LOG_I(TAG, "ToRadio heartbeat, no reply expected");
    } else {
        /* Decoded independently of the handshake's own admin handling, purely
         * to log which field a real connection actually asked for. The device
         * has disconnected after a full cycle with every counter clean, so the
         * question left is not whether a request was answered but whether the
         * three requests a real phone sends (canned messages, ringtone,
         * set_config) are the same three seen here, and in what order. */
        PhoneAdminRequest admin_probe;
        if(phone_decode_admin_request(data, len, &admin_probe)) {
            FURI_LOG_I(
                TAG,
                "ToRadio admin field=%lu want_response=%d req_id=%lu",
                (unsigned long)admin_probe.admin_field,
                (int)admin_probe.want_response,
                (unsigned long)admin_probe.packet_id);
        }
    }

    /* The first tag of the ToRadio message. ToRadio is a oneof, so this is what
     * the phone actually sent: 3 is want_config_id, 7 is heartbeat, 1 is a
     * packet, 4 is disconnect. */
    if(len > 0) {
        service->stat_last_write_field = (uint8_t)(data[0] >> 3);
        service->stat_last_write_len = (uint8_t)(len > 255 ? 255 : len);
    }

    /* service->reply is guarded by the same mutex as the queue. Writes only
     * ever arrive on the BLE thread, so contention is nil, but taking the lock
     * keeps the invariant simple. */
    furi_mutex_acquire(service->mutex, FuriWaitForever);
    bool have_reply = handshake_handle_to_radio(&service->handshake, data, len, &service->reply);
    size_t count = have_reply ? service->reply.count : 0;
    furi_mutex_release(service->mutex);

    if(have_reply) understood = true;

    /* The line worth having. A ToRadio nobody recognised is the shape of every
     * stall on this project, and the counters cannot show it: they only say a
     * write arrived, not that it went unanswered. */
    if(!understood) {
        /* Say where the admin walk gave up. "Not understood" was equally true
         * of a heartbeat, a want_config and a malformed admin request, so a
         * packet arriving with portnum 6 and going unanswered looked identical
         * to one we had no business answering. */
        PhoneAdminReason why = PhoneAdminOk;
        PhoneAdminRequest probe;
        phone_decode_get_owner_request_why(data, len, &probe, &why);

        FURI_LOG_W(
            TAG,
            "ToRadio not understood: tag=%u len=%u admin=%s",
            len > 0 ? (unsigned)(data[0] >> 3) : 0,
            (unsigned)len,
            phone_admin_reason_name(why));
        FURI_LOG_W(TAG, "  bytes: %s", hex);
    }

    if(count > 0) FURI_LOG_I(TAG, "queueing %u replies", (unsigned)count);

    for(size_t i = 0; i < count; i++) {
        if(!meshtastic_ble_service_queue(
               service, service->reply.messages[i].data, service->reply.messages[i].len)) {
            /* A dropped reply truncates a sequence the client assumes the shape
             * of, so it must never pass unremarked. */
            FURI_LOG_E(TAG, "queue full, reply %u of %u dropped", (unsigned)i, (unsigned)count);
        }
    }

    if(service->callback) {
        service->callback(data, len, service->callback_context);
    }
}

/* The BLE stack hands every GATT event to every registered handler. We only
 * want writes to our ToRadio characteristic.
 *
 * The SDK does not expose the stack's packet structs to applications. Its own
 * comment in event_dispatcher.h says so: "Using other types so not to leak all
 * the BLE stack headers". The three below are copied verbatim from the
 * STM32WB BLE stack, ble_legacy.h, and must stay byte-compatible with it.
 *
 * ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE is deliberately not used. It is not
 * in the FAP SDK and I could not obtain it from a citable source, so the
 * filter here is the event type plus an exact handle match instead. That is
 * looser than the firmware's own serial service, which checks the event code
 * as well. Adding that check is worthwhile hardening once the constant can be
 * confirmed against ble_events.h. Until then a stray vendor event would have
 * to carry our exact attribute handle at the right offset to get through, and
 * anything that does still has to survive the protobuf parser. */
typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t data[1];
} MeshHciUartPacket;

typedef struct __attribute__((packed)) {
    uint8_t evt;
    uint8_t plen;
    uint8_t data[1];
} MeshHciEventPacket;

typedef struct __attribute__((packed)) {
    uint16_t ecode;
    uint8_t data[1];
} MeshBlecoreEvent;

static BleEventAckStatus gatt_event_handler(void* event, void* context) {
    MeshtasticBleService* service = context;

    if(service == NULL || event == NULL) return BleEventNotAck;

    service->stat_events++;

    MeshHciEventPacket* packet = (MeshHciEventPacket*)(((MeshHciUartPacket*)event)->data);

    /* HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE, 0xFF. This one is in the SDK, at
     * lib/stm32wb_copro/wpan/ble/core/ble_std.h:62. */
    if(packet->evt != HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE) return BleEventNotAck;

    service->stat_vendor_events++;

    MeshBlecoreEvent* blecore = (MeshBlecoreEvent*)packet->data;
    aci_gatt_attribute_modified_event_rp0* modified =
        (aci_gatt_attribute_modified_event_rp0*)blecore->data;
    service->stat_last_attr_handle = modified->Attr_Handle;

    /* The value handle is one past the declaration handle, and the descriptor
     * is two past. The firmware's serial service does the same arithmetic at
     * serial_service.c:82-90. */
    if(modified->Attr_Handle != service->to_radio.handle + 1) return BleEventNotAck;

    /* Copy and post. Nothing here may call back into the BLE stack, and
     * nothing here may be large: this is the stack thread's stack. */
    size_t len = modified->Attr_Data_Length;
    if(len > QUEUE_MESSAGE_MAX) len = QUEUE_MESSAGE_MAX;
    memcpy(service->inbound.data, modified->Attr_Data, len);
    service->inbound.len = len;

    service->stat_writes++;
    furi_message_queue_put(service->write_queue, &service->inbound, 0);

    return BleEventAckFlowEnable;
}

/* This project spent a long time chasing a "the client re-polls every
 * 200ms" claim that traced to a file that does not exist in the current
 * client. The real mechanism, confirmed by reading
 * KableMeshtasticRadioProfile.kt: FromNum notifies once, the client then
 * reads FromRadio in a tight loop with no artificial delay until a read
 * comes back empty.
 *
 * A real phone over real BLE was measured at roughly 140-150ms per message
 * end to end (write ack, notify, read, protobuf parse, persist). At the
 * previous 150ms drain step that is close to a 1:1 race: stage two is only
 * two messages, so the whole batch, including the empty marker that ends it,
 * is gone in 450ms. If the client's very first read after the doorbell lands
 * even slightly late, it gets nothing from that batch, including
 * config_complete_id, the one message that ends the stage. The device's own
 * counters still report "2 sent, 0 refused", because our queue emptied
 * correctly; the client just never saw it. This is consistent with what was
 * observed on hardware: the phone's UI reaches "Retrieving nodes" (stage two)
 * and then drops, every cycle.
 *
 * 350ms gives roughly 2x margin over the measured per-message rate rather
 * than roughly 1x, at the cost of a slower overall handshake. There is still
 * no way to detect a real read from a FAP, so this is tuned to the one
 * measurement available rather than fixed by an assumed spec value. */
#define DRAIN_INTERVAL_MS 350

/* Wake often enough to hold that interval without idle spinning. */
#define WORKER_POLL_MS 50

/* The one thread allowed to touch the BLE stack for us. It handles inbound
 * writes and paces the outbound queue. Both jobs live here so that no callback
 * context, ours or the stack's, ever calls into the stack. */
static int32_t ble_worker(void* context) {
    MeshtasticBleService* service = context;
    PendingWrite write;

    while(service->worker_running) {
        if(furi_message_queue_get(service->write_queue, &write, WORKER_POLL_MS) == FuriStatusOk) {
            if(!service->worker_running) break;
            handle_to_radio(service, write.data, write.len);
        }
        if(!service->worker_running) break;

        /* Proof of life. If the app is wedged and this is not climbing, the
         * worker is the thing that stopped. If it is climbing while nothing
         * else moves, the fault is somewhere else entirely. */
        service->stat_worker_ticks++;

        bool due;
        furi_mutex_acquire(service->mutex, FuriWaitForever);
        due = service->drain_active && (int32_t)(furi_get_tick() - service->drain_due_tick) >= 0;
        if(due) service->drain_due_tick = furi_get_tick() + furi_ms_to_ticks(DRAIN_INTERVAL_MS);
        furi_mutex_release(service->mutex);

        if(due) drain_step(service);
    }
    return 0;
}

MeshtasticBleService* meshtastic_ble_service_alloc(const MeshConfig* config) {
    MeshtasticBleService* service = malloc(sizeof(MeshtasticBleService));
    memset(service, 0, sizeof(MeshtasticBleService));

    service->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(config) service->config = *config;
    handshake_init(&service->handshake, config);

    /* The session passkey guards every state-changing admin request, so it has
     * to be unpredictable. The handshake layer has no Flipper dependencies and
     * cannot generate it, so it is seeded here. */
    uint8_t passkey[PHONE_SESSION_PASSKEY_LEN];
    furi_hal_random_fill_buf(passkey, sizeof(passkey));
    handshake_set_session_passkey(&service->handshake, passkey);

    const Service_UUID_t service_uuid = {.Service_UUID_128 = UUID_REVERSED_MESH_SERVICE};
    if(!ble_gatt_service_add(
           UUID_TYPE_128, &service_uuid, PRIMARY_SERVICE, 12, &service->svc_handle)) {
        FURI_LOG_E(TAG, "could not add the Meshtastic service");
        furi_mutex_free(service->mutex);
        free(service);
        return NULL;
    }

    /* The characteristic descriptors are const, so the per-instance context is
     * supplied through the update call rather than baked into them. */
    ble_gatt_characteristic_init(service->svc_handle, &to_radio_params, &service->to_radio);
    ble_gatt_characteristic_init(service->svc_handle, &from_radio_params, &service->from_radio);
    ble_gatt_characteristic_init(service->svc_handle, &from_num_params, &service->from_num);

    service->write_queue = furi_message_queue_alloc(WRITE_QUEUE_DEPTH, sizeof(PendingWrite));
    service->worker_running = true;
    /* This thread calls into the BLE stack and runs the handshake, so it needs
     * more than the default. 2048 was sized before it did either. */
    service->worker = furi_thread_alloc_ex("MeshBle", 4096, ble_worker, service);
    furi_thread_start(service->worker);
    service->event_handler =
        ble_event_dispatcher_register_svc_handler(gatt_event_handler, service);

    active_service = service;
    FURI_LOG_I(TAG, "Meshtastic GATT service up, handle %u", service->svc_handle);
    return service;
}

void meshtastic_ble_service_free(MeshtasticBleService* service) {
    if(service == NULL) return;

    if(service->worker) {
        service->worker_running = false;
        furi_thread_join(service->worker);
        furi_thread_free(service->worker);
    }
    if(service->write_queue) furi_message_queue_free(service->write_queue);
    if(service->event_handler) ble_event_dispatcher_unregister_svc_handler(service->event_handler);

    ble_gatt_characteristic_delete(service->svc_handle, &service->to_radio);
    ble_gatt_characteristic_delete(service->svc_handle, &service->from_radio);
    ble_gatt_characteristic_delete(service->svc_handle, &service->from_num);
    ble_gatt_service_delete(service->svc_handle);

    if(active_service == service) active_service = NULL;

    furi_mutex_free(service->mutex);
    free(service);
}

/* Never blocks, and must not.
 *
 * This is called from the ViewPort draw callback. view_port.h marks that
 * callback "@warning called from GUI thread", and the GUI thread is a shared
 * system service rather than something this app owns. Waiting on this mutex
 * there, while the BLE worker holds it and calls into the stack, freezes the
 * whole UI and not just this app. That is what locked the device up on the
 * Phone page.
 *
 * When the lock is busy the last snapshot is returned instead. One frame of
 * stale counters is the right trade against stalling the system. */
void meshtastic_ble_service_stats(MeshtasticBleService* service, MeshBleStats* out) {
    if(out == NULL) return;
    if(service == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }

    if(furi_mutex_acquire(service->mutex, 0) == FuriStatusOk) {
        MeshBleStats* snap = &service->snapshot;
        snap->writes = service->stat_writes;
        snap->last_nonce = service->stat_last_nonce;
        snap->queued = service->stat_queued;
        snap->drained = service->stat_drained;
        snap->pending = (uint32_t)service->pending;
        snap->stage = (uint8_t)handshake_stage(&service->handshake);
        snap->publishes = service->stat_publishes;
        snap->doorbells = service->stat_doorbells;
        snap->last_write_field = service->stat_last_write_field;
        snap->last_write_len = service->stat_last_write_len;
        snap->worker_ticks = service->stat_worker_ticks;
        snap->fail_radio = service->stat_fail_radio;
        snap->fail_num = service->stat_fail_num;
        snap->events = service->stat_events;
        snap->vendor_events = service->stat_vendor_events;
        snap->last_attr_handle = service->stat_last_attr_handle;
        snap->to_radio_handle = (uint16_t)(service->to_radio.handle + 1);
        snap->from_radio_handle = service->from_radio.handle;
        snap->from_num_handle = service->from_num.handle;
        furi_mutex_release(service->mutex);
    }

    *out = service->snapshot;
}
