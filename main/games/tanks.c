// Tanks - top-down tank battle. 1 PLAYER: waves of AI tanks. VERSUS/PARTY:
// free-for-all rounds, first to 3 round wins.
#include "game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_random.h"

#define TILE      4
#define HUD       6
#define COLS      32
#define MAX_ROWS  30
#define NT        6   // tanks: 4 players, or player + 5 AI
#define NB        32  // bullets
#define TS        5   // tank size in px
#define PROT_T    50  // spawn protection, ticks
#define COOLDOWN  20
#define MAX_OWN   3
#define B_SPEED   470 // 8.8 px/tick
#define B_DIAG    332
#define T_SPEED   180
#define AI_SPEED  140
#define PAUSE_T   100
#define WAVE_T    50
#define WIN_ROUNDS 3
#define LIVES     3
#define LAST_WAVE 5

#define C_WALL    RGB(80, 90, 130)
#define C_WALL_HI RGB(125, 135, 175)
#define C_BRICK   RGB(170, 80, 40)
#define C_MORTAR  RGB(100, 45, 25)
#define C_AI      RGB(170, 60, 60)
#define C_ORANGE  RGB(255, 140, 0)

typedef struct {
    int32_t x, y; // top-left of the box, 8.8 fixed point px
    int8_t fx, fy; // facing
    int prot, cd;
    bool alive, ai;
    int8_t adx, ady; // AI move direction
    int turn_t, aim_t, stuck_t, lx, ly;
} tank_t;

typedef struct {
    int32_t x, y, vx, vy;
    int8_t owner;
    uint8_t bounces;
    bool alive;
} bullet_t;

enum { ST_PLAY, ST_PAUSE, ST_END };

typedef struct {
    uint8_t players;
    bool versus;
    int rows, layout;
    uint8_t grid[MAX_ROWS][COLS]; // 0 empty, 1 wall, 2 brick
    tank_t t[NT];
    int nt;
    bullet_t b[NB];
    int st, pt;
    game_status_t result;
    // versus
    int wins[MAX_PLAYERS];
    int round_winner; // -1 = nobody
    uint8_t win_mask;
    // single
    int lives, wave, kills, cleared, respawn_t;
} tanks_t;

_Static_assert(sizeof(tanks_t) <= GAME_STATE_MAX, "tanks state too big");

// left half of the arena, mirrored at init. 15 rows: 2 panels use rows 0-12 and
// a border row, 4 panels use them mirrored vertically. # wall, b brick.
#define NL 4
static const char layouts[NL][15][17] = {
    {
        "################", "#...............", "#...............", "#...............",
        "#....##.....bb..", "#....##.....bb..", "#...............", "#..bb.....##....",
        "#..bb.....##....", "#...............", "#...............", "#........bb.....",
        "#........bb.....", "#...............", "#...............",
    },
    {
        "################", "#...............", "#...............", "#...............",
        "#...######......", "#...............", "#...............", "#.........bbbb..",
        "#.........bbbb..", "#...............", "#...............", "#...............",
        "#.....######....", "#...............", "#...............",
    },
    {
        "################", "#...............", "#...............", "#...............",
        "#...bbbb........", "#...b...........", "#...b...........", "#........####...",
        "#...............", "#...b...........", "#...b...........", "#...bbbb........",
        "#...............", "#...............", "#...............",
    },
    {
        "################", "#...............", "#....#..........", "#....#..........",
        "#....#...bb.....", "#....#...bb.....", "#...............", "#...........#...",
        "#..##.......#...", "#..##.......#...", "#...............", "#.........#.....",
        "#.........#.....", "#...............", "#...............",
    },
};

static uint8_t wall_dummy = 1;

static int rnd(int n)
{
    return esp_random() % n;
}

static int sgn(int v)
{
    return (v > 0) - (v < 0);
}

static bool playing(const tanks_t *s, int p)
{
    return (s->players >> p) & 1;
}

// ---- arena ----

static void load_layout(tanks_t *s)
{
    s->layout = (s->layout + 1 + rnd(NL - 1)) % NL;
    for (int r = 0; r < s->rows; r++) {
        int lr = r < 15 ? r : s->rows - 1 - r;
        for (int c = 0; c < COLS; c++) {
            char ch = layouts[s->layout][lr][c < 16 ? c : 31 - c];
            uint8_t v = ch == '#' ? 1 : ch == 'b' ? 2 : 0;
            if (r == 0 || r == s->rows - 1 || c == 0 || c == COLS - 1)
                v = 1;
            s->grid[r][c] = v;
        }
    }
}

