add_executable(rc522_spi
        rc522_spi.c
        )

# pull in common dependencies and additional spi hardware support
target_link_libraries(rc522_spi pico_stdlib hardware_spi)

# Enable USB stdio and disable UART stdio so logs appear over USB serial
pico_enable_stdio_usb(rc522_spi 1)
pico_enable_stdio_uart(rc522_spi 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(rc522_spi)

# add url via pico_set_program_url
example_auto_set_url(rc522_spi)


