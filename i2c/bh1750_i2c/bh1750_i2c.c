#include <stdio.h>

#include "hardware/i2c.h"
#include "pico/binary_info.h"
#include "pico/stdlib.h"

#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

// BH1750 default address selection is pulled low (ADDR pin -> GND).
#define BH1750_ADDR _u(0x23)

#define BH1750_CMD_POWER_DOWN 0x00
#define BH1750_CMD_POWER_ON 0x01
#define BH1750_CMD_RESET 0x07
#define BH1750_CMD_CONT_HIGH_RES 0x10  // 1 lx resolution, 120 ms typical

static void bh1750_write_cmd(uint8_t cmd) {
    i2c_write_blocking(i2c_default, BH1750_ADDR, &cmd, 1, false);
}

static void bh1750_init(void) {
    bh1750_write_cmd(BH1750_CMD_POWER_ON);
    sleep_ms(10);
    bh1750_write_cmd(BH1750_CMD_RESET);
    sleep_ms(10);
    bh1750_write_cmd(BH1750_CMD_CONT_HIGH_RES);
}

static bool bh1750_read_lux(float* lux_out) {
    uint8_t buf[2] = { 0 };
    int read = i2c_read_blocking(i2c_default, BH1750_ADDR, buf, 2, false);
    if (read != 2) {
        return false;
    }
    uint16_t raw = (buf[0] << 8) | buf[1];
    *lux_out = (float)raw / 1.2f;  // per datasheet conversion
    return true;
}

int main() {
#if !defined(i2c_default) || !defined(PICO_DEFAULT_I2C_SDA_PIN) || !defined(PICO_DEFAULT_I2C_SCL_PIN)
#error i2c/bh1750_i2c requires a board with I2C pins defined
#endif

    stdio_init_all();

#ifdef CYW43_WL_GPIO_LED_PIN
    if (cyw43_arch_init()) {
        printf("Failed to init CYW43\n");
        return -1;
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
#endif

    i2c_init(i2c_default, 100 * 1000);
    gpio_set_function(PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(PICO_DEFAULT_I2C_SDA_PIN);
    gpio_pull_up(PICO_DEFAULT_I2C_SCL_PIN);

    bi_decl(bi_program_description("Read illuminance values from BH1750 via I2C"));
    bi_decl(bi_2pins_with_func(PICO_DEFAULT_I2C_SDA_PIN,
                               PICO_DEFAULT_I2C_SCL_PIN,
                               GPIO_FUNC_I2C));

    bh1750_init();

    absolute_time_t next_sample = make_timeout_time_ms(500);
    while (true) {
        if (absolute_time_diff_us(get_absolute_time(), next_sample) <= 0) {
            next_sample = delayed_by_ms(next_sample, 500);

            float lux = 0.0f;
            if (bh1750_read_lux(&lux)) {
                printf("Illuminance: %.2f lux\n", lux);
            } else {
                printf("Read failed\n");
            }
        }
        tight_loop_contents();
    }
}

