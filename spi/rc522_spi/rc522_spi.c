/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/spi.h"

/* Example code to talk to an RFID-RC522 RFID reader/writer via SPI.

   NOTE: Ensure the device is capable of being driven at 3.3v NOT 5v. The Pico
   GPIO (and therefore SPI) cannot be used at 5v.

   You will need to use a level shifter on the SPI lines if you want to run the
   board at 5v.

   Connections on Raspberry Pi Pico W board and a generic RC522 board:

   GPIO 4  -> MISO on RC522 board
   GPIO 5  -> SDA/NSS (Chip select) on RC522 board
   GPIO 6  -> SCK on RC522 board
   GPIO 7  -> MOSI on RC522 board
   GPIO 22 -> RST on RC522 board
   3.3v    -> 3.3V on RC522 board
   GND     -> GND on RC522 board

   Note: SPI devices can have a number of different naming schemes for pins. See
   the Wikipedia page at https://en.wikipedia.org/wiki/Serial_Peripheral_Interface
   for variations.

   This code implements basic RFID card detection and reading functionality.
   For more advanced features, refer to the RC522 datasheet.
*/

#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  6
#define PIN_MOSI 7
#define PIN_RST  22

#define SPI_PORT spi0

// RC522 Register definitions
#define RC522_REG_VERSION      0x37
#define RC522_REG_COMMAND      0x01
#define RC522_REG_COM_I_EN     0x02
#define RC522_REG_DIV_IRQ      0x03
#define RC522_REG_COM_IRQ      0x04
#define RC522_REG_DIV_EN       0x05
#define RC522_REG_STATUS1      0x07
#define RC522_REG_STATUS2      0x08
#define RC522_REG_FIFO_DATA    0x09
#define RC522_REG_FIFO_LEVEL   0x0A
#define RC522_REG_CONTROL      0x0C
#define RC522_REG_ERROR        0x06
#define RC522_REG_BIT_FRAMING  0x0D
#define RC522_REG_MODE         0x11
#define RC522_REG_TX_CONTROL   0x14
#define RC522_REG_TX_AUTO      0x15
#define RC522_REG_MIFARE_KEY   0x24
#define RC522_REG_T_MODE       0x2A
#define RC522_REG_T_PRESCALER  0x2B
#define RC522_REG_T_RELOAD_H   0x2C
#define RC522_REG_T_RELOAD_L   0x2D
#define RC522_REG_RF_CFG       0x26

// RC522 Commands
#define RC522_CMD_IDLE         0x00
#define RC522_CMD_MEM           0x01
#define RC522_CMD_GEN_RAND_ID  0x02
#define RC522_CMD_CALC_CRC     0x03
#define RC522_CMD_TRANSMIT     0x04
#define RC522_CMD_NO_CMD_CHANGE 0x07
#define RC522_CMD_RECEIVE      0x08
#define RC522_CMD_TRANSCEIVE   0x0C
#define RC522_CMD_MF_AUTH      0x0E
#define RC522_CMD_SOFT_RESET   0x0F

// MIFARE Commands
#define MIFARE_CMD_REQA        0x26
#define MIFARE_CMD_WUPA        0x52
#define MIFARE_CMD_SELECT      0x93
#define MIFARE_CMD_AUTH_KEY_A  0x60
#define MIFARE_CMD_AUTH_KEY_B  0x61
#define MIFARE_CMD_READ        0x30
#define MIFARE_CMD_WRITE       0xA0

#define READ_BIT 0x80
#define RC522_MAX_LEN 16

static inline void cs_select() {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_CS, 0);  // Active low
    asm volatile("nop \n nop \n nop");
}

static inline void cs_deselect() {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_CS, 1);
    asm volatile("nop \n nop \n nop");
}

