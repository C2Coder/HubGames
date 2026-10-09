// Side scroller: one hand-made level, A jump (hold = higher), B run.
#include "game.h"

#include <stdio.h>
#include <string.h>

#define T         4 // tile size in px
#define LW        224
#define LH        14
#define HUD       8
#define MAX_E     24
#define HERO_W    3
#define HERO_H    7

// physics, 8.8 fixed point per 20 ms tick
#define ACC       10
#define FRIC      15
#define WALK      128
#define RUN       230
#define JUMP      (-500)
#define GRAV      31
#define FALL      640
#define ENEMY_V   64

#define DEAD_TICKS  50
#define SLIDE_TICKS 30
#define SINGLE_TIME (200 * 50)
#define VS_TIME     (120 * 50)

enum { ST_PLAY, ST_DEAD, ST_WIN };

static const char *const level[LH] = {
    "                                                                                                                                                                                                                                ",
    "                                                                                                                                                                                                                      F         ",
    "                                                                                                                                                                                                                      F         ",
    "                                                                                                                                                                                                                      F         ",
    "                                                                                                                                                                                                                      F         ",
    "                                                                                                                                                                                                                      F         ",
    "                                                                                                                                                                                                                      F         ",
    "                                                                                oo                    ooo       ooooo                                                                                                 F         ",
    "                     ooo                              ooo     ooo                       oooo                                  ooooo   ooo   ooo                           oooo         ooo   ooo   ooo           XXXXXXXXXXXXX  ",
    "                    BBBBB                                     BBB               PP                    XXX                     BBBBB                         PP                         BBB   BBB   BBB          XXXXXXXXXXXXXX  ",
    "        oooo                          PP                                        PP                   XXXXX                                            PP    PP    PP                                           XXXXXXXXXXXXXXX  ",
    "                              E       PP      E                   E   E         PP              E   XXXXXXX              E  E    E                    PP E  PP E  PP                                       E  XXXXXXXXXXXXXXXX  ",
    "######################################################   ###############################    ####################     #################   ###   ###########################    ######                   #########################",
    "######################################################   ###############################    ####################     #################   ###   ###########################    ######                   #########################",
};

static const int cp_col[] = {0, 76, 146};
#define N_CP ((int)(sizeof(cp_col) / sizeof(cp_col[0])))

typedef struct {
    int32_t x, y; // 8.8 px
    int16_t vx, vy;
    uint8_t st; // 0 gone, 1 walking, 2 squashed
    uint8_t t;
} enemy_t;

typedef struct {
    view_t v;
    int player;
    bool versus, right, ground, won, a_held;
    int st, dead_t, win_t;
    int32_t x, y;
    int vx, vy;
    int cam, cp;
    int lives, coins, stomps, furthest; // furthest in px
    int ticks_left, frame, jump_buf, coyote;
    uint32_t stomped;
    enemy_t e[MAX_E];
    uint8_t coin_taken[LW * LH / 8 + 1];
} mario_t;

_Static_assert(sizeof(mario_t) <= PANEL_STATE_MAX, "mario state too big");

static bool solid(int col, int row)
{
    if (col < 0 || col >= LW)
        return true;
    if (row < 0 || row >= LH)
        return false;
    char c = level[row][col];
    return c == '#' || c == 'B' || c == 'X' || c == 'P';
}

static bool box_solid(int x, int y, int w, int h)
{
    for (int r = y >> 2; r <= (y + h - 1) >> 2; r++)
        for (int c = x >> 2; c <= (x + w - 1) >> 2; c++)
            if (solid(c, r))
                return true;
    return false;
}

// move 8.8 position by v, one pixel at a time, true if blocked
static bool move_x(int32_t *fx, int v, int y, int w, int h)
{
    int32_t nx = *fx + v;
    int cur = *fx >> 8, tgt = nx >> 8, d = tgt > cur ? 1 : -1;
    while (cur != tgt) {
        if (box_solid(cur + d, y, w, h)) {
            *fx = cur << 8;
            return true;
        }
        cur += d;
    }
    *fx = nx;
    return false;
}

