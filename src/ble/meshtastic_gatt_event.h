/* The layout of a BLE stack event as the GATT event handler receives it.
 *
 * The SDK does not expose the stack's packet structs to applications. Its own
 * comment in event_dispatcher.h says so: "Using other types so not to leak all
 * the BLE stack headers". The three structs below are copied from the STM32WB
 * BLE stack, where they are hci_uart_pckt, hci_event_pckt and evt_blecore_aci
 * (stm32wb_copro wpan/interface/patterns/ble_thread/tl/tl.h:244-270, itself
 * marked "copied from ble_legacy.h"), and must stay byte-compatible with them.
 *
 * No Flipper headers, so the layout is host-tested in test_gatt_event.c. */
#ifndef MESHTASTIC_GATT_EVENT_H
#define MESHTASTIC_GATT_EVENT_H

#include <stdint.h>

/* Vendor event code of ACI_GATT_ATTRIBUTE_MODIFIED_EVENT, the event a GATT
 * client write produces. The FAP SDK does not carry it. The value is from the
 * STM32WB BLE stack, stm32wb_copro wpan/ble/core/auto/ble_vs_codes.h:59
 * ("#define ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE 0x0C01U"), at the commit the
 * Flipper firmware pins as its lib/stm32wb_copro submodule. The firmware's own
 * serial service filters on the same constant before its handle match, at
 * targets/f7/ble_glue/services/serial_service.c:82. */
#define MESH_ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE 0x0C01U

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t data[1];
} MeshHciUartPacket;

typedef struct __attribute__((packed)) {
    uint8_t evt;
    uint8_t plen;
    uint8_t data[1];
} MeshHciEventPacket;

typedef struct __attribute__((packed)) {
    uint16_t ecode;
    uint8_t data[1];
} MeshBlecoreEvent;

#endif
