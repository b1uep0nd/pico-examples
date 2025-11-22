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

// Root certificate for httpbin.org
// httpbin.org uses Let's Encrypt certificates, which are signed by ISRG Root X1
// 
// This is the ISRG Root X1 certificate downloaded from:
// https://letsencrypt.org/certs/isrgrootx1.pem
//
// If this certificate doesn't work or you need to use a different certificate,
// you can get it from your browser:
// 1. Open https://httpbin.org in your browser (Chrome, Firefox, etc.)
// 2. Click the padlock icon in the address bar
// 3. Click "Connection is secure" or "Certificate" (depending on browser)
// 4. Go to the "Details" or "Certification Path" tab
// 5. Select the root certificate (usually "ISRG Root X1" or "DST Root CA X3")
// 6. Click "Export" or "View Certificate" and export it in PEM/Base64 format
// 7. Copy the entire certificate including "-----BEGIN CERTIFICATE-----" and "-----END CERTIFICATE-----"
// 8. Replace the certificate below, keeping the format with \n\ at the end of each line
#define TLS_ROOT_CERT_HTTPBIN "-----BEGIN CERTIFICATE-----\n\
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n\
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n\
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n\
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n\
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n\
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n\
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n\
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\n\
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\n\
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\n\
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\n\
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\n\
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn\n\
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn\n\
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw\n\
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI\n\
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV\n\
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq\n\
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL\n\
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ\n\
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK\n\
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5\n\
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur\n\
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC\n\
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc\n\
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq\n\
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA\n\
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d\n\
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=\n\
-----END CERTIFICATE-----\n"

static volatile bool ntp_complete = false;
static volatile time_t ntp_time = 0;

// Get error string for err_t
static const char* err_to_string(err_t err) {
    switch(err) {
        case ERR_OK: return "OK";
        case ERR_MEM: return "Out of memory";
        case ERR_BUF: return "Buffer error";
        case ERR_TIMEOUT: return "Timeout";
        case ERR_RTE: return "Routing problem";
        case ERR_INPROGRESS: return "In progress";
        case ERR_VAL: return "Illegal value";
        case ERR_WOULDBLOCK: return "Would block";
        case ERR_USE: return "Address in use";
        case ERR_ALREADY: return "Already connecting";
        case ERR_ISCONN: return "Already connected";
        case ERR_CONN: return "Not connected";
        case ERR_IF: return "Low-level netif error";
        case ERR_ABRT: return "Connection aborted";
        case ERR_RST: return "Connection reset";
        case ERR_CLSD: return "Connection closed";
        case ERR_ARG: return "Illegal argument";
        default: return "Unknown error";
    }
}

// Get result string for httpc_result_t
// Note: httpc_result_t is typically 0 for success, non-zero for error
static const char* httpc_result_to_string(httpc_result_t result) {
    if (result == 0) {
        return "Success";
    }
    // Common HTTP client result codes (these may vary by lwIP version)
    switch((int)result) {
        case 1: return "Connection failed";
        case 2: return "Timeout";
        case 3: return "Invalid response";
        case 4: return "Memory error";
        default: return "Unknown error";
    }
}

// Structure to store detailed request result
typedef struct {
    httpc_result_t httpc_result;
    u32_t rx_content_len;
    u32_t srv_res;
    err_t err;
    bool complete;
} DETAILED_REQUEST_RESULT_T;

// Custom result callback to capture detailed information
static void detailed_result_fn(void *arg, httpc_result_t httpc_result, u32_t rx_content_len, u32_t srv_res, err_t err) {
    DETAILED_REQUEST_RESULT_T *result = (DETAILED_REQUEST_RESULT_T*)arg;
    result->httpc_result = httpc_result;
    result->rx_content_len = rx_content_len;
    result->srv_res = srv_res;
    result->err = err;
    result->complete = true;
}