static bool move_y(int32_t *fy, int v, int x, int w, int h)
{
    int32_t ny = *fy + v;
    int cur = *fy >> 8, tgt = ny >> 8, d = tgt > cur ? 1 : -1;
    while (cur != tgt) {
        if (box_solid(x, cur + d, w, h)) {
            *fy = cur << 8;
            return true;
        }
        cur += d;
    }
    *fy = ny;
    return false;
}

static void spawn_enemies(mario_t *s)
{
    memset(s->e, 0, sizeof(s->e));
    int n = 0;
    for (int c = 0; c < LW && n < MAX_E; c++)
        for (int r = 0; r < LH && n < MAX_E; r++)
            if (level[r][c] == 'E') {
                s->e[n].x = (int32_t)(c * T) << 8;
                s->e[n].y = (int32_t)(r * T) << 8;
                s->e[n].vx = -ENEMY_V;
                s->e[n].st = 1;
                n++;
            }
}

static void respawn(mario_t *s)
{
    int c = cp_col[s->cp], r = 0;
    while (r < LH && !solid(c, r))
        r++;
    s->x = (int32_t)(c * T + 1) << 8;
    s->y = (int32_t)(r * T - HERO_H) << 8;
    s->vx = s->vy = 0;
    s->st = ST_PLAY;
    s->jump_buf = s->coyote = 0;
    s->cam = c * T - s->v.w / 4;
    if (s->cam < 0)
        s->cam = 0;
    spawn_enemies(s);
}

static void init(void *p, view_t v, player_mask_t players, bool versus)
{
    mario_t *s = p;
    memset(s, 0, sizeof(*s));
    s->v = v;
    s->player = __builtin_ctz(players);
    s->versus = versus;
    s->right = true;
    s->lives = 3;
    s->ticks_left = versus ? VS_TIME : SINGLE_TIME;
    respawn(s);
}

static void die(mario_t *s)
{
    s->st = ST_DEAD;
    s->dead_t = 0;
    s->vy = -450;
    fx_burst(s->v.x + (s->x >> 8) - s->cam + 1, s->v.y + HUD + (s->y >> 8) + 3, C_RED, 16);
    if (!s->versus)
        s->lives--;
}

static void hero_move(mario_t *s, const input_t *in)
{
    s->a_held = in->held & BTN_A;
    int max = (in->held & BTN_B) ? RUN : WALK;
    int dir = ((in->held & BTN_RIGHT) ? 1 : 0) - ((in->held & BTN_LEFT) ? 1 : 0);
    int v = s->vx;

    if (dir) {
        s->right = dir > 0;
        int nv = v + dir * (v * dir < 0 ? 2 * ACC : ACC);
        if (nv * dir > max) {
            if (v * dir > max) { // faster than max (released B): slow down on the ground
                nv = s->ground ? v - dir * FRIC : v;
                if (nv * dir < max)
                    nv = max * dir;
            } else {
                nv = max * dir;
            }
        }
        v = nv;
    } else if (s->ground) {
        v = v > FRIC ? v - FRIC : v < -FRIC ? v + FRIC : 0;
    }
    s->vx = v;

    // jump with a little input buffer and coyote time
    if (in->pressed & BTN_A)
        s->jump_buf = 6;
    else if (s->jump_buf > 0)
        s->jump_buf--;
    if (s->ground)
        s->coyote = 4;
    else if (s->coyote > 0)
        s->coyote--;
    if (s->jump_buf > 0 && s->coyote > 0) {
        s->vy = JUMP;
        s->jump_buf = s->coyote = 0;
        s->ground = false;
    }
    int g = (s->vy < 0 && !(in->held & BTN_A)) ? 2 * GRAV : GRAV;
    s->vy += g;
    if (s->vy > FALL)
        s->vy = FALL;

    int y = s->y >> 8, x = s->x >> 8;
    if (move_x(&s->x, s->vx, y, HERO_W, HERO_H))
        s->vx = 0;
    if (s->x < (int32_t)s->cam << 8) { // cannot go back off screen
        s->x = (int32_t)s->cam << 8;
        if (s->vx < 0)
            s->vx = 0;
    }
    x = s->x >> 8;
    if (move_y(&s->y, s->vy, x, HERO_W, HERO_H))
        s->vy = 0;
    s->ground = box_solid(x, (s->y >> 8) + 1, HERO_W, HERO_H);
    if (s->ground && s->vy > 0)
        s->vy = 0;
}

