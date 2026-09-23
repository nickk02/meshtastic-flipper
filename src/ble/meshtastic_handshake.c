#include "src/ble/meshtastic_handshake.h"

/* How many times each stage two message is queued. See the comment where it
 * is used. */
#define STAGE_TWO_REPEATS 1

/* config.proto, Config.device and Config.lora. */
#define CONFIG_VARIANT_DEVICE 1
#define CONFIG_VARIANT_LORA   6

#include <string.h>

void handshake_init(Handshake* h, const MeshConfig* config) {
    if(h == NULL) return;
    memset(h, 0, sizeof(*h));
    memset(h->session_passkey, 0, sizeof(h->session_passkey));

    if(config != NULL) {
        h->config = *config;
        /* The wire identity is derived here rather than passed in, so there is
         * no way to hand the handshake an identity that disagrees with the
         * record it answers config questions from. */
        phone_identity_from_config(config, &h->identity);
    }
    h->stage = HandshakeIdle;
}

void handshake_set_session_passkey(Handshake* h, const uint8_t* passkey) {
    if(h == NULL || passkey == NULL) return;
    memcpy(h->session_passkey, passkey, PHONE_SESSION_PASSKEY_LEN);
}

void handshake_reset(Handshake* h) {
    if(h == NULL) return;
    h->stage = HandshakeIdle;
}

HandshakeStage handshake_stage(const Handshake* h) {
    return h == NULL ? HandshakeIdle : h->stage;
}

bool handshake_is_complete(const Handshake* h) {
    return h != NULL && h->stage == HandshakeComplete;
}

static bool push(HandshakeReply* reply, size_t len) {
    if(len == 0 || len > HANDSHAKE_MAX_MESSAGE) return false;
    reply->messages[reply->count].len = len;
    reply->count++;
    return true;
}

