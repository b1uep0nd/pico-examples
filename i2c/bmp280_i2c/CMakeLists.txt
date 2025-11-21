if (TARGET tinyusb_device)
    add_executable(bmp280_i2c
            bmp280_i2c.c
            )

    # pull in common dependencies and additional i2c hardware support
    target_link_libraries(bmp280_i2c pico_stdlib hardware_i2c)

    # For Pico W, we need cyw43_arch for LED control
    if (PICO_CYW43_SUPPORTED)
        target_link_libraries(bmp280_i2c pico_cyw43_arch_none)
    endif()

    # enable usb output, disable uart output
    pico_enable_stdio_usb(bmp280_i2c 1)
    pico_enable_stdio_uart(bmp280_i2c 0)

    # create map/bin/hex file etc.
    pico_add_extra_outputs(bmp280_i2c)

    # add url via pico_set_program_url
    example_auto_set_url(bmp280_i2c)
else()
    message("Skipping bmp280_i2c because TinyUSB submodule is not initialized in the SDK")
endif()