static void enemies(mario_t *s)
{
    int hx = s->x >> 8, hy = s->y >> 8;
    for (int i = 0; i < MAX_E; i++) {
        enemy_t *e = &s->e[i];
        if (!e->st)
            continue;
        if (e->st == 2) {
            if (++e->t >= 20)
                e->st = 0;
            continue;
        }
        int ex = e->x >> 8, ey = e->y >> 8;
        if (ex < s->cam - 16 || ex > s->cam + s->v.w + 24)
            continue; // frozen outside the view
        if (move_x(&e->x, e->vx, ey, 4, 4))
            e->vx = -e->vx;
        e->vy += GRAV;
        if (e->vy > FALL)
            e->vy = FALL;
        if (move_y(&e->y, e->vy, e->x >> 8, 4, 4))
            e->vy = 0;
        ex = e->x >> 8;
        ey = e->y >> 8;
        if (ey > LH * T + 8) {
            e->st = 0;
            continue;
        }
        if (s->st != ST_PLAY)
            continue;
        if (hx < ex + 4 && hx + HERO_W > ex && hy < ey + 4 && hy + HERO_H > ey) {
            if (s->vy > 0 && hy + HERO_H - ey <= 3) {
                e->st = 2;
                e->t = 0;
                fx_burst(s->v.x + ex - s->cam + 2, s->v.y + HUD + ey + 2, RGB(200, 130, 60), 8);
                if (!(s->stomped & (1u << i))) {
                    s->stomped |= 1u << i;
                    s->stomps++;
                }
                s->vy = s->a_held ? JUMP : -300;
            } else {
                die(s);
            }
        }
    }
}

// burst around the flag top
static void fireworks(mario_t *s)
{
    static const int8_t dx[4] = {-8, 6, -3, 9}, dy[4] = {2, 0, -3, 5};
    int i = s->win_t / 8 & 3;
    int x = s->v.x + (s->x >> 8) - s->cam + dx[i];
    int y = s->v.y + HUD + 6 + dy[i];
    fx_burst(x, y, gfx_hsv(s->frame * 23, 255, 255), 14);
}

static void pickups(mario_t *s)
{
    int x = s->x >> 8, y = s->y >> 8;
    for (int r = y >> 2; r <= (y + HERO_H - 1) >> 2; r++)
        for (int c = x >> 2; c <= (x + HERO_W - 1) >> 2; c++) {
            if (c < 0 || c >= LW || r < 0 || r >= LH)
                continue;
            int idx = r * LW + c;
            if (level[r][c] == 'o' && !(s->coin_taken[idx >> 3] & (1 << (idx & 7)))) {
                s->coin_taken[idx >> 3] |= 1 << (idx & 7);
                s->coins++;
                fx_burst(s->v.x + c * T - s->cam + 2, s->v.y + HUD + r * T + 2, C_YELLOW, 6);
            } else if (level[r][c] == 'F' && s->st == ST_PLAY) {
                s->st = ST_WIN;
                s->won = true;
                s->win_t = 0;
                s->vx = s->vy = 0;
                fireworks(s);
            }
        }
}

static game_status_t update(void *p, const input_t *in)
{
    mario_t *s = p;
    s->frame++;

    if (s->st == ST_WIN) { // slide down the pole
        move_y(&s->y, 128, s->x >> 8, HERO_W, HERO_H);
        if (s->win_t % 8 == 7)
            fireworks(s);
        if (s->versus || ++s->win_t >= SLIDE_TICKS)
            return GAME_WON;
        return GAME_RUNNING;
    }

    if (--s->ticks_left <= 0)
        return GAME_OVER;

    if (s->st == ST_DEAD) {
        s->y += s->vy;
        s->vy += GRAV;
        if (++s->dead_t >= DEAD_TICKS) {
            if (!s->versus && s->lives <= 0)
                return GAME_OVER;
            respawn(s);
        }
        return GAME_RUNNING;
    }

    hero_move(s, in);
    enemies(s);
    if (s->st == ST_PLAY && (s->y >> 8) > LH * T + 8)
        die(s);
    if (s->st == ST_PLAY) {
        pickups(s);
        int hx = s->x >> 8;
        if (hx > s->furthest)
            s->furthest = hx;
        for (int i = s->cp + 1; i < N_CP; i++)
            if (hx / T >= cp_col[i])
                s->cp = i;
    }
    if (s->st == ST_WIN && s->versus)
        return GAME_WON;

    // camera follows forward only
    int tgt = (s->x >> 8) - s->v.w / 3;
    if (tgt > s->cam)
        s->cam = tgt;
    if (s->cam > LW * T - s->v.w)
        s->cam = LW * T - s->v.w;
    return GAME_RUNNING;
}

