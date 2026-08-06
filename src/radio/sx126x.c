#include "src/radio/sx126x.h"

#include <furi.h>
#include <furi_hal_gpio.h>
#include <furi_hal_resources.h>
#include <furi_hal_spi.h>
#include <furi_hal_spi_types.h>
#include <string.h>

#include "src/radio/sx126x_regs.h"

#define TAG "SX126x"

/* furi_hal_spi_bus_handle_external is const and its CS is PA4, which on this
 * board is the CC1101. The reference driver solves that by copying the handle
 * and overriding the CS pin (lora.c:1014-1019); the same trick is used here. */
static const GpioPin* const pin_nss_lora = &gpio_ext_pc0; /* header 16 */
static const GpioPin* const pin_nss_cc1101 = &gpio_ext_pa4; /* header 4 */
static const GpioPin* const pin_reset = &gpio_ext_pc1; /* header 15 */
static const GpioPin* const pin_busy = &gpio_usart_rx; /* PB7, header 14 */
static const GpioPin* const pin_dio1 = &gpio_ext_pc3; /* header 7 */
static const GpioPin* const pin_ant_sw = &gpio_usart_tx; /* PB6, header 13 */

#define SPI_TIMEOUT_MS  1000
#define BUSY_TIMEOUT_MS 100

struct Sx126x {
    FuriHalSpiBusHandle spi;
    bool started;
};

/* BUSY high means the chip is still digesting the previous command. Every
 * command has to wait for it to fall first. Datasheet 8.3. */
static bool wait_not_busy(void) {
    uint32_t waited = 0;
    while(furi_hal_gpio_read(pin_busy)) {
        if(waited >= BUSY_TIMEOUT_MS) {
            FURI_LOG_E(TAG, "BUSY stuck high for %lums", (unsigned long)waited);
            return false;
        }
        furi_delay_ms(1);
        waited++;
    }
    return true;
}

static void select(Sx126x* radio) {
    UNUSED(radio);
    furi_hal_gpio_write(pin_nss_lora, false);
}

static void deselect(Sx126x* radio) {
    UNUSED(radio);
    furi_hal_gpio_write(pin_nss_lora, true);
}

static bool command(Sx126x* radio, const uint8_t* tx, size_t len) {
    if(!wait_not_busy()) return false;

    select(radio);
    furi_hal_spi_acquire(&radio->spi);
    bool ok = furi_hal_spi_bus_tx(&radio->spi, (uint8_t*)tx, len, SPI_TIMEOUT_MS);
    furi_hal_spi_release(&radio->spi);
    deselect(radio);

    if(!ok) FURI_LOG_E(TAG, "SPI tx failed for opcode 0x%02x", tx[0]);
    return ok;
}

/* Write the prologue, then read the reply in the same chip select window. The
 * SX1262 returns status bytes in place of the bytes clocked out. */
static bool
    command_read(Sx126x* radio, const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len) {
    if(!wait_not_busy()) return false;

    select(radio);
    furi_hal_spi_acquire(&radio->spi);
    bool ok = furi_hal_spi_bus_tx(&radio->spi, (uint8_t*)tx, tx_len, SPI_TIMEOUT_MS);
    if(ok && rx_len > 0) {
        memset(rx, 0x00, rx_len);
        ok = furi_hal_spi_bus_rx(&radio->spi, rx, rx_len, SPI_TIMEOUT_MS);
    }
    furi_hal_spi_release(&radio->spi);
    deselect(radio);

    if(!ok) FURI_LOG_E(TAG, "SPI trx failed for opcode 0x%02x", tx[0]);
    return ok;
}

Sx126x* sx126x_alloc(void) {
    Sx126x* radio = malloc(sizeof(Sx126x));
    memset(radio, 0, sizeof(Sx126x));

    radio->spi = *(FuriHalSpiBusHandle*)&furi_hal_spi_bus_handle_external;
    radio->spi.cs = pin_nss_lora;
    return radio;
}

void sx126x_free(Sx126x* radio) {
    if(radio == NULL) return;
    if(radio->started) sx126x_end(radio);
    free(radio);
}

