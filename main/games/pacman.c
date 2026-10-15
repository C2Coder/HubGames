// Pac-Man: 21x21 tile maze of 3x3 px tiles, 4 ghosts, power pellets, demo AI.
#include "game.h"

#include <stdio.h>
#include <string.h>
#include "esp_random.h"

#define N          21
#define T          3
#define TU         768 // one tile in position units (1/256 px)
#define MAZE_PX    (N * T)
#define MAX_Q      (N * N)
#define LIVES      3
#define READY_T    50
#define DEAD_T     70
#define CLEAR_T    80
#define HOUSE_X    10 // eyes return here, ghosts leave through the door above
#define HOUSE_Y    9
#define OUT_X      10 // tile above the door
#define OUT_Y      7
#define TUNNEL_Y   10

// left half (columns 0..10), mirrored at init. # wall, - door, o power pellet, space = no dot
static const char *const half[N] = {
    "###########",
    "#o...#.....",
    "#.##.#.#.#.",
    "#..........",
    "#.##.#.###.",
    "#....#.....",
    "####.#.####",
    "####.#.....",
    "####.#..##-",
    "####.#..#  ",
    "    ....#  ",
    "####.#..#  ",
    "####.#..###",
    "#..........",
    "#.##.#.###.",
    "#..........",
    "##.#.#.####",
    "#....#.....",
    "#.##.###.#.",
    "#o.........",
    "###########",
};

static const int8_t DX[4] = {0, -1, 0, 1}; // up, left, down, right (tie break order)
static const int8_t DY[4] = {-1, 0, 1, 0};

static const uint16_t phase_len[] = {350, 1000, 350, 1000, 250, 1000, 250}; // scatter, chase, ...
#define PHASES (int)(sizeof(phase_len) / sizeof(phase_len[0]))

static const int8_t corner_x[4] = {19, 1, 19, 1};
static const int8_t corner_y[4] = {0, 0, 20, 20};
static const int16_t release_t[4] = {0, 40, 200, 380};
static const int8_t start_x[4] = {10, 10, 9, 11};
static const int8_t start_y[4] = {7, 10, 10, 10};

#define C_WALL    RGB(30, 40, 220)
#define C_DOT     RGB(150, 105, 85)
#define C_PELLET  RGB(255, 190, 160)
#define C_PACMAN  RGB(255, 220, 0)
#define C_DOOR    RGB(200, 100, 160)
#define C_FRIGHT  RGB(30, 50, 255)

static const uint16_t ghost_color[4] = {RGB(255, 0, 0), RGB(255, 140, 220), RGB(0, 230, 230), RGB(255, 150, 0)};

enum { M_HOUSE, M_LEAVE, M_OUT, M_EYES };
enum { ST_READY, ST_PLAY, ST_DEAD, ST_CLEAR };

typedef struct {
    int tx, ty, dx, dy; // tile and direction
    int prog;           // 0..TU-1 travelled towards the next tile
    int mode, rel;
    bool fright, rev;
} actor_t;

typedef struct {
    view_t v;
    int ox, oy;
    bool wide, demo;
    int state, state_t, tick;
    int level, lives, points, dots;
    int phase, phase_t, fright_t, combo;
    int wdx, wdy; // wanted pac direction
    uint8_t cell[N][N]; // 0 empty, 1 dot, 2 pellet
    uint8_t hd[N][N];   // eyes: distance to the house
    actor_t pac, g[4];
} pacman_t;

_Static_assert(sizeof(pacman_t) <= GAME_STATE_MAX, "pacman state too big");

static char tile(int x, int y)
{
    if (y < 0 || y >= N)
        return '#';
    x = ((x % N) + N) % N;
    return half[y][x <= 10 ? x : 20 - x];
}

static bool walk(int x, int y, bool door)
{
    char c = tile(x, y);
    return c != '#' && (c != '-' || door);
}

static int wrapx(int x)
{
    return ((x % N) + N) % N;
}

static int iabs(int a)
{
    return a < 0 ? -a : a;
}