// ---- drawing, everything is clipped to the view ----

#define SKY     RGB(70, 110, 230)
#define C_DIRT  RGB(150, 90, 40)
#define C_GRASS RGB(60, 190, 60)
#define C_BRICK RGB(200, 80, 40)
#define C_MORT  RGB(90, 30, 10)
#define C_PIPE  RGB(0, 170, 40)
#define C_PIPEL RGB(120, 240, 120)
#define C_HARD  RGB(150, 150, 165)

static void rect(const view_t *v, int x, int y, int w, int h, uint16_t c)
{
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > v->w) x1 = v->w;
    if (y1 > v->h) y1 = v->h;
    if (x1 > x && y1 > y)
        gfx_rect(v->x + x, v->y + y, x1 - x, y1 - y, c);
}

static uint16_t sprite_color(char ch, uint16_t pc)
{
    switch (ch) {
    case 'r': return pc;
    case 's': return RGB(255, 190, 130);
    case 'k': return RGB(30, 20, 10);
    case 'b': return RGB(40, 60, 220);
    case 'n': return RGB(110, 60, 20);
    case 'w': return RGB(255, 235, 200);
    case 'f': return RGB(60, 30, 10);
    }
    return 0;
}

static void sprite(const view_t *v, int x, int y, const char *const *rows, int w, int h,
                   bool flip, uint16_t pc)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            char ch = rows[j][flip ? w - 1 - i : i];
            if (ch != '.')
                rect(v, x + i, y + j, 1, 1, sprite_color(ch, pc));
        }
}

static const char *const hero_stand[] = {"rrr", "sks", ".s.", "rrr", "bbb", "b.b", "n.n"};
static const char *const hero_run1[] = {"rrr", "sks", ".s.", "rrr", "bbb", "bb.", "nn."};
static const char *const hero_run2[] = {"rrr", "sks", ".s.", "rrr", "bbb", ".bb", ".nn"};
static const char *const hero_jump[] = {"rrr", "sks", ".s.", "rrr", "bbb", "bbb", "n.n"};
static const char *const walk_a[] = {".nn.", "nwwn", ".ww.", "ff.."};
static const char *const walk_b[] = {".nn.", "nwwn", ".ww.", "..ff"};
static const char *const squash[] = {"nnnn", "wwww"};

static void draw_tile(const view_t *v, int x, int y, char ch, bool above_same)
{
    switch (ch) {
    case '#':
        rect(v, x, y, T, T, C_DIRT);
        if (!above_same)
            rect(v, x, y, T, 1, C_GRASS);
        else
            rect(v, x + 1, y + 2, 1, 1, RGB(110, 60, 20));
        break;
    case 'B':
        rect(v, x, y, T, T, C_BRICK);
        rect(v, x, y + 1, T, 1, C_MORT);
        rect(v, x, y + 3, T, 1, C_MORT);
        rect(v, x + 1, y + 2, 1, 1, C_MORT);
        break;
    case 'X':
        rect(v, x, y, T, T, C_HARD);
        rect(v, x, y, T, 1, C_WHITE);
        rect(v, x + 3, y + 1, 1, 3, C_GRAY);
        break;
    case 'P':
        rect(v, x, y, T, T, C_PIPE);
        rect(v, x, y, 1, T, C_PIPEL);
        if (!above_same)
            rect(v, x, y, T, 1, C_PIPEL);
        break;
    case 'F':
        rect(v, x + 1, y, 2, T, C_WHITE);
        break;
    }
}

