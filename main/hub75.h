// Driver for the RP-HUB75 display board connected to the Saturn board over QSPI.
// Two 64x64 panels are daisy chained into one 128x64 framebuffer.
#pragma once

#include <stdint.h>

#define HUB75_W 128
#define HUB75_H 64

// RGB565 framebuffer, draw into it and call hub75_show()
extern uint16_t hub75_fb[HUB75_H][HUB75_W];

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}

void hub75_init(void);
void hub75_show(uint8_t brightness);
