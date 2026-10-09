// Donkey Kong style: climb girders and ladders, jump over barrels, reach the top.
#include "game.h"

#include <stdio.h>
#include <string.h>
#include "esp_random.h"

#define FLOORS     5
#define MAX_BARREL 8
#define LEVELS     3
#define VS_TICKS   4500 // 90 s
#define DEAD_TICKS 50
#define CLEAR_TICKS 60
#define GOAL_X     40
#define APE_X      50
#define WALK       154 // 0.6 px/tick in 8.8
#define CLIMB      128
#define JUMP_VY    (-269)
#define GRAVITY    26
#define HUD        6

typedef struct {
    int32_t x, y; // 8.8, y = girder surface under the barrel
    uint8_t floor, state, scored, lad;
    int8_t dir;
    uint16_t serial;
} barrel_t;

enum { B_FREE, B_ROLL, B_FALL, B_LADDER };
enum { ST_PLAY, ST_DEAD, ST_CLEAR };

typedef struct {
    view_t v;
    int player, ox;
    bool versus;
    int level, lives, state, state_t;
    int points, bonus, tick, time_left, best_h;
    uint32_t seed;
    // player
    int32_t px, py, pvx, pvy; // 8.8, py = girder surface under the feet
    int floor, lad;           // lad = ladder gap while climbing, else -1
    int jfloor, face, anim;
    bool air;
    int ape_t;
    barrel_t b[MAX_BARREL];
} dk_t;

_Static_assert(sizeof(dk_t) <= GAME_STATE_MAX, "dk state too big");

static uint32_t shared_seed = 1;

// girder surface at x: floors alternate slope, even floors go down to the left
static const uint8_t base[FLOORS] = {59, 48, 37, 26, 15};

static int surf(int f, int x)
{
    return base[f] + ((f & 1) ? x : 63 - x) / 16;
}

static int span_lo(int f) { return (f & 1) ? 0 : (f ? 3 : 0); }
static int span_hi(int f) { return (f & 1) ? 60 : 63; }

// ladder between floor gap and gap + 1
static const struct { uint8_t gap, x; } ladders[] = {
    {0, 44}, {1, 14}, {1, 50}, {2, 44}, {2, 20}, {3, 16},
};
#define N_LAD ((int)(sizeof(ladders) / sizeof(ladders[0])))

static uint32_t hash(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t h = a ^ (b * 2654435761u) ^ (c * 40503u);
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    h *= 3266489917u;
    h ^= h >> 16;
    return h;
}

static void respawn(dk_t *s)
{
    memset(s->b, 0, sizeof(s->b));
    s->floor = 0;
    s->px = 4 << 8;
    s->py = surf(0, 4) << 8;
    s->pvx = s->pvy = 0;
    s->lad = -1;
    s->air = false;
    s->face = 1;
    s->state = ST_PLAY;
    s->state_t = 0;
}

static void init(void *p, view_t v, uint8_t players, bool versus)
{
    dk_t *s = p;
    memset(s, 0, sizeof(*s));
    s->v = v;
    s->player = __builtin_ctz(players);
    s->versus = versus;
    s->ox = v.w >= 128 ? v.x + 32 : v.x;
    if (players & 0x1)
        shared_seed = esp_random() | 1; // player 0 is inited first, player 1 reuses it
    s->seed = shared_seed;
    s->level = versus ? 2 : 1;
    s->lives = 3;
    s->bonus = 1000;
    s->time_left = VS_TICKS;
    respawn(s);
}

static void spawn_barrel(dk_t *s)
{
    int interval = 120 - 12 * s->level;
    if (interval < 50) interval = 50;
    if (s->tick % interval)
        return;
    for (int i = 0; i < MAX_BARREL; i++) {
        barrel_t *b = &s->b[i];
        if (b->state)
            continue;
        memset(b, 0, sizeof(*b));
        b->state = B_ROLL;
        b->floor = FLOORS - 1;
        b->dir = -1;
        b->x = APE_X << 8;
        b->y = surf(b->floor, APE_X) << 8;
        b->serial = s->tick / interval;
        s->ape_t = 14;
        return;
    }
}

