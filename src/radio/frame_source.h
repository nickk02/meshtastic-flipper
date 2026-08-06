/* Where frames come from.
 *
 * The point of this seam is that the app can be built and proven end to end
 * before the SX1262 exists. A simulated source replays known-good frames
 * through the identical decode and display path the radio will use, so when
 * the hardware arrives only one implementation of this interface is new.
 *
 * Implementations live in src/radio and may use the Flipper HAL. */
#ifndef FRAME_SOURCE_H
#define FRAME_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A whole LoRa payload, header included. MAX_LORA_PAYLOAD_LEN in
 * RadioInterface.h:20 is 255 and covers the entire frame, not just the part
 * after the 16 byte header. */
#define RAW_FRAME_MAX 255

typedef struct {
    uint8_t data[RAW_FRAME_MAX];
    size_t len;
    int16_t rssi; /* dBm, 0 when the source is not a radio */
    int8_t snr; /* dB, 0 when the source is not a radio */
} RawFrame;

typedef struct FrameSource FrameSource;

struct FrameSource {
    /* Shown in the UI so it is never ambiguous whether frames are real. */
    const char* name;

    /* Returns false if the source cannot be brought up, for example when no
     * radio answers on SPI. The caller should surface that rather than sit
     * silently receiving nothing. */
    bool (*start)(FrameSource* self);

    void (*stop)(FrameSource* self);

    /* Waits up to timeout_ms for a frame. Returns true when out was filled.
     * Returning false is normal and just means nothing arrived. */
    bool (*poll)(FrameSource* self, RawFrame* out, uint32_t timeout_ms);

    void* ctx;
};

#endif
