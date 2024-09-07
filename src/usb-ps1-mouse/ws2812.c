#include "ws2812.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

void set_ws2812(uint32_t color) {
  for (int32_t i = 23; i != -1; --i) {
    gpio_set_mask(1 << GP_RGB);

    // 350 ns = 42 nop -> 300 ns = 36 nop

    asm volatile(
        "nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop");
    asm volatile(
        "nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop");
    asm volatile(
        "nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop");
    asm volatile("nop \n nop \n nop \n nop \n nop \n nop");

    if (color & (1 << i)) {
      // 350 ns = 42 nop

      asm volatile(
          "nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop");
      asm volatile(
          "nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop");
      asm volatile(
          "nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop");
      asm volatile(
          "nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop");
      asm volatile("nop \n nop");

      gpio_clr_mask(1 << GP_RGB);

    } else {
      gpio_clr_mask(1 << GP_RGB);

      // 200 ns = 24 nop

      asm volatile(
          "nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop");
      asm volatile(
          "nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop");
      asm volatile("nop \n nop \n nop \n nop");
    }

    // 600 ns = 72 nop -> 570 ns = 68 nop

    asm volatile(
        "nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop");
    asm volatile(
        "nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop");
    asm volatile(
        "nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop");
    asm volatile(
        "nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop");
    asm volatile(
        "nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop");
    asm volatile(
        "nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop");
    asm volatile("nop \n nop \n nop \n nop \n nop \n nop \n nop \n nop");
  }
}
