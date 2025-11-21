add_executable(adafruit_io_bmp280
        adafruit_io_bmp280.c
        )

target_compile_definitions(adafruit_io_bmp280 PRIVATE
        WIFI_SSID=\"${WIFI_SSID}\"
        WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
        ADAFRUIT_IO_USERNAME=\"${ADAFRUIT_IO_USERNAME}\"
        ADAFRUIT_IO_KEY=\"${ADAFRUIT_IO_KEY}\"
        ADAFRUIT_IO_FEED_TEMP=\"${ADAFRUIT_IO_FEED_TEMP}\"
        ADAFRUIT_IO_FEED_PRESSURE=\"${ADAFRUIT_IO_FEED_PRESSURE}\"
        )

target_include_directories(adafruit_io_bmp280 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/../wifi  # for common lwipopts and mbedtls_config.h
        )

target_link_libraries(adafruit_io_bmp280
        pico_cyw43_arch_lwip_threadsafe_background
        pico_lwip_mbedtls
        pico_mbedtls
        pico_stdlib
        hardware_i2c
        )

# Enable USB serial output, disable UART output
pico_enable_stdio_usb(adafruit_io_bmp280 1)
pico_enable_stdio_uart(adafruit_io_bmp280 0)

pico_add_extra_outputs(adafruit_io_bmp280)

example_auto_set_url(adafruit_io_bmp280)