void sx126x_set_antenna_switch(Sx126x* radio, bool to_lora) {
    UNUSED(radio);
    furi_hal_gpio_write(pin_ant_sw, to_lora);
}

bool sx126x_begin(Sx126x* radio) {
    uint8_t status = 0;

    furi_hal_gpio_init_simple(pin_nss_lora, GpioModeOutputPushPull);
    furi_hal_gpio_init_simple(pin_nss_cc1101, GpioModeOutputPushPull);
    furi_hal_gpio_init_simple(pin_reset, GpioModeOutputPushPull);
    furi_hal_gpio_init_simple(pin_ant_sw, GpioModeOutputPushPull);
    furi_hal_gpio_init_simple(pin_busy, GpioModeInput);
    furi_hal_gpio_init_simple(pin_dio1, GpioModeInput);

    /* Park the CC1101's chip select high so it stays off the bus while we use
     * it. The reference drives this low, which would leave the other chip
     * selected; that looks like a bug in their code rather than a requirement. */
    furi_hal_gpio_write(pin_nss_cc1101, true);
    deselect(radio);

    /* Point the board's antenna switch at this radio. Whether this is needed
     * is unknown, see the header. Defaulting to true is the interpretation
     * that at least makes the intent explicit. */
    sx126x_set_antenna_switch(radio, true);

    furi_hal_spi_bus_handle_init(&radio->spi);
    radio->started = true;

    /* Hardware reset. Datasheet 8.1: hold NRST low, then let the chip boot. */
    furi_hal_gpio_write(pin_reset, false);
    furi_delay_ms(2);
    furi_hal_gpio_write(pin_reset, true);
    furi_delay_ms(20);

    if(!wait_not_busy()) {
        FURI_LOG_E(TAG, "no radio: BUSY never went low after reset");
        return false;
    }

    uint8_t standby[2] = {SX126X_CMD_SET_STANDBY, SX126X_STANDBY_RC};
    if(!command(radio, standby, sizeof(standby))) return false;

    status = sx126x_get_status(radio);

    /* An absent or dead chip reads back as all ones or all zeros because MISO
     * is simply floating or held. Anything else means something answered. */
    if(status == 0x00 || status == 0xFF) {
        FURI_LOG_E(TAG, "no radio: GetStatus returned 0x%02x", status);
        return false;
    }

    FURI_LOG_I(TAG, "radio present, status 0x%02x", status);
    return true;
}

bool sx126x_end(Sx126x* radio) {
    if(radio == NULL || !radio->started) return false;

    furi_hal_spi_bus_handle_deinit(&radio->spi);
    furi_hal_gpio_init_simple(pin_nss_lora, GpioModeAnalog);
    furi_hal_gpio_init_simple(pin_reset, GpioModeAnalog);
    furi_hal_gpio_init_simple(pin_ant_sw, GpioModeAnalog);
    furi_hal_gpio_init_simple(pin_busy, GpioModeAnalog);
    furi_hal_gpio_init_simple(pin_dio1, GpioModeAnalog);
    radio->started = false;
    return true;
}

uint8_t sx126x_get_status(Sx126x* radio) {
    uint8_t tx[1] = {SX126X_CMD_GET_STATUS};
    uint8_t rx[1] = {0};

    if(!command_read(radio, tx, sizeof(tx), rx, sizeof(rx))) return 0xFF;
    return rx[0];
}

bool sx126x_write_register(Sx126x* radio, uint16_t address, uint8_t value) {
    uint8_t tx[4] = {
        SX126X_CMD_WRITE_REGISTER,
        (uint8_t)(address >> 8),
        (uint8_t)(address & 0xFF),
        value,
    };
    return command(radio, tx, sizeof(tx));
}

bool sx126x_read_register(Sx126x* radio, uint16_t address, uint8_t* value) {
    /* 13.2.2: opcode, address high, address low, then one NOP before the data
     * byte appears. */
    uint8_t tx[4] = {
        SX126X_CMD_READ_REGISTER,
        (uint8_t)(address >> 8),
        (uint8_t)(address & 0xFF),
        0x00,
    };
    uint8_t rx[1] = {0};

    if(!command_read(radio, tx, sizeof(tx), rx, sizeof(rx))) return false;
    *value = rx[0];
    return true;
}

