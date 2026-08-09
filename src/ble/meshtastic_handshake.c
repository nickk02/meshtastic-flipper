#include "src/ble/meshtastic_handshake.h"

/* What this device tells the phone its radio is set to.
 *
 * These duplicate values the radio side also holds, which is exactly the
 * duplication to remove once a shared config store exists. Until then they live
 * in one place rather than being spread through the encoders, so there is a
 * single line to change. */
#define HANDSHAKE_CHANNEL_NAME      "LongFast"
#define HANDSHAKE_CHANNEL_PSK_INDEX 1
#define HANDSHAKE_LORA_CHANNEL_NUM  20

#include <string.h>

void handshake_init(Handshake* h, const PhoneIdentity* identity) {
    if(h == NULL) return;
    memset(h, 0, sizeof(*h));
    if(identity != NULL) h->identity = *identity;
    h->stage = HandshakeIdle;
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

        written = phone_encode_node_info(
            &h->identity, reply->messages[reply->count].data, HANDSHAKE_MAX_MESSAGE);
        if(!push(reply, written)) return false;

        written = phone_encode_device_metadata(
            &h->identity, reply->messages[reply->count].data, HANDSHAKE_MAX_MESSAGE);
        if(!push(reply, written)) return false;

        written = phone_encode_primary_channel(
            HANDSHAKE_CHANNEL_NAME,
            HANDSHAKE_CHANNEL_PSK_INDEX,
            reply->messages[reply->count].data,
            HANDSHAKE_MAX_MESSAGE);
        if(!push(reply, written)) return false;

        written = phone_encode_lora_config(
            HANDSHAKE_LORA_CHANNEL_NUM, reply->messages[reply->count].data, HANDSHAKE_MAX_MESSAGE);
        if(!push(reply, written)) return false;

        written = phone_encode_config_complete(
            PHONE_NONCE_CONFIG, reply->messages[reply->count].data, HANDSHAKE_MAX_MESSAGE);
        if(!push(reply, written)) return false;

        h->stage = HandshakeConfigRequested;
        return true;
    }

    if(nonce == PHONE_NONCE_NODE_INFO) {
        written = phone_encode_node_info(
            &h->identity, reply->messages[reply->count].data, HANDSHAKE_MAX_MESSAGE);
        if(!push(reply, written)) return false;

        written = phone_encode_config_complete(
            PHONE_NONCE_NODE_INFO, reply->messages[reply->count].data, HANDSHAKE_MAX_MESSAGE);
        if(!push(reply, written)) return false;

        h->stage = HandshakeComplete;
        return true;
    }

    /* Unknown nonce. Replying to a stage the app did not ask for makes it
     * discard the response and stall, so send nothing. */
    return false;
}