static void move_barrels(dk_t *s)
{
    int spd = 205 + 26 * s->level;
    if (spd > 410) spd = 410;
    int chance = 25 + 10 * (s->level - 1);
    if (chance > 50) chance = 50;

    for (int i = 0; i < MAX_BARREL; i++) {
        barrel_t *b = &s->b[i];
        if (b->state == B_ROLL) {
            int ox = b->x >> 8;
            b->x += b->dir * spd;
            int nx = b->x >> 8;
            if (b->floor == 0 && nx < -3) {
                b->state = B_FREE;
                continue;
            }
            if (b->floor && (nx < span_lo(b->floor) || nx > span_hi(b->floor))) {
                b->state = B_FALL;
                continue;
            }
            b->y = surf(b->floor, nx < 0 ? 0 : nx) << 8;
            // maybe take a ladder down
            for (int l = 0; l < N_LAD && b->floor; l++) {
                int lx = ladders[l].x;
                if (ladders[l].gap != b->floor - 1)
                    continue;
                if ((ox - lx) * (nx - lx) <= 0 && ox != nx &&
                    hash(s->seed, b->serial, l) % 100 < (uint32_t)chance) {
                    b->state = B_LADDER;
                    b->x = lx << 8;
                    b->lad = b->floor - 1;
                    break;
                }
            }
        } else if (b->state == B_FALL || b->state == B_LADDER) {
            b->y += b->state == B_FALL ? 384 : 180;
            int nf = b->floor - 1;
            int x = b->x >> 8;
            if (b->y >= surf(nf, x) << 8) {
                b->floor = nf;
                b->y = surf(nf, x) << 8;
                b->dir = (nf & 1) ? 1 : -1;
                b->state = B_ROLL;
            }
        }
    }
}

static bool hit(const dk_t *s, const barrel_t *b)
{
    int px = s->px >> 8, py = s->py >> 8, bx = b->x >> 8, by = b->y >> 8;
    // player rect (px-1, py-5, 3, 5), barrel rect (bx-2, by-4, 4, 4)
    return px + 2 > bx - 2 && px - 1 < bx + 2 && py > by - 4 && py - 5 < by;
}

static void die(dk_t *s)
{
    s->state = ST_DEAD;
    s->state_t = 0;
    s->air = false;
    if (!s->versus)
        s->lives--;
}

static int ladder_near(const dk_t *s, int gap)
{
    int x = s->px >> 8;
    for (int l = 0; l < N_LAD; l++)
        if (ladders[l].gap == gap && x >= ladders[l].x - 2 && x <= ladders[l].x + 2)
            return l;
    return -1;
}

static void move_player(dk_t *s, const input_t *in)
{
    if (s->lad >= 0) { // climbing
        int lx = s->px >> 8;
        if (in->held & BTN_UP) {
            s->py -= CLIMB;
            s->anim++;
            if (s->py <= surf(s->lad + 1, lx) << 8) {
                s->floor = s->lad + 1;
                s->py = surf(s->floor, lx) << 8;
                s->lad = -1;
            }
        } else if (in->held & BTN_DOWN) {
            s->py += CLIMB;
            s->anim++;
            if (s->py >= surf(s->lad, lx) << 8) {
                s->floor = s->lad;
                s->py = surf(s->floor, lx) << 8;
                s->lad = -1;
            }
        }
        return;
    }

    if (s->air) {
        int32_t oy = s->py;
        s->px += s->pvx;
        int x = s->px >> 8;
        if (x < 1) x = 1, s->px = x << 8;
        if (x > 62) x = 62, s->px = x << 8;
        s->pvy += GRAVITY;
        s->py += s->pvy;
        if (s->pvy > 0)
            for (int f = FLOORS - 1; f >= 0; f--)
                if (x >= span_lo(f) && x <= span_hi(f) && oy <= surf(f, x) << 8 &&
                    s->py >= surf(f, x) << 8) {
                    s->floor = f;
                    s->py = surf(f, x) << 8;
                    s->air = false;
                    break;
                }
        return;
    }

    // on a girder
    if (in->pressed & BTN_A) {
        s->air = true;
        s->pvy = JUMP_VY;
        s->pvx = (in->held & BTN_RIGHT ? WALK : 0) - (in->held & BTN_LEFT ? WALK : 0);
        s->jfloor = s->floor;
        return;
    }
    if (in->held & BTN_UP) {
        int l = ladder_near(s, s->floor);
        if (l >= 0) {
            s->lad = s->floor;
            s->px = ladders[l].x << 8;
            return;
        }
    }
    if ((in->held & BTN_DOWN) && s->floor) {
        int l = ladder_near(s, s->floor - 1);
        if (l >= 0) {
            s->lad = s->floor - 1;
            s->px = ladders[l].x << 8;
            return;
        }
    }
    int dx = (in->held & BTN_RIGHT ? WALK : 0) - (in->held & BTN_LEFT ? WALK : 0);
    if (dx) {
        s->face = dx > 0 ? 1 : -1;
        s->anim++;
        s->px += dx;
        int lo = span_lo(s->floor) << 8, hi = span_hi(s->floor) << 8;
        if (s->px < lo) s->px = lo;
        if (s->px > hi) s->px = hi;
        s->py = surf(s->floor, s->px >> 8) << 8;
    }
}

