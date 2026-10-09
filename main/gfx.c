#include "gfx.h"

#include <string.h>

// one octal digit per row, 4 = left pixel, 1 = right pixel
static const char glyph_chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ :!-.?/><";
static const char glyph_rows[][6] = {
    "75557", "26227", "71747", "71317", "55711", "74717", "74757", "71222", "75757", "75717",
    "25755", "65656", "34443", "65556", "74647", "74644", "34553", "55755", "72227", "11152",
    "55655", "44447", "57755", "65555", "25552", "65644", "25563", "65655", "34216", "72222",
    "55557", "55552", "55775", "55255", "55222", "71247",
    "00000", "02020", "22202", "00700", "00002", "61202", "11244", "42124", "12421",
};

void gfx_clear(void)
{
    memset(hub75_fb, 0, sizeof(hub75_fb));
}

void gfx_pixel(int x, int y, uint16_t c)
{
    if ((unsigned)x < HUB75_W && (unsigned)y < (unsigned)screen_h)
        hub75_fb[y][x] = c;
}

void gfx_rect(int x, int y, int w, int h, uint16_t c)
{
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > HUB75_W) x1 = HUB75_W;
    if (y1 > screen_h) y1 = screen_h;
    for (int py = y; py < y1; py++)
        for (int px = x; px < x1; px++)
            hub75_fb[py][px] = c;
}

void gfx_text(int x, int y, int scale, uint16_t c, const char *s)
{
    for (; *s; s++, x += 4 * scale) {
        char ch = *s;
        if (ch >= 'a' && ch <= 'z')
            ch -= 'a' - 'A';
        const char *p = strchr(glyph_chars, ch);
        if (!p)
            continue;
        const char *g = glyph_rows[p - glyph_chars];
        for (int r = 0; r < 5; r++)
            for (int col = 0; col < 3; col++)
                if ((g[r] - '0') & (4 >> col))
                    gfx_rect(x + col * scale, y + r * scale, scale, scale, c);
    }
}

int gfx_text_width(int scale, const char *s)
{
    int n = strlen(s);
    return n ? n * 4 * scale - scale : 0;
}

void gfx_text_center(int cx, int y, int scale, uint16_t c, const char *s)
{
    gfx_text(cx - gfx_text_width(scale, s) / 2, y, scale, c, s);
}

void gfx_darken(int x, int y, int w, int h)
{
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > HUB75_W) x1 = HUB75_W;
    if (y1 > screen_h) y1 = screen_h;
    for (int py = y; py < y1; py++)
        for (int px = x; px < x1; px++)
            hub75_fb[py][px] = (hub75_fb[py][px] >> 2) & 0x39e7; // quarter brightness
}

void gfx_scale2(void)
{
    // backwards so the source is read before it is overwritten
    for (int y = 63; y >= 0; y--)
        for (int x = 63; x >= 0; x--) {
            uint16_t c = hub75_fb[y][x];
            hub75_fb[2 * y][2 * x] = hub75_fb[2 * y][2 * x + 1] = c;
            hub75_fb[2 * y + 1][2 * x] = hub75_fb[2 * y + 1][2 * x + 1] = c;
        }
}

uint16_t gfx_hsv(uint8_t h, uint8_t s, uint8_t v)
{
    uint8_t region = h / 43, rem = (h - region * 43) * 6;
    uint8_t p = (v * (255 - s)) >> 8;
    uint8_t q = (v * (255 - ((s * rem) >> 8))) >> 8;
    uint8_t t = (v * (255 - ((s * (255 - rem)) >> 8))) >> 8;
    switch (region) {
    case 0: return RGB(v, t, p);
    case 1: return RGB(q, v, p);
    case 2: return RGB(p, v, t);
    case 3: return RGB(p, q, v);
    case 4: return RGB(t, p, v);
    default: return RGB(v, p, q);
    }
}