// Print detailed error information
static void print_request_result(const char* label, int ret, DETAILED_REQUEST_RESULT_T *detail) {
    printf("\n=== %s ===\n", label);
    printf("Return code: %d (%s)\n", ret, ret == 0 ? "OK" : "Error");
    printf("HTTP result: %d (%s)\n", detail->httpc_result, httpc_result_to_string(detail->httpc_result));
    printf("Received content length: %u bytes\n", detail->rx_content_len);
    printf("Server response code: %u", detail->srv_res);
    if (detail->srv_res >= 200 && detail->srv_res < 300) {
        printf(" (Success)");
    } else if (detail->srv_res >= 300 && detail->srv_res < 400) {
        printf(" (Redirect)");
    } else if (detail->srv_res >= 400 && detail->srv_res < 500) {
        printf(" (Client Error)");
    } else if (detail->srv_res >= 500) {
        printf(" (Server Error)");
    }
    printf("\n");
    printf("lwIP error: %d (%s)\n", detail->err, err_to_string(detail->err));
    if (ret != 0 || detail->httpc_result != 0 || detail->err != ERR_OK) {
        printf("*** ERROR DETECTED ***\n");
    }
    printf("===================\n");
}

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

    // HTTP requests
    DETAILED_REQUEST_RESULT_T detail1 = {0};
    EXAMPLE_HTTP_REQUEST_T req1 = {0};
    req1.hostname = HOST;
    req1.url = URL_REQUEST;
    req1.headers_fn = http_client_header_print_fn;
    req1.recv_fn = http_client_receive_print_fn;
    req1.result_fn = detailed_result_fn;
    req1.callback_arg = &detail1;
    
    int result = http_client_request_sync(cyw43_arch_async_context(), &req1);
    print_request_result("HTTP Sync Request 1", result, &detail1);
    result += req1.result;
    
    detail1 = (DETAILED_REQUEST_RESULT_T){0};
    result += http_client_request_sync(cyw43_arch_async_context(), &req1); // repeat
    print_request_result("HTTP Sync Request 2", result, &detail1);
    result += req1.result;

    // test async
    DETAILED_REQUEST_RESULT_T detail2 = {0};
    EXAMPLE_HTTP_REQUEST_T req2 = req1;
    req2.callback_arg = &detail2;
    
    result += http_client_request_async(cyw43_arch_async_context(), &req1);
    result += http_client_request_async(cyw43_arch_async_context(), &req2);
    while(!req1.complete && !req2.complete) {
        async_context_poll(cyw43_arch_async_context());
        async_context_wait_for_work_ms(cyw43_arch_async_context(), 1000);
    }
    print_request_result("HTTP Async Request 1", 0, &detail1);
    print_request_result("HTTP Async Request 2", 0, &detail2);
    result += req1.result;
    result += req2.result;

    // HTTPS requests with root certificate
    // Create and free tls_config for each request independently to prevent memory leaks
    static const uint8_t root_cert[] = TLS_ROOT_CERT_HTTPBIN;
    
    // === HTTPS Sync Request 1 ===
    // 1. Create tls_config
    req1.tls_config = altcp_tls_create_config_client(root_cert, sizeof(root_cert));
    if (!req1.tls_config) {
        printf("Failed to create TLS config for first request\n");
        panic("TLS config failed");
    }
    
    // 2. Reset state and make request
    detail1 = (DETAILED_REQUEST_RESULT_T){0};
    req1.complete = false;
    req1.result = 0;
    req1.callback_arg = &detail1;
    req1.tls_allocator.alloc = NULL; // Reset allocator
    result += http_client_request_sync(cyw43_arch_async_context(), &req1);
    print_request_result("HTTPS Sync Request 1", result, &detail1);
    result += req1.result;
    
    // 3. Wait for connection to fully close
    printf("Waiting for connection cleanup after first request...\n");
    for (int i = 0; i < 20; i++) {
        async_context_poll(cyw43_arch_async_context());
        async_context_wait_for_work_ms(cyw43_arch_async_context(), 50);
    }
    sleep_ms(200);
    
    // 4. Free tls_config after first request
    altcp_tls_free_config(req1.tls_config);
    req1.tls_config = NULL;
    
    // === HTTPS Sync Request 2 ===
    // 1. Create new tls_config
    req1.tls_config = altcp_tls_create_config_client(root_cert, sizeof(root_cert));
    if (!req1.tls_config) {
        printf("Failed to create TLS config for second request\n");
        panic("TLS config failed");
    }
    
    // 2. Reset state and make request
    detail1 = (DETAILED_REQUEST_RESULT_T){0};
    req1.complete = false;
    req1.result = 0;
    req1.callback_arg = &detail1;
    req1.tls_allocator.alloc = NULL; // Reset allocator
    result += http_client_request_sync(cyw43_arch_async_context(), &req1);
    print_request_result("HTTPS Sync Request 2", result, &detail1);
    result += req1.result;
    
    // 3. Wait for connection to fully close
    printf("Waiting for connection cleanup after second request...\n");
    for (int i = 0; i < 20; i++) {
        async_context_poll(cyw43_arch_async_context());
        async_context_wait_for_work_ms(cyw43_arch_async_context(), 50);
    }
    sleep_ms(200);
    
    // 4. Free tls_config after second request
    altcp_tls_free_config(req1.tls_config);
    req1.tls_config = NULL;

    printf("Total result: %d\n", result);
    if (result != 0) {
        printf("test failed");
        return 0;
    }
    cyw43_arch_deinit();
    printf("Test passed\n");
    sleep_ms(100);
    return 0;
}