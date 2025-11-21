/**
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Pico W example: Send BMP280 sensor data to Adafruit IO
 * 
 * Connections:
 *   GPIO 4 (Pin 6) -> SDA on BMP280
 *   GPIO 5 (Pin 7) -> SCL on BMP280
 *   3.3v (Pin 36) -> VCC on BMP280
 *   GND (Pin 38)  -> GND on BMP280
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/i2c.h"
#include "lwip/pbuf.h"
#include "lwip/altcp_tcp.h"
#include "lwip/altcp_tls.h"
#include "lwip/dns.h"

// Adafruit IO configuration - set these in CMakeLists.txt or define here
#ifndef ADAFRUIT_IO_USERNAME
#define ADAFRUIT_IO_USERNAME "your_username"
#endif

#ifndef ADAFRUIT_IO_KEY
#define ADAFRUIT_IO_KEY "your_key"
#endif

#ifndef ADAFRUIT_IO_FEED_TEMP
#define ADAFRUIT_IO_FEED_TEMP "bmp280-temp"
#endif

#ifndef ADAFRUIT_IO_FEED_PRESSURE
#define ADAFRUIT_IO_FEED_PRESSURE "bmp280-pressure"
#endif

// BMP280 I2C address
#define BMP280_ADDR 0x76

// BMP280 registers
#define REG_CONFIG 0xF5
#define REG_CTRL_MEAS 0xF4
#define REG_PRESSURE_MSB 0xF7
#define REG_DIG_T1_LSB 0x88
#define NUM_CALIB_PARAMS 24

struct bmp280_calib_param {
    uint16_t dig_t1;
    int16_t dig_t2;
    int16_t dig_t3;
    uint16_t dig_p1;
    int16_t dig_p2;
    int16_t dig_p3;
    int16_t dig_p4;
    int16_t dig_p5;
    int16_t dig_p6;
    int16_t dig_p7;
    int16_t dig_p8;
    int16_t dig_p9;
};

typedef struct HTTP_POST_STATE_T_ {
    struct altcp_pcb *pcb;
    bool complete;
    int error;
    char http_request[512];
    int timeout;
} HTTP_POST_STATE_T;

static struct altcp_tls_config *tls_config = NULL;

// BMP280 functions (from bmp280_i2c.c)
void bmp280_init() {
    uint8_t buf[2];
    const uint8_t reg_config_val = ((0x04 << 5) | (0x05 << 2)) & 0xFC;
    buf[0] = REG_CONFIG;
    buf[1] = reg_config_val;
    i2c_write_blocking(i2c_default, BMP280_ADDR, buf, 2, false);

    const uint8_t reg_ctrl_meas_val = (0x01 << 5) | (0x03 << 2) | (0x03);
    buf[0] = REG_CTRL_MEAS;
    buf[1] = reg_ctrl_meas_val;
    i2c_write_blocking(i2c_default, BMP280_ADDR, buf, 2, false);
}

void bmp280_read_raw(int32_t* temp, int32_t* pressure) {
    uint8_t buf[6];
    uint8_t reg = REG_PRESSURE_MSB;
    i2c_write_blocking(i2c_default, BMP280_ADDR, &reg, 1, true);
    i2c_read_blocking(i2c_default, BMP280_ADDR, buf, 6, false);
    *pressure = (buf[0] << 12) | (buf[1] << 4) | (buf[2] >> 4);
    *temp = (buf[3] << 12) | (buf[4] << 4) | (buf[5] >> 4);
}

void bmp280_get_calib_params(struct bmp280_calib_param* params) {
    uint8_t buf[NUM_CALIB_PARAMS] = { 0 };
    uint8_t reg = REG_DIG_T1_LSB;
    i2c_write_blocking(i2c_default, BMP280_ADDR, &reg, 1, true);
    i2c_read_blocking(i2c_default, BMP280_ADDR, buf, NUM_CALIB_PARAMS, false);

    params->dig_t1 = (uint16_t)(buf[1] << 8) | buf[0];
    params->dig_t2 = (int16_t)(buf[3] << 8) | buf[2];
    params->dig_t3 = (int16_t)(buf[5] << 8) | buf[4];
    params->dig_p1 = (uint16_t)(buf[7] << 8) | buf[6];
    params->dig_p2 = (int16_t)(buf[9] << 8) | buf[8];
    params->dig_p3 = (int16_t)(buf[11] << 8) | buf[10];
    params->dig_p4 = (int16_t)(buf[13] << 8) | buf[12];
    params->dig_p5 = (int16_t)(buf[15] << 8) | buf[14];
    params->dig_p6 = (int16_t)(buf[17] << 8) | buf[16];
    params->dig_p7 = (int16_t)(buf[19] << 8) | buf[18];
    params->dig_p8 = (int16_t)(buf[21] << 8) | buf[20];
    params->dig_p9 = (int16_t)(buf[23] << 8) | buf[22];
}

int32_t bmp280_convert(int32_t temp, struct bmp280_calib_param* params) {
    int32_t var1, var2;
    var1 = ((((temp >> 3) - ((int32_t)params->dig_t1 << 1))) * ((int32_t)params->dig_t2)) >> 11;
    var2 = (((((temp >> 4) - ((int32_t)params->dig_t1)) * ((temp >> 4) - ((int32_t)params->dig_t1))) >> 12) * ((int32_t)params->dig_t3)) >> 14;
    return var1 + var2;
}

int32_t bmp280_convert_temp(int32_t temp, struct bmp280_calib_param* params) {
    int32_t t_fine = bmp280_convert(temp, params);
    return (t_fine * 5 + 128) >> 8;
}

int32_t bmp280_convert_pressure(int32_t pressure, int32_t temp, struct bmp280_calib_param* params) {
    int32_t t_fine = bmp280_convert(temp, params);
    int32_t var1, var2;
    uint32_t converted = 0.0;
    var1 = (((int32_t)t_fine) >> 1) - (int32_t)64000;
    var2 = (((var1 >> 2) * (var1 >> 2)) >> 11) * ((int32_t)params->dig_p6);
    var2 += ((var1 * ((int32_t)params->dig_p5)) << 1);
    var2 = (var2 >> 2) + (((int32_t)params->dig_p4) << 16);
    var1 = (((params->dig_p3 * (((var1 >> 2) * (var1 >> 2)) >> 13)) >> 3) + ((((int32_t)params->dig_p2) * var1) >> 1)) >> 18;
    var1 = ((((32768 + var1)) * ((int32_t)params->dig_p1)) >> 15);
    if (var1 == 0) {
        return 0;
    }
    converted = (((uint32_t)(((int32_t)1048576) - pressure) - (var2 >> 12))) * 3125;
    if (converted < 0x80000000) {
        converted = (converted << 1) / ((uint32_t)var1);
    } else {
        converted = (converted / (uint32_t)var1) * 2;
    }
    var1 = (((int32_t)params->dig_p9) * ((int32_t)(((converted >> 3) * (converted >> 3)) >> 13))) >> 12;
    var2 = (((int32_t)(converted >> 2)) * ((int32_t)params->dig_p8)) >> 13;
    converted = (uint32_t)((int32_t)converted + ((var1 + var2 + params->dig_p7) >> 4));
    return converted;
}

// HTTP POST functions
static err_t http_post_close(void *arg) {
    HTTP_POST_STATE_T *state = (HTTP_POST_STATE_T*)arg;
    err_t err = ERR_OK;
    state->complete = true;
    if (state->pcb != NULL) {
        altcp_arg(state->pcb, NULL);
        altcp_poll(state->pcb, NULL, 0);
        altcp_recv(state->pcb, NULL);
        altcp_err(state->pcb, NULL);
        err = altcp_close(state->pcb);
        if (err != ERR_OK) {
            altcp_abort(state->pcb);
            err = ERR_ABRT;
        }
        state->pcb = NULL;
    }
    return err;
}

static err_t http_post_connected(void *arg, struct altcp_pcb *pcb, err_t err) {
    HTTP_POST_STATE_T *state = (HTTP_POST_STATE_T*)arg;
    if (err != ERR_OK) {
        const char *err_str = "Unknown";
        switch(err) {
            case ERR_MEM: err_str = "Out of memory"; break;
            case ERR_TIMEOUT: err_str = "Timeout"; break;
            case ERR_RTE: err_str = "Routing problem"; break;
            case ERR_CONN: err_str = "Not connected"; break;
            case ERR_ABRT: err_str = "Connection aborted"; break;
            case ERR_RST: err_str = "Connection reset"; break;
            case ERR_CLSD: err_str = "Connection closed"; break;
        }
        printf("HTTP POST connect failed %d (%s)\n", err, err_str);
        return http_post_close(state);
    }
    printf("TLS connected, sending POST request\n");
    printf("Request length: %zu bytes\n", strlen(state->http_request));
    err = altcp_write(state->pcb, state->http_request, strlen(state->http_request), TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        printf("Error writing data, err=%d\n", err);
        return http_post_close(state);
    }
    // Note: altcp_output() is not needed here - data is sent automatically
    printf("POST request sent\n");
    return ERR_OK;
}

static err_t http_post_poll(void *arg, struct altcp_pcb *pcb) {
    HTTP_POST_STATE_T *state = (HTTP_POST_STATE_T*)arg;
    printf("HTTP POST timed out\n");
    state->error = PICO_ERROR_TIMEOUT;
    return http_post_close(arg);
}

static void http_post_err(void *arg, err_t err) {
    HTTP_POST_STATE_T *state = (HTTP_POST_STATE_T*)arg;
    const char *err_str = "Unknown";
    switch(err) {
        case ERR_OK: err_str = "OK"; break;
        case ERR_MEM: err_str = "Out of memory"; break;
        case ERR_BUF: err_str = "Buffer error"; break;
        case ERR_TIMEOUT: err_str = "Timeout"; break;
        case ERR_RTE: err_str = "Routing problem"; break;
        case ERR_INPROGRESS: err_str = "In progress"; break;
        case ERR_VAL: err_str = "Illegal value"; break;
        case ERR_WOULDBLOCK: err_str = "Would block"; break;
        case ERR_USE: err_str = "Address in use"; break;
        case ERR_ALREADY: err_str = "Already connecting"; break;
        case ERR_ISCONN: err_str = "Already connected"; break;
        case ERR_CONN: err_str = "Not connected"; break;
        case ERR_IF: err_str = "Low-level netif error"; break;
        case ERR_ABRT: err_str = "Connection aborted"; break;
        case ERR_RST: err_str = "Connection reset"; break;
        case ERR_CLSD: err_str = "Connection closed"; break;
        case ERR_ARG: err_str = "Illegal argument"; break;
    }
    printf("HTTP POST error %d (%s)\n", err, err_str);
    http_post_close(state);
    state->error = PICO_ERROR_GENERIC;
}

static err_t http_post_recv(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err) {
    HTTP_POST_STATE_T *state = (HTTP_POST_STATE_T*)arg;
    if (!p) {
        printf("Connection closed by server\n");
        state->error = 0; // Normal closure, might be OK
        return http_post_close(state);
    }
    if (p->tot_len > 0) {
        char buf[512];
        size_t len = p->tot_len < sizeof(buf) - 1 ? p->tot_len : sizeof(buf) - 1;
        pbuf_copy_partial(p, buf, len, 0);
        buf[len] = 0;
        printf("HTTP Response (%zu bytes):\n%s\n", p->tot_len, buf);
        
        // Check for HTTP status code
        if (strncmp(buf, "HTTP/1.1 200", 12) == 0 || strncmp(buf, "HTTP/1.1 201", 12) == 0) {
            printf("Success! Status: 200/201\n");
            state->error = 0;
        } else if (strncmp(buf, "HTTP/1.1 401", 12) == 0) {
            printf("Error: Authentication failed (401)\n");
            state->error = PICO_ERROR_GENERIC;
        } else if (strncmp(buf, "HTTP/1.1 404", 12) == 0) {
            printf("Error: Feed not found (404)\n");
            state->error = PICO_ERROR_GENERIC;
        } else {
            printf("Warning: Unexpected status code\n");
        }
        
        altcp_recved(pcb, p->tot_len);
    }
    pbuf_free(p);
    return ERR_OK;
}

static void http_post_connect_to_server_ip(const ip_addr_t *ipaddr, HTTP_POST_STATE_T *state) {
    err_t err;
    u16_t port = 443;
    printf("Connecting to %s port %d (TLS)\n", ipaddr_ntoa(ipaddr), port);
    err = altcp_connect(state->pcb, ipaddr, port, http_post_connected);
    if (err != ERR_OK) {
        if (err == ERR_INPROGRESS) {
            printf("Connection in progress...\n");
        } else {
            printf("Error initiating connect, err=%d\n", err);
            http_post_close(state);
        }
    }
}

static void http_post_dns_found(const char* hostname, const ip_addr_t *ipaddr, void *arg) {
    if (ipaddr) {
        printf("DNS resolved\n");
        http_post_connect_to_server_ip(ipaddr, (HTTP_POST_STATE_T *) arg);
    } else {
        printf("Error resolving hostname %s\n", hostname);
        http_post_close(arg);
    }
}

static bool http_post_open(const char *hostname, HTTP_POST_STATE_T *state) {
    err_t err;
    ip_addr_t server_ip;
    state->pcb = altcp_tls_new(tls_config, IPADDR_TYPE_ANY);
    if (!state->pcb) {
        printf("Failed to create PCB\n");
        return false;
    }
    altcp_arg(state->pcb, state);
    altcp_poll(state->pcb, http_post_poll, state->timeout * 2);
    altcp_recv(state->pcb, http_post_recv);
    altcp_err(state->pcb, http_post_err);
    mbedtls_ssl_set_hostname(altcp_tls_context(state->pcb), hostname);
    printf("Resolving %s\n", hostname);
    cyw43_arch_lwip_begin();
    err = dns_gethostbyname(hostname, &server_ip, http_post_dns_found, state);
    if (err == ERR_OK) {
        http_post_connect_to_server_ip(&server_ip, state);
    } else if (err != ERR_INPROGRESS) {
        printf("Error initiating DNS resolving, err=%d\n", err);
        http_post_close(state);
    }
    cyw43_arch_lwip_end();
    return err == ERR_OK || err == ERR_INPROGRESS;
}

static HTTP_POST_STATE_T* http_post_init(void) {
    HTTP_POST_STATE_T *state = calloc(1, sizeof(HTTP_POST_STATE_T));
    if (!state) {
        printf("Failed to allocate state\n");
        return NULL;
    }
    return state;
}

bool send_to_adafruit_io(const char *feed_key, float value) {
    char json_body[128];
    int json_len = snprintf(json_body, sizeof(json_body), "{\"value\":\"%.2f\"}", value);
    
    // Create TLS config if not already created
    if (!tls_config) {
        tls_config = altcp_tls_create_config_client(NULL, 0);
        if (!tls_config) {
            printf("Failed to create TLS config\n");
            return false;
        }
    }
    
    HTTP_POST_STATE_T *state = http_post_init();
    if (!state) {
        return false;
    }
    
    int req_len = snprintf(state->http_request, sizeof(state->http_request),
        "POST /api/v2/%s/feeds/%s/data HTTP/1.1\r\n"
        "Host: io.adafruit.com\r\n"
        "X-AIO-Key: %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        ADAFRUIT_IO_USERNAME, feed_key, ADAFRUIT_IO_KEY, json_len, json_body);
    
    if (req_len >= sizeof(state->http_request)) {
        printf("HTTP request too long\n");
        free(state);
        return false;
    }
    
    state->timeout = 15;
    
    printf("Opening connection to io.adafruit.com...\n");
    if (!http_post_open("io.adafruit.com", state)) {
        printf("Failed to open connection\n");
        free(state);
        return false;
    }
    
    printf("Waiting for connection...\n");
    absolute_time_t timeout = make_timeout_time_ms(30000); // 30 second timeout
    while(!state->complete) {
        if (time_reached(timeout)) {
            printf("Connection timeout\n");
            state->error = PICO_ERROR_TIMEOUT;
            state->complete = true;
            break;
        }
#if PICO_CYW43_ARCH_POLL
        cyw43_arch_poll();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(10));
#else
        sleep_ms(10);
#endif
    }
    
    bool success = (state->error == 0);
    free(state);
    // Don't free tls_config here - reuse it for next connection
    return success;
}

int main() {
    stdio_init_all();
    
    // Initialize LED
    if (cyw43_arch_init()) {
        printf("Failed to initialize cyw43\n");
        return 1;
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    
    // Wait for serial connection
    sleep_ms(2000);
    
    printf("\n=== Adafruit IO BMP280 Example ===\n");
    printf("Connecting to Wi-Fi...\n");
    
    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("Failed to connect to Wi-Fi\n");
        cyw43_arch_deinit();
        return 1;
    }
    
    printf("Wi-Fi connected!\n");
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
    
    // Initialize TLS config once at startup
    tls_config = altcp_tls_create_config_client(NULL, 0);
    if (!tls_config) {
        printf("Failed to create TLS config\n");
        cyw43_arch_deinit();
        return 1;
    }
    
    // Initialize I2C
#if !defined(i2c_default) || !defined(PICO_DEFAULT_I2C_SDA_PIN) || !defined(PICO_DEFAULT_I2C_SCL_PIN)
    printf("I2C pins not defined\n");
    return 1;
#else
    i2c_init(i2c_default, 100 * 1000);
    gpio_set_function(PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(PICO_DEFAULT_I2C_SDA_PIN);
    gpio_pull_up(PICO_DEFAULT_I2C_SCL_PIN);
    
    // Initialize BMP280
    bmp280_init();
    struct bmp280_calib_param params;
    bmp280_get_calib_params(&params);
    
    sleep_ms(250);
    
    printf("Reading sensor and sending to Adafruit IO...\n");
    printf("Feed: %s (temp), %s (pressure)\n", ADAFRUIT_IO_FEED_TEMP, ADAFRUIT_IO_FEED_PRESSURE);
    
    int32_t raw_temperature, raw_pressure;
    bool led_state = false;
    int send_count = 0;
    
    while (1) {
        bmp280_read_raw(&raw_temperature, &raw_pressure);
        int32_t temperature = bmp280_convert_temp(raw_temperature, &params);
        int32_t pressure = bmp280_convert_pressure(raw_pressure, raw_temperature, &params);
        
        float temp_c = temperature / 100.0f;
        float pressure_kpa = pressure / 1000.0f;
        
        printf("Temp: %.2f C, Pressure: %.3f kPa\n", temp_c, pressure_kpa);
        
        // Toggle LED
        led_state = !led_state;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
        
        // Send to Adafruit IO every 5 seconds
        if (send_count % 10 == 0) {
            printf("Sending to Adafruit IO...\n");
            bool temp_sent = send_to_adafruit_io(ADAFRUIT_IO_FEED_TEMP, temp_c);
            sleep_ms(1000);
            bool pressure_sent = send_to_adafruit_io(ADAFRUIT_IO_FEED_PRESSURE, pressure_kpa);
            
            if (temp_sent && pressure_sent) {
                printf("Data sent successfully!\n");
            } else {
                printf("Failed to send data\n");
            }
        }
        
        send_count++;
        sleep_ms(500);
    }
#endif
}

