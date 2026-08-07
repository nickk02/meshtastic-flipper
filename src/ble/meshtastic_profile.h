/* BLE profile wrapper for the Meshtastic service.
 *
 * UNVERIFIED. Never run against a phone.
 *
 * The Flipper switches BLE profiles rather than adding services to the running
 * one. An app calls bt_profile_start with a template, and calls
 * bt_profile_restore_default on the way out. Failing to restore leaves the
 * Flipper unable to pair with its own companion app, so the exit path matters
 * as much as the entry path.
 *
 * The profile advertises the Meshtastic service UUID, which is what makes the
 * Flipper appear in the phone app's scan list. */
#ifndef MESHTASTIC_PROFILE_H
#define MESHTASTIC_PROFILE_H

#include <furi.h>
#include <furi_ble/profile_interface.h>

#include "src/proto/phone_encode.h"

/* Profile template to hand to bt_profile_start. */
extern const FuriHalBleProfileTemplate* ble_profile_meshtastic;

typedef struct MeshtasticBleService MeshtasticBleService;

/* Why BLE is or is not running. Shown in the UI, because every failure here is
 * otherwise silent: the profile can start successfully and still never
 * advertise. */
typedef enum {
    MeshBleIdle,
    MeshBleAdvertising,
    MeshBleUnsupported, /* core2 not alive, or GATT/GAP unavailable */
    MeshBleProfileFailed, /* bt_profile_start returned NULL */
} MeshBleState;

MeshBleState meshtastic_ble_state(void);
const char* meshtastic_ble_state_name(MeshBleState state);

/* Bring the profile up. Returns NULL if Bluetooth is unavailable or the
 * profile fails to start, in which case the caller should carry on without
 * phone support rather than failing outright. */
MeshtasticBleService* meshtastic_ble_start(const PhoneIdentity* identity);

/* Bring it down and restore the Flipper's default profile. */
void meshtastic_ble_stop(MeshtasticBleService* service);

#endif
