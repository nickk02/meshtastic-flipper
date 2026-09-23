/* The two-stage connection handshake the Meshtastic phone app performs.
 *
 * Pure state machine. No BLE, no Flipper headers, so it is host-tested.
 *
 * The app writes want_config_id with a nonce. The device replies with the
 * messages for that stage, then config_complete_id carrying the same nonce.
 *
 *   Stage 1, nonce 69420: MyNodeInfo, then config_complete
 *   Stage 2, nonce 69421: our NodeInfo, one NodeInfo per heard node, then
 *                         config_complete
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

/* Stage two is the own NodeInfo and config_complete, each repeated (see
 * STAGE_TWO_REPEATS), with one NodeInfo per heard node between them. Heard
 * nodes are capped so the stage fits HANDSHAKE_MAX_REPLIES, which also keeps
 * it below the service's 40 slot queue with room for the admin replies stage
 * one leaves behind. With the roster full, the least recently heard nodes are
 * the ones left out. */
#define HANDSHAKE_STAGE_TWO_REPEATS 4
#define HANDSHAKE_MAX_OTHER_NODES   (HANDSHAKE_MAX_REPLIES - 2 * HANDSHAKE_STAGE_TWO_REPEATS)

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
    /* Heard nodes for stage two. NULL means none. Borrowed, not owned. */
    const NodeRoster* roster;
} Handshake;

/* The config record is copied, not referenced. The handshake runs on the BLE
 * worker thread and the record is edited from the UI thread, so sharing a
 * pointer would need a lock on every field read. */
void handshake_init(Handshake* h, const MeshConfig* config);

/* Nodes heard on the air, sent in stage two after this node's own NodeInfo.
 *
 * Held by pointer and read during handshake_handle_to_radio. The roster the
 * app keeps is written by the radio thread under the app's mutex, and this
 * runs on the BLE worker, so the caller must not hand over that live roster:
 * the service passes a snapshot it copied under the app's mutex instead (see
 * meshtastic_ble_service_set_roster). NULL sends no other nodes. */
void handshake_set_roster(Handshake* h, const NodeRoster* roster);

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
