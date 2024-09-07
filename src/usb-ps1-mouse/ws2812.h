#ifndef WS2812_H
#define WS2812_H

#include <stdint.h>

#define GP_RGB 16
#define WS2812_RESET_US 100

void set_ws2812(uint32_t color);

#endif
