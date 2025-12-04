if (TARGET tinyusb_device)
    add_executable(bh1750_i2c
            bh1750_i2c.c
    )

    target_link_libraries(bh1750_i2c pico_stdlib hardware_i2c)

    if (PICO_CYW43_SUPPORTED)
        target_link_libraries(bh1750_i2c pico_cyw43_arch_none)
    endif()

    pico_enable_stdio_usb(bh1750_i2c 1)
    pico_enable_stdio_uart(bh1750_i2c 0)

    pico_add_extra_outputs(bh1750_i2c)
    example_auto_set_url(bh1750_i2c)
else()
    message("Skipping bh1750_i2c because TinyUSB submodule is not initialized in the SDK")
endif()

