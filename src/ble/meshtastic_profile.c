#include "src/ble/meshtastic_profile.h"

#include <bt/bt_service/bt.h>
#include <furi_ble/event_dispatcher.h>
#include <furi_ble/gatt.h>
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

    /* The app shows whatever the device advertises, so the name should say
     * what this actually is. */
    snprintf(config->adv_name, sizeof(config->adv_name), "Flipper Mesh");

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

    Bt* bt = furi_record_open(RECORD_BT);
    FuriHalBleProfileBase* base = bt_profile_start(bt, ble_profile_meshtastic, NULL);

    if(base == NULL) {
        FURI_LOG_W(TAG, "could not start the Meshtastic BLE profile");
        furi_record_close(RECORD_BT);
        return NULL;
    }

    MeshtasticProfile* profile = (MeshtasticProfile*)base;
    furi_record_close(RECORD_BT);
    FURI_LOG_I(TAG, "advertising as Flipper Mesh");
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
}