// BFS distances from n source tiles, 255 = unreachable
static void fill(uint8_t d[N][N], const int *sx, const int *sy, int n, bool door)
{
    uint16_t q[MAX_Q];
    int h = 0, t = 0;
    memset(d, 255, N * N);
    for (int i = 0; i < n; i++) {
        d[sy[i]][sx[i]] = 0;
        q[t++] = sy[i] * N + sx[i];
    }
    while (h < t) {
        int x = q[h] % N, y = q[h] / N;
        h++;
        for (int k = 0; k < 4; k++) {
            int nx = wrapx(x + DX[k]), ny = y + DY[k];
            if (!walk(nx, ny, door) || d[ny][nx] != 255)
                continue;
            d[ny][nx] = d[y][x] + 1;
            q[t++] = ny * N + nx;
        }
    }
}

static int level_speed(int base, int per, int cap, int lvl)
{
    int v = base + per * lvl;
    return v > cap ? cap : v;
}

static void fill_maze(pacman_t *s)
{
    s->dots = 0;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            char c = tile(x, y);
            s->cell[y][x] = c == '.' ? 1 : c == 'o' ? 2 : 0;
            if (c == '.' || c == 'o')
                s->dots++;
        }
}

static void reset_actors(pacman_t *s)
{
    memset(&s->pac, 0, sizeof(s->pac));
    s->pac.tx = 10;
    s->pac.ty = 15;
    s->wdx = s->wdy = 0;
    for (int i = 0; i < 4; i++) {
        actor_t *g = &s->g[i];
        memset(g, 0, sizeof(*g));
        g->tx = start_x[i];
        g->ty = start_y[i];
        g->mode = i ? M_HOUSE : M_OUT;
        g->dx = i ? 0 : -1;
        g->rel = release_t[i] - 30 * s->level;
        if (g->rel < 0)
            g->rel = 0;
    }
    s->phase = 0;
    s->phase_t = phase_len[0];
    s->fright_t = 0;
    s->state = ST_READY;
    s->state_t = READY_T;
}

static void init(void *p, view_t v, uint8_t players, bool versus)
{
    pacman_t *s = p;
    (void)versus;
    memset(s, 0, sizeof(*s));
    s->v = v;
    s->demo = players == 0;
    s->wide = v.w >= 128;
    s->ox = v.x + (s->wide ? (v.w - MAZE_PX) / 2 : 0);
    s->oy = v.y;
    s->lives = LIVES;
    int hx = HOUSE_X, hy = HOUSE_Y;
    fill(s->hd, &hx, &hy, 1, true);
    fill_maze(s);
    reset_actors(s);
}

// ---------- movement ----------

static void apos(const actor_t *a, int *x, int *y)
{
    *x = a->tx * TU + a->dx * a->prog;
    *y = a->ty * TU + a->dy * a->prog;
}

static bool is_threat(const actor_t *g)
{
    return (g->mode == M_OUT || g->mode == M_LEAVE) && !g->fright;
}

static bool fright_at(const pacman_t *s, int x, int y)
{
    for (int i = 0; i < 4; i++) {
        const actor_t *g = &s->g[i];
        if (g->fright && g->mode != M_EYES && g->tx == x && g->ty == y)
            return true;
    }
    return false;
}

// Demo AI: BFS to the nearest dot or close frightened ghost, cells a threatening ghost can reach first are blocked.
static int demo_search(const pacman_t *s, const uint8_t gd[N][N], bool allow_rev)
{
    uint16_t q[MAX_Q];
    uint8_t d[N][N], first[N][N];
    int h = 0, t = 0;
    int rev = -1;
    for (int k = 0; k < 4; k++)
        if (DX[k] == -s->pac.dx && DY[k] == -s->pac.dy && (s->pac.dx || s->pac.dy))
            rev = k;
    memset(d, 255, sizeof(d));
    d[s->pac.ty][s->pac.tx] = 0;
    q[t++] = s->pac.ty * N + s->pac.tx;
    while (h < t) {
        int x = q[h] % N, y = q[h] / N;
        h++;
        for (int k = 0; k < 4; k++) {
            if (h == 1 && k == rev && !allow_rev)
                continue;
            int nx = wrapx(x + DX[k]), ny = y + DY[k];
            if (!walk(nx, ny, false) || d[ny][nx] != 255)
                continue;
            int nd = d[y][x] + 1;
            if (gd[ny][nx] <= nd + 1)
                continue;
            d[ny][nx] = nd;
            first[ny][nx] = h == 1 ? k : first[y][x];
            if (s->cell[ny][nx] || (nd <= 8 && s->fright_t > nd * 8 && fright_at(s, nx, ny)))
                return first[ny][nx];
            q[t++] = ny * N + nx;
        }
    }
    return -1;
}

