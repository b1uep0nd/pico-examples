if (TARGET tinyusb_device)
    add_executable(test_serial
            test_serial.c
            )

    # pull in common dependencies
    target_link_libraries(test_serial pico_stdlib)

    # For Pico W, we need cyw43_arch for LED control
    if (PICO_CYW43_SUPPORTED)
        target_link_libraries(test_serial pico_cyw43_arch_none)
    endif()

    # enable usb output, disable uart output
    pico_enable_stdio_usb(test_serial 1)
    pico_enable_stdio_uart(test_serial 0)

    # create map/bin/hex file etc.
    pico_add_extra_outputs(test_serial)

    # add url via pico_set_program_url
    example_auto_set_url(test_serial)
else()
    message("Skipping test_serial because TinyUSB submodule is not initialized in the SDK")
endif()