static uint8_t *tile_at(tanks_t *s, int px, int py)
{
    int tx = px / TILE, ty = (py - HUD) / TILE;
    if (px < 0 || py < HUD || tx >= COLS || ty >= s->rows)
        return &wall_dummy;
    return &s->grid[ty][tx];
}

static bool box_free(tanks_t *s, int px, int py, int self)
{
    for (int y = py; y < py + TS; y += TS - 1)
        for (int x = px; x < px + TS; x += TS - 1)
            if (*tile_at(s, x, y))
                return false;
    for (int i = 0; i < s->nt; i++) {
        const tank_t *o = &s->t[i];
        if (i != self && o->alive && abs((o->x >> 8) - px) < TS && abs((o->y >> 8) - py) < TS)
            return false;
    }
    return true;
}

static bool los(tanks_t *s, int x0, int y0, int x1, int y1)
{
    int dx = x1 - x0, dy = y1 - y0;
    int n = (abs(dx) > abs(dy) ? abs(dx) : abs(dy)) / 2 + 1;
    for (int k = 1; k < n; k++)
        if (*tile_at(s, x0 + dx * k / n, y0 + dy * k / n))
            return false;
    return true;
}

// ---- tanks ----

// spawn spots (tile of a 2x2 block): corners in player order, then more
static void spot(const tanks_t *s, int k, int *tx, int *ty)
{
    static const int8_t sp[8][2] = {{1, 1}, {29, -3}, {29, 1}, {1, -3}, {15, 1}, {15, -3}, {1, 0}, {29, 0}};
    *tx = sp[k][0];
    *ty = sp[k][1] == -3 ? s->rows - 3 : sp[k][1] == 0 ? s->rows / 2 - 1 : sp[k][1];
}

static void start_tank(tanks_t *s, int i, int k, bool ai)
{
    int tx, ty;
    spot(s, k, &tx, &ty);
    tank_t *t = &s->t[i];
    memset(t, 0, sizeof(*t));
    t->alive = true;
    t->ai = ai;
    t->x = (tx * TILE + 2) << 8;
    t->y = (HUD + ty * TILE + 2) << 8;
    t->fx = tx < COLS / 2 ? 1 : -1;
    t->prot = ai ? 0 : PROT_T;
    t->aim_t = -1;
    t->lx = -1;
}

// free spot farthest from the other living tanks
static int best_spot(tanks_t *s, int i)
{
    int best = 0, bd = -1;
    for (int k = 0; k < 8; k++) {
        int tx, ty;
        spot(s, k, &tx, &ty);
        int px = tx * TILE + 2, py = HUD + ty * TILE + 2;
        if (!box_free(s, px, py, i))
            continue;
        int md = 1 << 20;
        for (int j = 0; j < s->nt; j++) {
            if (j == i || !s->t[j].alive)
                continue;
            int d = abs((s->t[j].x >> 8) - px) + abs((s->t[j].y >> 8) - py);
            if (d < md)
                md = d;
        }
        if (md > bd) {
            bd = md;
            best = k;
        }
    }
    return best;
}

static void nudge(tanks_t *s, int i, int dx, int dy, int sp)
{
    tank_t *t = &s->t[i];
    int px = t->x >> 8, py = t->y >> 8;
    for (int o = 1; o <= 2; o++)
        for (int sg = -1; sg <= 1; sg += 2) {
            if (dx && box_free(s, px + dx, py + sg * o, i) && box_free(s, px, (t->y + sg * sp) >> 8, i)) {
                t->y += sg * sp;
                return;
            }
            if (dy && box_free(s, px + sg * o, py + dy, i) && box_free(s, (t->x + sg * sp) >> 8, py, i)) {
                t->x += sg * sp;
                return;
            }
        }
}

