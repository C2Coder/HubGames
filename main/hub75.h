// Driver for the RP-HUB75 display board connected to the Saturn board over QSPI.
// 2 panels: 128x64 side by side. 4 panels: 2x2, 128x128. The panels are
// daisy chained, the chain order is set in panel_map in hub75.c.
#pragma once

#include <stdint.h>

#define HUB75_W 128
#define HUB75_H 128 // framebuffer height, the visible part is screen_h

extern int screen_h; // 64 or 128
extern int hub75_panels; // 2 or 4

// RGB565 framebuffer, draw into it and call hub75_show()
extern uint16_t hub75_fb[HUB75_H][HUB75_W];

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}

void hub75_init(void);
void hub75_set_panels(int panels);
void hub75_show(uint8_t brightness);
