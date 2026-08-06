#include "src/radio/source_sim.h"

#include <furi.h>
#include <string.h>

#include "src/radio/sim_frames.h"

typedef struct {
    uint32_t interval_ms;
    size_t next;
    bool running;
} SimContext;

static bool sim_start(FrameSource* self) {
    SimContext* ctx = self->ctx;
    ctx->next = 0;
    ctx->running = true;
    return true;
}

static void sim_stop(FrameSource* self) {
    SimContext* ctx = self->ctx;
    ctx->running = false;
}

static bool sim_poll(FrameSource* self, RawFrame* out, uint32_t timeout_ms) {
    SimContext* ctx = self->ctx;
    if(!ctx->running) return false;

    /* Sleep in short slices so stopping the app stays responsive rather than
     * blocking for the whole interval. */
    uint32_t waited = 0;
    uint32_t target = ctx->interval_ms < timeout_ms ? ctx->interval_ms : timeout_ms;
    while(waited < target) {
        if(!ctx->running) return false;
        uint32_t slice = (target - waited) > 50 ? 50 : (target - waited);
        furi_delay_ms(slice);
        waited += slice;
    }
    if(!ctx->running) return false;

    const SimFrame* frame = &SIM_FRAMES[ctx->next];
    ctx->next = (ctx->next + 1) % SIM_FRAME_COUNT;

    size_t len = frame->frame_len;
    if(len > RAW_FRAME_MAX) len = RAW_FRAME_MAX;
    memcpy(out->data, frame->frame, len);
    out->len = len;

    /* Left at zero deliberately. These are meaningless without a radio, and
     * inventing plausible values would make the UI look like it is receiving
     * when it is not. */
    out->rssi = 0;
    out->snr = 0;
    return true;
}

FrameSource* source_sim_alloc(uint32_t interval_ms) {
    FrameSource* source = malloc(sizeof(FrameSource));
    SimContext* ctx = malloc(sizeof(SimContext));

    ctx->interval_ms = interval_ms;
    ctx->next = 0;
    ctx->running = false;

    source->name = "Simulated";
    source->start = sim_start;
    source->stop = sim_stop;
    source->poll = sim_poll;
    source->ctx = ctx;
    return source;
}

void source_sim_free(FrameSource* source) {
    if(source == NULL) return;
    free(source->ctx);
    free(source);
}