// axis separated, so tanks slide along walls; a pure move that is blocked by a
// corner is nudged sideways
static void move_tank(tanks_t *s, int i, int dx, int dy)
{
    tank_t *t = &s->t[i];
    int base = t->ai ? AI_SPEED : T_SPEED;
    int sp = dx && dy ? base * 181 / 256 : base;
    if (dx) {
        int nx = t->x + dx * sp;
        if (box_free(s, nx >> 8, t->y >> 8, i))
            t->x = nx;
        else if (!dy)
            nudge(s, i, dx, 0, sp);
    }
    if (dy) {
        int ny = t->y + dy * sp;
        if (box_free(s, t->x >> 8, ny >> 8, i))
            t->y = ny;
        else if (!dx)
            nudge(s, i, 0, dy, sp);
    }
}

static void read_dir(const input_t *in, const tank_t *t, int *dx, int *dy)
{
    int ax = abs(in->jx), ay = abs(in->jy);
    if (ax > 30 || ay > 30) { // analog, 8 directions
        *dx = ax * 100 > ay * 41 ? sgn(in->jx) : 0;
        *dy = ay * 100 > ax * 41 ? sgn(in->jy) : 0;
        return;
    }
    *dx = !!(in->held & BTN_RIGHT) - !!(in->held & BTN_LEFT);
    *dy = !!(in->held & BTN_DOWN) - !!(in->held & BTN_UP);
    if (*dx && *dy) { // arrows give 4 directions, take the new axis
        if (t->fy == 0)
            *dx = 0;
        else
            *dy = 0;
    }
}

static void fire_bullet(tanks_t *s, int i)
{
    tank_t *t = &s->t[i];
    int own = 0, slot = -1;
    for (int j = 0; j < NB; j++) {
        if (!s->b[j].alive)
            slot = j;
        else if (s->b[j].owner == i)
            own++;
    }
    if (t->cd > 0 || slot < 0 || own >= (t->ai ? 2 : MAX_OWN))
        return;
    t->cd = COOLDOWN;
    bool diag = t->fx && t->fy;
    int off = diag ? 3 : 4;
    int32_t cx = t->x + (TS * 128), cy = t->y + (TS * 128);
    int32_t bx = cx + t->fx * off * 256, by = cy + t->fy * off * 256;
    if (*tile_at(s, bx >> 8, by >> 8)) { // barrel is inside a wall
        bx = cx;
        by = cy;
    }
    bullet_t *b = &s->b[slot];
    b->x = bx;
    b->y = by;
    b->vx = t->fx * (diag ? B_DIAG : B_SPEED);
    b->vy = t->fy * (diag ? B_DIAG : B_SPEED);
    b->owner = i;
    b->bounces = 0;
    b->alive = true;
    fx_burst(bx >> 8, by >> 8, C_YELLOW, 4);
}

static uint16_t tank_color(const tanks_t *s, int i)
{
    return s->t[i].ai ? C_AI : player_color[i];
}

static void kill_tank(tanks_t *s, int k, int owner)
{
    tank_t *t = &s->t[k];
    int x = (t->x >> 8) + TS / 2, y = (t->y >> 8) + TS / 2;
    t->alive = false;
    fx_burst(x, y, tank_color(s, k), 20);
    fx_burst(x, y, C_RED, 10);
    fx_burst(x, y, C_ORANGE, 10);
    if (s->versus)
        return;
    if (k == 0) {
        if (--s->lives <= 0) {
            s->st = ST_END;
            s->pt = 0;
            s->result = GAME_OVER;
        } else {
            s->respawn_t = 50;
        }
    } else if (owner == 0) {
        s->kills++;
    }
}

// ---- bullets ----

static void hit_wall(bullet_t *b, uint8_t *tile)
{
    if (*tile == 2) {
        *tile = 0;
        fx_burst(b->x >> 8, b->y >> 8, C_BRICK, 6);
        b->alive = false;
    } else if (b->bounces >= 2) {
        b->alive = false;
    } else {
        b->bounces++;
        fx_burst(b->x >> 8, b->y >> 8, C_WHITE, 2);
    }
}

