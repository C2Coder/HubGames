// A small grid-based tower race: no line clears, overhangs slide, three lives.
#include "game.h"
#include <stdio.h>
#include <string.h>
#include "esp_random.h"

#define CELL 2
#define HUD 7
#define COLS (HUB75_W / CELL)
#define ROWS ((HUB75_H - HUD) / CELL)
static const uint8_t pieces[7][4][2] = {
    {{0,1},{1,1},{2,1},{3,1}}, {{1,1},{2,1},{1,2},{2,2}},
    {{1,1},{0,2},{1,2},{2,2}}, {{0,1},{0,2},{1,2},{2,2}},
    {{2,1},{0,2},{1,2},{2,2}}, {{1,1},{2,1},{0,2},{1,2}},
    {{0,1},{1,1},{1,2},{2,2}}
};
typedef struct {
    int rows, width, tick, end_t, target;
    player_mask_t players, alive, winner;
    uint8_t board[ROWS][COLS];
    struct { int16_t x, y; uint8_t start, kind, rot, lives, repeat, fall; int height; } tower[MAX_PLAYERS];
} towers_t;
_Static_assert(sizeof(towers_t) <= GAME_STATE_MAX, "towers state too big");
static void block(int kind, int rot, int i, int *x, int *y)
{
    *x = pieces[kind][i][0]; *y = pieces[kind][i][1];
    if (kind == 1) return;
    for (int k = 0; k < rot; k++) { int old = *x; *x = 3 - *y; *y = old; }
}
static bool fits(const towers_t *s, int pl, int x, int y, int rot)
{
    for (int i = 0; i < 4; i++) {
        int bx, by; block(s->tower[pl].kind, rot, i, &bx, &by);
        bx += x; by += y;
        if (bx < 0 || bx >= s->width - 1 || by >= s->rows) return false;
        if (by >= 0 && s->board[by][s->tower[pl].start + bx]) return false;
    }
    return true;
}
// true if any block of the piece would leave the player's well after moving by dx
static bool off_edge(const towers_t *s, int pl, int dx)
{
    for (int i = 0; i < 4; i++) {
        int bx, by; block(s->tower[pl].kind, s->tower[pl].rot, i, &bx, &by);
        bx += s->tower[pl].x + dx;
        if (bx < 0 || bx >= s->width - 1) return true;
    }
    return false;
}
static void spawn(towers_t *s, int pl)
{
    s->tower[pl].kind = esp_random() % 7; s->tower[pl].rot = 0;
    s->tower[pl].x = (s->width - 4) / 2; s->tower[pl].y = -1;
    s->tower[pl].fall = 0;
    if (!fits(s, pl, s->tower[pl].x, -1, 0)) s->alive &= ~(1u << pl);
}
static void init(void *p, view_t v, player_mask_t players, bool versus)
{
    (void)versus;
    towers_t *s = p;
    memset(s, 0, sizeof(*s)); s->players = s->alive = players;
    s->rows = (v.h - HUD) / CELL;
    int n = __builtin_popcount(players), rank = 0;
    s->width = COLS / (n ? n : 1); s->target = s->rows / 3;
    for (int pl = 0; pl < MAX_PLAYERS; pl++) if (players & (1u << pl)) {
        s->tower[pl].start = rank++ * s->width;
        s->tower[pl].lives = 3; spawn(s, pl);
    }
}
static void land(towers_t *s, int pl)
{
    int sum = 0, lo = COLS, hi = -1;
    for (int i = 0; i < 4; i++) {
        int x, y; block(s->tower[pl].kind, s->tower[pl].rot, i, &x, &y);
        x += s->tower[pl].x; y += s->tower[pl].y; sum += x;
        if (y + 1 >= s->rows || (y >= -1 && s->board[y + 1][s->tower[pl].start + x])) {
            if (x < lo) lo = x;
            if (x > hi) hi = x;
        }
    }
    int slide = sum < lo * 4 - 2 ? -1 : sum > hi * 4 + 2 ? 1 : 0;
    if (slide && fits(s, pl, s->tower[pl].x + slide, s->tower[pl].y, s->tower[pl].rot)) {
        s->tower[pl].x += slide;
        return; // gravity resumes; the rigid piece slides off its narrow support
    }
    if (slide && off_edge(s, pl, slide)) {
        fx_burst((s->tower[pl].start + s->tower[pl].x) * CELL,
                 HUD + s->tower[pl].y * CELL, player_color[pl], 8);
        if (!--s->tower[pl].lives) s->alive &= ~(1u << pl);
        else spawn(s, pl);
        return;
    }
    for (int i = 0; i < 4; i++) {
        int x, y; block(s->tower[pl].kind, s->tower[pl].rot, i, &x, &y);
        x += s->tower[pl].x; y += s->tower[pl].y;
        if (y < 0) { s->alive &= ~(1u << pl); continue; }
        s->board[y][s->tower[pl].start + x] = 1;
        if (s->rows - y > s->tower[pl].height) s->tower[pl].height = s->rows - y;
    }
    if (s->tower[pl].height >= s->rows - s->target) s->winner |= 1u << pl;
    if (s->alive & (1u << pl)) spawn(s, pl);
}
static game_status_t update(void *p, const input_t *in)
{
    towers_t *s = p;
    if (s->winner || __builtin_popcount(s->alive) <= 1) {
        if (!s->winner) s->winner = s->alive;
        return ++s->end_t >= 60 ? GAME_OVER : GAME_RUNNING;
    }
    s->tick++;
    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        if (!(s->alive & (1u << pl))) continue;
        if (s->tower[pl].repeat) s->tower[pl].repeat--;
        int move = !!(in[pl].held & BTN_RIGHT) - !!(in[pl].held & BTN_LEFT);
        if (move && (!s->tower[pl].repeat || (in[pl].pressed & (BTN_LEFT | BTN_RIGHT)))) {
            if (fits(s, pl, s->tower[pl].x + move, s->tower[pl].y, s->tower[pl].rot)) s->tower[pl].x += move;
            s->tower[pl].repeat = 7;
        }
        if (in[pl].pressed & BTN_A) {
            int rot = (s->tower[pl].rot + 1) & 3;
            for (int kick = 0; kick < 3; kick++) {
                int x = s->tower[pl].x + (kick == 1 ? -1 : kick == 2 ? 1 : 0);
                if (fits(s, pl, x, s->tower[pl].y, rot)) { s->tower[pl].x = x; s->tower[pl].rot = rot; break; }
            }
        }
        bool drop = in[pl].pressed & BTN_B;
        int interval = in[pl].held & BTN_DOWN ? 2 : 18;
        if (drop || ++s->tower[pl].fall >= interval) {
            s->tower[pl].fall = 0;
            if (drop) while (fits(s, pl, s->tower[pl].x, s->tower[pl].y + 1, s->tower[pl].rot)) s->tower[pl].y++;
            if (fits(s, pl, s->tower[pl].x, s->tower[pl].y + 1, s->tower[pl].rot)) s->tower[pl].y++;
            else land(s, pl);
        }
    }
    if (s->tick >= 4500) {
        int best = -1;
        for (int pl = 0; pl < MAX_PLAYERS; pl++) if (s->alive & (1u << pl)) {
            if (s->tower[pl].height > best) { best = s->tower[pl].height; s->winner = 0; }
            if (s->tower[pl].height == best) s->winner |= 1u << pl;
        }
    }
    return GAME_RUNNING;
}
static void draw(const void *p)
{
    const towers_t *s = p;
    char buf[16];
    for (int pl = 0; pl < MAX_PLAYERS; pl++) if (s->players & (1u << pl)) {
        int ox = s->tower[pl].start * CELL;
        snprintf(buf, sizeof(buf), "%d", pl + 1);
        gfx_text_center(ox + s->width, 0, 1, player_color[pl], buf);
        gfx_rect(ox + (s->width - 1) * CELL, HUD, 1, s->rows * CELL, C_DARK);
        gfx_rect(ox, HUD + s->rows * CELL - 1, (s->width - 1) * CELL, 1, C_WHITE);
        for (int x = 0; x < s->width - 1; x += 2) gfx_pixel(ox + x * CELL, HUD + s->target * CELL, C_GRAY);
        for (int y = 0; y < s->rows; y++)
            for (int x = 0; x < s->width - 1; x++)
                if (s->board[y][s->tower[pl].start + x])
                    gfx_rect(ox + x * CELL, HUD + y * CELL, CELL, CELL, (player_color[pl] >> 1) & 0x7bef);
        if (s->alive & (1u << pl)) for (int i = 0; i < 4; i++) {
            int x, y; block(s->tower[pl].kind, s->tower[pl].rot, i, &x, &y);
            y += s->tower[pl].y;
            if (y >= 0) gfx_rect(ox + (s->tower[pl].x + x) * CELL, HUD + y * CELL, CELL, CELL, player_color[pl]);
        }
        for (int i = 0; i < s->tower[pl].lives; i++) gfx_pixel(ox + i * 2, 5, C_WHITE);
    }
}
static int score_of(const void *p, int pl) { return ((const towers_t *)p)->tower[pl].height; }
static player_mask_t winners(const void *p) { return ((const towers_t *)p)->winner; }
const game_t game_towers = {.shared = true, .opaque = true, .big = BIG_NATIVE, .max_players = 8,
    .init = init, .update = update, .draw = draw, .score_of = score_of, .winners = winners};
