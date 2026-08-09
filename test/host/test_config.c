/* The shared config record.
 *
 * This is the single source of truth for identity, channel and LoRa settings.
 * Everything else reads from it, so a wrong default or a validator that accepts
 * a corrupt record propagates into the phone handshake and the radio at once. */
#include "tinytest.h"

#include "mesh_config.h"

#define TEST_NODE 0x11223344u

TEST(test_defaults_are_valid) {
    MeshConfig c;
    mesh_config_defaults(&c, TEST_NODE);
    ASSERT_TRUE(mesh_config_valid(&c));
    ASSERT_EQ_INT(c.version, MESH_CONFIG_VERSION);
    ASSERT_EQ_INT(c.owner.node_num, TEST_NODE);
}

TEST(test_defaults_are_us_longfast_on_the_default_channel) {
    MeshConfig c;
    mesh_config_defaults(&c, TEST_NODE);
    ASSERT_EQ_INT(c.lora.region, MESH_REGION_US);
    ASSERT_EQ_INT(c.lora.modem_preset, MESH_PRESET_LONG_FAST);
    ASSERT_EQ_MEM(c.channel.name, "LongFast", 8);
    /* A one byte psk is a key index. Index 1 is the default channel key. */
    ASSERT_EQ_INT(c.channel.psk_index, 1);
}

TEST(test_transmit_is_off_by_default) {
    /* There is no transmit path yet. Telling the phone otherwise makes it queue
     * messages that never leave. */
    MeshConfig c;
    mesh_config_defaults(&c, TEST_NODE);
    ASSERT_TRUE(!c.lora.tx_enabled);
}

TEST(test_default_name_distinguishes_two_devices) {
    /* Two Flippers on one mesh must not look identical. */
    MeshConfig a, b;
    mesh_config_defaults(&a, 0x11223344u);
    mesh_config_defaults(&b, 0x11225566u);
    ASSERT_TRUE(strcmp(a.owner.long_name, b.owner.long_name) != 0);
    ASSERT_TRUE(strcmp(a.owner.short_name, b.owner.short_name) != 0);
}

TEST(test_node_id_uses_the_meshtastic_form) {
    MeshConfig c;
    char id[16];
    mesh_config_defaults(&c, 0x0abbccddu);
    mesh_config_node_id(&c, id, sizeof(id));
    ASSERT_EQ_MEM(id, "!0abbccdd", 9);
}

TEST(test_long_name_is_truncated_not_rejected) {
    MeshConfig c;
    char huge[120];
    memset(huge, 'A', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = '\0';

    mesh_config_defaults(&c, TEST_NODE);
    ASSERT_TRUE(mesh_config_set_long_name(&c, huge));
    ASSERT_EQ_INT(strlen(c.owner.long_name), MESH_CONFIG_LONG_NAME_MAX - 1);
    ASSERT_TRUE(mesh_config_valid(&c));
}

TEST(test_empty_name_is_refused) {
    /* A node with no name is exactly what this project spent four releases
     * fixing, so an edit may not reintroduce one. */
    MeshConfig c;
    mesh_config_defaults(&c, TEST_NODE);
    ASSERT_TRUE(!mesh_config_set_long_name(&c, ""));
    ASSERT_TRUE(!mesh_config_set_short_name(&c, ""));
    ASSERT_TRUE(!mesh_config_set_long_name(&c, "\x01\x02"));
    ASSERT_TRUE(mesh_config_valid(&c));
}

TEST(test_control_characters_are_dropped_from_edits) {
    MeshConfig c;
    mesh_config_defaults(&c, TEST_NODE);
    ASSERT_TRUE(mesh_config_set_short_name(
        &c,
        "A\x01"
        "B"));
    ASSERT_EQ_MEM(c.owner.short_name, "AB", 2);
}

TEST(test_wrong_version_is_invalid) {
    /* A stored record from an older layout is discarded, not reinterpreted. */
    MeshConfig c;
    mesh_config_defaults(&c, TEST_NODE);
    c.version = MESH_CONFIG_VERSION + 1;
    ASSERT_TRUE(!mesh_config_valid(&c));
}

TEST(test_reserved_node_numbers_are_invalid) {
    MeshConfig c;
    mesh_config_defaults(&c, TEST_NODE);
    c.owner.node_num = 0;
    ASSERT_TRUE(!mesh_config_valid(&c));
    /* Meshtastic reserves the high range. */
    mesh_config_defaults(&c, TEST_NODE);
    c.owner.node_num = 0x80000001u;
    ASSERT_TRUE(!mesh_config_valid(&c));
}

TEST(test_unterminated_strings_are_invalid) {
    /* This is the check that makes a corrupt record from storage safe to hold.
     * Without it every later read runs off the end of the array. */
    MeshConfig c;
    mesh_config_defaults(&c, TEST_NODE);
    memset(c.owner.long_name, 'A', sizeof(c.owner.long_name));
    ASSERT_TRUE(!mesh_config_valid(&c));
}

TEST(test_unset_region_is_invalid) {
    MeshConfig c;
    mesh_config_defaults(&c, TEST_NODE);
    c.lora.region = MESH_REGION_UNSET;
    ASSERT_TRUE(!mesh_config_valid(&c));
}

TEST(test_tolerates_null) {
    char id[8];
    ASSERT_TRUE(!mesh_config_valid(NULL));
    ASSERT_TRUE(!mesh_config_set_long_name(NULL, "x"));
    ASSERT_TRUE(!mesh_config_set_short_name(NULL, "x"));
    mesh_config_defaults(NULL, 1);
    mesh_config_node_id(NULL, id, sizeof(id));
    ASSERT_EQ_INT(id[0], 0);
}

TEST_MAIN_BEGIN()
RUN_TEST(test_defaults_are_valid);
RUN_TEST(test_defaults_are_us_longfast_on_the_default_channel);
RUN_TEST(test_transmit_is_off_by_default);
RUN_TEST(test_default_name_distinguishes_two_devices);
RUN_TEST(test_node_id_uses_the_meshtastic_form);
RUN_TEST(test_long_name_is_truncated_not_rejected);
RUN_TEST(test_empty_name_is_refused);
RUN_TEST(test_control_characters_are_dropped_from_edits);
RUN_TEST(test_wrong_version_is_invalid);
RUN_TEST(test_reserved_node_numbers_are_invalid);
RUN_TEST(test_unterminated_strings_are_invalid);
RUN_TEST(test_unset_region_is_invalid);
RUN_TEST(test_tolerates_null);
TEST_MAIN_END()
