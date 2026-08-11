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
/* my_info, own node_info, metadata, one channel, ten config variants,
 * thirteen module config variants, config_complete. */
/* my_info, deviceuiConfig, own node_info, metadata, eight channel slots,
 * ten config variants, thirteen module config variants, config_complete. */
#define HANDSHAKE_MAX_REPLIES 36
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
    MeshConfig config;
    /* Seeded by the caller, since randomness is a platform concern and this
     * layer has no Flipper dependencies. */
    uint8_t session_passkey[PHONE_SESSION_PASSKEY_LEN];
    /* Held here rather than on the stack. This runs on the BLE worker thread
     * and the same reasoning applies as to HandshakeReply. */
    PhoneAdminRequest admin;
    PhoneIdentity identity;
    HandshakeStage stage;
} Handshake;

/* The config record is copied, not referenced. The handshake runs on the BLE
 * worker thread and the record is edited from the UI thread, so sharing a
 * pointer would need a lock on every field read. */
void handshake_init(Handshake* h, const MeshConfig* config);

/* Seed the session passkey the admin exchange hands to the phone.
 *
 * The passkey must be unpredictable, and this layer cannot generate it without
 * taking a platform dependency, so the caller supplies it.
 * PHONE_SESSION_PASSKEY_LEN bytes. */
void handshake_set_session_passkey(Handshake* h, const uint8_t* passkey);

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
