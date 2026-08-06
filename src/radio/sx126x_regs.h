/* SX1262 opcodes, registers and field values.
 *
 * Every value here is traceable to the Semtech SX1262 datasheet or to
 * ElectronicCats/flipper-SX1262-LoRa, which is proven working on this exact
 * board. Nothing is written from memory. Where the reference and the datasheet
 * are both cited, they agree.
 *
 * Section numbers refer to the SX1262 datasheet. */
#ifndef SX126X_REGS_H
#define SX126X_REGS_H

/* Commands. Reference: lora.c opcode comments, cross-checked with section 13. */
#define SX126X_CMD_SET_STANDBY               0x80 /* 13.1.2 */
#define SX126X_CMD_SET_RX                    0x82 /* 13.1.5 */
#define SX126X_CMD_SET_TX                    0x83 /* 13.1.4 */
#define SX126X_CMD_SET_RF_FREQUENCY          0x86 /* 13.4.1 */
#define SX126X_CMD_SET_PACKET_TYPE           0x8A /* 13.4.2 */
#define SX126X_CMD_SET_MODULATION_PARAMS     0x8B /* 13.4.5 */
#define SX126X_CMD_SET_PACKET_PARAMS         0x8C /* 13.4.6 */
#define SX126X_CMD_SET_TX_PARAMS             0x8E /* 13.4.4 */
#define SX126X_CMD_SET_BUFFER_BASE_ADDRESS   0x8F /* 13.4.3 */
#define SX126X_CMD_SET_PA_CONFIG             0x95 /* 13.1.14 */
#define SX126X_CMD_STOP_TIMER_ON_PREAMBLE    0x9F /* 13.4.9 */
#define SX126X_CMD_SET_DIO2_AS_RF_SWITCH     0x9D /* 13.3.5 */
#define SX126X_CMD_SET_LORA_SYMB_NUM_TIMEOUT 0xA0 /* 13.4.10 */
#define SX126X_CMD_SET_DIO_IRQ_PARAMS        0x08 /* 13.3.1 */
#define SX126X_CMD_CLEAR_IRQ_STATUS          0x02 /* 13.3.4 */
#define SX126X_CMD_GET_IRQ_STATUS            0x12 /* 13.3.3 */
#define SX126X_CMD_GET_RX_BUFFER_STATUS      0x13 /* 13.5.2 */
#define SX126X_CMD_GET_PACKET_STATUS         0x14 /* 13.5.3 */
#define SX126X_CMD_GET_STATUS                0xC0 /* 13.5.1 */
#define SX126X_CMD_WRITE_REGISTER            0x0D /* 13.2.1 */
#define SX126X_CMD_READ_REGISTER             0x1D /* 13.2.2 */
#define SX126X_CMD_WRITE_BUFFER              0x0E /* 13.2.3 */
#define SX126X_CMD_READ_BUFFER               0x1E /* 13.2.4 */

/* Standby modes. 13.1.2 */
#define SX126X_STANDBY_RC   0x00
#define SX126X_STANDBY_XOSC 0x01

/* Packet types. 13.4.2 */
#define SX126X_PACKET_TYPE_LORA 0x01

/* LoRa header type. 13.4.6 */
#define SX126X_LORA_HEADER_EXPLICIT 0x00
#define SX126X_LORA_HEADER_IMPLICIT 0x01

/* CRC and IQ. 13.4.6 */
#define SX126X_LORA_CRC_OFF     0x00
#define SX126X_LORA_CRC_ON      0x01
#define SX126X_LORA_IQ_STANDARD 0x00
#define SX126X_LORA_IQ_INVERTED 0x01

/* Coding rate codes. Register 0x01 to 0x04 mean 4/5 to 4/8, so the register
 * value is the Meshtastic coding rate minus 4. 13.4.5.3, and the reference
 * comment at lora.c:243. */
#define SX126X_CR_FROM_MESHTASTIC(cr) ((uint8_t)((cr) - 4))

/* Low data rate optimization. 13.4.5.4 */
#define SX126X_LDRO_OFF 0x00
#define SX126X_LDRO_ON  0x01

/* IRQ bits. 13.3.1 table 13-29 */
#define SX126X_IRQ_TX_DONE           0x0001
#define SX126X_IRQ_RX_DONE           0x0002
#define SX126X_IRQ_PREAMBLE_DETECTED 0x0004
#define SX126X_IRQ_SYNC_WORD_VALID   0x0008
#define SX126X_IRQ_HEADER_VALID      0x0010
#define SX126X_IRQ_HEADER_ERR        0x0020
#define SX126X_IRQ_CRC_ERR           0x0040
#define SX126X_IRQ_CAD_DONE          0x0080
#define SX126X_IRQ_CAD_DETECTED      0x0100
#define SX126X_IRQ_TIMEOUT           0x0200
#define SX126X_IRQ_ALL               0xFFFF

/* Sync word register pair. 13.4.7
 *
 * The register is 16 bits and RadioLib splits a one byte sync word across
 * nibbles before writing it, which the reference reproduces at lora.c:576-586:
 *   msb = (sync & 0xF0) | ((ctrl & 0xF0) >> 4)
 *   lsb = ((sync & 0x0F) << 4) | (ctrl & 0x0F)
 * With Meshtastic's 0x2b and RadioLib's default control bits 0x44 that gives
 * 0x24 and 0xB4.
 *
 * This is the trap that would otherwise produce a radio which hears nothing,
 * silently, with no diagnostic. Writing 0x2b straight into the register does
 * not work. */
#define SX126X_REG_LORA_SYNC_WORD_MSB 0x0740
#define SX126X_REG_LORA_SYNC_WORD_LSB 0x0741
#define SX126X_SYNC_WORD_CONTROL_BITS 0x44
#define SX126X_SYNC_MSB(sync) \
    ((uint8_t)(((sync) & 0xF0) | ((SX126X_SYNC_WORD_CONTROL_BITS & 0xF0) >> 4)))
#define SX126X_SYNC_LSB(sync) \
    ((uint8_t)((((sync) & 0x0F) << 4) | (SX126X_SYNC_WORD_CONTROL_BITS & 0x0F)))

/* Continuous receive. 13.1.5: 0xFFFFFF means stay in RX until told otherwise. */
#define SX126X_RX_CONTINUOUS 0xFFFFFF

#endif
