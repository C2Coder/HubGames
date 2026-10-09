// Glue between Doom (components/doomgeneric) and the panels / phone.
#include "doom.h"

#include "doom_port.h"
#include "esp_psram.h"
#include "gfx.h"
#include "hub75.h"
#include "input.h"
#include "settings.h"

// Doom renders 320x200, shrink it with box averaging. 4 panels: 128x80
// letterboxed, 2 panels: squashed to 128x64.
static void draw(const uint32_t *fb)
{
    int h = screen_h == 128 ? 80 : 64, oy = (screen_h - h) / 2;
    gfx_clear();
    for (int y = 0; y < h; y++) {
        int y0 = y * 200 / h, y1 = (y + 1) * 200 / h;
        for (int x = 0; x < HUB75_W; x++) {
            int x0 = x * 320 / HUB75_W, x1 = (x + 1) * 320 / HUB75_W;
            uint32_t r = 0, g = 0, b = 0, n = 0;
            for (int sy = y0; sy < y1; sy++)
                for (int sx = x0; sx < x1; sx++) {
                    uint32_t c = fb[sy * 320 + sx];
                    r += (c >> 16) & 0xff;
                    g += (c >> 8) & 0xff;
                    b += c & 0xff;
                    n++;
                }
            hub75_fb[oy + y][x] = RGB(r / n, g / n, b / n);
        }
    }
    hub75_show(set_brightness);
}

// phone buttons -> Doom keys, A also confirms menus, B also answers yes
#define STRAFE_L 0x100 // virtual buttons from the joystick in strafe mode
#define STRAFE_R 0x200

static const struct {
    uint16_t btn;
    unsigned char key;
} keymap[] = {
    {BTN_UP, DOOM_KEY_UPARROW},
    {BTN_DOWN, DOOM_KEY_DOWNARROW},
    {BTN_LEFT, DOOM_KEY_LEFTARROW},
    {BTN_RIGHT, DOOM_KEY_RIGHTARROW},
    {BTN_A, DOOM_KEY_FIRE},
    {BTN_A, DOOM_KEY_ENTER},
    {BTN_B, DOOM_KEY_USE},
    {BTN_B, 'y'},
    {BTN_START, DOOM_KEY_ESCAPE},
    {STRAFE_L, DOOM_KEY_STRAFE_L},
    {STRAFE_R, DOOM_KEY_STRAFE_R},
};
#define KEYMAP_N (int)(sizeof(keymap) / sizeof(keymap[0]))

static struct {
    uint8_t pressed, key;
} queue[32];
static int q_head, q_tail;

static void push(int pressed, unsigned char key)
{
    queue[q_tail].pressed = pressed;
    queue[q_tail].key = key;
    q_tail = (q_tail + 1) % 32;
}

static int get_key(int *pressed, unsigned char *key)
{
    static uint16_t held;
    static bool started;

    if (q_head == q_tail) {
        if (!started) {
            started = true;
            push(1, DOOM_KEY_RSHIFT); // always run
        }
        input_t in;
        input_get(0, &in);
        uint16_t now = in.held;
        if (in.strafe && in.jx < -40)
            now |= STRAFE_L;
        if (in.strafe && in.jx > 40)
            now |= STRAFE_R;
        uint16_t changed = now ^ held;
        for (int i = 0; i < KEYMAP_N; i++)
            if (changed & keymap[i].btn)
                push(!!(now & keymap[i].btn), keymap[i].key);
        held = now;
    }
    if (q_head == q_tail)
        return 0;
    *pressed = queue[q_head].pressed;
    *key = queue[q_head].key;
    q_head = (q_head + 1) % 32;
    return 1;
}

bool doom_available(void)
{
    return esp_psram_is_initialized() && doom_wad_ok();
}

bool doom_run(void)
{
    doom_hooks_t h = {.draw = draw, .get_key = get_key};
    return doom_start(&h);
}
