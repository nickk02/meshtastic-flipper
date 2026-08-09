/* The two-stage connection handshake the Meshtastic phone app performs.
 *
 * Pure state machine. No BLE, no Flipper headers, so it is host-tested.
 *
 * The app writes want_config_id with a nonce. The device replies with the
 * messages for that stage, then config_complete_id carrying the same nonce.
 *
 *   Stage 1, nonce 69420: MyNodeInfo, then config_complete
 *   Stage 2, nonce 69421: our NodeInfo, then config_complete
 *
 * Real firmware also sends config blocks, module config, channels, metadata and
 * a file manifest. The client does not require any of them. Its own test
 * completes Stage 1 from MyNodeInfo and config_complete alone. See
 * docs/feasibility-full-node.md. */
#ifndef MESHTASTIC_HANDSHAKE_H
#define MESHTASTIC_HANDSHAKE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "src/proto/phone_encode.h"

typedef enum {
    HandshakeIdle, /* nothing asked for yet */
    HandshakeConfigRequested,
    HandshakeNodeInfoRequested,
    HandshakeComplete,
} HandshakeStage;

/* Up to two messages are produced per request: a body, then the completion
 * marker. */
#define HANDSHAKE_MAX_REPLIES 6
#define HANDSHAKE_MAX_MESSAGE 192

typedef struct {
    uint8_t data[HANDSHAKE_MAX_MESSAGE];
    size_t len;
} HandshakeMessage;

typedef struct {
    HandshakeMessage messages[HANDSHAKE_MAX_REPLIES];
    size_t count;
} HandshakeReply;

typedef struct {
    PhoneIdentity identity;
    HandshakeStage stage;
} Handshake;

void handshake_init(Handshake* h, const PhoneIdentity* identity);

/* Handle a ToRadio the phone wrote.
 *
 * Fills reply with the FromRadio messages to queue, in order. Returns false
 * when the message carries no want_config_id or the nonce is unrecognized, in
 * which case reply is emptied and nothing should be sent.
 *
 * An unknown nonce is rejected rather than guessed at. Replying to a stage the
 * app did not ask for makes it discard the response and stall. */
bool handshake_handle_to_radio(
    Handshake* h,
    const uint8_t* data,
    size_t len,
    HandshakeReply* reply);

HandshakeStage handshake_stage(const Handshake* h);
bool handshake_is_complete(const Handshake* h);

/* Reset to idle. Call when the phone disconnects, so a reconnect starts a
 * fresh handshake rather than resuming a stale one. */
void handshake_reset(Handshake* h);

#endif