static void update_bullets(tanks_t *s, bool hits)
{
    for (int j = 0; j < NB; j++) {
        bullet_t *b = &s->b[j];
        for (int sub = 0; sub < 2 && b->alive; sub++) {
            int32_t nx = b->x + b->vx / 2;
            uint8_t *tl = tile_at(s, nx >> 8, b->y >> 8);
            if (*tl) {
                hit_wall(b, tl);
                if (b->alive)
                    b->vx = -b->vx;
            } else {
                b->x = nx;
            }
            if (!b->alive)
                break;
            int32_t ny = b->y + b->vy / 2;
            tl = tile_at(s, b->x >> 8, ny >> 8);
            if (*tl) {
                hit_wall(b, tl);
                if (b->alive)
                    b->vy = -b->vy;
            } else {
                b->y = ny;
            }
            if (!b->alive || !hits)
                continue;
            int bx = b->x >> 8, by = b->y >> 8;
            for (int k = 0; k < s->nt; k++) {
                const tank_t *t = &s->t[k];
                if (!t->alive || t->prot > 0 || (k == b->owner && b->bounces == 0))
                    continue;
                int tx = t->x >> 8, ty = t->y >> 8;
                if (bx >= tx && bx < tx + TS && by >= ty && by < ty + TS) {
                    b->alive = false;
                    kill_tank(s, k, b->owner);
                    break;
                }
            }
        }
    }
    for (int j = 0; j < NB; j++)
        for (int k = j + 1; k < NB; k++) {
            bullet_t *a = &s->b[j], *c = &s->b[k];
            if (a->alive && c->alive && abs(a->x - c->x) < 384 && abs(a->y - c->y) < 384) {
                fx_burst(a->x >> 8, a->y >> 8, C_WHITE, 3);
                a->alive = c->alive = false;
            }
        }
}

// ---- AI ----

