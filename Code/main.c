#include "pico/stdlib.h"

// Starter sketch to verify the toolchain + SDK build.
// Blinks the on-board LED and prints over USB serial once per second.
int main(void) {
    stdio_init_all();

    const uint LED_PIN = PICO_DEFAULT_LED_PIN;
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    while (true) {
        gpio_put(LED_PIN, 1);
        sleep_ms(500);
        gpio_put(LED_PIN, 0);
        sleep_ms(500);
        printf("PicoParanoia alive\n");
    }
}