static void draw(const void *p)
{
    const mario_t *s = p;
    const view_t *v = &s->v;
    uint16_t pc = s->versus ? player_color[s->player] : C_RED;

    rect(v, 0, HUD, v->w, v->h - HUD, SKY);

    int c0 = s->cam / T, c1 = (s->cam + v->w - 1) / T;
    for (int c = c0; c <= c1 && c < LW; c++)
        for (int r = 0; r < LH; r++) {
            char ch = level[r][c];
            if (ch == ' ')
                continue;
            int x = c * T - s->cam, y = HUD + r * T;
            if (ch == 'o') {
                int idx = r * LW + c;
                if (!(s->coin_taken[idx >> 3] & (1 << (idx & 7)))) {
                    bool wide = (s->frame / 10 + c) % 2;
                    rect(v, x + (wide ? 0 : 1), y, wide ? 3 : 2, T, C_YELLOW);
                }
            } else if (ch != 'E') {
                bool same = r > 0 && level[r - 1][c] == ch;
                draw_tile(v, x, y, ch, same);
                if (ch == 'F' && r == 1) {
                    rect(v, x - 5, y, 5, 3, C_RED);
                    rect(v, x - 3, y + 3, 3, 1, C_RED);
                    rect(v, x, y - 1, 3, 1, C_YELLOW);
                }
            }
        }

    for (int i = 0; i < MAX_E; i++) {
        const enemy_t *e = &s->e[i];
        if (!e->st)
            continue;
        int x = (e->x >> 8) - s->cam, y = HUD + (e->y >> 8);
        if (e->st == 2)
            sprite(v, x, y + 2, squash, 4, 2, false, 0);
        else
            sprite(v, x, y, ((s->frame / 10) & 1) ? walk_a : walk_b, 4, 4, false, 0);
    }

    const char *const *hs = hero_stand;
    if (!s->ground)
        hs = hero_jump;
    else if (s->vx > 20 || s->vx < -20)
        hs = ((s->frame / 4) & 1) ? hero_run1 : hero_run2;
    sprite(v, (s->x >> 8) - s->cam, HUD + (s->y >> 8), hs, 3, 7, !s->right, pc);

    // HUD
    rect(v, 0, 0, v->w, HUD, C_BLACK);
    char buf[24];
    int secs = (s->ticks_left + 49) / 50;
    uint16_t tc = secs <= 20 ? C_RED : C_WHITE;
    if (s->versus) {
        rect(v, 1, 2, 2, 3, C_YELLOW);
        snprintf(buf, sizeof(buf), "%d", s->coins);
        gfx_text(v->x + 4, v->y + 1, 1, C_WHITE, buf);
        snprintf(buf, sizeof(buf), "%d", secs);
        gfx_text(v->x + 28, v->y + 1, 1, tc, buf);
        snprintf(buf, sizeof(buf), "P%d", s->player + 1);
        gfx_text(v->x + v->w - 9, v->y + 1, 1, player_color[s->player], buf);
    } else {
        // wide view: spread out, 64 px view: compact
        bool wide = v->w >= 128;
        int cx = wide ? 44 : 22, tx = wide ? 70 : 35;
        snprintf(buf, sizeof(buf), "%d", s->coins * 10 + s->stomps * 10 + s->furthest / T);
        gfx_text(v->x + 1, v->y + 1, 1, C_WHITE, buf);
        rect(v, cx, 2, 2, 3, C_YELLOW);
        snprintf(buf, sizeof(buf), "%d", s->coins);
        gfx_text(v->x + cx + 3, v->y + 1, 1, C_YELLOW, buf);
        snprintf(buf, sizeof(buf), "T%d", secs);
        gfx_text(v->x + tx, v->y + 1, 1, tc, buf);
        snprintf(buf, sizeof(buf), "L%d", s->lives);
        gfx_text(v->x + v->w - 9, v->y + 1, 1, C_RED, buf);
    }
}

static int score(const void *p)
{
    const mario_t *s = p;
    if (s->versus)
        return s->furthest + (s->won ? 10000 : 0); // distance in px decides
    int sc = s->coins * 10 + s->stomps * 10 + s->furthest / T;
    if (s->won)
        sc += 100 + (s->ticks_left / 50) * 5;
    return sc;
}

const game_t game_mario = {
    .vs_rule = VS_SCORE,
    .shared = false,
    .big = BIG_SCALE2,
    .init = init,
    .update = update,
    .draw = draw,
    .score = score,
};
