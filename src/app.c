#include "src/app.h"

#include <furi_hal_version.h>
#include <string.h>

#include "src/proto/mesh_channel.h"
#include "src/model/mesh_event.h"
#include "src/proto/mesh_user.h"
#include "src/ble/meshtastic_profile.h"
#include "src/radio/source_radio.h"
#include "src/ui/app_view.h"

/* The primary channel. Meshtastic's default channel has an empty name and the
 * preset name is used for the hash, so "LongFast" is the string that produces
 * the hash real nodes put in the header. Confirmed against Channels::getHash. */
#define PRIMARY_CHANNEL_NAME "LongFast"
#define PRIMARY_PSK_INDEX    1

static void input_callback(InputEvent* event, void* context) {
    FuriMessageQueue* queue = context;
    furi_message_queue_put(queue, event, FuriWaitForever);
}

static int32_t radio_thread(void* context) {
    MeshApp* app = context;

    uint8_t key[MESH_PSK_LEN];
    uint8_t channel_hash;
    RawFrame raw;
    MeshDecoded decoded;
    MeshEvent event;

    if(!mesh_channel_expand_psk(PRIMARY_PSK_INDEX, key)) return 0;
    channel_hash = mesh_channel_hash(PRIMARY_CHANNEL_NAME, key, MESH_PSK_LEN);

    while(app->running) {
        if(!app->source->poll(app->source, &raw, 250)) continue;
        if(!app->running) break;

        MeshDecodeResult result =
            mesh_decode_frame(raw.data, raw.len, key, channel_hash, &decoded);
        mesh_event_from_decoded(&event, &decoded, result, raw.rssi, raw.snr);

        furi_mutex_acquire(app->mutex, FuriWaitForever);

        app->total_frames++;
        if((size_t)result < MESH_RESULT_COUNT) app->counters[result]++;

        /* Both of these ignore what they should ignore, so no filtering here:
         * the ring drops non-text, the roster drops headerless frames. */
        message_ring_push(&app->messages, &event);
        node_roster_observe(&app->roster, &event, furi_get_tick());

        /* NODEINFO_APP carries a User message. It is how the node list gets
         * real names instead of hex numbers. portnums.proto:57-61. */
        if(result == MESH_OK && decoded.data.portnum == MESH_PORTNUM_NODEINFO_APP) {
            MeshUser user;
            if(mesh_user_parse(decoded.data.payload, decoded.data.payload_len, &user)) {
                node_roster_set_user(&app->roster, event.from, &user, furi_get_tick());
            }
        }

        app->crc_errors = source_radio_crc_errors(app->source);

        size_t copy = raw.len < RAW_FRAME_MAX ? raw.len : RAW_FRAME_MAX;
        memcpy(app->last_raw, raw.data, copy);
        app->last_raw_len = copy;

        furi_mutex_release(app->mutex);

        view_port_update(app->view_port);
    }

    return 0;
}

MeshApp* mesh_app_alloc(void) {
    MeshApp* app = malloc(sizeof(MeshApp));
    memset(app, 0, sizeof(MeshApp));

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    message_ring_init(&app->messages);
    node_roster_init(&app->roster);

    app->page = PageHome;
    app->scroll = 0;

    /* The SX1262 is the only frame source. */
    app->source = source_radio_alloc();

    /* Node identity. Derived from the Flipper's own BLE MAC so two Flippers
     * running this do not claim the same node number. The top bit is cleared
     * because Meshtastic treats the high range as reserved. */
    const uint8_t* mac = furi_hal_version_get_ble_mac();
    uint32_t node_num =
        ((uint32_t)mac[0] << 24 | (uint32_t)mac[1] << 16 | (uint32_t)mac[2] << 8 | mac[3]) &
        0x7FFFFFFFu;
    PhoneIdentity identity;
    phone_identity_init(&identity, node_num, "Flipper Mesh", "FLPR");

    /* Cached for the UI. The LoRa frame shows what the radio is actually
     * tuned to, so it comes from the same config the driver uses. */
    lora_config_us_longfast(LORA_PRIMARY_CHANNEL_NAME, &app->lora);
    strncpy(app->node_name, identity.long_name, sizeof(app->node_name) - 1);
    /* Real devices show the last two bytes of the BLE MAC here. */
    snprintf(app->ble_id, sizeof(app->ble_id), "%02x%02x", mac[4], mac[5]);
    app->ble = meshtastic_ble_start(&identity);

    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, app_view_draw, app);
    view_port_input_callback_set(app->view_port, input_callback, app->input_queue);

    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    return app;
}

static void free_source(MeshApp* app) {
    if(app->source == NULL) return;
    source_radio_free(app->source);
    app->source = NULL;
}

void mesh_app_free(MeshApp* app) {
    if(app == NULL) return;

    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);
    furi_message_queue_free(app->input_queue);

    if(app->ble) meshtastic_ble_stop(app->ble);

    free_source(app);
    furi_mutex_free(app->mutex);
    free(app);
}

static void clamp_scroll(MeshApp* app) {
    size_t rows = app_view_row_count(app);
    if(rows == 0) {
        app->scroll = 0;
    } else if(app->scroll >= rows) {
        app->scroll = rows - 1;
    }
}

void mesh_app_run(MeshApp* app) {
    InputEvent event;

    app->running = true;

    /* No fallback. If the radio does not answer, the UI says so. Showing
     * invented traffic instead would make a broken radio look like a
     * working one. */
    app->source_started = app->source->start(app->source);
    if(!app->source_started) {
        FURI_LOG_W("MeshApp", "no SX1262 detected");
    }

    app->thread = furi_thread_alloc_ex("MeshRadio", 2048, radio_thread, app);
    furi_thread_start(app->thread);

    while(furi_message_queue_get(app->input_queue, &event, FuriWaitForever) == FuriStatusOk) {
        if(event.type != InputTypeShort && event.type != InputTypeRepeat) continue;

        if(event.key == InputKeyBack) break;

        switch(event.key) {
        case InputKeyLeft:
            app->page = (app->page + PageCount - 1) % PageCount;
            app->scroll = 0;
            break;
        case InputKeyRight:
            app->page = (app->page + 1) % PageCount;
            app->scroll = 0;
            break;
        case InputKeyUp:
            if(app->scroll > 0) app->scroll--;
            break;
        case InputKeyDown:
            app->scroll++;
            clamp_scroll(app);
            break;
        default:
            break;
        }

        view_port_update(app->view_port);
    }

    app->running = false;
    app->source->stop(app->source);
    furi_thread_join(app->thread);
    furi_thread_free(app->thread);
}
