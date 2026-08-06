#include "src/radio/source_radio.h"

#include <furi.h>
#include <string.h>

#include "src/radio/lora_config.h"
#include "src/radio/sx126x.h"

#define TAG "SourceRadio"

/* Poll interval. At LongFast a short packet is airborne for hundreds of
 * milliseconds, so 10ms is far finer than the signal needs and cheap on a
 * 64MHz part. The spec chose polling over a DIO1 interrupt for exactly this
 * reason: the timing margin is enormous and polling is simpler to get right. */
#define POLL_SLICE_MS 10

typedef struct {
    Sx126x* radio;
    LoraConfig config;
    uint32_t crc_errors;
    bool running;
} RadioContext;

static bool radio_start(FrameSource* self) {
    RadioContext* ctx = self->ctx;

    lora_config_us_longfast(LORA_PRIMARY_CHANNEL_NAME, &ctx->config);
    FURI_LOG_I(
        TAG,
        "US LongFast: %lu Hz, slot %lu, SF%u BW%d",
        (unsigned long)ctx->config.freq_hz,
        (unsigned long)ctx->config.channel_num,
        ctx->config.sf,
        (int)ctx->config.bw_khz);

    if(!sx126x_begin(ctx->radio)) {
        FURI_LOG_W(TAG, "no radio detected");
        sx126x_end(ctx->radio);
        return false;
    }

    if(!sx126x_configure_lora(ctx->radio, &ctx->config)) {
        FURI_LOG_E(TAG, "configuration failed");
        sx126x_end(ctx->radio);
        return false;
    }

    if(!sx126x_start_rx(ctx->radio)) {
        FURI_LOG_E(TAG, "could not enter receive");
        sx126x_end(ctx->radio);
        return false;
    }

    ctx->crc_errors = 0;
    ctx->running = true;
    return true;
}

static void radio_stop(FrameSource* self) {
    RadioContext* ctx = self->ctx;
    if(!ctx->running) return;
    ctx->running = false;
    sx126x_end(ctx->radio);
}

static bool radio_poll(FrameSource* self, RawFrame* out, uint32_t timeout_ms) {
    RadioContext* ctx = self->ctx;
    uint32_t waited = 0;

    if(!ctx->running) return false;

    while(waited < timeout_ms) {
        size_t len = 0;
        bool crc_error = false;

        if(sx126x_poll_rx(
               ctx->radio, out->data, RAW_FRAME_MAX, &len, &out->rssi, &out->snr, &crc_error)) {
            out->len = len;
            return true;
        }

        if(crc_error) ctx->crc_errors++;
        if(!ctx->running) return false;

        furi_delay_ms(POLL_SLICE_MS);
        waited += POLL_SLICE_MS;
    }

    return false;
}

FrameSource* source_radio_alloc(void) {
    FrameSource* source = malloc(sizeof(FrameSource));
    RadioContext* ctx = malloc(sizeof(RadioContext));

    memset(ctx, 0, sizeof(RadioContext));
    ctx->radio = sx126x_alloc();

    source->name = "SX1262";
    source->start = radio_start;
    source->stop = radio_stop;
    source->poll = radio_poll;
    source->ctx = ctx;
    return source;
}

void source_radio_free(FrameSource* source) {
    if(source == NULL) return;
    RadioContext* ctx = source->ctx;
    sx126x_free(ctx->radio);
    free(ctx);
    free(source);
}

uint32_t source_radio_crc_errors(FrameSource* source) {
    if(source == NULL) return 0;
    RadioContext* ctx = source->ctx;
    return ctx->crc_errors;
}
