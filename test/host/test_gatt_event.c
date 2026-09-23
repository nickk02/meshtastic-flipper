/* The byte layout gatt_event_handler reads out of a BLE stack event.
 *
 * meshtastic_service.c is Flipper-only, so this does not run the handler. It
 * builds the bytes the stack would hand it and checks that the structs in
 * meshtastic_gatt_event.h put evt and ecode where the stack puts them:
 *
 *   [0] type   [1] evt 0xFF   [2] plen   [3..4] ecode, little-endian   [5..] data
 *
 * and that the event code filter tells an attribute-modified event (0x0C01)
 * from its neighbour, ACI_GATT_PROC_TIMEOUT (0x0C02, ble_vs_codes.h:62). */
#include <stddef.h>
#include <stdint.h>

#include "tinytest.h"

#include "src/ble/meshtastic_gatt_event.h"

#define HCI_EVENT_PKT_TYPE 0x04 /* tl.h:35, TL_BLEEVT_PKT_TYPE */
#define VENDOR_EVT         0xFF /* ble_std.h:62, HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE */

/* The same filter gatt_event_handler applies before the handle match. */
static int is_attribute_modified(const void* event) {
    const MeshHciEventPacket* packet =
        (const MeshHciEventPacket*)(((const MeshHciUartPacket*)event)->data);
    if(packet->evt != VENDOR_EVT) return 0;
    const MeshBlecoreEvent* blecore = (const MeshBlecoreEvent*)packet->data;
    return blecore->ecode == MESH_ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE;
}

/* A vendor event with the given code and a two-byte payload, written byte by
 * byte in wire order rather than through the structs. */
static void build_vendor_event(uint8_t* buf, uint8_t evt, uint16_t ecode) {
    buf[0] = HCI_EVENT_PKT_TYPE;
    buf[1] = evt;
    buf[2] = 4; /* plen: ecode plus two payload bytes */
    buf[3] = (uint8_t)(ecode & 0xFF);
    buf[4] = (uint8_t)(ecode >> 8);
    buf[5] = 0xAA;
    buf[6] = 0xBB;
}

TEST(test_constant_matches_stack) {
    ASSERT_EQ_INT(MESH_ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE, 0x0C01);
}

TEST(test_struct_offsets) {
    ASSERT_EQ_INT(offsetof(MeshHciUartPacket, data), 1);
    ASSERT_EQ_INT(offsetof(MeshHciEventPacket, evt), 0);
    ASSERT_EQ_INT(offsetof(MeshHciEventPacket, plen), 1);
    ASSERT_EQ_INT(offsetof(MeshHciEventPacket, data), 2);
    ASSERT_EQ_INT(offsetof(MeshBlecoreEvent, ecode), 0);
    ASSERT_EQ_INT(offsetof(MeshBlecoreEvent, data), 2);
}

TEST(test_parser_reads_evt_and_ecode_from_wire_offsets) {
    uint8_t buf[8] = {0};
    build_vendor_event(buf, VENDOR_EVT, 0x0C01);

    const MeshHciUartPacket* uart = (const MeshHciUartPacket*)buf;
    const MeshHciEventPacket* packet = (const MeshHciEventPacket*)uart->data;
    const MeshBlecoreEvent* blecore = (const MeshBlecoreEvent*)packet->data;

    /* evt is byte 1 of the UART packet, ecode starts at byte 3. */
    ASSERT_TRUE((const uint8_t*)packet == buf + 1);
    ASSERT_TRUE((const uint8_t*)blecore == buf + 3);
    ASSERT_TRUE(blecore->data == buf + 5);

    ASSERT_EQ_INT(packet->evt, 0xFF);
    ASSERT_EQ_INT(packet->plen, 4);
    ASSERT_EQ_INT(blecore->ecode, 0x0C01);
    ASSERT_EQ_INT(blecore->data[0], 0xAA);
    ASSERT_EQ_INT(blecore->data[1], 0xBB);
}

TEST(test_attribute_modified_passes) {
    uint8_t buf[8] = {0};
    build_vendor_event(buf, VENDOR_EVT, 0x0C01);
    ASSERT_TRUE(is_attribute_modified(buf));
}

TEST(test_other_vendor_code_rejected) {
    uint8_t buf[8] = {0};
    build_vendor_event(buf, VENDOR_EVT, 0x0C02);
    ASSERT_TRUE(!is_attribute_modified(buf));
}

/* Byte-swapped 0x0C01 is 0x010C. A big-endian read would accept this and
 * reject the real event, so it pins the byte order. */
TEST(test_byte_swapped_code_rejected) {
    uint8_t buf[8] = {0};
    build_vendor_event(buf, VENDOR_EVT, 0x010C);
    ASSERT_TRUE(!is_attribute_modified(buf));
}

TEST(test_non_vendor_event_rejected) {
    uint8_t buf[8] = {0};
    build_vendor_event(buf, 0x0E, 0x0C01); /* ble_std.h:54, command complete */
    ASSERT_TRUE(!is_attribute_modified(buf));
}

TEST_MAIN_BEGIN()
RUN_TEST(test_constant_matches_stack);
RUN_TEST(test_struct_offsets);
RUN_TEST(test_parser_reads_evt_and_ecode_from_wire_offsets);
RUN_TEST(test_attribute_modified_passes);
RUN_TEST(test_other_vendor_code_rejected);
RUN_TEST(test_byte_swapped_code_rejected);
RUN_TEST(test_non_vendor_event_rejected);
TEST_MAIN_END()
