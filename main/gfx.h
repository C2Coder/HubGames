// Drawing into the hub75 framebuffer, everything is clipped to the screen.
#pragma once

#include <stdint.h>
#include "hub75.h"

#define RGB(r, g, b) ((((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | ((b) >> 3))

#define C_BLACK  RGB(0, 0, 0)
#define C_WHITE  RGB(255, 255, 255)
#define C_GRAY   RGB(120, 120, 120)
#define C_DARK   RGB(40, 40, 40)
#define C_RED    RGB(255, 0, 0)
#define C_GREEN  RGB(0, 255, 0)
#define C_BLUE   RGB(0, 128, 255)
#define C_YELLOW RGB(255, 200, 0)
#define C_PINK   RGB(255, 0, 255)

typedef struct {
    int x, y, w, h;
} view_t;

void gfx_clear(void);
void gfx_pixel(int x, int y, uint16_t c);
void gfx_rect(int x, int y, int w, int h, uint16_t c);
void gfx_darken(int x, int y, int w, int h); // quarter brightness, for text over backgrounds
void gfx_scale2(void);                       // upscale the top-left 64x64 to 128x128
uint16_t gfx_hsv(uint8_t h, uint8_t s, uint8_t v);

// 3x5 font, each char is 4*scale wide including spacing
void gfx_text(int x, int y, int scale, uint16_t c, const char *s);
int gfx_text_width(int scale, const char *s);
void gfx_text_center(int cx, int y, int scale, uint16_t c, const char *s);
