/* Meshtastic BLE GATT service.
 *
 * UNVERIFIED. Never run against a phone. The protobuf encoding behind it is
 * host-tested; this transport layer is not.
 *
 * Mirrors what ESP32 nodes expose, from src/BluetoothCommon.h:9-14 in the
 * Meshtastic firmware. Three characteristics on one service:
 *
 *   ToRadio    the phone writes a ToRadio protobuf
 *   FromRadio  the phone reads one FromRadio message per read
 *   FromNum    the device notifies. A doorbell, carrying no useful payload
 *
 * The phone reads FromRadio repeatedly until a read returns empty. That is the
 * whole flow. */
#ifndef MESHTASTIC_SERVICE_H
#define MESHTASTIC_SERVICE_H

#include <furi.h>

#include "src/proto/phone_encode.h"

typedef struct MeshtasticBleService MeshtasticBleService;

/* Called on the BLE stack's thread when the phone writes to ToRadio.
 * Keep it short. */
typedef void (*MeshtasticBleToRadioCallback)(const uint8_t* data, size_t len, void* context);

MeshtasticBleService* meshtastic_ble_service_alloc(const PhoneIdentity* identity);
void meshtastic_ble_service_free(MeshtasticBleService* service);

void meshtastic_ble_service_set_callback(
    MeshtasticBleService* service,
    MeshtasticBleToRadioCallback callback,
    void* context);

/* Queue a FromRadio message for the phone to read.
 *
 * Returns false when the queue is full. Dropping is preferable to blocking the
 * radio thread, and the phone re-reads until empty anyway. */
bool meshtastic_ble_service_queue(MeshtasticBleService* service, const uint8_t* data, size_t len);

/* Number of messages waiting. Exposed for the debug view. */
size_t meshtastic_ble_service_pending(MeshtasticBleService* service);

/* What the BLE side has actually done.
 *
 * Every step of this protocol is invisible from outside the device: a phone
 * that says "communicating" tells us nothing about which messages arrived or
 * what we answered. These counters are the difference between diagnosing and
 * guessing. */
typedef struct {
    uint32_t writes; /* ToRadio writes received */
    uint32_t last_nonce; /* want_config_id from the most recent write */
    uint32_t queued; /* FromRadio messages queued, cumulative */
    uint32_t drained; /* messages the drain step has retired */
    uint32_t pending; /* messages waiting now */
    uint8_t stage; /* HandshakeStage */

    /* Attempted publishes, and the refusals split by characteristic. They are
     * separate because they mean different things: a FromRadio failure means
     * the phone gets no data at all, a FromNum failure costs only the doorbell,
     * and the phone polls regardless. */
    uint32_t publishes;
    uint32_t fail_radio;
    uint32_t fail_num;

    /* Every GATT event reaching our handler, and every vendor event among
     * them. If events is 0 the dispatcher is not calling us at all, which is a
     * different problem from being called and rejecting the event. */
    uint32_t events;
    uint32_t vendor_events;
    uint16_t last_attr_handle; /* handle from the last attribute-modified event */
    uint16_t to_radio_handle; /* the handle we compare against */

    /* Handles the stack assigned to the two outbound characteristics. Zero
     * means aci_gatt_add_char never succeeded for it, so the phone cannot see
     * that characteristic at all and every update against it will fail. That is
     * a completely different fault from a characteristic that exists and
     * rejects writes, and the two are indistinguishable without this. */
    uint16_t from_radio_handle;
    uint16_t from_num_handle;
} MeshBleStats;

void meshtastic_ble_service_stats(MeshtasticBleService* service, MeshBleStats* out);

/* True once a phone has completed both handshake stages. */
bool meshtastic_ble_service_is_connected(MeshtasticBleService* service);

#endif
