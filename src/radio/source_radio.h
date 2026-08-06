/* FrameSource backed by the SX1262.
 *
 * UNVERIFIED against hardware. See sx126x.h.
 *
 * start() returns false when no radio answers, which lets the app fall back to
 * the simulated source and say so, rather than sitting on an empty screen
 * looking identical to a radio that is present but misconfigured. */
#ifndef SOURCE_RADIO_H
#define SOURCE_RADIO_H

#include "src/radio/frame_source.h"

FrameSource* source_radio_alloc(void);
void source_radio_free(FrameSource* source);

/* Frames dropped for CRC or header errors since start.
 *
 * Counted rather than discarded because it is the single most useful number
 * during bring-up: a rising CRC count means the modem is nearly right, which
 * is a completely different problem from a silent radio. */
uint32_t source_radio_crc_errors(FrameSource* source);

#endif
