// Original micro FPS, inspired by early shooters. Integer DDA raycasting, no WAD/PSRAM.
#include "game.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAP 16
#define ENEMIES 6
#define FP 256
static const int16_t sn[64] = {
    0,25,50,74,98,121,142,162,181,198,213,226,237,245,251,255,
    256,255,251,245,237,226,213,198,181,162,142,121,98,74,50,25,
    0,-25,-50,-74,-98,-121,-142,-162,-181,-198,-213,-226,-237,-245,-251,-255,
    -256,-255,-251,-245,-237,-226,-213,-198,-181,-162,-142,-121,-98,-74,-50,-25
};
static const char *const level[MAP] = {
    "1111111111111111", "1000000000000001", "1000000200000001", "1000000200000001",
    "1000000200000001", "1000000000000001", "1222000000222201", "1000000000000001",
    "1000033000000001", "1000033000000001", "1000000000000001", "1033300000333001",
    "1000000000000001", "1000000000000001", "1000000000000001", "1111111111111111"
};
typedef struct {
    view_t v;
    int player, health, kills, tick, cooldown, flash, hurt, end_t;
    int32_t x, y;
    uint16_t yaw;
    bool won;
    struct { int32_t x, y; uint8_t health, attack; } enemy[ENEMIES];
} fps_t;
_Static_assert(sizeof(fps_t) <= PANEL_STATE_MAX, "fps state too big");
static int sine(int angle)
{
    angle &= 255;
    int a = angle >> 2;
    return sn[a] + (sn[(a + 1) & 63] - sn[a]) * (angle & 3) / 4;
}
static int wall(int x, int y)
{
    int mx = x / FP, my = y / FP;
    if (x < 0 || y < 0 || mx >= MAP || my >= MAP) return 1;
    return level[my][mx] - '0';
}
static bool free_at(int x, int y)
{
    return !wall(x - 40, y - 40) && !wall(x + 40, y - 40) &&
           !wall(x - 40, y + 40) && !wall(x + 40, y + 40);
}
// Distance along a ray, Q8. Ray components are Q8; perpendicular rays avoid fisheye.
static int cast(int px, int py, int rx, int ry, int *side, int *tile, int *texture)
{
    int mx = px / FP, my = py / FP;
    int ax = abs(rx), ay = abs(ry);
    int ddx = ax ? 65536 / ax : 1 << 20, ddy = ay ? 65536 / ay : 1 << 20;
    int stepx = rx < 0 ? -1 : 1, stepy = ry < 0 ? -1 : 1;
    int tx = ax ? (rx < 0 ? px % FP : FP - px % FP) * ddx / FP : 1 << 20;
    int ty = ay ? (ry < 0 ? py % FP : FP - py % FP) * ddy / FP : 1 << 20;
    int distance = FP;
    for (int step = 0; step < MAP * 2; step++) {
        if (tx < ty) { distance = tx; tx += ddx; mx += stepx; *side = 0; }
        else { distance = ty; ty += ddy; my += stepy; *side = 1; }
        if (mx < 0 || my < 0 || mx >= MAP || my >= MAP) { *tile = 1; break; }
        *tile = level[my][mx] - '0';
        if (*tile) break;
    }
    *texture = ((*side ? px + rx * distance / FP : py + ry * distance / FP) & 255);
    return distance < 24 ? 24 : distance;
}
static bool visible(const fps_t *s, int ex, int ey)
{
    int dx = ex - s->x, dy = ey - s->y;
    int steps = (abs(dx) > abs(dy) ? abs(dx) : abs(dy)) / 64 + 1;
    for (int k = 1; k < steps; k++)
        if (wall(s->x + dx * k / steps, s->y + dy * k / steps)) return false;
    return true;
}
static void init(void *p, view_t v, player_mask_t players, bool versus)
{
    (void)versus;
    fps_t *s = p;
    memset(s, 0, sizeof(*s)); s->v = v; s->player = __builtin_ctz(players);
    s->x = 3 * FP + FP / 2; s->y = 3 * FP + FP / 2; s->health = 100;
    const uint8_t pos[ENEMIES][2] = {{5,3},{10,4},{3,8},{11,8},{7,12},{12,13}};
    for (int i = 0; i < ENEMIES; i++) {
        s->enemy[i].x = pos[i][0] * FP + FP / 2;
        s->enemy[i].y = pos[i][1] * FP + FP / 2;
        s->enemy[i].health = 2;
    }
}
static game_status_t update(void *p, const input_t *in)
{
    fps_t *s = p;
    if (s->health <= 0 || s->won)
        return ++s->end_t >= 60 ? (s->won ? GAME_WON : GAME_OVER) : GAME_RUNNING;
    s->tick++;
    if (s->cooldown) s->cooldown--;
    if (s->flash) s->flash--;
    if (s->hurt) s->hurt--;
    int jx = in->jx, jy = in->jy;
    if (abs(jx) < 12) jx = 100 * (!!(in->held & BTN_RIGHT) - !!(in->held & BTN_LEFT));
    if (abs(jy) < 12) jy = 100 * (!!(in->held & BTN_DOWN) - !!(in->held & BTN_UP));
    bool strafe = in->held & BTN_B;
    if (!strafe) s->yaw += jx * 384 / 100;
    int angle = s->yaw >> 8, dx = sine(angle + 64), dy = sine(angle);
    int forward = -jy * 14 / 100, lateral = strafe ? jx * 14 / 100 : 0;
    if (forward && lateral) { forward = forward * 181 / 256; lateral = lateral * 181 / 256; }
    int nx = s->x + (dx * forward - dy * lateral) / FP;
    int ny = s->y + (dy * forward + dx * lateral) / FP;
    if (free_at(nx, s->y)) s->x = nx;
    if (free_at(s->x, ny)) s->y = ny;
    if ((in->held & BTN_A) && !s->cooldown) {
        s->cooldown = 12; s->flash = 4;
        int target = -1, best = 100000;
        for (int i = 0; i < ENEMIES; i++) {
            if (!s->enemy[i].health) continue;
            int ex = s->enemy[i].x - s->x, ey = s->enemy[i].y - s->y;
            int depth = (ex * dx + ey * dy) / FP, cross = abs(ex * dy - ey * dx) / FP;
            // Gentle aim assist makes touch controls usable at 128x64.
            if (depth > 0 && cross < 75 + depth / 12 && depth < best && visible(s, s->enemy[i].x, s->enemy[i].y)) {
                target = i; best = depth;
            }
        }
        if (target >= 0 && !--s->enemy[target].health) {
            s->kills++;
            fx_burst(s->v.x + s->v.w / 2, s->v.y + s->v.h / 2, C_RED, 5);
            s->health = s->health + 8 > 100 ? 100 : s->health + 8;
        }
    }
    for (int i = 0; i < ENEMIES; i++) {
        if (!s->enemy[i].health) continue;
        if (s->enemy[i].attack) s->enemy[i].attack--;
        int ex = s->x - s->enemy[i].x, ey = s->y - s->enemy[i].y;
        int distance = abs(ex) + abs(ey);
        if (distance < 180 && !s->enemy[i].attack) {
            s->health -= 10; s->hurt = 8; s->enemy[i].attack = 40;
        } else if (s->tick % 3 == i % 3 && distance < 8 * FP && visible(s, s->enemy[i].x, s->enemy[i].y)) {
            int nx = s->enemy[i].x + (ex > 0 ? 7 : ex < 0 ? -7 : 0);
            int ny = s->enemy[i].y + (ey > 0 ? 7 : ey < 0 ? -7 : 0);
            if (free_at(nx, s->enemy[i].y)) s->enemy[i].x = nx;
            if (free_at(s->enemy[i].x, ny)) s->enemy[i].y = ny;
        }
    }
    if (s->kills == ENEMIES && s->x > 13 * FP && s->y > 13 * FP) s->won = true;
    return GAME_RUNNING;
}
static void draw(const void *p)
{
    const fps_t *s = p; view_t v = s->v;
    int vh = v.h - 7, horizon = v.y + vh / 2;
    gfx_rect(v.x, v.y, v.w, vh / 2, RGB(16, 20, 40));
    for (int y = vh / 2; y < vh; y++)
        gfx_rect(v.x, v.y + y, v.w, 1, RGB(12 + y / 2, 12 + y / 3, 12 + y / 4));
    int angle = s->yaw >> 8, dx = sine(angle + 64), dy = sine(angle);
    int plane_x = -dy * 170 / FP, plane_y = dx * 170 / FP;
    int zbuf[HUB75_W];
    for (int x = 0; x < v.w; x++) {
        int camera = (2 * x - v.w) * FP / v.w;
        int rx = dx + plane_x * camera / FP, ry = dy + plane_y * camera / FP;
        int side = 0, tile = 1, texture = 0;
        int distance = cast(s->x, s->y, rx, ry, &side, &tile, &texture);
        zbuf[x] = distance;
        int height = vh * FP / distance;
        if (height > vh * 4) height = vh * 4;
        int top = horizon - height / 2, bottom = top + height;
        if (top < v.y) top = v.y;
        if (bottom > v.y + vh) bottom = v.y + vh;
        int light = 220 - distance / 12 - side * 35;
        if (light < 35) light = 35;
        uint16_t color = tile == 1 ? RGB(light, light / 3, light / 4) :
                         tile == 2 ? RGB(light / 3, light / 2, light) : RGB(light / 2, light, light / 3);
        uint16_t dark = (color >> 1) & 0x7bef;
        int uv_step = 65536 / (height ? height : 1);
        int uv = (top - horizon + height / 2) * uv_step;
        for (int y = top; y < bottom; y++, uv += uv_step) {
            bool mortar = (texture & 63) < 4 || ((uv >> 8) & 63) < 4;
            gfx_pixel(v.x + x, y, mortar ? dark : color);
        }
    }
    // Sort six billboards far-to-near; walls use the same perpendicular depth buffer.
    int order[ENEMIES], depth[ENEMIES], cross[ENEMIES];
    for (int i = 0; i < ENEMIES; i++) {
        order[i] = i;
        int ex = s->enemy[i].x - s->x, ey = s->enemy[i].y - s->y;
        depth[i] = (ex * dx + ey * dy) / FP;
        cross[i] = (ey * dx - ex * dy) / FP;
    }
    for (int i = 1; i < ENEMIES; i++) {
        int id = order[i], j = i;
        while (j && depth[order[j - 1]] < depth[id]) { order[j] = order[j - 1]; j--; }
        order[j] = id;
    }
    for (int k = 0; k < ENEMIES; k++) {
        int i = order[k], d = depth[i];
        if (!s->enemy[i].health || d < 40) continue;
        int center = v.w / 2 + cross[i] * v.w * 256 / (d * 340);
        int size = vh * 180 / d;
        if (size < 2) size = 2;
        if (size > vh * 2) size = vh * 2;
        int top = horizon - size / 2, left = center - size / 3;
        int right = center + size / 3;
        if (left < 0) left = 0;
        if (right >= v.w) right = v.w - 1;
        for (int x = left; x <= right; x++) {
            if (d >= zbuf[x]) continue;
            int lo = top < v.y ? v.y : top, hi = top + size;
            if (hi > v.y + vh) hi = v.y + vh;
            for (int y = lo; y < hi; y++) {
                int sy = (y - top) * 8 / size;
                uint16_t color = sy < 2 ? RGB(224, 96, 32) : sy < 6 ? C_RED : RGB(80, 32, 16);
                if (sy == 1 && abs(x - center) > size / 12) color = C_YELLOW;
                gfx_pixel(v.x + x, y, color);
            }
        }
    }
    if (s->kills == ENEMIES) gfx_text_center(v.x + v.w / 2, v.y + 2, 1, C_GREEN, "EXIT SE CORNER");
    int gun_x = v.x + v.w / 2, gun_y = v.y + vh - 11;
    gfx_rect(gun_x - 4, gun_y + 3, 9, 8, RGB(112, 112, 120));
    gfx_rect(gun_x - 2, gun_y, 5, 9, C_GRAY);
    if (s->flash) {
        gfx_rect(gun_x - 4, gun_y - 4, 9, 3, C_YELLOW);
        gfx_rect(gun_x - 1, gun_y - 6, 3, 7, C_WHITE);
    }
    gfx_rect(v.x + v.w / 2 - 2, horizon, 5, 1, C_WHITE);
    gfx_rect(v.x + v.w / 2, horizon - 2, 1, 5, C_WHITE);
    gfx_rect(v.x, v.y + vh, v.w, 7, C_BLACK);
    char buf[24];
    snprintf(buf, sizeof(buf), "HP %d K %d/%d", s->health > 0 ? s->health : 0, s->kills, ENEMIES);
    gfx_text(v.x + 1, v.y + vh + 1, 1, s->hurt ? C_RED : C_WHITE, buf);
}
static int score(const void *p) { const fps_t *s = p; return s->kills * 100 + (s->health > 0 ? s->health : 0); }
const game_t game_fps = {.vs_rule = VS_SCORE, .opaque = true, .big = BIG_NATIVE,
    .init = init, .update = update, .draw = draw, .score = score};
