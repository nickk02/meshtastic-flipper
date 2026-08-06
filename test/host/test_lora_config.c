/* The radio parameters are the single largest silent failure mode in this
 * project: get the frequency or spreading factor wrong and the radio receives
 * nothing at all, with no error to look at. Every value here is derived from
 * the Meshtastic source and checked against an independently known result. */
#include "tinytest.h"

#include "src/radio/lora_config.h"
#include "src/radio/sx126x_regs.h"

TEST(test_hash_is_djb2) {
    /* RadioInterface.cpp:943-952. Seed 5381, hash * 33 + c. */
    ASSERT_EQ_INT(lora_hash(""), 5381);
    ASSERT_EQ_INT(lora_hash("a"), 5381u * 33u + 'a');
    ASSERT_EQ_INT(lora_hash(NULL), 5381);
}

TEST(test_hash_of_longfast) {
    /* Computed independently in Python from the same algorithm. */
    ASSERT_EQ_INT(lora_hash("LongFast"), 130429955u);
}

TEST(test_us_band_has_104_slots) {
    /* 902 to 928 MHz, zero spacing and padding, 250kHz bandwidth.
     * (928 - 902) / 0.25 = 104. */
    uint32_t slots = lora_num_freq_slots(
        LORA_US_FREQ_START_MHZ,
        LORA_US_FREQ_END_MHZ,
        LORA_STD_SPACING_MHZ,
        LORA_STD_PADDING_MHZ,
        LORA_LONGFAST_BW_KHZ);
    ASSERT_EQ_INT(slots, 104);
}

TEST(test_first_slot_is_half_a_bandwidth_above_the_band_edge) {
    uint32_t hz = lora_slot_frequency_hz(
        LORA_US_FREQ_START_MHZ,
        LORA_STD_SPACING_MHZ,
        LORA_STD_PADDING_MHZ,
        LORA_LONGFAST_BW_KHZ,
        0);
    ASSERT_EQ_INT(hz, 902125000u);
}

TEST(test_slots_are_spaced_by_one_bandwidth) {
    uint32_t a = lora_slot_frequency_hz(
        LORA_US_FREQ_START_MHZ, LORA_STD_SPACING_MHZ, LORA_STD_PADDING_MHZ, 250.0f, 0);
    uint32_t b = lora_slot_frequency_hz(
        LORA_US_FREQ_START_MHZ, LORA_STD_SPACING_MHZ, LORA_STD_PADDING_MHZ, 250.0f, 1);
    ASSERT_EQ_INT(b - a, 250000u);
}

/* The load-bearing test. 906.875 MHz is the frequency the Meshtastic community
 * documents for US LongFast, and it is reached here purely by deriving from
 * region and preset constants read out of the firmware source. Two independent
 * routes agreeing is what makes this trustworthy. */
TEST(test_us_longfast_lands_on_906_875_mhz) {
    LoraConfig cfg;
    ASSERT_TRUE(lora_config_us_longfast(LORA_PRIMARY_CHANNEL_NAME, &cfg));

    ASSERT_EQ_INT(cfg.freq_hz, 906875000u);
    ASSERT_EQ_INT(cfg.channel_num, 19); /* channel 20 in the 1-based numbering nodes show */
    ASSERT_EQ_INT(cfg.num_slots, 104);
}

TEST(test_us_longfast_modem_parameters) {
    LoraConfig cfg;
    ASSERT_TRUE(lora_config_us_longfast(LORA_PRIMARY_CHANNEL_NAME, &cfg));

    ASSERT_TRUE(cfg.bw_khz == 250.0f);
    ASSERT_EQ_INT(cfg.sf, 11);
    ASSERT_EQ_INT(cfg.cr, 5);
    ASSERT_EQ_INT(cfg.sync_word, 0x2b);
    ASSERT_EQ_INT(cfg.preamble_length, 16);
}

TEST(test_different_channel_name_picks_a_different_slot) {
    LoraConfig a;
    LoraConfig b;
    lora_config_us_longfast("LongFast", &a);
    lora_config_us_longfast("ShortFast", &b);
    ASSERT_TRUE(a.channel_num != b.channel_num);
    ASSERT_TRUE(b.channel_num < b.num_slots);
}

TEST(test_config_rejects_null) {
    ASSERT_TRUE(!lora_config_us_longfast("LongFast", NULL));
}

/* SX1262 datasheet 13.4.1. Expected values computed independently. */
TEST(test_pll_conversion) {
    ASSERT_EQ_INT(lora_freq_to_pll(906875000u), 0x38AE0000u);
    ASSERT_EQ_INT(lora_freq_to_pll(902125000u), 0x38620000u);
    ASSERT_EQ_INT(lora_freq_to_pll(928000000u), 0x3A000000u);
}

TEST(test_pll_does_not_overflow_32_bits) {
    /* freq << 25 exceeds 32 bits for any real frequency, so the computation
       has to widen. A naive 32 bit version returns nonsense here. */
    uint32_t pll = lora_freq_to_pll(928000000u);
    ASSERT_TRUE(pll > 900000000u);
}

