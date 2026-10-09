// Pocket Racers: a whole-screen circuit, automatic throttle, drift and items.
#include "game.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_random.h"

#define HUD 8
#define LAPS 3
#define ITEMS 24
static const int16_t sn[64] = {
    0,25,50,74,98,121,142,162,181,198,213,226,237,245,251,255,
    256,255,251,245,237,226,213,198,181,162,142,121,98,74,50,25,
    0,-25,-50,-74,-98,-121,-142,-162,-181,-198,-213,-226,-237,-245,-251,-255,
    -256,-255,-251,-245,-237,-226,-213,-198,-181,-162,-142,-121,-98,-74,-50,-25
};
typedef struct { int32_t x, y; int16_t vx, vy; uint16_t life; uint8_t kind, owner; } item_t;
typedef struct {
    int h, tick, end_t;
    player_mask_t players, winner;
    struct {
        int32_t x, y;
        int16_t speed;
        uint8_t angle, checkpoint, lap, item, charge;
        uint16_t boost, stun, immune;
    } car[MAX_PLAYERS];
    item_t items[ITEMS];
    uint16_t crate[4];
} racers_t;
_Static_assert(sizeof(racers_t) <= GAME_STATE_MAX, "racers state too big");

static bool road(const racers_t *s, int x, int y)
{
    return x >= 4 && x < 124 && y >= HUD + 1 && y < s->h - 1 &&
        !(x >= 22 && x < 106 && y >= HUD + 17 && y < s->h - 17);
}
// Checkpoints are the four corner boxes, clockwise from top right. Every way
// around the infield passes through each box, so no racing line can skip one.
static bool at_checkpoint(const racers_t *s, int k, int x, int y)
{
    bool right = x >= 106, left = x < 22, top = y < HUD + 17, bottom = y >= s->h - 17;
    return k == 0 ? right && top : k == 1 ? right && bottom : k == 2 ? left && bottom : left && top;
}
static void init(void *p, view_t v, player_mask_t players, bool versus)
{
    (void)versus;
    racers_t *s = p;
    memset(s, 0, sizeof(*s));
    s->players = players; s->h = v.h;
    int rank = 0;
    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        if (!(players & (1u << pl))) continue;
        s->car[pl].x = (88 - rank / 2 * 8) * 256;
        s->car[pl].y = (HUD + 6 + (rank & 1) * 6) * 256;
        rank++;
    }
}
static game_status_t update(void *p, const input_t *in)
{
    racers_t *s = p;
    if (s->winner) return ++s->end_t >= 60 ? GAME_OVER : GAME_RUNNING;
    s->tick++;
    for (int k = 0; k < 4; k++) if (s->crate[k]) s->crate[k]--;
    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        if (!(s->players & (1u << pl))) continue;
        if (s->car[pl].stun) s->car[pl].stun--;
        if (s->car[pl].immune) s->car[pl].immune--;
        if (s->car[pl].boost) s->car[pl].boost--;
        int turn = 0;
        if (in[pl].held & BTN_LEFT) turn = -1;
        if (in[pl].held & BTN_RIGHT) turn = 1;
        bool drift = (in[pl].held & BTN_B) && turn;
        if (s->tick % (drift ? 2 : 3) == 0)
            s->car[pl].angle = (s->car[pl].angle + turn + 64) & 63;
        if (drift) {
            if (s->car[pl].charge < 100) s->car[pl].charge++;
        } else if (s->car[pl].charge) {
            if (s->car[pl].charge >= 35) s->car[pl].boost = 30;
            s->car[pl].charge = 0;
        }
        if ((in[pl].pressed & BTN_A) && s->car[pl].item) {
            int kind = s->car[pl].item;
            if (kind == 1) s->car[pl].boost = 65;
            else for (int i = 0; i < ITEMS; i++) if (!s->items[i].life) {
                int a = s->car[pl].angle;
                s->items[i] = (item_t){s->car[pl].x, s->car[pl].y,
                    kind == 3 ? sn[(a + 16) & 63] * 3 : 0,
                    kind == 3 ? sn[a] * 3 : 0, 250, kind, pl};
                break;
            }
            s->car[pl].item = 0;
        }
        int target = s->car[pl].stun ? 48 : s->car[pl].boost ? 320 : drift ? 140 : 205;
        if (s->car[pl].speed < target) s->car[pl].speed += 5;
        if (s->car[pl].speed > target) s->car[pl].speed -= 8;
        int a = s->car[pl].angle;
        int32_t nx = s->car[pl].x + sn[(a + 16) & 63] * s->car[pl].speed / 256;
        int32_t ny = s->car[pl].y + sn[a] * s->car[pl].speed / 256;
        int x = nx >> 8, y = ny >> 8;
        if (road(s, x - 2, y) && road(s, x + 2, y) && road(s, x, y - 2) && road(s, x, y + 2)) {
            s->car[pl].x = nx; s->car[pl].y = ny;
        } else s->car[pl].speed = 32;
        x = s->car[pl].x >> 8; y = s->car[pl].y >> 8;
        if (at_checkpoint(s, s->car[pl].checkpoint, x, y)) {
            if (++s->car[pl].checkpoint == 4) {
                s->car[pl].checkpoint = 0;
                if (++s->car[pl].lap == LAPS) s->winner |= 1u << pl;
                fx_burst(x, y, player_color[pl], 8);
            }
        }
        // Item boxes on the four straight sections.
        for (int k = 0; k < 4; k++) {
            int cx = k & 1 ? (k == 1 ? 115 : 13) : 64;
            int cy = k & 1 ? (HUD + s->h) / 2 : (k == 0 ? HUD + 9 : s->h - 9);
            int dx = x - cx, dy = y - cy;
            if (!s->crate[k] && !s->car[pl].item && dx * dx + dy * dy < 36) {
                s->car[pl].item = 1 + esp_random() % 3;
                s->crate[k] = 180;
            }
        }
    }
    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        if (!(s->players & (1u << pl))) continue;
        for (int o = pl + 1; o < MAX_PLAYERS; o++) {
            if (!(s->players & (1u << o))) continue;
            int dx = s->car[pl].x - s->car[o].x, dy = s->car[pl].y - s->car[o].y;
            if (abs(dx) >= 3 * 256 || abs(dy) >= 3 * 256) continue;
            int pushx = dx < 0 ? -96 : dx > 0 ? 96 : 0;
            int pushy = dy < 0 ? -96 : dy > 0 ? 96 : 0;
            if (!pushx && !pushy) pushy = 96;
            int32_t px = s->car[pl].x + pushx, py = s->car[pl].y + pushy;
            int32_t ox = s->car[o].x - pushx, oy = s->car[o].y - pushy;
            if (road(s, (px >> 8) - 2, py >> 8) && road(s, (px >> 8) + 2, py >> 8) &&
                road(s, px >> 8, (py >> 8) - 2) && road(s, px >> 8, (py >> 8) + 2)) {
                s->car[pl].x = px; s->car[pl].y = py;
            }
            if (road(s, (ox >> 8) - 2, oy >> 8) && road(s, (ox >> 8) + 2, oy >> 8) &&
                road(s, ox >> 8, (oy >> 8) - 2) && road(s, ox >> 8, (oy >> 8) + 2)) {
                s->car[o].x = ox; s->car[o].y = oy;
            }
        }
    }
    for (int i = 0; i < ITEMS; i++) {
        item_t *it = &s->items[i];
        if (!it->life) continue;
        it->life--;
        it->x += it->vx; it->y += it->vy;
        int x = it->x >> 8, y = it->y >> 8;
        if (!road(s, x, y)) { it->life = 0; continue; }
        for (int pl = 0; pl < MAX_PLAYERS; pl++) {
            if (!(s->players & (1u << pl)) || pl == it->owner || s->car[pl].immune) continue;
            int dx = x - (s->car[pl].x >> 8), dy = y - (s->car[pl].y >> 8);
            if (dx * dx + dy * dy < 25) {
                s->car[pl].stun = 45; s->car[pl].immune = 75;
                it->life = 0;
                fx_burst(x, y, player_color[pl], 10);
                break;
            }
        }
    }
    // A hard time limit also ends a race where nobody can finish.
    if (s->tick >= 4500 && !s->winner) {
        int best = -1;
        for (int pl = 0; pl < MAX_PLAYERS; pl++) {
            if (!(s->players & (1u << pl))) continue;
            int score = s->car[pl].lap * 4 + s->car[pl].checkpoint;
            if (score > best) { best = score; s->winner = 0; }
            if (score == best) s->winner |= 1u << pl;
        }
    }
    return GAME_RUNNING;
}
static void draw(const void *p)
{
    const racers_t *s = p;
    gfx_rect(4, HUD + 1, 120, s->h - HUD - 2, RGB(24, 24, 32));
    gfx_rect(22, HUD + 17, 84, s->h - HUD - 34, RGB(0, 40, 24));
    gfx_rect(4, HUD + 1, 120, 1, C_PINK);
    gfx_rect(4, s->h - 2, 120, 1, C_PINK);
    gfx_rect(4, HUD + 1, 1, s->h - HUD - 2, C_BLUE);
    gfx_rect(123, HUD + 1, 1, s->h - HUD - 2, C_BLUE);
    for (int y = HUD + 2; y < HUD + 17; y += 2)
        gfx_rect(40, y, 2, 1, C_WHITE);
    gfx_text(1, 0, 1, C_GRAY, "A ITEM B DRIFT");
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", (4500 - s->tick) / 50);
    gfx_text(114, 0, 1, C_WHITE, buf);
    for (int k = 0; k < 4; k++) if (!s->crate[k]) {
        int x = k & 1 ? (k == 1 ? 115 : 13) : 64;
        int y = k & 1 ? (HUD + s->h) / 2 : (k == 0 ? HUD + 9 : s->h - 9);
        gfx_rect(x - 2, y - 2, 4, 4, C_YELLOW);
        gfx_pixel(x, y, C_WHITE);
    }
    for (int i = 0; i < ITEMS; i++) if (s->items[i].life)
        gfx_rect((s->items[i].x >> 8) - 1, (s->items[i].y >> 8) - 1, 2, 2,
                 s->items[i].kind == 3 ? C_RED : C_YELLOW);
    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        if (!(s->players & (1u << pl))) continue;
        int x = s->car[pl].x >> 8, y = s->car[pl].y >> 8, a = s->car[pl].angle;
        gfx_rect(x - 1, y - 1, 3, 3, player_color[pl]);
        gfx_pixel(x + sn[(a + 16) & 63] / 128, y + sn[a] / 128, C_WHITE);
        if (s->car[pl].boost) gfx_pixel(x - sn[(a + 16) & 63] / 128, y - sn[a] / 128, C_YELLOW);
    }
    // The infield gives eight drivers their lap/item status without covering the track.
    int slot = 0;
    for (int pl = 0; pl < MAX_PLAYERS; pl++) if (s->players & (1u << pl)) {
        snprintf(buf, sizeof(buf), "P%d %d %c", pl + 1, s->car[pl].lap + 1,
                 s->car[pl].item == 1 ? 'T' : s->car[pl].item == 2 ? 'O' : s->car[pl].item == 3 ? 'S' : '-');
        gfx_text(25 + (slot % 2) * 40, HUD + 18 + (slot / 2) * 5, 1, player_color[pl], buf);
        slot++;
    }
}
static int score_of(const void *p, int pl)
{
    const racers_t *s = p;
    return s->car[pl].lap * 4 + s->car[pl].checkpoint;
}
static player_mask_t winners(const void *p) { return ((const racers_t *)p)->winner; }
const game_t game_racers = {.shared = true, .opaque = true, .big = BIG_NATIVE, .max_players = 8,
    .init = init, .update = update, .draw = draw, .score_of = score_of, .winners = winners};
