/* Meshtastic receiver for Flipper Zero.
 *
 * Requires an external SX1262 module. The Flipper's built-in CC1101 cannot
 * demodulate LoRa, so until that board is attached the app runs against a
 * simulated frame source, and the Stats page names whichever source is live so
 * it is never ambiguous whether frames are real. */
#include "src/app.h"

int32_t meshtastic_flipper_app(void* p) {
    UNUSED(p);

    MeshApp* app = mesh_app_alloc();
    mesh_app_run(app);
    mesh_app_free(app);
    return 0;
}