bool sx126x_configure_lora(Sx126x* radio, const LoraConfig* config) {
    uint8_t bw_code = lora_bw_khz_to_code(config->bw_khz);
    if(bw_code == 0xFF) {
        FURI_LOG_E(TAG, "unsupported bandwidth");
        return false;
    }

    uint8_t standby[2] = {SX126X_CMD_SET_STANDBY, SX126X_STANDBY_RC};
    if(!command(radio, standby, sizeof(standby))) return false;

    uint8_t packet_type[2] = {SX126X_CMD_SET_PACKET_TYPE, SX126X_PACKET_TYPE_LORA};
    if(!command(radio, packet_type, sizeof(packet_type))) return false;

    /* DIO2 drives the SX1262's own transmit and receive switch, which is why
     * no external TXEN and RXEN lines are needed. 13.3.5, lora.c:348. */
    uint8_t dio2_rf[2] = {SX126X_CMD_SET_DIO2_AS_RF_SWITCH, 0x01};
    if(!command(radio, dio2_rf, sizeof(dio2_rf))) return false;

    uint32_t pll = lora_freq_to_pll(config->freq_hz);
    uint8_t freq[5] = {
        SX126X_CMD_SET_RF_FREQUENCY,
        (uint8_t)(pll >> 24),
        (uint8_t)(pll >> 16),
        (uint8_t)(pll >> 8),
        (uint8_t)(pll),
    };
    if(!command(radio, freq, sizeof(freq))) return false;

    uint8_t modulation[5] = {
        SX126X_CMD_SET_MODULATION_PARAMS,
        config->sf,
        bw_code,
        SX126X_CR_FROM_MESHTASTIC(config->cr),
        lora_ldro_required(config->sf, config->bw_khz) ? SX126X_LDRO_ON : SX126X_LDRO_OFF,
    };
    if(!command(radio, modulation, sizeof(modulation))) return false;

    /* 13.4.6. Explicit header, CRC on and standard IQ are what Meshtastic
     * uses. Payload length is only meaningful for implicit headers, but the
     * field still has to be sent. */
    uint8_t packet[7] = {
        SX126X_CMD_SET_PACKET_PARAMS,
        (uint8_t)(config->preamble_length >> 8),
        (uint8_t)(config->preamble_length & 0xFF),
        SX126X_LORA_HEADER_EXPLICIT,
        0xFF,
        SX126X_LORA_CRC_ON,
        SX126X_LORA_IQ_STANDARD,
    };
    if(!command(radio, packet, sizeof(packet))) return false;

    uint8_t base[3] = {SX126X_CMD_SET_BUFFER_BASE_ADDRESS, 0x00, 0x00};
    if(!command(radio, base, sizeof(base))) return false;

    /* The nibble split, see sx126x_regs.h. Getting this wrong is silent. */
    if(!sx126x_write_register(
           radio, SX126X_REG_LORA_SYNC_WORD_MSB, SX126X_SYNC_MSB(config->sync_word))) {
        return false;
    }
    if(!sx126x_write_register(
           radio, SX126X_REG_LORA_SYNC_WORD_LSB, SX126X_SYNC_LSB(config->sync_word))) {
        return false;
    }

    /* Report RxDone, CRC errors and header errors on DIO1. CRC errors are
     * wanted rather than filtered: they distinguish "nearly working" from
     * "hearing nothing". */
    uint16_t irq_mask = SX126X_IRQ_RX_DONE | SX126X_IRQ_CRC_ERR | SX126X_IRQ_HEADER_ERR;
    uint8_t irq[9] = {
        SX126X_CMD_SET_DIO_IRQ_PARAMS,
        (uint8_t)(irq_mask >> 8),
        (uint8_t)(irq_mask & 0xFF),
        (uint8_t)(irq_mask >> 8),
        (uint8_t)(irq_mask & 0xFF),
        0x00,
        0x00,
        0x00,
        0x00,
    };
    if(!command(radio, irq, sizeof(irq))) return false;

    uint8_t clear[3] = {
        SX126X_CMD_CLEAR_IRQ_STATUS,
        (uint8_t)(SX126X_IRQ_ALL >> 8),
        (uint8_t)(SX126X_IRQ_ALL & 0xFF),
    };
    return command(radio, clear, sizeof(clear));
}

