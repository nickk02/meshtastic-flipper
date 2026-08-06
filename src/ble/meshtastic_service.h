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

/* True once a phone has completed both handshake stages. */
bool meshtastic_ble_service_is_connected(MeshtasticBleService* service);

#endif