static void ai_pick(tanks_t *s, int i)
{
    static const int8_t d8[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    tank_t *t = &s->t[i];
    const tank_t *p = &s->t[0];
    int ddx = (p->x - t->x) >> 8, ddy = (p->y - t->y) >> 8;
    bool stuck = t->stuck_t >= 6;
    int dx = 0, dy = 0;
    for (int tries = 0; tries < 4; tries++) {
        int r = rnd(100);
        dx = dy = 0;
        if (p->alive && !stuck && r < 50) { // along the longer axis towards the player
            if (abs(ddx) >= abs(ddy)) dx = sgn(ddx); else dy = sgn(ddy);
        } else if (p->alive && !stuck && r < 75) { // the other axis
            if (abs(ddx) >= abs(ddy)) dy = sgn(ddy); else dx = sgn(ddx);
        }
        if (!dx && !dy) {
            int k = rnd(10) < 8 ? rnd(4) : rnd(8);
            dx = d8[k][0];
            dy = d8[k][1];
        }
        if (!stuck || dx != t->adx || dy != t->ady)
            break;
    }
    t->adx = dx;
    t->ady = dy;
    t->turn_t = 20 + rnd(40);
    t->stuck_t = 0;
}

static void ai_think(tanks_t *s, int i, int *dx, int *dy, bool *fire)
{
    tank_t *t = &s->t[i];
    const tank_t *p = &s->t[0];
    int ox = t->x >> 8, oy = t->y >> 8;
    if (ox == t->lx && oy == t->ly)
        t->stuck_t++;
    else
        t->stuck_t = 0;
    t->lx = ox;
    t->ly = oy;
    if (--t->turn_t <= 0 || t->stuck_t >= 6 || (!t->adx && !t->ady))
        ai_pick(s, i);

    *fire = false;
    int ddx = (p->x - t->x) >> 8, ddy = (p->y - t->y) >> 8;
    int ax = 0, ay = 0;
    bool aligned = false;
    if (p->alive) {
        if (abs(ddx) <= 4) { ay = sgn(ddy); aligned = true; }
        else if (abs(ddy) <= 4) { ax = sgn(ddx); aligned = true; }
        else if (abs(abs(ddx) - abs(ddy)) <= 3) { ax = sgn(ddx); ay = sgn(ddy); aligned = true; }
    }
    if (aligned && los(s, ox + TS / 2, oy + TS / 2, ox + ddx + TS / 2, oy + ddy + TS / 2)) {
        t->adx = ax;
        t->ady = ay;
        if (t->aim_t < 0)
            t->aim_t = 12 + rnd(14); // reaction delay
        else if (--t->aim_t <= 0) {
            *fire = true;
            t->aim_t = 20 + rnd(25);
        }
    } else {
        t->aim_t = -1;
    }
    *dx = t->adx;
    *dy = t->ady;
}

// ---- rounds ----

static void clear_bullets(tanks_t *s)
{
    memset(s->b, 0, sizeof(s->b));
}

static void new_round(tanks_t *s)
{
    load_layout(s);
    clear_bullets(s);
    for (int p = 0; p < MAX_PLAYERS; p++) {
        s->t[p].alive = false;
        if (playing(s, p))
            start_tank(s, p, p, false);
    }
    s->st = ST_PLAY;
    s->pt = 0;
    s->round_winner = -1;
}

static void start_wave(tanks_t *s)
{
    int n = s->wave;
    int nai = s->rows > 14 ? (n + 1 < 5 ? n + 1 : 5) : (n < 3 ? n : 3);
    load_layout(s);
    clear_bullets(s);
    s->nt = 1 + nai;
    start_tank(s, 0, 0, false);
    for (int i = 1; i <= nai; i++)
        start_tank(s, i, best_spot(s, i), true);
    s->respawn_t = 0;
    s->st = ST_PLAY;
    s->pt = 0;
}

static void init(void *p, view_t v, uint8_t players, bool versus)
{
    tanks_t *s = p;
    memset(s, 0, sizeof(*s));
    s->players = players;
    s->versus = versus;
    s->rows = (v.h - HUD) / TILE;
    if (s->rows > MAX_ROWS)
        s->rows = MAX_ROWS;
    s->layout = rnd(NL);
    s->round_winner = -1;
    if (versus) {
        s->nt = MAX_PLAYERS;
        new_round(s);
    } else {
        s->lives = LIVES;
        s->wave = 1;
        start_wave(s);
    }
}

static void check_versus(tanks_t *s)
{
    int alive = 0, last = -1;
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (s->t[i].alive) {
            alive++;
            last = i;
        }
    if (alive > 1)
        return;
    s->pt = 0;
    s->st = ST_PAUSE;
    if (alive == 1) {
        s->round_winner = last;
        if (++s->wins[last] >= WIN_ROUNDS) {
            s->win_mask = 1 << last;
            s->st = ST_END;
            s->result = GAME_OVER;
        }
    }
}

static void check_single(tanks_t *s)
{
    for (int i = 1; i < s->nt; i++)
        if (s->t[i].alive)
            return;
    s->cleared++;
    s->pt = 0;
    if (s->wave >= LAST_WAVE) {
        s->st = ST_END;
        s->result = GAME_WON;
    } else {
        s->st = ST_PAUSE;
    }
}

static game_status_t update(void *p, const input_t *in)
{
    tanks_t *s = p;

    if (s->versus && __builtin_popcount(s->players) < 2)
        return GAME_OVER;

    if (s->st == ST_END) {
        update_bullets(s, false);
        return ++s->pt >= PAUSE_T ? s->result : GAME_RUNNING;
    }
    if (s->st == ST_PAUSE) {
        update_bullets(s, false);
        if (++s->pt < (s->versus ? PAUSE_T : WAVE_T))
            return GAME_RUNNING;
        if (s->versus) {
            new_round(s);
        } else {
            s->wave++;
            start_wave(s);
        }
        return GAME_RUNNING;
    }

    for (int i = 0; i < s->nt; i++) {
        tank_t *t = &s->t[i];
        if (!t->alive)
            continue;
        if (t->prot > 0) t->prot--;
        if (t->cd > 0) t->cd--;
        int dx, dy;
        bool fire;
        if (t->ai) {
            ai_think(s, i, &dx, &dy, &fire);
        } else {
            read_dir(&in[i], t, &dx, &dy);
            fire = in[i].held & BTN_A;
        }
        if (dx || dy) {
            t->fx = dx;
            t->fy = dy;
            move_tank(s, i, dx, dy);
        }
        if (fire)
            fire_bullet(s, i);
    }

    update_bullets(s, true);

    if (s->versus) {
        check_versus(s);
    } else if (s->st == ST_PLAY) {
        if (s->respawn_t > 0 && --s->respawn_t == 0)
            start_tank(s, 0, best_spot(s, 0), false);
        if (s->st == ST_PLAY)
            check_single(s);
    }
    return GAME_RUNNING;
}

// ---- drawing ----

static uint16_t dim(uint16_t c)
{
    return (c >> 1) & 0x7bef;
}

static void draw_tank(const tanks_t *s, int i)
{
    const tank_t *t = &s->t[i];
    if (!t->alive || (t->prot > 0 && (t->prot / 3) % 2))
        return;
    int x = t->x >> 8, y = t->y >> 8;
    uint16_t c = tank_color(s, i), tc = dim(c);
    gfx_rect(x, y, TS, TS, c);
    if (t->fx && t->fy) { // diagonal: dark corners beside the barrel
        bool bs = t->fx * t->fy > 0;
        gfx_pixel(x + (bs ? TS - 1 : 0), y, tc);
        gfx_pixel(x + (bs ? 0 : TS - 1), y + TS - 1, tc);
    } else if (t->fx) {
        gfx_rect(x, y, TS, 1, tc);
        gfx_rect(x, y + TS - 1, TS, 1, tc);
    } else {
        gfx_rect(x, y, 1, TS, tc);
        gfx_rect(x + TS - 1, y, 1, TS, tc);
    }
    for (int k = 0; k <= 3; k++)
        gfx_pixel(x + TS / 2 + t->fx * k, y + TS / 2 + t->fy * k, k ? RGB(220, 220, 220) : tc);
}

static void draw(const void *p)
{
    const tanks_t *s = p;
    char buf[32];

    for (int r = 0; r < s->rows; r++)
        for (int c = 0; c < COLS; c++) {
            int v = s->grid[r][c], x = c * TILE, y = HUD + r * TILE;
            if (v == 1) {
                gfx_rect(x, y, TILE, TILE, C_WALL);
                gfx_rect(x, y, TILE, 1, C_WALL_HI);
            } else if (v == 2) {
                gfx_rect(x, y, TILE, TILE, C_BRICK);
                gfx_rect(x, y + TILE - 1, TILE, 1, C_MORTAR);
                gfx_rect(x + ((r & 1) ? 1 : 3), y, 1, TILE - 1, C_MORTAR);
            }
        }

    for (int i = 0; i < s->nt; i++)
        draw_tank(s, i);
    for (int j = 0; j < NB; j++)
        if (s->b[j].alive)
            gfx_rect(s->b[j].x >> 8, s->b[j].y >> 8, 2, 2, C_WHITE);

    if (s->versus) {
        for (int pl = 0; pl < MAX_PLAYERS; pl++) {
            if (!playing(s, pl))
                continue;
            snprintf(buf, sizeof(buf), "%d", s->wins[pl]);
            gfx_text_center(16 + pl * 32, 0, 1, player_color[pl], buf);
        }
    } else {
        snprintf(buf, sizeof(buf), "L%d", s->lives);
        gfx_text(1, 0, 1, player_color[0], buf);
        snprintf(buf, sizeof(buf), "W%d", s->wave);
        gfx_text_center(64, 0, 1, C_GRAY, buf);
        snprintf(buf, sizeof(buf), "%d", s->kills * 100 + s->cleared * 250);
        gfx_text(HUB75_W - 1 - gfx_text_width(1, buf), 0, 1, C_WHITE, buf);
    }
    gfx_rect(0, HUD - 1, HUB75_W, 1, C_DARK);

    if (s->versus && s->st != ST_PLAY && s->pt >= 25) {
        int cy = HUD + s->rows * TILE / 2;
        gfx_darken(0, cy - 8, HUB75_W, 16);
        if (s->round_winner < 0) {
            gfx_text_center(64, cy - 5, 2, C_GRAY, "DRAW");
        } else if (s->st == ST_END) {
            snprintf(buf, sizeof(buf), "P%d WINS", s->round_winner + 1);
            gfx_text_center(64, cy - 5, 2, player_color[s->round_winner], buf);
        } else { // "P2 +1", the font has no plus
            uint16_t c = player_color[s->round_winner];
            snprintf(buf, sizeof(buf), "P%d", s->round_winner + 1);
            gfx_text(64 - 17, cy - 5, 2, c, buf);
            gfx_rect(64 - 1, cy - 1, 6, 2, c);
            gfx_rect(64 + 1, cy - 3, 2, 6, c);
            gfx_text(64 + 11, cy - 5, 2, c, "1");
        }
    }
}

static int score(const void *p)
{
    const tanks_t *s = p;
    return s->kills * 100 + s->cleared * 250;
}

static int score_of(const void *p, int player)
{
    return ((const tanks_t *)p)->wins[player];
}

static uint8_t winners(const void *p)
{
    return ((const tanks_t *)p)->win_mask;
}

const game_t game_tanks = {
    .shared = true,
    .big = BIG_NATIVE,
    .init = init,
    .update = update,
    .draw = draw,
    .score = score,
    .score_of = score_of,
    .winners = winners,
};
