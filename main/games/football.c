// Whole-pitch football, balanced teams, A kick, B sprint. Fixed point physics.
#include "game.h"
#include <stdio.h>
#include <string.h>

#define HUD 7
#define WIN_PTS 5
#define MATCH_T 6000
#define FP 256

typedef struct {
    int h, tick, serve, end_t;
    player_mask_t players, left, winner;
    int pts[2];
    int32_t bx, by, vx, vy;
    struct { int32_t x, y; int8_t dx, dy; uint8_t kick; uint16_t energy; } man[MAX_PLAYERS];
} football_t;
_Static_assert(sizeof(football_t) <= GAME_STATE_MAX, "football state too big");
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static void kickoff(football_t *s)
{
    int n = __builtin_popcount(s->players), rank = 0;
    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        if (!(s->players & (1u << pl))) continue;
        int side = !(s->left & (1u << pl));
        int team_n = side ? n / 2 : (n + 1) / 2;
        s->man[pl].x = (side ? 92 : 36) * FP;
        s->man[pl].y = (HUD + 3 + (rank / 2 + 1) * (s->h - HUD - 6) / (team_n + 1)) * FP;
        s->man[pl].dx = side ? -1 : 1; s->man[pl].dy = 0;
        s->man[pl].energy = 150;
        rank++;
    }
    s->bx = 64 * FP; s->by = ((HUD + s->h) / 2) * FP;
    s->vx = s->vy = 0; s->serve = 50;
}
static void init(void *p, view_t v, player_mask_t players, bool versus)
{
    (void)versus;
    football_t *s = p;
    memset(s, 0, sizeof(*s)); s->players = players; s->h = v.h;
    int rank = 0;
    for (int pl = 0; pl < MAX_PLAYERS; pl++) if (players & (1u << pl)) {
        if (!(rank & 1)) s->left |= 1u << pl;
        rank++;
    }
    kickoff(s);
}
static game_status_t update(void *p, const input_t *in)
{
    football_t *s = p;
    if (s->winner) return ++s->end_t >= 75 ? GAME_OVER : GAME_RUNNING;
    if (s->serve) { s->serve--; return GAME_RUNNING; }
    s->tick++;
    int goal_lo = (HUD + s->h) / 2 - (s->h - HUD) / 5;
    int goal_hi = (HUD + s->h) / 2 + (s->h - HUD) / 5;
    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        if (!(s->players & (1u << pl))) continue;
        int dx = !!(in[pl].held & BTN_RIGHT) - !!(in[pl].held & BTN_LEFT);
        int dy = !!(in[pl].held & BTN_DOWN) - !!(in[pl].held & BTN_UP);
        if (in[pl].jx > 20 || in[pl].jx < -20) dx = in[pl].jx > 0 ? 1 : -1;
        if (in[pl].jy > 20 || in[pl].jy < -20) dy = in[pl].jy > 0 ? 1 : -1;
        int sp = 150;
        if ((in[pl].held & BTN_B) && s->man[pl].energy >= 3) {
            sp = 245; s->man[pl].energy -= 3;
        } else if (s->man[pl].energy < 150) s->man[pl].energy++;
        if (dx && dy) sp = sp * 181 / 256;
        s->man[pl].x = clampi(s->man[pl].x + dx * sp, 4 * FP, 123 * FP);
        s->man[pl].y = clampi(s->man[pl].y + dy * sp, (HUD + 3) * FP, (s->h - 4) * FP);
        if (dx || dy) { s->man[pl].dx = dx; s->man[pl].dy = dy; }
        if (in[pl].pressed & BTN_A) s->man[pl].kick = 8; // buffer a kick while approaching
        int bx = (s->bx - s->man[pl].x) / FP, by = (s->by - s->man[pl].y) / FP;
        if (bx * bx + by * by < 25) {
            int strength = s->man[pl].kick ? 850 : 240;
            int kx = s->man[pl].dx, ky = s->man[pl].dy;
            if (kx && ky) strength = strength * 181 / 256;
            s->vx = kx * strength; s->vy = ky * strength;
            s->bx = s->man[pl].x + kx * 4 * FP;
            s->by = s->man[pl].y + ky * 4 * FP;
            if (s->man[pl].kick) fx_burst(s->bx / FP, s->by / FP, C_WHITE, 4);
            s->man[pl].kick = 0;
        }
        if (s->man[pl].kick) s->man[pl].kick--;
    }
    s->bx += s->vx; s->by += s->vy;
    s->vx = s->vx * 250 / 256; s->vy = s->vy * 250 / 256;
    if (s->by < (HUD + 2) * FP || s->by > (s->h - 3) * FP) {
        s->by = clampi(s->by, (HUD + 2) * FP, (s->h - 3) * FP);
        s->vy = -s->vy;
    }
    int scorer = -1;
    if (s->bx < 2 * FP || s->bx > 125 * FP) {
        if (s->by >= goal_lo * FP && s->by <= goal_hi * FP)
            scorer = s->bx < 64 * FP ? 1 : 0;
        else { s->bx = clampi(s->bx, 2 * FP, 125 * FP); s->vx = -s->vx; }
    }
    if (scorer >= 0) {
        fx_burst(scorer ? 1 : 126, s->by / FP, scorer ? C_PINK : C_BLUE, 30);
        if (++s->pts[scorer] >= WIN_PTS) s->winner = scorer ? s->players & ~s->left : s->left;
        else kickoff(s);
    }
    if (s->tick >= MATCH_T) {
        if (s->pts[0] != s->pts[1]) s->winner = s->pts[0] > s->pts[1] ? s->left : s->players & ~s->left;
        else return GAME_OVER;
    }
    return GAME_RUNNING;
}
static void draw(const void *p)
{
    const football_t *s = p;
    int cy = (HUD + s->h) / 2, gh = (s->h - HUD) / 5;
    for (int y = HUD; y < s->h; y++)
        gfx_rect(1, y, 126, 1, (y / 6) & 1 ? RGB(0, 32, 12) : RGB(0, 40, 16));
    gfx_rect(1, HUD, 126, 1, C_GRAY); gfx_rect(1, s->h - 1, 126, 1, C_GRAY);
    gfx_rect(1, HUD, 1, s->h - HUD, C_GRAY); gfx_rect(126, HUD, 1, s->h - HUD, C_GRAY);
    gfx_rect(63, HUD, 1, s->h - HUD, RGB(80, 100, 80));
    gfx_rect(0, cy - gh, 2, gh * 2 + 1, C_BLUE);
    gfx_rect(126, cy - gh, 2, gh * 2 + 1, C_PINK);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d : %d  %d", s->pts[0], s->pts[1], (MATCH_T - s->tick) / 50);
    gfx_text_center(64, 0, 1, C_WHITE, buf);
    for (int pl = 0; pl < MAX_PLAYERS; pl++) if (s->players & (1u << pl)) {
        int x = s->man[pl].x / FP, y = s->man[pl].y / FP;
        gfx_rect(x - 1, y - 1, 3, 3, player_color[pl]);
        gfx_rect(x - 1, y + 2, 3, 1, s->left & (1u << pl) ? C_BLUE : C_PINK);
        if (s->man[pl].energy < 20) gfx_pixel(x, y, C_DARK);
    }
    gfx_rect(s->bx / FP - 1, s->by / FP - 1, 3, 3, C_WHITE);
    gfx_pixel(s->bx / FP, s->by / FP, C_YELLOW);
    if (s->serve) gfx_text_center(64, cy - 11, 1, C_YELLOW, "KICKOFF");
}
static int score_of(const void *p, int pl)
{
    const football_t *s = p;
    return s->pts[!(s->left & (1u << pl))];
}
static player_mask_t winners(const void *p) { return ((const football_t *)p)->winner; }
const game_t game_football = {.shared = true, .opaque = true, .big = BIG_NATIVE, .init = init, .update = update,
    .draw = draw, .score_of = score_of, .winners = winners};
