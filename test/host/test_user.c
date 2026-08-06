/* User message parsing, the payload of NODEINFO_APP packets.
 *
 * Field numbers are checked against mesh.proto rather than assumed. Field 4 is
 * retired, so hw_model is 5; reading it as 4 would silently produce zero. */
#include "tinytest.h"

#include "mesh_user.h"
#include "pb_write.h"

static size_t build_user(
    uint8_t* buf,
    size_t cap,
    const char* id,
    const char* long_name,
    const char* short_name,
    uint32_t hw_model) {
    PbWriter w;
    pb_writer_init(&w, buf, cap);
    pb_write_string_field(&w, 1, id);
    pb_write_string_field(&w, 2, long_name);
    pb_write_string_field(&w, 3, short_name);
    pb_write_varint_field(&w, 5, hw_model);
    return pb_writer_len(&w);
}

TEST(test_parses_all_fields) {
    uint8_t buf[128];
    MeshUser u;
    size_t len = build_user(buf, sizeof(buf), "!11223344", "Base Station", "BASE", 9);

    ASSERT_TRUE(mesh_user_parse(buf, len, &u));
    ASSERT_EQ_MEM(u.id, "!11223344", 9);
    ASSERT_EQ_MEM(u.long_name, "Base Station", 12);
    ASSERT_EQ_MEM(u.short_name, "BASE", 4);
    ASSERT_EQ_INT(u.hw_model, 9);
    ASSERT_TRUE(u.has_long_name);
    ASSERT_TRUE(u.has_short_name);
}

TEST(test_strings_are_terminated) {
    uint8_t buf[128];
    MeshUser u;
    size_t len = build_user(buf, sizeof(buf), "!aa", "Node", "ND", 0);

    ASSERT_TRUE(mesh_user_parse(buf, len, &u));
    ASSERT_EQ_INT(u.long_name[4], 0);
    ASSERT_EQ_INT(u.short_name[2], 0);
}

TEST(test_absent_names_are_flagged) {
    uint8_t buf[64];
    MeshUser u;
    size_t len = build_user(buf, sizeof(buf), "!bb", NULL, NULL, 0);

    ASSERT_TRUE(mesh_user_parse(buf, len, &u));
    ASSERT_TRUE(!u.has_long_name);
    ASSERT_TRUE(!u.has_short_name);
    ASSERT_EQ_INT(u.long_name[0], 0);
}

TEST(test_overlong_name_is_truncated_not_rejected) {
    /* A 60 character name is still worth listing. */
    uint8_t buf[160];
    MeshUser u;
    char huge[80];
    memset(huge, 'A', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = '\0';

    size_t len = build_user(buf, sizeof(buf), "!cc", huge, "LONG", 0);
    ASSERT_TRUE(mesh_user_parse(buf, len, &u));
    ASSERT_EQ_INT(strlen(u.long_name), MESH_USER_LONG_NAME_MAX - 1);
    ASSERT_TRUE(u.has_long_name);
}

TEST(test_non_printable_bytes_are_replaced) {
    /* Names arrive from the air and can contain anything. Control characters
       would corrupt the display, so they become dots. */
    uint8_t buf[64];
    MeshUser u;
    PbWriter w;
    const uint8_t nasty[] = {'O', 'K', 0x01, 0x1B, 'x'};

    pb_writer_init(&w, buf, sizeof(buf));
    pb_write_bytes_field(&w, 2, nasty, sizeof(nasty));

    ASSERT_TRUE(mesh_user_parse(buf, pb_writer_len(&w), &u));
    ASSERT_EQ_MEM(u.long_name, "OK..x", 5);
}

TEST(test_hw_model_is_field_5_not_4) {
    /* Field 4 is retired in mesh.proto. Reading it would give zero. */
    uint8_t buf[32];
    MeshUser u;
    PbWriter w;

    pb_writer_init(&w, buf, sizeof(buf));
    pb_write_varint_field_always(&w, 4, 77); /* retired field */
    pb_write_varint_field_always(&w, 5, 42); /* hw_model */

    ASSERT_TRUE(mesh_user_parse(buf, pb_writer_len(&w), &u));
    ASSERT_EQ_INT(u.hw_model, 42);
}

TEST(test_unknown_fields_are_skipped) {
    uint8_t buf[64];
    MeshUser u;
    PbWriter w;
    const uint8_t key[] = {1, 2, 3, 4};

    pb_writer_init(&w, buf, sizeof(buf));
    pb_write_bytes_field(&w, 8, key, sizeof(key)); /* public_key */
    pb_write_string_field(&w, 3, "SKIP");
    pb_write_varint_field_always(&w, 9, 1); /* is_unmessagable */

    ASSERT_TRUE(mesh_user_parse(buf, pb_writer_len(&w), &u));
    ASSERT_EQ_MEM(u.short_name, "SKIP", 4);
}

TEST(test_rejects_malformed) {
    const uint8_t truncated[] = {0x12, 0x40, 'a'}; /* length runs past the end */
    const uint8_t bad_varint[] = {0x28, 0x80};
    MeshUser u;

    ASSERT_TRUE(!mesh_user_parse(truncated, sizeof(truncated), &u));
    ASSERT_TRUE(!mesh_user_parse(bad_varint, sizeof(bad_varint), &u));
    ASSERT_TRUE(!mesh_user_parse(truncated, sizeof(truncated), NULL));
}

TEST(test_empty_message_is_valid) {
    MeshUser u;
    ASSERT_TRUE(mesh_user_parse((const uint8_t*)"", 0, &u));
    ASSERT_TRUE(!u.has_long_name);
    ASSERT_EQ_INT(u.hw_model, 0);
}

TEST_MAIN_BEGIN()
RUN_TEST(test_parses_all_fields);
RUN_TEST(test_strings_are_terminated);
RUN_TEST(test_absent_names_are_flagged);
RUN_TEST(test_overlong_name_is_truncated_not_rejected);
RUN_TEST(test_non_printable_bytes_are_replaced);
RUN_TEST(test_hw_model_is_field_5_not_4);
RUN_TEST(test_unknown_fields_are_skipped);
RUN_TEST(test_rejects_malformed);
RUN_TEST(test_empty_message_is_valid);
TEST_MAIN_END()