static void demo_choose(pacman_t *s)
{
    uint8_t gd[N][N];
    int sx[4], sy[4], n = 0;
    for (int i = 0; i < 4; i++)
        if (is_threat(&s->g[i])) {
            sx[n] = s->g[i].tx;
            sy[n] = s->g[i].ty;
            n++;
        }
    if (n)
        fill(gd, sx, sy, n, false);
    else
        memset(gd, 255, sizeof(gd));

    int k = demo_search(s, gd, false);
    if (k < 0)
        k = demo_search(s, gd, true);
    if (k < 0) { // boxed in, run away from the ghosts
        int best = -1;
        for (int i = 0; i < 4; i++) {
            int nx = wrapx(s->pac.tx + DX[i]), ny = s->pac.ty + DY[i];
            if (walk(nx, ny, false) && gd[ny][nx] > best) {
                best = gd[ny][nx];
                k = i;
            }
        }
    }
    if (k >= 0) {
        s->wdx = DX[k];
        s->wdy = DY[k];
    }
}

static void pac_choose(pacman_t *s)
{
    actor_t *a = &s->pac;
    if (s->demo)
        demo_choose(s);
    if ((s->wdx || s->wdy) && walk(wrapx(a->tx + s->wdx), a->ty + s->wdy, false)) {
        a->dx = s->wdx;
        a->dy = s->wdy;
    } else if (!walk(wrapx(a->tx + a->dx), a->ty + a->dy, false)) {
        a->dx = a->dy = 0;
    }
}

static void eat(pacman_t *s)
{
    const actor_t *a = &s->pac;
    int x = a->tx, y = a->ty;
    if (a->prog >= TU / 2) {
        x = wrapx(x + a->dx);
        y += a->dy;
    }
    uint8_t c = s->cell[y][x];
    if (!c)
        return;
    s->cell[y][x] = 0;
    s->dots--;
    s->points += c == 2 ? 50 : 10;
    if (c == 2) {
        fx_burst(s->ox + x * T + 1, s->oy + y * T + 1, C_PELLET, 12);
        s->fright_t = level_speed(360, -40, 120, s->level);
        s->combo = 0;
        for (int i = 0; i < 4; i++)
            if (s->g[i].mode != M_EYES) {
                s->g[i].fright = true;
                if (s->g[i].mode == M_OUT)
                    s->g[i].rev = true;
            }
    }
}

static void pac_move(pacman_t *s)
{
    actor_t *a = &s->pac;
    int rem = level_speed(88, 3, 112, s->level);
    // turning around in the middle of a tile
    if (a->prog > 0 && (s->wdx == -a->dx && s->wdy == -a->dy) && (a->dx || a->dy)) {
        a->tx = wrapx(a->tx + a->dx);
        a->ty += a->dy;
        a->prog = TU - a->prog;
        a->dx = -a->dx;
        a->dy = -a->dy;
    }
    while (rem > 0) {
        if (a->prog == 0) {
            pac_choose(s);
            if (!a->dx && !a->dy)
                break;
        }
        int step = TU - a->prog;
        if (step > rem)
            step = rem;
        a->prog += step;
        rem -= step;
        if (a->prog == TU) {
            a->tx = wrapx(a->tx + a->dx);
            a->ty += a->dy;
            a->prog = 0;
        }
    }
    eat(s);
}

