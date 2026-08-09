#include "mesh_config.h"

#include <stdio.h>
#include <string.h>

/* Copies at most cap - 1 bytes and always terminates. Non-printable bytes are
 * dropped rather than substituted: unlike a name arriving from the air, this
 * one is being typed by the user, so there is nothing to preserve. */
static size_t copy_printable(char* dest, size_t cap, const char* src) {
    size_t out = 0;
    if(src == NULL) {
        dest[0] = '\0';
        return 0;
    }
    for(size_t i = 0; src[i] != '\0' && out + 1 < cap; i++) {
        unsigned char c = (unsigned char)src[i];
        if(c >= 0x20 && c <= 0x7E) dest[out++] = (char)c;
    }
    dest[out] = '\0';
    return out;
}

void mesh_config_defaults(MeshConfig* out, uint32_t node_num) {
    if(out == NULL) return;

    memset(out, 0, sizeof(*out));
    out->version = MESH_CONFIG_VERSION;
    out->owner.node_num = node_num;

    /* The last two bytes of the node number, which is what a real device shows
     * and what makes two Flippers on one mesh tellable apart. */
    snprintf(
        out->owner.long_name,
        sizeof(out->owner.long_name),
        "Flipper %04lx",
        (unsigned long)(node_num & 0xFFFFu));
    snprintf(
        out->owner.short_name,
        sizeof(out->owner.short_name),
        "F%03lx",
        (unsigned long)(node_num & 0xFFFu));

    copy_printable(out->channel.name, sizeof(out->channel.name), "LongFast");
    out->channel.psk_index = 1;

    out->lora.region = MESH_REGION_US;
    out->lora.modem_preset = MESH_PRESET_LONG_FAST;
    out->lora.channel_num = 20;
    out->lora.tx_enabled = false;
}

bool mesh_config_valid(const MeshConfig* config) {
    if(config == NULL) return false;
    if(config->version != MESH_CONFIG_VERSION) return false;
    if(config->owner.node_num == 0) return false;

    /* The high range is reserved by Meshtastic, so a node number with the top
     * bit set was never legitimately assigned. */
    if((config->owner.node_num & 0x80000000u) != 0) return false;

    if(config->owner.long_name[0] == '\0') return false;
    if(config->owner.short_name[0] == '\0') return false;
    if(config->channel.name[0] == '\0') return false;
    if(config->lora.region == MESH_REGION_UNSET) return false;

    /* A terminator must exist inside every array, or later reads run off the
     * end. This is the check that makes a corrupt stored record safe. */
    if(memchr(config->owner.long_name, '\0', sizeof(config->owner.long_name)) == NULL)
        return false;
    if(memchr(config->owner.short_name, '\0', sizeof(config->owner.short_name)) == NULL)
        return false;
    if(memchr(config->channel.name, '\0', sizeof(config->channel.name)) == NULL) return false;

    return true;
}

bool mesh_config_set_long_name(MeshConfig* config, const char* name) {
    if(config == NULL) return false;
    char scratch[MESH_CONFIG_LONG_NAME_MAX];
    if(copy_printable(scratch, sizeof(scratch), name) == 0) return false;
    memcpy(config->owner.long_name, scratch, sizeof(scratch));
    return true;
}

bool mesh_config_set_short_name(MeshConfig* config, const char* name) {
    if(config == NULL) return false;
    char scratch[MESH_CONFIG_SHORT_NAME_MAX];
    if(copy_printable(scratch, sizeof(scratch), name) == 0) return false;
    memcpy(config->owner.short_name, scratch, sizeof(scratch));
    return true;
}

void mesh_config_node_id(const MeshConfig* config, char* out, size_t out_len) {
    if(out == NULL || out_len == 0) return;
    if(config == NULL) {
        out[0] = '\0';
        return;
    }
    snprintf(out, out_len, "!%08lx", (unsigned long)config->owner.node_num);
}
