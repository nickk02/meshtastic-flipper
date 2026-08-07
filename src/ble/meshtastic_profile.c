#include "src/ble/meshtastic_profile.h"

#include <bt/bt_service/bt.h>
#include <furi_ble/event_dispatcher.h>
#include <furi_ble/gatt.h>
#include <ble/core/ble_std.h>
#include <furi_hal_bt.h>
#include <furi_hal_version.h>
#include <string.h>

#include "src/ble/meshtastic_service.h"

#define TAG "MeshBLE"

/* Same UUID as the service, in the reversed byte order BLE uses on the wire.
 * Advertising this is what puts the Flipper in the phone app's scan list. */
static const uint8_t meshtastic_service_uuid[16] = {
    0xfd,
    0xea,
    0x73,
    0xe2,
    0xca,
    0x5d,
    0xa8,
    0x9f,
    0x1f,
    0x46,
    0xa8,
    0x15,
    0x18,
    0xb2,
    0xa1,
    0x6b,
};

typedef struct {
    FuriHalBleProfileBase base;
    MeshtasticBleService* service;
} MeshtasticProfile;

static PhoneIdentity pending_identity;
static bool pending_identity_set = false;
static MeshBleState ble_state = MeshBleIdle;

MeshBleState meshtastic_ble_state(void) {
    return ble_state;
}

const char* meshtastic_ble_state_name(MeshBleState state) {
    switch(state) {
    case MeshBleIdle:
        return "idle";
    case MeshBleAdvertising:
        return "advertising";
    case MeshBleUnsupported:
        return "unsupported";
    case MeshBleProfileFailed:
        return "failed";
    }
    return "unknown";
}

static FuriHalBleProfileBase* profile_start(FuriHalBleProfileParams params) {
    UNUSED(params);

    MeshtasticProfile* profile = malloc(sizeof(MeshtasticProfile));
    memset(profile, 0, sizeof(MeshtasticProfile));
    profile->base.config = ble_profile_meshtastic;

    profile->service =
        meshtastic_ble_service_alloc(pending_identity_set ? &pending_identity : NULL);
    if(profile->service == NULL) {
        FURI_LOG_E(TAG, "GATT service failed to start");
        free(profile);
        return NULL;
    }

    return &profile->base;
}

static void profile_stop(FuriHalBleProfileBase* base) {
    if(base == NULL) return;
    MeshtasticProfile* profile = (MeshtasticProfile*)base;
    meshtastic_ble_service_free(profile->service);
    free(profile);
}

static void profile_get_gap_config(GapConfig* config, FuriHalBleProfileParams params) {
    UNUSED(params);

    memset(config, 0, sizeof(GapConfig));

    config->adv_service.UUID_Type = UUID_TYPE_128;
    memcpy(
        config->adv_service.Service_UUID_128,
        meshtastic_service_uuid,
        sizeof(meshtastic_service_uuid));

    config->bonding_mode = false;
    config->pairing_method = GapPairingNone;

    /* Derived from the Flipper's own address so two Flippers running this do
     * not collide. The low byte is altered so the Meshtastic profile does not
     * share an address with the Flipper's default profile. */
    memcpy(config->mac_address, furi_hal_version_get_ble_mac(), sizeof(config->mac_address));
    config->mac_address[0] ^= 0x0F;

    /* Two constraints decide the advertised name, and getting either wrong
     * makes the device invisible to the phone app.
     *
     * First, adv_name[0] must be AD_TYPE_COMPLETE_LOCAL_NAME, not a
     * character. gap.c:357 skips the first byte when registering the GAP name
     * ("Skip fist symbol AD_TYPE_COMPLETE_LOCAL_NAME"), and the whole buffer
     * including that byte is handed to aci_gap_set_discoverable as the local
     * name AD structure. furi_hal_version.c:105 sets it the same way.
     *
     * Second, the packet is 31 bytes. The stack spends 3 on flags, and a
     * 128-bit service UUID costs 18 more. That leaves 10 for the name field,
     * of which 2 are its length and type bytes, so the name itself can be at
     * most 8 characters. A longer one overflows and the stack drops a field.
     * If the field it drops is the service UUID, the Meshtastic app never
     * matches its scan filter and the device simply never appears. */
    config->adv_name[0] = AD_TYPE_COMPLETE_LOCAL_NAME;
    snprintf(
        config->adv_name + 1,
        sizeof(config->adv_name) - 1,
        "Mesh%04lx",
        (unsigned long)(pending_identity.node_num & 0xFFFF));

    config->conn_param.conn_int_min = 0x18; /* 30 ms */
    config->conn_param.conn_int_max = 0x24; /* 45 ms */
    config->conn_param.slave_latency = 0;
    config->conn_param.supervisor_timeout = 0;
}

static const FuriHalBleProfileTemplate profile_template = {
    .start = profile_start,
    .stop = profile_stop,
    .get_gap_config = profile_get_gap_config,
};

const FuriHalBleProfileTemplate* ble_profile_meshtastic = &profile_template;

MeshtasticBleService* meshtastic_ble_start(const PhoneIdentity* identity) {
    if(identity != NULL) {
        pending_identity = *identity;
        pending_identity_set = true;
    }

    /* The Bt service skips this whole path when the second core is not alive
     * or GATT/GAP is unavailable, and does so without reporting anything.
     * bt.c:396. */
    if(!furi_hal_bt_is_alive() || !furi_hal_bt_is_gatt_gap_supported()) {
        FURI_LOG_E(
            TAG,
            "BLE unavailable: alive=%d gatt=%d",
            (int)furi_hal_bt_is_alive(),
            (int)furi_hal_bt_is_gatt_gap_supported());
        ble_state = MeshBleUnsupported;
        return NULL;
    }

    Bt* bt = furi_record_open(RECORD_BT);
    FuriHalBleProfileBase* base = bt_profile_start(bt, ble_profile_meshtastic, NULL);

    if(base == NULL) {
        FURI_LOG_W(TAG, "could not start the Meshtastic BLE profile");
        ble_state = MeshBleProfileFailed;
        furi_record_close(RECORD_BT);
        return NULL;
    }

    MeshtasticProfile* profile = (MeshtasticProfile*)base;
    furi_record_close(RECORD_BT);

    /* The Bt service only starts advertising when Bluetooth is enabled in
     * Flipper settings (bt.c:411). With it switched off the profile starts
     * cleanly and then sits silent, which is indistinguishable from a bug.
     * The user launched an app whose whole purpose is a BLE connection, so
     * advertise regardless and show the state on screen. */
    furi_hal_bt_start_advertising();

    ble_state = furi_hal_bt_is_active() ? MeshBleAdvertising : MeshBleProfileFailed;
    FURI_LOG_I(TAG, "BLE %s", meshtastic_ble_state_name(ble_state));
    return profile->service;
}

void meshtastic_ble_stop(MeshtasticBleService* service) {
    UNUSED(service);

    /* Restoring the default profile is not optional. Leaving the Meshtastic
     * profile running would stop the Flipper pairing with its own app until
     * the next reboot. */
    Bt* bt = furi_record_open(RECORD_BT);
    bt_profile_restore_default(bt);
    furi_record_close(RECORD_BT);
    ble_state = MeshBleIdle;
}