static void next_level(dk_t *s)
{
    s->level++;
    s->bonus = 1000;
    s->tick = 0;
    respawn(s);
}

static game_status_t update(void *p, const input_t *in)
{
    dk_t *s = p;

    if (s->versus && --s->time_left <= 0)
        return GAME_OVER;

    if (s->state == ST_CLEAR) {
        if (++s->state_t >= CLEAR_TICKS)
            next_level(s);
        return GAME_RUNNING;
    }

    s->tick++;
    if (!s->versus && s->tick % 20 == 0 && s->bonus > 0)
        s->bonus -= 10;
    if (s->ape_t)
        s->ape_t--;
    spawn_barrel(s);
    move_barrels(s);

    if (s->state == ST_DEAD) {
        if (++s->state_t >= DEAD_TICKS) {
            if (s->lives <= 0)
                return GAME_OVER;
            respawn(s);
        }
        return GAME_RUNNING;
    }

    move_player(s, in);

    int h = 62 - (s->py >> 8);
    if (h > s->best_h)
        s->best_h = h;

    int px = s->px >> 8, py = s->py >> 8;
    for (int i = 0; i < MAX_BARREL; i++) {
        barrel_t *b = &s->b[i];
        if (!b->state)
            continue;
        if (hit(s, b)) {
            die(s);
            return GAME_RUNNING;
        }
        // jumped over a rolling barrel on the same floor
        int bx = b->x >> 8, by = b->y >> 8;
        if (s->air && b->state == B_ROLL && !b->scored && b->floor == s->jfloor &&
            bx - px >= -3 && bx - px <= 3 && py <= by - 3) {
            b->scored = 1;
            s->points += 100;
        }
    }

    if (s->floor == FLOORS - 1 && !s->air && s->lad < 0 && px >= GOAL_X - 3 && px <= GOAL_X + 3) {
        if (s->versus)
            return GAME_WON;
        s->points += 500 + s->bonus;
        if (s->level >= LEVELS)
            return GAME_WON;
        s->state = ST_CLEAR;
        s->state_t = 0;
        memset(s->b, 0, sizeof(s->b));
    }
    return GAME_RUNNING;
}

// ---- drawing ----

static uint16_t pal(char c, uint16_t pc)
{
    switch (c) {
    case 'h': return RGB(255, 210, 160);
    case 'b': return pc;
    case 'l': return RGB(60, 80, 255);
    case 'n': return RGB(140, 80, 20);
    case 'm': return RGB(220, 150, 50);
    case 'a': return RGB(150, 90, 50);
    case 'd': return RGB(230, 180, 130);
    }
    return 0;
}

static void sprite(int x, int y, int w, int h, const char *const *rows, uint16_t pc, bool flip)
{
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++) {
            char ch = rows[r][flip ? w - 1 - c : c];
            if (ch != '.')
                gfx_pixel(x + c, y + r, pal(ch, pc));
        }
}

static const char *const spr_player[6][6] = {
    {".hhh.", ".hhh.", "bbbbb", ".bbb.", ".l.l.", ".l.l."},
    {".hhh.", ".hhh.", "bbbbb", ".bbb.", ".l.l.", "l...l"},
    {".hhh.", ".hhh.", "bbbbb", ".bbb.", "..l..", ".l.l."},
    {"bhhhb", ".hhh.", ".bbb.", ".bbb.", ".l.l.", "l...l"},
    {".hhh.", ".hhh.", "bbb.b", ".bbb.", ".l...", "...l."},
    {".hhh.", ".hhh.", "b.bbb", ".bbb.", "...l.", ".l..."},
};
static const char *const spr_barrel[2][4] = {
    {".nn.", "nmnn", "nnmn", ".nn."},
    {".nn.", "nnmn", "nmnn", ".nn."},
};
static const char *const spr_ape[2][8] = {
    {"a..dddddd..a", "aa.dddddd.aa", ".aaaaaaaaaa.", "..aaaaaaaa..", "..aaaaaaaa..",
     "..aa....aa..", "..aa....aa..", ".aaa....aaa."},
    {"...dddddd...", "..aaaaaaaa..", "aaaaaaaaaaaa", "aa.aaaaaa.aa", "aa.aaaaaa.aa",
     "...aa..aa...", "...aa..aa...", ".aaaa..aaaa."},
};