static void ghost_choose(pacman_t *s, int i)
{
    actor_t *g = &s->g[i];
    bool door = g->mode == M_LEAVE || g->mode == M_EYES;
    int rev = -1, ok[4], n = 0;
    for (int k = 0; k < 4; k++) {
        if (DX[k] == -g->dx && DY[k] == -g->dy && (g->dx || g->dy))
            rev = k;
        if (walk(wrapx(g->tx + DX[k]), g->ty + DY[k], door))
            ok[n++] = k;
    }
    int pick = -1;
    if (g->mode == M_EYES) {
        int best = 999;
        for (int j = 0; j < n; j++) {
            int k = ok[j];
            int d = s->hd[g->ty + DY[k]][wrapx(g->tx + DX[k])];
            if (d < best) {
                best = d;
                pick = k;
            }
        }
    } else if (g->rev && rev >= 0 && walk(wrapx(g->tx + DX[rev]), g->ty + DY[rev], door)) {
        pick = rev;
    } else {
        int cand[4], m = 0;
        for (int j = 0; j < n; j++)
            if (ok[j] != rev)
                cand[m++] = ok[j];
        if (!m)
            pick = rev;
        else if (g->mode == M_OUT && g->fright) {
            pick = cand[esp_random() % m];
        } else {
            int tx, ty;
            bool chase = (s->phase & 1) == 1;
            const actor_t *p = &s->pac;
            if (g->mode == M_LEAVE) {
                tx = OUT_X;
                ty = OUT_Y;
            } else if (!chase) {
                tx = corner_x[i];
                ty = corner_y[i];
            } else if (i == 0) {
                tx = p->tx;
                ty = p->ty;
            } else if (i == 1) {
                tx = p->tx + 4 * p->dx;
                ty = p->ty + 4 * p->dy;
            } else if (i == 2) {
                tx = 2 * (p->tx + 2 * p->dx) - s->g[0].tx;
                ty = 2 * (p->ty + 2 * p->dy) - s->g[0].ty;
            } else {
                int dx = g->tx - p->tx, dy = g->ty - p->ty;
                if (dx * dx + dy * dy > 64) {
                    tx = p->tx;
                    ty = p->ty;
                } else {
                    tx = corner_x[i];
                    ty = corner_y[i];
                }
            }
            int best = 1 << 30;
            for (int j = 0; j < m; j++) {
                int k = cand[j];
                int dx = g->tx + DX[k] - tx, dy = g->ty + DY[k] - ty;
                int d = dx * dx + dy * dy;
                if (d < best) {
                    best = d;
                    pick = k;
                }
            }
        }
    }
    g->rev = false;
    if (pick >= 0) {
        g->dx = DX[pick];
        g->dy = DY[pick];
    } else {
        g->dx = g->dy = 0;
    }
}

static void ghost_move(pacman_t *s, int i)
{
    actor_t *g = &s->g[i];
    if (g->mode == M_HOUSE) {
        if (--g->rel <= 0) {
            g->mode = M_LEAVE;
            g->prog = 0;
        }
        return;
    }
    int rem;
    if (g->mode == M_EYES)
        rem = 150;
    else if (g->fright)
        rem = 46;
    else if (g->ty == TUNNEL_Y && (g->tx < 4 || g->tx > 16))
        rem = 48;
    else
        rem = level_speed(78, 4, 104, s->level);
    while (rem > 0) {
        if (g->prog == 0) {
            ghost_choose(s, i);
            if (!g->dx && !g->dy)
                break;
        }
        int step = TU - g->prog;
        if (step > rem)
            step = rem;
        g->prog += step;
        rem -= step;
        if (g->prog == TU) {
            g->tx = wrapx(g->tx + g->dx);
            g->ty += g->dy;
            g->prog = 0;
            if (g->mode == M_LEAVE && g->tx == OUT_X && g->ty == OUT_Y)
                g->mode = M_OUT;
            else if (g->mode == M_EYES && g->tx == HOUSE_X && g->ty == HOUSE_Y) {
                g->mode = M_LEAVE;
                g->fright = false;
            }
        }
    }
}

static void collide(pacman_t *s)
{
    int px, py;
    apos(&s->pac, &px, &py);
    for (int i = 0; i < 4; i++) {
        actor_t *g = &s->g[i];
        if (g->mode == M_HOUSE || g->mode == M_EYES)
            continue;
        int gx, gy;
        apos(g, &gx, &gy);
        if (iabs(gx - px) >= 512 || iabs(gy - py) >= 512)
            continue;
        int sx = s->ox + (gx >> 8) + 1, sy = s->oy + (gy >> 8) + 1;
        if (g->fright) {
            s->points += 200 << s->combo;
            if (s->combo < 3)
                s->combo++;
            g->mode = M_EYES;
            g->fright = false;
            g->rev = false;
            fx_burst(sx, sy, ghost_color[i], 14);
        } else {
            s->lives--;
            s->state = ST_DEAD;
            s->state_t = 0;
            fx_burst(s->ox + (px >> 8) + 1, s->oy + (py >> 8) + 1, C_PACMAN, 30);
            return;
        }
    }
}