TEST(test_bandwidth_codes) {
    /* SX1262 datasheet 13.4.5.2. Cross-checked against the reference driver,
       which uses 0x04 for 125kHz and 0x06 for 500kHz. */
    ASSERT_EQ_INT(lora_bw_khz_to_code(125.0f), 0x04);
    ASSERT_EQ_INT(lora_bw_khz_to_code(250.0f), 0x05);
    ASSERT_EQ_INT(lora_bw_khz_to_code(500.0f), 0x06);
    ASSERT_EQ_INT(lora_bw_khz_to_code(62.5f), 0x03);
    ASSERT_EQ_INT(lora_bw_khz_to_code(999.0f), 0xFF);
}

/* The trap. The reference driver comments that LDRO is "required for SF11 and
   SF12", which is true only at narrow bandwidths. US LongFast is SF11 at
   250kHz, where the symbol is 8.192ms and LDRO must stay off. Enabling it
   would misconfigure the modem against every real node. */
TEST(test_ldro_is_off_for_us_longfast) {
    ASSERT_TRUE(!lora_ldro_required(11, 250.0f));
}

TEST(test_ldro_follows_symbol_duration_not_spreading_factor) {
    /* 2^SF / BW_kHz milliseconds, threshold 16ms. */
    ASSERT_TRUE(lora_ldro_required(11, 125.0f)); /* 16.384ms, on */
    ASSERT_TRUE(!lora_ldro_required(11, 250.0f)); /* 8.192ms, off */
    ASSERT_TRUE(lora_ldro_required(12, 250.0f)); /* 16.384ms, on */
    ASSERT_TRUE(!lora_ldro_required(12, 500.0f)); /* 8.192ms, off */
    ASSERT_TRUE(!lora_ldro_required(7, 125.0f)); /* 1.024ms, off */
    ASSERT_TRUE(!lora_ldro_required(11, 0.0f)); /* guard against divide by zero */
}

/* The sync word nibble split, sx126x_regs.h.
 *
 * This is the highest-consequence constant in the project. Writing 0x2b
 * directly into register 0x0740 produces a radio that receives nothing at all,
 * with no error and no way to tell it apart from a broken antenna. The split
 * is pure arithmetic, so it can at least be checked here even though the SPI
 * write itself cannot be. */
TEST(test_sync_word_nibble_split) {
    ASSERT_EQ_INT(SX126X_SYNC_MSB(LORA_SYNC_WORD), 0x24);
    ASSERT_EQ_INT(SX126X_SYNC_LSB(LORA_SYNC_WORD), 0xB4);
}

TEST(test_sync_word_split_is_reversible) {
    /* Recovering the original byte proves the halves carry all four nibbles
       of the sync word and none of the control bits. */
    uint8_t msb = SX126X_SYNC_MSB(LORA_SYNC_WORD);
    uint8_t lsb = SX126X_SYNC_LSB(LORA_SYNC_WORD);
    uint8_t recovered = (uint8_t)((msb & 0xF0) | ((lsb & 0xF0) >> 4));
    ASSERT_EQ_INT(recovered, LORA_SYNC_WORD);
}

TEST(test_coding_rate_register_mapping) {
    /* Datasheet 13.4.5.3: register 0x01 to 0x04 mean coding rate 4/5 to 4/8,
       so Meshtastic's cr value minus 4. */
    ASSERT_EQ_INT(SX126X_CR_FROM_MESHTASTIC(5), 0x01);
    ASSERT_EQ_INT(SX126X_CR_FROM_MESHTASTIC(8), 0x04);
}

TEST(test_spreading_factor_is_used_verbatim) {
    /* SetModulationParams takes SF as a plain number, 0x05 to 0x0C, so SF11
       goes on the wire as 0x0B with no conversion. */
    LoraConfig cfg;
    lora_config_us_longfast(LORA_PRIMARY_CHANNEL_NAME, &cfg);
    ASSERT_EQ_INT(cfg.sf, 0x0B);
}

TEST_MAIN_BEGIN()
RUN_TEST(test_hash_is_djb2);
RUN_TEST(test_hash_of_longfast);
RUN_TEST(test_us_band_has_104_slots);
RUN_TEST(test_first_slot_is_half_a_bandwidth_above_the_band_edge);
RUN_TEST(test_slots_are_spaced_by_one_bandwidth);
RUN_TEST(test_us_longfast_lands_on_906_875_mhz);
RUN_TEST(test_us_longfast_modem_parameters);
RUN_TEST(test_different_channel_name_picks_a_different_slot);
RUN_TEST(test_config_rejects_null);
RUN_TEST(test_pll_conversion);
RUN_TEST(test_pll_does_not_overflow_32_bits);
RUN_TEST(test_bandwidth_codes);
RUN_TEST(test_ldro_is_off_for_us_longfast);
RUN_TEST(test_ldro_follows_symbol_duration_not_spreading_factor);
RUN_TEST(test_sync_word_nibble_split);
RUN_TEST(test_sync_word_split_is_reversible);
RUN_TEST(test_coding_rate_register_mapping);
RUN_TEST(test_spreading_factor_is_used_verbatim);
TEST_MAIN_END()
