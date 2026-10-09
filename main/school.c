// School identity show: only the school's #008ACC and white, no runtime assets.
#include "gfx.h"
#include "school.h"
#include "assets/school_logo.h"
#include <string.h>

#define SLOT 450
static const int8_t wave[32] = {0,2,4,6,7,8,9,10,10,10,9,8,7,6,4,2,
                              0,-2,-4,-6,-7,-8,-9,-10,-10,-10,-9,-8,-7,-6,-4,-2};
static void outline(int x, int y, int w, int h)
{
    gfx_rect(x, y, w, 1, C_WHITE); gfx_rect(x, y + h - 1, w, 1, C_WHITE);
    gfx_rect(x, y, 1, h, C_WHITE); gfx_rect(x + w - 1, y, 1, h, C_WHITE);
}
static void emblem(int x, int y, int scale)
{
    for (int py = 0; py < 48; py++)
        for (int px = 0; px < 48; px++)
            if (school_logo[py * 6 + px / 8] & (0x80 >> (px & 7)))
                gfx_rect(x + px * scale, y + py * scale, scale, scale, C_WHITE);
}
static void cloud(int x, int y)
{
    gfx_rect(x + 4, y + 4, 22, 6, C_WHITE);
    gfx_rect(x + 8, y + 1, 8, 9, C_WHITE);
    gfx_rect(x + 15, y + 3, 8, 7, C_WHITE);
    gfx_rect(x + 1, y + 6, 28, 3, C_WHITE);
}
void school_show(uint32_t t)
{
    int h = screen_h, cy = h / 2, scene = (t / SLOT) % 5;
    uint32_t lt = t % SLOT;
    gfx_rect(0, 0, HUB75_W, h, SCHOOL_BLUE);
    emblem(0, cy - 24, 1);
    if (scene == 0) {
        gfx_text(48, cy - 14, 2, C_WHITE, "SPŠ");
        gfx_text(48, cy + 3, 2, C_WHITE, "TRUTNOV");
        // White cloud fragments travel along the bottom edge without crossing the title.
        cloud(48 + (int)(t / 3 % 110), h - 11);
        cloud(48 + (int)((t / 5 + 55) % 110), h - 9);
    } else {
        gfx_text_center(86, cy - 26, 1, C_WHITE, "SPŠ TRUTNOV");
        if (scene == 1) {
            outline(62, cy - 14, 46, 29);
            gfx_rect(83, cy + 15, 4, 4, C_WHITE);
            gfx_rect(74, cy + 19, 23, 2, C_WHITE);
            for (int row = 0; row < 3; row++) {
                char bits[10];
                for (int i = 0; i < 9; i++) bits[i] = '0' + ((t / 10 + i * 13 + row * 7) >> (i % 5) & 1);
                bits[9] = 0;
                gfx_text(67, cy - 8 + row * 7, 1, C_WHITE, bits);
            }
        } else if (scene == 2) {
            outline(45, cy - 17, 79, 36);
            gfx_rect(45, cy - 10, 79, 1, C_WHITE);
            gfx_text(50, cy - 15, 1, C_WHITE, "> CODE");
            const char *const code[] = {"VOID LOOP() {", "  LED = 1;", "  WAIT(20);", "}"};
            int chars = lt / 3 % 80;
            for (int i = 0; i < 4; i++) {
                char line[20];
                int len = strlen(code[i]), count = chars < len ? chars : len;
                if (count < 0) count = 0;
                memcpy(line, code[i], count); line[count] = 0;
                gfx_text(50, cy - 6 + i * 6, 1, C_WHITE, line);
                chars -= len + 4;
            }
            if ((t / 20) & 1) gfx_rect(112, cy + 12, 3, 1, C_WHITE);
        } else if (scene == 3) {
            outline(79, cy - 10, 20, 20);
            for (int k = 0; k < 4; k++) {
                int y = cy - 7 + k * 4;
                gfx_rect(73, y, 6, 1, C_WHITE); gfx_rect(99, y, 6, 1, C_WHITE);
                gfx_rect(82 + k * 4, cy - 16, 1, 6, C_WHITE);
                gfx_rect(82 + k * 4, cy + 10, 1, 6, C_WHITE);
            }
            gfx_text(83, cy - 2, 1, C_WHITE, "CPU");
            gfx_rect(49, cy - 7, 24, 1, C_WHITE);
            gfx_rect(49, cy - 7, 1, 21, C_WHITE);
            gfx_rect(49, cy + 13, 33, 1, C_WHITE);
            gfx_rect(105, cy + 5, 15, 1, C_WHITE);
            gfx_rect(119, cy - 14, 1, 20, C_WHITE);
            int x = 49 + t / 3 % 24;
            gfx_rect(x, cy - 9, 3, 3, C_WHITE);
            gfx_text_center(86, cy + 21, 1, C_WHITE, "ELEKTRO");
        } else {
            outline(46, cy - 17, 77, 34);
            gfx_text(51, cy - 14, 1, C_WHITE, "SIGNAL");
            for (int x = 49; x < 121; x++) {
                int y = cy + 3 + wave[(x + t / 2) & 31];
                gfx_pixel(x, y, C_WHITE);
                int next = cy + 3 + wave[(x + 1 + t / 2) & 31];
                int lo = y < next ? y : next, hi = y > next ? y : next;
                gfx_rect(x, lo, 1, hi - lo + 1, C_WHITE);
            }
            gfx_text_center(86, cy + 21, 1, C_WHITE, "AUTOMATIZACE");
        }
    }
}
