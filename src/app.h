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
#include "src/model/message_ring.h"
#include "src/model/node_roster.h"
#include "src/ble/meshtastic_profile.h"
#include "src/radio/frame_source.h"

typedef enum {
    PageMessages,
    PageNodes,
    PageStats,
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
    bool source_started;
    /* True when the SX1262 answered. False means we fell back to simulation,
     * which the UI states plainly rather than looking like a silent radio. */
    bool source_is_radio;
    uint32_t crc_errors;

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