static inline void rc522_reset() {
    gpio_put(PIN_RST, 0);
    sleep_ms(10);
    gpio_put(PIN_RST, 1);
    sleep_ms(10);
}
static uint8_t write_register(uint8_t reg, uint8_t data) {
    uint8_t buf[2];
    buf[0] = (reg << 1) & 0x7E;  // RC522 uses bit 0 for read/write
    buf[1] = data;
    cs_select();
    spi_write_blocking(SPI_PORT, buf, 2);
    cs_deselect();
    return data;
}

static uint8_t read_register(uint8_t reg) {
    uint8_t buf[2];
    buf[0] = ((reg << 1) & 0x7E) | READ_BIT;
    buf[1] = 0;
    cs_select();
    spi_write_read_blocking(SPI_PORT, buf, buf, 2);
    cs_deselect();
    return buf[1];
}

static void write_command(uint8_t cmd) {
    write_register(RC522_REG_COMMAND, cmd);
}

static void clear_bit_mask(uint8_t reg, uint8_t mask) {
    uint8_t tmp = read_register(reg);
    write_register(reg, tmp & ~mask);
}

static void set_bit_mask(uint8_t reg, uint8_t mask) {
    uint8_t tmp = read_register(reg);
    write_register(reg, tmp | mask);
}

static uint8_t rc522_to_card(uint8_t cmd, uint8_t *send_data, uint8_t send_len,
                             uint8_t *back_data, uint8_t *back_len);

static void rc522_init() {
    // Hardware reset
    rc522_reset();
    
    // Soft reset
    write_register(RC522_REG_COMMAND, RC522_CMD_SOFT_RESET);
    sleep_ms(50);

    // Timer: TPrescaler * TReloadVal / 6.78MHz = 24ms
    write_register(RC522_REG_T_MODE, 0x8D);
    write_register(RC522_REG_T_PRESCALER, 0x3E);
    write_register(RC522_REG_T_RELOAD_L, 30);
    write_register(RC522_REG_T_RELOAD_H, 0);

    // 106kBaud
    write_register(RC522_REG_TX_AUTO, 0x40);
    write_register(RC522_REG_MODE, 0x3D);

    // Enable antenna
    set_bit_mask(RC522_REG_TX_CONTROL, 0x03);
}

static uint8_t rc522_request(uint8_t req_mode, uint8_t *tag_type) {
    uint8_t status;
    uint8_t back_bits;
    uint8_t buf[2];

    write_register(RC522_REG_BIT_FRAMING, 0x07);
    buf[0] = req_mode;
    status = rc522_to_card(RC522_CMD_TRANSCEIVE, buf, 1, buf, &back_bits);

    if ((status != 0) || (back_bits != 0x10)) {
        status = 1;
    }

    if (status == 0) {
        *tag_type = buf[0];
    }

    return status;
}

static uint8_t rc522_to_card(uint8_t cmd, uint8_t *send_data, uint8_t send_len,
                             uint8_t *back_data, uint8_t *back_len) {
    uint8_t status = 2;
    uint8_t irq_en = 0x00;
    uint8_t wait_irq = 0x00;
    uint8_t last_bits;
    uint8_t n;
    uint8_t i;
    uint16_t timeout;

    if (cmd == RC522_CMD_MF_AUTH) {
        irq_en = 0x12;
        wait_irq = 0x10;
    }
    if (cmd == RC522_CMD_TRANSCEIVE) {
        irq_en = 0x77;
        wait_irq = 0x30;
    }

    write_register(RC522_REG_COM_I_EN, irq_en | 0x80);
    clear_bit_mask(RC522_REG_COM_IRQ, 0x80);
    set_bit_mask(RC522_REG_FIFO_LEVEL, 0x80);
    write_register(RC522_REG_COMMAND, RC522_CMD_IDLE);

    // Writing data to the FIFO
    for (i = 0; i < send_len; i++) {
        write_register(RC522_REG_FIFO_DATA, send_data[i]);
    }

    // Execute the command
    write_register(RC522_REG_COMMAND, cmd);
    if (cmd == RC522_CMD_TRANSCEIVE) {
        set_bit_mask(RC522_REG_BIT_FRAMING, 0x80);
    }

    // Waiting to receive data
    timeout = 2000;
    do {
        n = read_register(RC522_REG_COM_IRQ);
        timeout--;
    } while ((timeout != 0) && !(n & 0x01) && !(n & wait_irq));

    clear_bit_mask(RC522_REG_BIT_FRAMING, 0x80);

    if (timeout != 0) {
        if (!(read_register(RC522_REG_ERROR) & 0x1B)) {
            status = 0;
            if (n & irq_en & 0x01) {
                status = 1;
            }

            if (cmd == RC522_CMD_TRANSCEIVE) {
                n = read_register(RC522_REG_FIFO_LEVEL);
                last_bits = read_register(RC522_REG_CONTROL) & 0x07;
                if (last_bits) {
                    *back_len = (n - 1) * 8 + last_bits;
                } else {
                    *back_len = n * 8;
                }

                if (n == 0) {
                    n = 1;
                }
                if (n > RC522_MAX_LEN) {
                    n = RC522_MAX_LEN;
                }

                // Reading the received data in FIFO
                for (i = 0; i < n; i++) {
                    back_data[i] = read_register(RC522_REG_FIFO_DATA);
                }
            }
        } else {
            status = 2;
        }
    }

    return status;
}

