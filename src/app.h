/* Application state and lifecycle.
 *
 * Threading: a radio thread owns everything from the frame source through
 * decode, and writes results into the state below under a mutex. The GUI
 * thread only reads and draws. Nothing in the receive path allocates. */
#ifndef APP_H
#define APP_H

#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>

#include "src/proto/mesh_decode.h"
#include "src/model/mesh_config.h"
#include "src/model/message_ring.h"
#include "src/model/mesh_event.h"
#include "src/model/node_roster.h"
#include "src/ble/meshtastic_profile.h"
#include "src/radio/frame_source.h"
#include "src/radio/lora_config.h"

/* Frame order mirrors a real device: status first, then messages, then the
 * node list views, then radio detail. */
typedef enum {
    PageHome,
    PageMessages,
    PageNodesHeard,
    PageNodesSignal,
    PageLora,
    PagePhone,
    PageCount,
} AppPage;

typedef struct {
    FuriMutex* mutex;

    /* Guarded by mutex. */
    MessageRing messages;
    NodeRoster roster;
    uint32_t counters[MESH_RESULT_COUNT];
    uint32_t total_frames;
    uint8_t last_raw[RAW_FRAME_MAX];
    size_t last_raw_len;

    /* GUI thread only. */
    AppPage page;
    size_t scroll;

    FrameSource* source;
    /* False when the SX1262 did not answer. The UI says so rather than
     * showing an empty screen that looks like a quiet channel. */
    bool source_started;
    uint32_t crc_errors;

    /* Shown on the Home and LoRa frames. Fixed for the life of the app. */
    /* Working buffers for the radio thread. Kept here rather than as locals
     * because together they are about 660 bytes, which is a third of that
     * thread's stack before the decode call frame and the AES key schedule. */
    RawFrame rx_frame;
    MeshDecoded rx_decoded;
    MeshEvent rx_event;

    /* What this node is: identity, channel, LoRa settings. The single source
     * the BLE handshake, the UI and later the radio all read from. */
    MeshConfig config;

    LoraConfig lora;
    char node_name[PHONE_LONG_NAME_MAX];
    char ble_id[8];

    FuriThread* thread;
    volatile bool running;

    /* NULL when Bluetooth could not start. The app carries on without phone
     * support rather than refusing to run. */
    MeshtasticBleService* ble;

    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* input_queue;
} MeshApp;

MeshApp* mesh_app_alloc(void);
void mesh_app_free(MeshApp* app);
void mesh_app_run(MeshApp* app);

#endif
