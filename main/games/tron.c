// Light trails: simultaneous cell movement, permanent trails, A = boost.
#include "game.h"
#include <stdio.h>
#include <string.h>

#define CELL 2
#define HUD 6
#define COLS (HUB75_W / CELL)
#define ROWS ((HUB75_H - HUD) / CELL)

typedef struct {
    player_mask_t players, alive;
    int rows, tick, end_t;
    uint8_t trail[ROWS][COLS]; // player + 1
    struct { int16_t x, y; int8_t dx, dy, qx, qy; int score; } bike[MAX_PLAYERS];
} tron_t;
_Static_assert(sizeof(tron_t) <= GAME_STATE_MAX, "tron state too big");

static void init(void *p, view_t v, player_mask_t players, bool versus)
{
    (void)versus;
    tron_t *s = p;
    memset(s, 0, sizeof(*s));
    s->players = s->alive = players;
    s->rows = (v.h - HUD) / CELL;
    int n = __builtin_popcount(players), rank = 0;
    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        if (!(players & (1u << pl))) continue;
        int side = rank & 1;
        s->bike[pl].x = side ? COLS * 4 / 5 : COLS / 5;
        s->bike[pl].y = (rank / 2 + 1) * s->rows / ((n + 1) / 2 + 1);
        s->bike[pl].dx = s->bike[pl].qx = side ? -1 : 1;
        s->trail[s->bike[pl].y][s->bike[pl].x] = pl + 1;
        rank++;
    }
}

static game_status_t update(void *p, const input_t *in)
{
    tron_t *s = p;
    if (__builtin_popcount(s->alive) <= 1)
        return ++s->end_t >= 60 ? GAME_OVER : GAME_RUNNING;
    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        if (!(s->alive & (1u << pl))) continue;
        int dx = 0, dy = 0;
        if (in[pl].pressed & BTN_LEFT) dx = -1;
        else if (in[pl].pressed & BTN_RIGHT) dx = 1;
        else if (in[pl].pressed & BTN_UP) dy = -1;
        else if (in[pl].pressed & BTN_DOWN) dy = 1;
        if ((dx || dy) && !(dx == -s->bike[pl].dx && dy == -s->bike[pl].dy)) {
            s->bike[pl].qx = dx; s->bike[pl].qy = dy;
        }
    }
    if (++s->tick % 3) return GAME_RUNNING;
    player_mask_t moving = 0, died = 0;
    int nx[MAX_PLAYERS], ny[MAX_PLAYERS];
    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        if (!(s->alive & (1u << pl))) continue;
        if (s->tick % 6 && !(in[pl].held & BTN_A)) continue;
        moving |= 1u << pl;
        s->bike[pl].dx = s->bike[pl].qx;
        s->bike[pl].dy = s->bike[pl].qy;
        nx[pl] = s->bike[pl].x + s->bike[pl].dx;
        ny[pl] = s->bike[pl].y + s->bike[pl].dy;
        if (nx[pl] < 0 || nx[pl] >= COLS || ny[pl] < 0 || ny[pl] >= s->rows ||
            s->trail[ny[pl]][nx[pl]]) died |= 1u << pl;
    }
    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        if (!(moving & (1u << pl))) continue;
        for (int o = pl + 1; o < MAX_PLAYERS; o++)
            if ((moving & (1u << o)) && nx[o] == nx[pl] && ny[o] == ny[pl])
                died |= (1u << pl) | (1u << o);
    }
    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        if (!(moving & (1u << pl))) continue;
        if (died & (1u << pl)) {
            fx_burst(s->bike[pl].x * CELL, HUD + s->bike[pl].y * CELL, player_color[pl], 16);
        } else {
            s->bike[pl].x = nx[pl]; s->bike[pl].y = ny[pl];
            s->trail[ny[pl]][nx[pl]] = pl + 1;
            s->bike[pl].score++;
        }
    }
    s->alive &= ~died;
    return GAME_RUNNING;
}

static void draw(const void *p)
{
    const tron_t *s = p;
    gfx_text(1, 0, 1, C_GRAY, "TRON  A BOOST");
    for (int y = 0; y < s->rows; y++)
        for (int x = 0; x < COLS; x++) {
            int pl = s->trail[y][x] - 1;
            if (pl < 0) continue;
            uint16_t c = (player_color[pl] >> 1) & 0x7bef;
            gfx_rect(x * CELL, HUD + y * CELL, CELL, CELL, c);
        }
    for (int pl = 0; pl < MAX_PLAYERS; pl++)
        if (s->alive & (1u << pl)) {
            int x = s->bike[pl].x * CELL, y = HUD + s->bike[pl].y * CELL;
            gfx_rect(x, y, CELL, CELL, player_color[pl]);
            gfx_pixel(x, y, C_WHITE);
        }
}
static int score_of(const void *p, int pl) { return ((const tron_t *)p)->bike[pl].score; }
static player_mask_t winners(const void *p) { return ((const tron_t *)p)->alive; }
const game_t game_tron = {.shared = true, .opaque = true, .big = BIG_NATIVE, .init = init, .update = update,
    .draw = draw, .score_of = score_of, .winners = winners};