static uint8_t rc522_anticoll(uint8_t *ser_num) {
    uint8_t status;
    uint8_t i;
    uint8_t ser_num_check = 0;
    uint8_t buf[RC522_MAX_LEN];

    write_register(RC522_REG_BIT_FRAMING, 0x00);
    buf[0] = MIFARE_CMD_SELECT;
    buf[1] = 0x70;
    status = rc522_to_card(RC522_CMD_TRANSCEIVE, buf, 2, buf, &i);

    if (status == 0) {
        // Check card serial number
        for (i = 0; i < 4; i++) {
            ser_num_check ^= buf[i];
        }
        if (ser_num_check != buf[i]) {
            status = 2;
        }
    }

    if (status == 0) {
        // Store card serial number
        for (i = 0; i < 4; i++) {
            ser_num[i] = buf[i];
        }
    }

    return status;
}

int main() {
    stdio_init_all();

    printf("Hello, RC522! RFID reader/writer example...\n");

    // This example will use SPI0 at 10MHz.
    spi_init(SPI_PORT, 10 * 1000 * 1000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    // Make the SPI pins available to picotool
    bi_decl(bi_3pins_with_func(PIN_MISO, PIN_MOSI, PIN_SCK, GPIO_FUNC_SPI));

    // Chip select is active-low, so we'll initialise it to a driven-high state
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);
    // Make the CS pin available to picotool
    bi_decl(bi_1pin_with_name(PIN_CS, "SPI CS"));

    // Initialize RST pin
    gpio_init(PIN_RST);
    gpio_set_dir(PIN_RST, GPIO_OUT);
    gpio_put(PIN_RST, 1);
    // Make the RST pin available to picotool
    bi_decl(bi_1pin_with_name(PIN_RST, "RC522 RST"));

    // Initialize RC522
    rc522_init();

    // Check version
    uint8_t version = read_register(RC522_REG_VERSION);
    printf("RC522 Version: 0x%02X\n", version);

    if (version == 0x00 || version == 0xFF) {
        printf("Warning: RC522 not detected! Check wiring.\n");
    } else {
        printf("RC522 initialized successfully!\n");
    }

    uint8_t tag_type[2];
    uint8_t ser_num[5];

    printf("Waiting for RFID card...\n");

    while (1) {
        // Request card
        if (rc522_request(MIFARE_CMD_REQA, tag_type) == 0) {
            printf("Card detected! Type: 0x%02X 0x%02X\n", tag_type[0], tag_type[1]);

            // Anti-collision
            if (rc522_anticoll(ser_num) == 0) {
                printf("Card UID: ");
                for (int i = 0; i < 4; i++) {
                    printf("%02X ", ser_num[i]);
                }
                printf("\n");
            }

            sleep_ms(1000);
        }

        sleep_ms(100);
    }
}

