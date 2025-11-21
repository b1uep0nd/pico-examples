/**
 * Simple serial communication test for Raspberry Pi Pico
 * This program prints messages to serial output to verify communication
 * and blinks the LED to show it's working
 */

#include <stdio.h>
#include "pico/stdlib.h"

// Pico W devices use a GPIO on the WIFI chip for the LED,
// so when building for Pico W, CYW43_WL_GPIO_LED_PIN will be defined
#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

// Perform LED initialisation
int pico_led_init(void) {
#if defined(PICO_DEFAULT_LED_PIN)
    // A device like Pico that uses a GPIO for the LED will define PICO_DEFAULT_LED_PIN
    // so we can use normal GPIO functionality to turn the led on and off
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    return 0;
#elif defined(CYW43_WL_GPIO_LED_PIN)
    // For Pico W devices we need to initialise the driver etc
    return cyw43_arch_init();
#else
    return 0;  // No LED available
#endif
}

// Turn the led on or off
void pico_set_led(bool led_on) {
#if defined(PICO_DEFAULT_LED_PIN)
    // Just set the GPIO on or off
    gpio_put(PICO_DEFAULT_LED_PIN, led_on);
#elif defined(CYW43_WL_GPIO_LED_PIN)
    // Ask the wifi "driver" to set the GPIO on or off
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
#endif
}

int main() {
    // Initialize stdio (UART or USB depending on configuration)
    stdio_init_all();

    // Initialize LED
    int rc = pico_led_init();
    if (rc != 0) {
        printf("LED initialization failed!\n");
    }

    // Wait a bit for serial connection to be established
    sleep_ms(2000);

    printf("\n\n");
    printf("========================================\n");
    printf("Pico Serial Communication Test\n");
    printf("========================================\n");
    printf("If you can see this message,\n");
    printf("serial communication is working!\n");
    printf("LED should be blinking now...\n");
    printf("========================================\n\n");

    int counter = 0;
    bool led_state = false;
    while (1) {
        // Toggle LED
        led_state = !led_state;
        pico_set_led(led_state);
        
        // Print counter
        printf("Counter: %d (LED: %s)\n", counter++, led_state ? "ON" : "OFF");
        sleep_ms(1000);  // Print and blink every 1 second
    }

    return 0;
}