bool sx126x_start_rx(Sx126x* radio) {
    uint8_t rx[4] = {
        SX126X_CMD_SET_RX,
        (uint8_t)(SX126X_RX_CONTINUOUS >> 16),
        (uint8_t)(SX126X_RX_CONTINUOUS >> 8),
        (uint8_t)(SX126X_RX_CONTINUOUS),
    };
    return command(radio, rx, sizeof(rx));
}

static bool get_irq_status(Sx126x* radio, uint16_t* out) {
    uint8_t tx[1] = {SX126X_CMD_GET_IRQ_STATUS};
    uint8_t rx[3] = {0};

    if(!command_read(radio, tx, sizeof(tx), rx, sizeof(rx))) return false;
    /* rx[0] is the status byte, then the 16 bit IRQ word. 13.3.3 */
    *out = (uint16_t)((rx[1] << 8) | rx[2]);
    return true;
}

static bool clear_irq(Sx126x* radio, uint16_t mask) {
    uint8_t tx[3] = {
        SX126X_CMD_CLEAR_IRQ_STATUS,
        (uint8_t)(mask >> 8),
        (uint8_t)(mask & 0xFF),
    };
    return command(radio, tx, sizeof(tx));
}

bool sx126x_poll_rx(
    Sx126x* radio,
    uint8_t* buffer,
    size_t buffer_len,
    size_t* out_len,
    int16_t* rssi,
    int8_t* snr,
    bool* crc_error) {
    uint16_t irq = 0;

    *crc_error = false;
    *out_len = 0;

    if(!get_irq_status(radio, &irq)) return false;
    if((irq & (SX126X_IRQ_RX_DONE | SX126X_IRQ_CRC_ERR | SX126X_IRQ_HEADER_ERR)) == 0) {
        return false;
    }

    if(irq & (SX126X_IRQ_CRC_ERR | SX126X_IRQ_HEADER_ERR)) {
        *crc_error = true;
        clear_irq(radio, SX126X_IRQ_ALL);
        return false;
    }

    /* 13.5.3 GetPacketStatus. Conversions per the reference at lora.c:869-872:
     * RSSI is -RssiPkt/2 dBm and SNR is a signed byte in quarter dB. */
    uint8_t status_tx[1] = {SX126X_CMD_GET_PACKET_STATUS};
    uint8_t status_rx[4] = {0};
    if(command_read(radio, status_tx, sizeof(status_tx), status_rx, sizeof(status_rx))) {
        *rssi = (int16_t)(-((int)status_rx[1]) / 2);
        *snr = (int8_t)(((int8_t)status_rx[2]) / 4);
    } else {
        *rssi = 0;
        *snr = 0;
    }

    /* 13.5.2 GetRxBufferStatus gives the length and where in the radio's
     * buffer the packet starts. */
    uint8_t buf_tx[1] = {SX126X_CMD_GET_RX_BUFFER_STATUS};
    uint8_t buf_rx[3] = {0};
    if(!command_read(radio, buf_tx, sizeof(buf_tx), buf_rx, sizeof(buf_rx))) {
        clear_irq(radio, SX126X_IRQ_ALL);
        return false;
    }

    size_t length = buf_rx[1];
    uint8_t offset = buf_rx[2];
    if(length > buffer_len) length = buffer_len;
    if(length == 0) {
        clear_irq(radio, SX126X_IRQ_ALL);
        return false;
    }

    /* 13.2.4 ReadBuffer: opcode, offset, then one NOP before the payload. */
    uint8_t read_tx[3] = {SX126X_CMD_READ_BUFFER, offset, 0x00};
    if(!command_read(radio, read_tx, sizeof(read_tx), buffer, length)) {
        clear_irq(radio, SX126X_IRQ_ALL);
        return false;
    }

    *out_len = length;
    clear_irq(radio, SX126X_IRQ_ALL);
    return true;
}