bool handshake_handle_to_radio(
    Handshake* h,
    const uint8_t* data,
    size_t len,
    HandshakeReply* reply) {
    uint32_t nonce = 0;
    size_t written;

    if(h == NULL || reply == NULL) return false;

    reply->count = 0;

    /* An admin get_owner_request is answered whatever stage we are in, and
     * before the want_config parse, since it is not a want_config at all.
     *
     * A capture of the Android client shows it asking for canned messages and a
     * ringtone and pushing a timezone with set_config once both config stages
     * finish, every one of them with want_response set. It never sends
     * get_owner_request. Answering only that one, which is what handshake-fsm.md
     * names, left every real question unanswered and the client timed out and
     * reconnected in a loop. */
    if(phone_decode_admin_request(data, len, &h->admin)) {
        written = phone_encode_admin_reply(
            &h->identity,
            &h->admin,
            h->session_passkey,
            reply->messages[reply->count].data,
            HANDSHAKE_MAX_MESSAGE);
        /* A request that wants no response is still handled, not ignored: the
         * caller uses the return to decide whether the message was understood,
         * and an unanswered admin message reads as a stalled device. */
        if(written == 0) return true;
        return push(reply, written);
    }

    /* A heartbeat gets one queueStatus, PhoneAPI.cpp handleToRadio (the
     * heartbeat case sets heartbeatReceived) and getFromRadio (which sends
     * the queueStatus before anything else). Nonce 1 is the firmware's
     * "nodeinfo ping", answered with a mesh broadcast rather than a reply to
     * the phone, so it gets nothing here. The stage is left alone: a
     * heartbeat is not part of the handshake. */
    if(phone_decode_heartbeat(data, len, &nonce)) {
        if(nonce == 1) return true;
        written = phone_encode_queue_status(
            0,
            HANDSHAKE_QUEUE_FREE_REPORT,
            HANDSHAKE_QUEUE_MAXLEN_REPORT,
            0,
            reply->messages[reply->count].data,
            HANDSHAKE_MAX_MESSAGE);
        return push(reply, written);
    }

    if(!phone_decode_want_config_id(data, len, &nonce)) return false;

    if(nonce == PHONE_NONCE_CONFIG) {
        /* Order follows the firmware's own state machine, PhoneAPI.cpp
         * getFromRadio(): my_info, then the node's own NodeInfo, then metadata,
         * then config_complete.
         *
         * The NodeInfo here is what carries this device's name. Sending it only
         * in stage 2, as this did before, completed the handshake and still left
         * the app showing a node with no name. */
        written = phone_encode_my_node_info(
            &h->identity, reply->messages[reply->count].data, HANDSHAKE_MAX_MESSAGE);
        if(!push(reply, written)) return false;

        written =
            phone_encode_device_ui(reply->messages[reply->count].data, HANDSHAKE_MAX_MESSAGE);
        if(!push(reply, written)) return false;

        written = phone_encode_node_info(
            &h->identity, reply->messages[reply->count].data, HANDSHAKE_MAX_MESSAGE);
        if(!push(reply, written)) return false;

        written = phone_encode_device_metadata(
            &h->identity, reply->messages[reply->count].data, HANDSHAKE_MAX_MESSAGE);
        if(!push(reply, written)) return false;

        /* Every channel slot, not just the one in use. STATE_SEND_CHANNELS
         * walks the whole table before moving on, so a client that gets one
         * channel is still waiting for seven more. Sending only the primary is
         * why a complete looking stage one was still refused. */
        for(uint32_t slot = 0; slot < PHONE_CHANNEL_SLOTS; slot++) {
            if(slot == 0) {
                written = phone_encode_primary_channel(
                    h->config.channel.name,
                    h->config.channel.psk_index,
                    reply->messages[reply->count].data,
                    HANDSHAKE_MAX_MESSAGE);
            } else {
                written = phone_encode_empty_channel(
                    slot, reply->messages[reply->count].data, HANDSHAKE_MAX_MESSAGE);
            }
            if(!push(reply, written)) return false;
        }

        /* Every Config variant, in field order. Device is field 1 and carries
         * a tzdef, so the iOS app does not write one back. LoRa is field 6 and
         * carries real settings; the rest are empty, meaning all defaults,
         * which is the truthful answer for a device that does not implement
         * them. Skipping them is what made the client abandon stage one and
         * reconnect. */
        for(uint32_t variant = 1; variant <= PHONE_CONFIG_VARIANTS; variant++) {
            if(variant == CONFIG_VARIANT_DEVICE) {
                written = phone_encode_device_config(
                    reply->messages[reply->count].data, HANDSHAKE_MAX_MESSAGE);
            } else if(variant == CONFIG_VARIANT_LORA) {
                written = phone_encode_lora_config(
                    h->config.lora.channel_num,
                    reply->messages[reply->count].data,
                    HANDSHAKE_MAX_MESSAGE);
            } else {
                written = phone_encode_config_variant(
                    variant, NULL, 0, reply->messages[reply->count].data, HANDSHAKE_MAX_MESSAGE);
            }
            if(!push(reply, written)) return false;
        }

        for(uint32_t variant = 1; variant <= PHONE_MODULECONFIG_VARIANTS; variant++) {
            written = phone_encode_moduleconfig_variant(
                variant, reply->messages[reply->count].data, HANDSHAKE_MAX_MESSAGE);
            if(!push(reply, written)) return false;
        }

        written = phone_encode_config_complete(
            PHONE_NONCE_CONFIG, reply->messages[reply->count].data, HANDSHAKE_MAX_MESSAGE);
        if(!push(reply, written)) return false;

        h->stage = HandshakeConfigRequested;
        return true;
    }

    if(nonce == PHONE_NONCE_NODE_INFO) {
        /* Each message is queued once. STAGE_TWO_REPEATS is 1.
         *
         * It was 4, on the theory that a single copy of this two message
         * batch was visible for well under a second and then gone. It is
         * not. The drain restates each frame for a full DRAIN_INTERVAL_MS
         * (350ms), and the iOS client (Meshtastic-Apple v2.7.21) reads in a
         * tight loop with no delay between reads (BLEConnection.swift
         * drainPendingPackets, a repeat of read() that only breaks on an
         * empty value). At the 88ms per read that test/host/ios_client.h
         * models, every published frame is already read about four times;
         * even at the 140-150ms per message timed on hardware (see
         * DRAIN_INTERVAL_MS in meshtastic_service.c) it is read twice.
         *
         * Repeats multiplied that, and each read is a full handler call:
         *
         * - handleNodeInfo bumps .retrievingDatabase(nodeCount) on every
         *   NodeInfo it sees (AccessoryManager+FromRadio.swift:330-333), with
         *   no check for a node it has already counted. Four copies read
         *   four times each is why the phone showed "16 nodes" for this one
         *   node. The host model in test/host/ios_client.h reproduces
         *   exactly 16.
         * - Every config_complete_id 69421 runs the NONCE_ONLY_DB branch of
         *   processFromRadio (AccessoryManager.swift:1165-1190), which
         *   flushes the deferred saves and batch saves the context. Four
         *   copies read four times each ran that save 16 times per connect
         *   in the same model.
         *
         * Duplicate reads of the single copy remain, since that is the
         * transport, but they no longer multiply. */
        for(int i = 0; i < STAGE_TWO_REPEATS; i++) {
            written = phone_encode_node_info(
                &h->identity, reply->messages[reply->count].data, HANDSHAKE_MAX_MESSAGE);
            if(!push(reply, written)) return false;
        }

        for(int i = 0; i < STAGE_TWO_REPEATS; i++) {
            written = phone_encode_config_complete(
                PHONE_NONCE_NODE_INFO, reply->messages[reply->count].data, HANDSHAKE_MAX_MESSAGE);
            if(!push(reply, written)) return false;
        }

        h->stage = HandshakeComplete;
        return true;
    }

    /* Unknown nonce. Replying to a stage the app did not ask for makes it
     * discard the response and stall, so send nothing. */
    return false;
}