static game_status_t update(void *p, const input_t *in)
{
    pacman_t *s = p;
    s->tick++;

    if (s->state == ST_READY) {
        if (--s->state_t <= 0)
            s->state = ST_PLAY;
        return GAME_RUNNING;
    }
    if (s->state == ST_DEAD) {
        if (++s->state_t < DEAD_T)
            return GAME_RUNNING;
        if (s->lives <= 0)
            return GAME_OVER;
        reset_actors(s);
        return GAME_RUNNING;
    }
    if (s->state == ST_CLEAR) {
        if (++s->state_t < CLEAR_T)
            return GAME_RUNNING;
        s->level++;
        fill_maze(s);
        reset_actors(s);
        return GAME_RUNNING;
    }

    if (!s->demo) {
        uint8_t h = in->held;
        uint8_t pr = in->pressed & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT);
        int want = -1;
        for (int k = 0; k < 4; k++) {
            uint8_t bit = (uint8_t[]){BTN_UP, BTN_LEFT, BTN_DOWN, BTN_RIGHT}[k];
            if (pr & bit) {
                want = k;
                break;
            }
        }
        if (want < 0) {
            bool still = false; // is the current wish still held
            for (int k = 0; k < 4; k++) {
                uint8_t bit = (uint8_t[]){BTN_UP, BTN_LEFT, BTN_DOWN, BTN_RIGHT}[k];
                if ((h & bit) && DX[k] == s->wdx && DY[k] == s->wdy)
                    still = true;
            }
            if (!still)
                for (int k = 0; k < 4; k++) {
                    uint8_t bit = (uint8_t[]){BTN_UP, BTN_LEFT, BTN_DOWN, BTN_RIGHT}[k];
                    if (h & bit) {
                        want = k;
                        break;
                    }
                }
        }
        if (want >= 0) {
            s->wdx = DX[want];
            s->wdy = DY[want];
        }
    }

    pac_move(s);
    for (int i = 0; i < 4; i++)
        ghost_move(s, i);
    collide(s);
    if (s->state != ST_PLAY)
        return GAME_RUNNING;

    if (s->fright_t > 0) {
        if (--s->fright_t == 0)
            for (int i = 0; i < 4; i++)
                s->g[i].fright = false;
    } else if (s->phase < PHASES && --s->phase_t <= 0) {
        s->phase++;
        s->phase_t = s->phase < PHASES ? phase_len[s->phase] : 0;
        for (int i = 0; i < 4; i++)
            if (s->g[i].mode == M_OUT)
                s->g[i].rev = true;
    }

    if (s->dots <= 0) {
        s->state = ST_CLEAR;
        s->state_t = 0;
    }
    return GAME_RUNNING;
}

// ---------- drawing ----------

// pixel inside the maze only
static void mpx(const pacman_t *s, int x, int y, uint16_t c)
{
    if (x < 0 || y < 0 || x >= MAZE_PX || y >= MAZE_PX)
        return;
    gfx_pixel(s->ox + x, s->oy + y, c);
}

static void draw_maze(const pacman_t *s)
{
    uint16_t wc = C_WALL;
    if (s->state == ST_CLEAR && (s->state_t / 8) % 2)
        wc = C_WHITE;
    bool blink = (s->tick / 12) % 2 == 0;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            char c = tile(x, y);
            int cx = x * T + 1, cy = y * T + 1;
            if (c == '#') {
                mpx(s, cx, cy, wc);
                if (x + 1 < N && tile(x + 1, y) == '#') {
                    mpx(s, cx + 1, cy, wc);
                    mpx(s, cx + 2, cy, wc);
                }
                if (y + 1 < N && tile(x, y + 1) == '#') {
                    mpx(s, cx, cy + 1, wc);
                    mpx(s, cx, cy + 2, wc);
                }
            } else if (c == '-') {
                for (int i = -1; i <= 1; i++)
                    mpx(s, cx + i, cy, C_DOOR);
            } else if (s->cell[y][x] == 1) {
                mpx(s, cx, cy, C_DOT);
            } else if (s->cell[y][x] == 2 && blink) {
                mpx(s, cx, cy, C_PELLET);
                mpx(s, cx - 1, cy, C_PELLET);
                mpx(s, cx + 1, cy, C_PELLET);
                mpx(s, cx, cy - 1, C_PELLET);
                mpx(s, cx, cy + 1, C_PELLET);
            }
        }
}

