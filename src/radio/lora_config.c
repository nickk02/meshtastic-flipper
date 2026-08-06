#include "src/radio/lora_config.h"

#include <math.h>

uint32_t lora_hash(const char* str) {
    uint32_t hash = 5381;
    int c;

    if(str == NULL) return hash;

    while((c = *str++) != 0) {
        hash = ((hash << 5) + hash) + (unsigned char)c; /* hash * 33 + c */
    }
    return hash;
}

static float slot_width_mhz(float spacing_mhz, float padding_mhz, float bw_khz) {
    return spacing_mhz + (padding_mhz * 2.0f) + (bw_khz / 1000.0f);
}

uint32_t lora_num_freq_slots(
    float freq_start_mhz,
    float freq_end_mhz,
    float spacing_mhz,
    float padding_mhz,
    float bw_khz) {
    float width = slot_width_mhz(spacing_mhz, padding_mhz, bw_khz);
    if(width <= 0.0f) return 0;
    return (uint32_t)lroundf((freq_end_mhz - freq_start_mhz + spacing_mhz) / width);
}

uint32_t lora_slot_frequency_hz(
    float freq_start_mhz,
    float spacing_mhz,
    float padding_mhz,
    float bw_khz,
    uint32_t slot) {
    /* Computed in kHz rather than Hz on purpose.
     *
     * A float carries 24 bits of mantissa, so it cannot represent 906875000
     * exactly; the nearest float is tens of hertz away and truncating it gives
     * the wrong answer. Frequencies in kHz stay well inside the range floats
     * represent exactly, so the arithmetic is done there and scaled up as an
     * integer. Using double instead would be simpler but trips
     * -Wdouble-promotion, and doubles are software-emulated on this part.
     *
     * The cost is rounding to the nearest kHz. Against a 250kHz channel
     * bandwidth that is irrelevant, and for every region profile this project
     * supports the padding is zero, so nothing is lost at all. */
    float width_khz = slot_width_mhz(spacing_mhz, padding_mhz, bw_khz) * 1000.0f;
    float khz = (freq_start_mhz * 1000.0f) + (bw_khz / 2.0f) + (padding_mhz * 1000.0f) +
                ((float)slot * width_khz);

    return (uint32_t)lroundf(khz) * 1000u;
}

bool lora_config_us_longfast(const char* channel_name, LoraConfig* out) {
    if(out == NULL) return false;

    out->bw_khz = LORA_LONGFAST_BW_KHZ;
    out->sf = LORA_LONGFAST_SF;
    out->cr = LORA_LONGFAST_CR;
    out->sync_word = LORA_SYNC_WORD;
    out->preamble_length = LORA_PREAMBLE_LENGTH;

    out->num_slots = lora_num_freq_slots(
        LORA_US_FREQ_START_MHZ,
        LORA_US_FREQ_END_MHZ,
        LORA_STD_SPACING_MHZ,
        LORA_STD_PADDING_MHZ,
        LORA_LONGFAST_BW_KHZ);

    /* US has overrideSlot 0, so the slot comes from the channel name hash.
     * RadioInterface.cpp:1341-1343. */
    out->channel_num = out->num_slots ? (lora_hash(channel_name) % out->num_slots) : 0;

    out->freq_hz = lora_slot_frequency_hz(
        LORA_US_FREQ_START_MHZ,
        LORA_STD_SPACING_MHZ,
        LORA_STD_PADDING_MHZ,
        LORA_LONGFAST_BW_KHZ,
        out->channel_num);

    return true;
}

uint32_t lora_freq_to_pll(uint32_t freq_hz) {
    /* SX1262 datasheet 13.4.1 SetRfFrequency. F_XTAL is 32 MHz and the PLL
     * step is F_XTAL / 2^25, so the register value is freq * 2^25 / F_XTAL.
     * Computed in 64 bits because freq * 2^25 overflows 32. */
    return (uint32_t)(((uint64_t)freq_hz << 25) / 32000000u);
}

uint8_t lora_bw_khz_to_code(float bw_khz) {
    /* SX1262 datasheet 13.4.5.2, LoRa bandwidth settings. */
    if(bw_khz == 7.81f) return 0x00;
    if(bw_khz == 10.42f) return 0x08;
    if(bw_khz == 15.63f) return 0x01;
    if(bw_khz == 20.83f) return 0x09;
    if(bw_khz == 31.25f) return 0x02;
    if(bw_khz == 41.67f) return 0x0A;
    if(bw_khz == 62.5f) return 0x03;
    if(bw_khz == 125.0f) return 0x04;
    if(bw_khz == 250.0f) return 0x05;
    if(bw_khz == 500.0f) return 0x06;
    return 0xFF;
}

bool lora_ldro_required(uint8_t sf, float bw_khz) {
    if(bw_khz <= 0.0f) return false;
    /* Symbol duration in milliseconds is 2^SF / BW_kHz. */
    float symbol_ms = (float)((uint32_t)1 << sf) / bw_khz;
    return symbol_ms >= 16.0f;
}
