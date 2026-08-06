#include "src/ble/meshtastic_service.h"

#include <ble/core/auto/ble_types.h>
#include <ble/core/ble_std.h>
#include <furi_ble/event_dispatcher.h>
#include <furi_ble/gatt.h>
#include <furi_ble/profile_interface.h>
#include <furi_hal_bt.h>
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
#define QUEUE_DEPTH       8
#define QUEUE_MESSAGE_MAX 256

typedef struct {
    uint8_t data[QUEUE_MESSAGE_MAX];
    size_t len;
} QueuedMessage;

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

    Handshake handshake;
    PhoneIdentity identity;
    GapSvcEventHandler* event_handler;

    MeshtasticBleToRadioCallback callback;
    void* callback_context;
};

static MeshtasticBleService* active_service = NULL;

/* Characteristic data callbacks. Called by the stack when a characteristic is
 * created, to learn the maximum length, and again on update. A NULL data
 * pointer means the stack only wants the length. */

static bool to_radio_data(const void* context, const uint8_t** data, uint16_t* data_len) {
    UNUSED(context);
    *data_len = QUEUE_MESSAGE_MAX;
    if(data) *data = NULL;
    return false;
}

/* Serves the next queued message, or an empty read when the queue is drained.
 * An empty read is how the phone learns to stop reading. */
static bool from_radio_data(const void* context, const uint8_t** data, uint16_t* data_len) {
    MeshtasticBleService* service = (MeshtasticBleService*)context;
    static uint8_t scratch[QUEUE_MESSAGE_MAX];

    if(service == NULL) {
        *data_len = 0;
        if(data) *data = NULL;
        return false;
    }

    if(data == NULL) {
        /* Length probe at creation time. */
        *data_len = QUEUE_MESSAGE_MAX;
        return false;
    }

    furi_mutex_acquire(service->mutex, FuriWaitForever);

    if(service->pending == 0) {
        *data_len = 0;
        *data = scratch;
    } else {
        QueuedMessage* msg = &service->queue[service->tail];
        memcpy(scratch, msg->data, msg->len);
        *data_len = (uint16_t)msg->len;
        *data = scratch;

        service->tail = (service->tail + 1) % QUEUE_DEPTH;
        service->pending--;
    }

    furi_mutex_release(service->mutex);
    return false;
}

static bool from_num_data(const void* context, const uint8_t** data, uint16_t* data_len) {
    MeshtasticBleService* service = (MeshtasticBleService*)context;
    static uint32_t counter = 0;

    /* FromNum is a doorbell. Its value only has to change to wake the phone,
     * which then reads FromRadio until empty. */
    if(service != NULL) counter = (uint32_t)service->pending;

    *data_len = sizeof(counter);
    if(data) *data = (const uint8_t*)&counter;
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
    .gatt_evt_mask = GATT_NOTIFY_READ_REQ_AND_WAIT_FOR_APPL_RESP,
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

    furi_mutex_release(service->mutex);

    /* Ring the doorbell so the phone starts reading. */
    ble_gatt_characteristic_update(service->svc_handle, &service->from_num, service);
    return true;
}

size_t meshtastic_ble_service_pending(MeshtasticBleService* service) {
    if(service == NULL) return 0;
    furi_mutex_acquire(service->mutex, FuriWaitForever);
    size_t pending = service->pending;
    furi_mutex_release(service->mutex);
    return pending;
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

/* Called when the phone writes ToRadio. Runs the handshake and queues whatever
 * it produces. */
static void handle_to_radio(MeshtasticBleService* service, const uint8_t* data, size_t len) {
    HandshakeReply reply;

    if(handshake_handle_to_radio(&service->handshake, data, len, &reply)) {
        for(size_t i = 0; i < reply.count; i++) {
            meshtastic_ble_service_queue(service, reply.messages[i].data, reply.messages[i].len);
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

    MeshHciEventPacket* packet = (MeshHciEventPacket*)(((MeshHciUartPacket*)event)->data);

    /* HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE, 0xFF. This one is in the SDK, at
     * lib/stm32wb_copro/wpan/ble/core/ble_std.h:62. */
    if(packet->evt != HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE) return BleEventNotAck;

    MeshBlecoreEvent* blecore = (MeshBlecoreEvent*)packet->data;
    aci_gatt_attribute_modified_event_rp0* modified =
        (aci_gatt_attribute_modified_event_rp0*)blecore->data;

    /* The value handle is one past the declaration handle, and the descriptor
     * is two past. The firmware's serial service does the same arithmetic at
     * serial_service.c:82-90. */
    if(modified->Attr_Handle != service->to_radio.handle + 1) return BleEventNotAck;

    handle_to_radio(service, modified->Attr_Data, modified->Attr_Data_Length);
    return BleEventAckFlowEnable;
}

MeshtasticBleService* meshtastic_ble_service_alloc(const PhoneIdentity* identity) {
    MeshtasticBleService* service = malloc(sizeof(MeshtasticBleService));
    memset(service, 0, sizeof(MeshtasticBleService));

    service->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(identity) service->identity = *identity;
    handshake_init(&service->handshake, identity);

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

    service->event_handler =
        ble_event_dispatcher_register_svc_handler(gatt_event_handler, service);

    active_service = service;
    FURI_LOG_I(TAG, "Meshtastic GATT service up, handle %u", service->svc_handle);
    return service;
}

void meshtastic_ble_service_free(MeshtasticBleService* service) {
    if(service == NULL) return;

    if(service->event_handler) ble_event_dispatcher_unregister_svc_handler(service->event_handler);

    ble_gatt_characteristic_delete(service->svc_handle, &service->to_radio);
    ble_gatt_characteristic_delete(service->svc_handle, &service->from_radio);
    ble_gatt_characteristic_delete(service->svc_handle, &service->from_num);
    ble_gatt_service_delete(service->svc_handle);

    if(active_service == service) active_service = NULL;

    furi_mutex_free(service->mutex);
    free(service);
}