static void draw_level(const dk_t *s)
{
    int ox = s->ox;
    uint16_t gc = RGB(200, 50, 40), gd = RGB(130, 30, 30);
    for (int f = 0; f < FLOORS; f++)
        for (int x = span_lo(f); x <= span_hi(f); x++) {
            int y = surf(f, x);
            gfx_pixel(ox + x, y, gc);
            gfx_pixel(ox + x, y + 1, x % 4 ? gd : gc);
        }
    for (int l = 0; l < N_LAD; l++) {
        int x = ladders[l].x, top = surf(ladders[l].gap + 1, x), bot = surf(ladders[l].gap, x);
        for (int y = top; y < bot; y++) {
            gfx_pixel(ox + x - 1, y, C_BLUE);
            gfx_pixel(ox + x + 1, y, C_BLUE);
            if (y % 2)
                gfx_pixel(ox + x, y, C_BLUE);
        }
    }
    // ape and goal
    sprite(ox + APE_X + 1, surf(FLOORS - 1, APE_X + 6) - 8, 12, 8, spr_ape[s->ape_t > 0], 0, false);
    int gy = surf(FLOORS - 1, GOAL_X);
    gfx_rect(ox + GOAL_X, gy - 5, 3, 2, C_PINK);
    gfx_rect(ox + GOAL_X + 1, gy - 3, 1, 3, C_PINK);
    gfx_pixel(ox + GOAL_X, gy - 1, C_PINK);
    gfx_pixel(ox + GOAL_X + 2, gy - 1, C_PINK);
}

static void draw(const void *p)
{
    const dk_t *s = p;
    const view_t *v = &s->v;
    int ox = s->ox;
    uint16_t pc = player_color[s->player];

    draw_level(s);

    for (int i = 0; i < MAX_BARREL; i++) {
        const barrel_t *b = &s->b[i];
        if (b->state)
            sprite(ox + (b->x >> 8) - 2, (b->y >> 8) - 4, 4, 4, spr_barrel[(b->x >> 11) & 1], 0, false);
    }

    if (!(s->state == ST_DEAD && (s->state_t / 5) % 2)) {
        int f = 0;
        if (s->lad >= 0)
            f = 4 + (s->anim / 6) % 2;
        else if (s->air)
            f = 3;
        else if (s->anim)
            f = (s->anim / 5) % 3;
        sprite(ox + (s->px >> 8) - 2, (s->py >> 8) - 6, 5, 6, spr_player[f], pc, s->face < 0);
    }

    char buf[16];
    if (v->w >= 128) {
        int lx = v->x + 1, rx = v->x + 97;
        gfx_text(lx, 2, 1, C_WHITE, "SCORE");
        snprintf(buf, sizeof(buf), "%d", s->points);
        gfx_text(lx, 9, 1, C_YELLOW, buf);
        gfx_text(lx, 22, 1, C_WHITE, "BONUS");
        snprintf(buf, sizeof(buf), "%d", s->bonus);
        gfx_text(lx, 29, 1, C_YELLOW, buf);
        snprintf(buf, sizeof(buf), "LVL %d", s->level);
        gfx_text(rx, 2, 1, C_WHITE, buf);
        for (int i = 0; i < s->lives; i++)
            gfx_rect(rx + i * 5, 12, 3, 5, pc);
        gfx_rect(ox - 1, 0, 1, 64, C_DARK);
        gfx_rect(ox + 64, 0, 1, 64, C_DARK);
    } else {
        snprintf(buf, sizeof(buf), "%d", s->best_h * 10 + s->points);
        gfx_text(v->x + 1, v->y, 1, C_WHITE, buf);
        snprintf(buf, sizeof(buf), "%d", (s->time_left + 49) / 50);
        gfx_text(v->x + 30, v->y, 1, C_YELLOW, buf);
        gfx_text(v->x + v->w - 8, v->y, 1, pc, s->player ? "P2" : "P1");
    }
}

static int score(const void *p)
{
    const dk_t *s = p;
    return s->versus ? s->best_h * 10 + s->points : s->points;
}

const game_t game_dk = {
    .vs_rule = VS_SCORE,
    .init = init,
    .update = update,
    .draw = draw,
    .score = score,
};
