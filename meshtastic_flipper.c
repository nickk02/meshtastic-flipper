/* M0 Task 1: measure real headroom on the device.
 *
 * This does no radio work. It cannot: the Flipper's CC1101 has no LoRa
 * demodulator, so nothing here can hear Meshtastic until the external SX1262
 * board is attached. What it proves is that the toolchain works, that a FAP we
 * build loads and runs, and how much heap an app actually gets.
 *
 * That last number matters more than it sounds. A FAP is loaded into the heap,
 * code included, so free heap is the real ceiling for everything we build. */
#include <furi.h>
#include <furi_hal_version.h>
#include <gui/gui.h>
#include <input/input.h>

#include "src/proto/mesh_channel.h"
#include "src/proto/mesh_decode.h"

typedef struct {
    size_t heap_free;
    size_t heap_total;
    size_t heap_watermark;
    bool decode_ok;
} MeasureState;

/* Prove the decode core actually runs on the Flipper, not just on a PC.
 *
 * The frame below was produced by test/tools/gen_vectors.py: a Data protobuf
 * carrying "hello mesh", AES128-CTR encrypted under the default channel PSK,
 * behind a real 16 byte header. If this decodes on device, the whole protocol
 * chain is confirmed on the target, and only the radio is left. */
static const uint8_t sample_frame[] = {
    /* header: to, from, id, flags, channel hash, next_hop, relay_node */
    0xff, 0xff, 0xff, 0xff, 0x44, 0x33, 0x22, 0x11,
    0x0d, 0x0c, 0x0b, 0x0a, 0x63, 0x08, 0x00, 0x00,
    /* AES128-CTR ciphertext of the Data protobuf */
    0xfa, 0x8b, 0x3a, 0xed, 0x00, 0xe8, 0x4b, 0x4b,
    0x41, 0xd2, 0x1e, 0x57, 0xf1, 0x7c,
};

static bool run_decode_self_test(void) {
    uint8_t key[MESH_PSK_LEN];
    MeshDecoded decoded;

    if(!mesh_channel_expand_psk(1, key)) return false;

    if(mesh_decode_frame(
           sample_frame,
           sizeof(sample_frame),
           key,
           mesh_channel_hash("LongFast", key, MESH_PSK_LEN),
           &decoded) != MESH_OK) {
        return false;
    }

    return decoded.data.payload_len == 10 &&
           memcmp(decoded.data.payload, "hello mesh", 10) == 0;
}

static void measure_draw(Canvas* canvas, void* ctx) {
    MeasureState* state = ctx;
    char line[48];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 11, "Meshtastic M0");

    canvas_set_font(canvas, FontSecondary);

    snprintf(line, sizeof(line), "Heap free:  %u", (unsigned)state->heap_free);
    canvas_draw_str(canvas, 2, 24, line);

    snprintf(line, sizeof(line), "Heap total: %u", (unsigned)state->heap_total);
    canvas_draw_str(canvas, 2, 34, line);

    snprintf(line, sizeof(line), "Watermark:  %u", (unsigned)state->heap_watermark);
    canvas_draw_str(canvas, 2, 44, line);

    snprintf(
        line, sizeof(line), "Decode core: %s", state->decode_ok ? "PASS" : "FAIL");
    canvas_draw_str(canvas, 2, 56, line);

    canvas_draw_str(canvas, 96, 11, "Back");
}

static void measure_input(InputEvent* event, void* ctx) {
    FuriMessageQueue* queue = ctx;
    furi_message_queue_put(queue, event, FuriWaitForever);
}

int32_t meshtastic_flipper_app(void* p) {
    UNUSED(p);

    MeasureState state = {
        .decode_ok = run_decode_self_test(),
        .heap_free = memmgr_get_free_heap(),
        .heap_total = memmgr_get_total_heap(),
        .heap_watermark = memmgr_get_minimum_free_heap(),
    };

    FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, measure_draw, &state);
    view_port_input_callback_set(view_port, measure_input, queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    InputEvent event;
    while(furi_message_queue_get(queue, &event, FuriWaitForever) == FuriStatusOk) {
        if(event.type == InputTypeShort && event.key == InputKeyBack) break;
    }

    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_message_queue_free(queue);
    return 0;
}
