/* LoRa parameters for the US LongFast channel.
 *
 * Pure arithmetic, no Flipper dependencies, so it is host-tested. That matters
 * more than it looks: a wrong frequency or spreading factor produces a radio
 * that receives absolutely nothing, with no error and nothing to debug. Being
 * able to check this on a PC removes the largest silent failure mode in M2.
 *
 * Every constant traces to the Meshtastic source or the Semtech datasheet. */
#ifndef LORA_CONFIG_H
#define LORA_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* US region. RadioInterface.cpp:112
 *   RDEF(US, 902.0f, 928.0f, 100, 30, false, false, PROFILE_STD, LONG_FAST, 0)
 * The 100 is duty cycle percent and the 30 is the dBm power limit. */
#define LORA_US_FREQ_START_MHZ 902.0f
#define LORA_US_FREQ_END_MHZ   928.0f

/* PROFILE_STD. RadioInterface.cpp:88, spacing and padding both zero. */
#define LORA_STD_SPACING_MHZ 0.0f
#define LORA_STD_PADDING_MHZ 0.0f

/* LONG_FAST with wideLora false. MeshRadio.h:282-287, and the named defaults
 * at MeshRadio.h:99, :104 and :106. */
#define LORA_LONGFAST_BW_KHZ 250.0f
#define LORA_LONGFAST_SF     11
#define LORA_LONGFAST_CR     5 /* coding rate 4/5 */

/* RadioLibInterface.h:84. Note this is the Meshtastic-level value; the SX1262
 * register pair is derived from it, see sx126x.h. */
#define LORA_SYNC_WORD 0x2b

/* RadioInterface.h:96, longer than the 8 default to increase sleep time while
 * receiving. */
#define LORA_PREAMBLE_LENGTH 16

/* The default primary channel carries an empty name, and Meshtastic then uses
 * the preset display name for both the frequency slot hash and the channel
 * hash. */
#define LORA_PRIMARY_CHANNEL_NAME "LongFast"

typedef struct {
    uint32_t freq_hz;
    uint32_t channel_num; /* zero based slot; add 1 for the number nodes show */
    uint32_t num_slots;
    float bw_khz;
    uint8_t sf;
    uint8_t cr;
    uint8_t sync_word;
    uint16_t preamble_length;
} LoraConfig;

/* djb2. RadioInterface.cpp:943-952. Used for the frequency slot, and is a
 * different function from the XOR fold used for the channel hash. */
uint32_t lora_hash(const char* str);

/* Number of frequency slots in a band. RadioInterface.cpp:1307-1308. */
uint32_t lora_num_freq_slots(
    float freq_start_mhz,
    float freq_end_mhz,
    float spacing_mhz,
    float padding_mhz,
    float bw_khz);

/* Centre frequency of a slot, in Hz. RadioInterface.cpp:1348. */
uint32_t lora_slot_frequency_hz(
    float freq_start_mhz,
    float spacing_mhz,
    float padding_mhz,
    float bw_khz,
    uint32_t slot);

/* Fill out with the US LongFast configuration for the given channel name.
 * Returns false only if out is NULL. */
bool lora_config_us_longfast(const char* channel_name, LoraConfig* out);

/* Convert Hz to the SX1262 SetRfFrequency argument.
 * Datasheet 13.4.1: RF = freq * 2^25 / F_XTAL, with F_XTAL 32 MHz. */
uint32_t lora_freq_to_pll(uint32_t freq_hz);

/* Bandwidth in kHz to the SX1262 SetModulationParams code.
 * Datasheet 13.4.5.2 table. Returns 0xFF for an unsupported bandwidth. */
uint8_t lora_bw_khz_to_code(float bw_khz);

/* Whether low data rate optimization must be enabled.
 *
 * RadioLib enables it when the symbol duration reaches 16ms, where symbol
 * duration in ms is 2^SF / BW_kHz. Meshtastic uses RadioLib's automatic
 * behaviour, so matching that rule is what makes our modem agree with a real
 * node.
 *
 * Worth stating plainly because the obvious shortcut is wrong: LDRO is not
 * simply "on for SF11 and SF12". At SF11 with 250kHz bandwidth the symbol is
 * 8.192ms, so it stays off. US LongFast is exactly that case. */
bool lora_ldro_required(uint8_t sf, float bw_khz);

#endif
