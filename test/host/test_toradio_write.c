/* ToRadio write sizes against the limit of one write.
 *
 * BLEConnection.swift send() (Meshtastic-Apple v2.7.21, line 533) writes
 * without response whenever ToRadio offers it. That write is limited to MTU
 * minus 3 and nothing tells the phone when a longer one is lost. Write with
 * response has no such limit: CoreBluetooth sends a longer value as a long
 * write and the phone gets an ATT response either way. These tests measure
 * what the phone writes against that limit. */
#include "tinytest.h"

#include "ios_client.h"

#define ADMIN_SET_OWNER 32
#define USER_ID         1
#define USER_LONG_NAME  2
#define USER_SHORT_NAME 3

static MeshConfig config(void) {
    MeshConfig c;
    mesh_config_defaults(&c, 0x11223344);
    return c;
}

/* saveUser, AccessoryManager+ToRadio.swift:2104: set_owner, no
 * want_response, sent to and from the local node. */
static size_t build_set_owner(uint8_t* buf, size_t cap) {
    uint8_t user[80];
    uint8_t admin[96];
    PbWriter w;

    pb_writer_init(&w, user, sizeof(user));
    pb_write_string_field(&w, USER_ID, "!11223344");
    pb_write_string_field(&w, USER_LONG_NAME, "Flipper Zero Meshtastic Node Long Name 4");
    pb_write_string_field(&w, USER_SHORT_NAME, "FLP1");
    size_t user_len = pb_writer_len(&w);

    pb_writer_init(&w, admin, sizeof(admin));
    pb_write_submessage(&w, ADMIN_SET_OWNER, user, user_len);
    return ios_build_admin(
        buf, cap, 0x11223344, 0x11223344, 0x40000123, admin, pb_writer_len(&w), false);
}

/* User 59 bytes (id 11, long_name 42, short_name 6), AdminMessage 62 (field
 * 32 takes a two byte key), Data 66, MeshPacket 87, ToRadio 89. */
TEST(test_set_owner_40_char_name_size) {
    uint8_t buf[192];
    ASSERT_EQ_INT(strlen("Flipper Zero Meshtastic Node Long Name 4"), 40);
    ASSERT_EQ_INT(build_set_owner(buf, sizeof(buf)), 89);
}

/* At MTU 185 the without-response limit is 182 and set_owner fits. At the
 * default MTU of 23 the limit is 20, and every admin message is over it: a
 * write without response that long is lost with nothing to tell the phone,
 * while a write with response becomes a long write the device answers. */
TEST(test_set_owner_against_write_limit) {
    MeshConfig cfg = config();
    IosClient c;
    uint8_t buf[192];
    size_t n = build_set_owner(buf, sizeof(buf));

    ios_client_init(&c, &cfg, IosTransportIdeal);
    ASSERT_EQ_INT(c.to_radio_max_write, 182);
    ios_device_write(&c, buf, n);
    ASSERT_EQ_INT(c.to_radio_oversize, 0);

    ios_client_init(&c, &cfg, IosTransportIdeal);
    c.to_radio_max_write = 23 - 3; /* default ATT MTU 23 */
    ios_device_write(&c, buf, n);
    ASSERT_EQ_INT(c.to_radio_oversize, 1);
}

/* The TO_RADIO_VALUE_MAX comment says every write of the connect flow is
 * under 120 bytes. */
TEST(test_connect_writes_under_120_bytes) {
    MeshConfig cfg = config();
    IosClient c;

    ios_client_init(&c, &cfg, IosTransportIdeal);
    c.to_radio_max_write = 119;
    ios_client_connect(&c);
    ios_client_report(&c);
    ASSERT_TRUE(c.to_radio_writes > 0);
    /* The device now sends a tzdef, so the phone writes no set_config. The
     * requests it still makes (canned messages, ringtone) are covered. */
    ASSERT_TRUE(c.sent_get_canned > 0 && c.sent_get_ringtone > 0);
    ASSERT_EQ_INT(c.to_radio_oversize, 0);
}

TEST_MAIN_BEGIN()
RUN_TEST(test_set_owner_40_char_name_size);
RUN_TEST(test_set_owner_against_write_limit);
RUN_TEST(test_connect_writes_under_120_bytes);
TEST_MAIN_END()
