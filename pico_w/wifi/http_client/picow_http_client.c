/**
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "pico/stdio.h"
#include "pico/cyw43_arch.h"
#include "pico/async_context.h"
#include "pico/util/datetime.h"
#include "hardware/rtc.h"
#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "lwip/altcp_tls.h"
#include "example_http_client_util.h"

#define HOST "httpbin.org"
#define URL_REQUEST "/get"
#define NTP_SERVER "pool.ntp.org"
#define NTP_MSG_LEN 48
#define NTP_PORT 123
#define NTP_DELTA 2208988800 // seconds between 1 Jan 1900 and 1 Jan 1970
#define NTP_TIMEOUT_MS 10000

static volatile bool ntp_complete = false;
static volatile time_t ntp_time = 0;

// NTP data received
static void ntp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    (void)arg;
    (void)pcb;
    uint8_t mode = pbuf_get_at(p, 0) & 0x7;
    uint8_t stratum = pbuf_get_at(p, 1);

    // Check the result
    if (port == NTP_PORT && p->tot_len == NTP_MSG_LEN && mode == 0x4 && stratum != 0) {
        uint8_t seconds_buf[4] = {0};
        pbuf_copy_partial(p, seconds_buf, sizeof(seconds_buf), 40);
        uint32_t seconds_since_1900 = seconds_buf[0] << 24 | seconds_buf[1] << 16 | seconds_buf[2] << 8 | seconds_buf[3];
        uint32_t seconds_since_1970 = seconds_since_1900 - NTP_DELTA;
        ntp_time = seconds_since_1970;
        ntp_complete = true;
        printf("NTP response received\n");
    } else {
        printf("invalid ntp response\n");
        ntp_complete = true;
    }
    pbuf_free(p);
}

// Call back with a DNS result
static void ntp_dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg) {
    (void)hostname;
    struct udp_pcb *ntp_pcb = (struct udp_pcb *)arg;
    if (ipaddr) {
        printf("NTP server address: %s\n", ipaddr_ntoa(ipaddr));
        cyw43_arch_lwip_begin();
        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, NTP_MSG_LEN, PBUF_RAM);
        uint8_t *req = (uint8_t *) p->payload;
        memset(req, 0, NTP_MSG_LEN);
        req[0] = 0x1b;
        udp_sendto(ntp_pcb, p, ipaddr, NTP_PORT);
        pbuf_free(p);
        cyw43_arch_lwip_end();
    } else {
        printf("NTP DNS request failed\n");
        ntp_complete = true;
    }
}

// Synchronize time with NTP server
static bool sync_time_with_ntp(void) {
    printf("Synchronizing time with NTP server...\n");
    
    struct udp_pcb *ntp_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (!ntp_pcb) {
        printf("Failed to create UDP PCB\n");
        return false;
    }
    
    udp_recv(ntp_pcb, ntp_recv, NULL);
    ntp_complete = false;
    ntp_time = 0;
    
    ip_addr_t ntp_server_address;
    int err = dns_gethostbyname(NTP_SERVER, &ntp_server_address, ntp_dns_found, ntp_pcb);
    if (err == ERR_OK) {
        // Cached DNS result, make NTP request immediately
        cyw43_arch_lwip_begin();
        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, NTP_MSG_LEN, PBUF_RAM);
        uint8_t *req = (uint8_t *) p->payload;
        memset(req, 0, NTP_MSG_LEN);
        req[0] = 0x1b;
        udp_sendto(ntp_pcb, p, &ntp_server_address, NTP_PORT);
        pbuf_free(p);
        cyw43_arch_lwip_end();
    } else if (err != ERR_INPROGRESS) {
        printf("DNS request failed: %d\n", err);
        udp_remove(ntp_pcb);
        return false;
    }
    
    // Wait for NTP response
    absolute_time_t timeout = make_timeout_time_ms(NTP_TIMEOUT_MS);
    while (!ntp_complete && !time_reached(timeout)) {
        async_context_poll(cyw43_arch_async_context());
        async_context_wait_for_work_ms(cyw43_arch_async_context(), 100);
    }
    
    udp_remove(ntp_pcb);
    
    if (!ntp_complete || ntp_time == 0) {
        printf("NTP synchronization failed\n");
        return false;
    }
    
    // Set RTC time
    time_t ntp_time_copy = ntp_time; // Copy to remove volatile qualifier
    struct tm *utc = gmtime(&ntp_time_copy);
    datetime_t t = {
        .year = utc->tm_year + 1900,
        .month = utc->tm_mon + 1,
        .day = utc->tm_mday,
        .dotw = utc->tm_wday,
        .hour = utc->tm_hour,
        .min = utc->tm_min,
        .sec = utc->tm_sec
    };
    
    rtc_init();
    if (!rtc_set_datetime(&t)) {
        printf("Failed to set RTC datetime\n");
        return false;
    }
    
    sleep_us(64); // Wait for RTC to update
    
    char datetime_buf[256];
    datetime_to_str(datetime_buf, sizeof(datetime_buf), &t);
    printf("Time synchronized: %s\n", datetime_buf);
    
    return true;
}

int main() {
    stdio_init_all();
    if (cyw43_arch_init()) {
        printf("failed to initialise\n");
        return 1;
    }
    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to connect\n");
        return 1;
    }
    
    // Synchronize time with NTP before making HTTPS requests
    if (!sync_time_with_ntp()) {
        printf("Warning: NTP synchronization failed, HTTPS may fail\n");
    }

    EXAMPLE_HTTP_REQUEST_T req1 = {0};
    req1.hostname = HOST;
    req1.url = URL_REQUEST;
    req1.headers_fn = http_client_header_print_fn;
    req1.recv_fn = http_client_receive_print_fn;
    int result = http_client_request_sync(cyw43_arch_async_context(), &req1);
    printf("sync request 1 result: %d, req->result: %d\n", result, req1.result);
    result += http_client_request_sync(cyw43_arch_async_context(), &req1); // repeat
    printf("sync request 2 result: %d, req->result: %d\n", result, req1.result);

    // test async
    EXAMPLE_HTTP_REQUEST_T req2 = req1;
    result += http_client_request_async(cyw43_arch_async_context(), &req1);
    result += http_client_request_async(cyw43_arch_async_context(), &req2);
    while(!req1.complete && !req2.complete) {
        async_context_poll(cyw43_arch_async_context());
        async_context_wait_for_work_ms(cyw43_arch_async_context(), 1000);
    }
    printf("async request 1 result: %d, req->result: %d\n", req1.result, req1.result);
    printf("async request 2 result: %d, req->result: %d\n", req2.result, req2.result);
    result += req1.result;
    result += req2.result;

    req1.tls_config = altcp_tls_create_config_client(NULL, 0); // https
    result += http_client_request_sync(cyw43_arch_async_context(), &req1);
    printf("https sync request 1 result: %d, req->result: %d\n", result, req1.result);
    result += http_client_request_sync(cyw43_arch_async_context(), &req1); // repeat
    printf("https sync request 2 result: %d, req->result: %d\n", result, req1.result);
    altcp_tls_free_config(req1.tls_config);

    printf("Total result: %d\n", result);
    if (result != 0) {
        panic("test failed");
    }
    cyw43_arch_deinit();
    printf("Test passed\n");
    sleep_ms(100);
    return 0;
}