/* SX1262 driver for the Electronic Cats Flipper Add-on Sub-GHz.
 *
 * UNVERIFIED. Written from the Semtech datasheet and from
 * ElectronicCats/flipper-SX1262-LoRa, but never run against hardware, because
 * the board has not arrived. Treat every function here as a hypothesis. The
 * layers below it, decode and configuration, are host-tested; this is not.
 *
 * Pin map is fixed by the board and was read from its published KiCad project,
 * not from the driver's naming, which is misleading:
 *
 *   MOSI      PA7   header 2
 *   MISO      PA6   header 3
 *   NSS0      PA4   header 4    the CC1101, NOT this radio
 *   SCK       PB3   header 5
 *   DIO1      PC3   header 7
 *   ANT_SW    PB6   header 13   board antenna switch
 *   BUSY      PB7   header 14
 *   NRST      PC1   header 15
 *   NSS1      PC0   header 16   this radio
 */
#ifndef SX126X_H
#define SX126X_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "src/radio/lora_config.h"

typedef struct Sx126x Sx126x;

Sx126x* sx126x_alloc(void);
void sx126x_free(Sx126x* radio);

/* Claim the GPIO and SPI, reset the chip, and confirm it answers.
 *
 * Returns false if GetStatus does not come back plausible, which is the M1
 * acceptance test and also the honest answer to "is a radio actually
 * connected". A caller that ignores this ends up silently receiving nothing
 * forever, which is the failure mode this project keeps trying to avoid. */
bool sx126x_begin(Sx126x* radio);

/* Release GPIO and SPI. Safe to call after a failed begin. */
bool sx126x_end(Sx126x* radio);

/* Raw GetStatus byte. 13.5.1. Exposed for bring-up and the debug view. */
uint8_t sx126x_get_status(Sx126x* radio);

/* Register round trip, for proving the bus before trusting anything else. */
bool sx126x_write_register(Sx126x* radio, uint16_t address, uint8_t value);
bool sx126x_read_register(Sx126x* radio, uint16_t address, uint8_t* value);

/* Apply a LoRa configuration and enter continuous receive. */
bool sx126x_configure_lora(Sx126x* radio, const LoraConfig* config);
bool sx126x_start_rx(Sx126x* radio);

/* Non-blocking. Returns true when a packet was read out.
 *
 * crc_error is set when the radio reported a CRC failure, which is counted
 * separately rather than silently dropped: a stream of CRC errors means the
 * modem is close but wrong, which is a very different diagnosis from hearing
 * nothing at all. */
bool sx126x_poll_rx(
    Sx126x* radio,
    uint8_t* buffer,
    size_t buffer_len,
    size_t* out_len,
    int16_t* rssi,
    int8_t* snr,
    bool* crc_error);

/* Drive the board's antenna switch toward this radio.
 *
 * The reference driver declares this pin and never writes it, so whether it
 * needs driving is genuinely unknown. Exposed so M1 can try both states and
 * find out rather than guess. */
void sx126x_set_antenna_switch(Sx126x* radio, bool to_lora);

#endif