// 3x3 pacman, mouth towards (dx, dy), frame 0 closed, 1 ajar, 2 open
static void draw_pac(const pacman_t *s, int x, int y, int dx, int dy, int frame)
{
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) {
            if (frame >= 1 && i == 1 + dx && j == 1 + dy)
                continue;
            if (frame >= 2 && i == 1 && j == 1)
                continue;
            mpx(s, x + i, y + j, C_PACMAN);
        }
}

static void draw_ghost(const pacman_t *s, int i, int x, int y)
{
    const actor_t *g = &s->g[i];
    if (g->mode == M_EYES) {
        mpx(s, x, y + 1, C_WHITE);
        mpx(s, x + 2, y + 1, C_WHITE);
        return;
    }
    uint16_t c = ghost_color[i];
    uint16_t eye = C_WHITE;
    if (g->fright) {
        bool flash = s->fright_t < 100 && (s->fright_t / 8) % 2;
        c = flash ? C_WHITE : C_FRIGHT;
        eye = flash ? C_FRIGHT : C_PELLET;
    }
    for (int k = 0; k < 3; k++)
        mpx(s, x + k, y, c);
    mpx(s, x + 1, y + 1, c);
    mpx(s, x, y + 1, eye);
    mpx(s, x + 2, y + 1, eye);
    mpx(s, x, y + 2, c);
    mpx(s, x + 2, y + 2, c);
}

static void draw(const void *p)
{
    const pacman_t *s = p;
    const view_t *v = &s->v;
    char buf[16];

    draw_maze(s);

    snprintf(buf, sizeof(buf), "%d", s->points);
    if (s->wide) {
        gfx_text(v->x + 2, v->y + 4, 1, C_GRAY, "SCORE");
        gfx_text(v->x + 2, v->y + 11, 1, C_WHITE, buf);
        gfx_text(v->x + v->w - 30, v->y + 4, 1, C_GRAY, "LEVEL");
        snprintf(buf, sizeof(buf), "%d", s->level + 1);
        gfx_text(v->x + v->w - 30, v->y + 11, 1, C_WHITE, buf);
        for (int i = 0; i < s->lives; i++)
            for (int k = 0; k < 3; k++)
                gfx_rect(v->x + v->w - 30 + i * 5, v->y + 24 + k, 3, 1, C_PACMAN);
    } else {
        gfx_text_center(s->ox + 31, s->oy + 28, 1, C_GRAY, buf); // over the ghost house
        for (int i = 0; i < s->lives; i++)
            gfx_rect(s->ox + 2 + i * 3, s->oy + MAZE_PX, 2, 1, C_PACMAN);
    }

    if (s->state == ST_CLEAR)
        return;
    int px, py;
    apos(&s->pac, &px, &py);
    px >>= 8;
    py >>= 8;
    if (s->state == ST_DEAD) {
        int n = 9 - s->state_t * 10 / DEAD_T; // shrinks away
        if (s->state_t > 10 && n > 0 && n <= 9) {
            static const uint8_t order[9] = {4, 1, 7, 3, 5, 0, 2, 6, 8};
            for (int k = 0; k < n; k++)
                mpx(s, px + order[k] % 3, py + order[k] / 3, C_PACMAN);
        } else if (s->state_t <= 10) {
            draw_pac(s, px, py, s->pac.dx, s->pac.dy, 0);
        }
        return;
    }
    for (int i = 0; i < 4; i++) {
        int gx, gy;
        apos(&s->g[i], &gx, &gy);
        draw_ghost(s, i, gx >> 8, gy >> 8);
    }
    static const uint8_t anim[4] = {0, 1, 2, 1};
    int frame = (s->pac.dx || s->pac.dy) ? anim[(s->tick / 3) % 4] : 1;
    int dx = s->pac.dx, dy = s->pac.dy;
    if (!dx && !dy)
        dx = 1;
    draw_pac(s, px, py, dx, dy, frame);
}

static int score(const void *p)
{
    return ((const pacman_t *)p)->points;
}

const game_t game_pacman = {
    .vs_rule = VS_SCORE,
    .shared = false,
    .big = BIG_SCALE2,
    .init = init,
    .update = update,
    .draw = draw,
    .score = score,
};
