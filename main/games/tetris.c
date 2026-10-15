#include "game.h"

#include <stdio.h>
#include <string.h>
#include "esp_random.h"

#define COLS        10
#define ROWS        20
#define CELL        3
#define DAS_FIRST   9
#define DAS_REPEAT  3
#define LOCK_TICKS  10
#define LOCK_RESETS 8
#define FLASH_TICKS 15
#define DEATH_TICKS 60
#define AI_STEP     4 // demo: ticks between AI moves

// base shapes as 4x4 bitmasks (bit = row * 4 + col), piece ids 0..6
static const uint16_t shape[7] = {0x00F0, 0x0066, 0x0072, 0x0036, 0x0063, 0x0071, 0x0074};
static const uint8_t shape_n[7] = {4, 2, 3, 3, 3, 3, 3}; // rotation box size
// I, O, T, S, Z, J, L
static const uint16_t col[8] = {0, RGB(0, 200, 200), RGB(200, 160, 0), RGB(150, 40, 200), RGB(0, 170, 0),
                                RGB(200, 0, 0), RGB(30, 60, 220), RGB(220, 110, 0)};
static const uint16_t bright[8] = {0, RGB(120, 255, 255), RGB(255, 255, 100), RGB(220, 120, 255), RGB(120, 255, 120),
                                   RGB(255, 110, 110), RGB(110, 150, 255), RGB(255, 190, 90)};
static const uint8_t gravity[15] = {40, 34, 28, 23, 19, 15, 12, 9, 7, 5, 4, 3, 2, 2, 1};
static const int points[5] = {0, 40, 100, 300, 1200};

static uint32_t seed; // shared piece sequence, drawn by the lowest player
static int last_player = 99;

typedef struct {
    view_t v;
    int player;
    bool versus, wide, demo;
    uint8_t board[ROWS][COLS]; // 0 empty, else colour index 1..7
    uint8_t bag[7];
    int bag_i;
    int next;
    int id, rot, x, y; // current piece
    uint16_t mask;     // current piece rotated
    int g_t, lock_t, resets, das, sd_t;
    int flash_t, dead_t;
    uint32_t clear_rows; // bit per row being cleared
    bool dead;
    int score, lines, level;
    uint32_t rng;
    int bx, by; // board origin
    int ai_x, ai_rot, ai_t; // demo: target column and rotation, move timer
} tetris_t;

_Static_assert(sizeof(tetris_t) <= GAME_STATE_MAX, "tetris state too big");

static uint32_t rnd(tetris_t *s)
{
    uint32_t x = s->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return s->rng = x;
}

static int next_piece(tetris_t *s)
{
    if (s->bag_i >= 7) {
        for (int i = 0; i < 7; i++)
            s->bag[i] = i;
        for (int i = 6; i > 0; i--) {
            int j = rnd(s) % (i + 1);
            uint8_t t = s->bag[i];
            s->bag[i] = s->bag[j];
            s->bag[j] = t;
        }
        s->bag_i = 0;
    }
    return s->bag[s->bag_i++];
}

// mask of piece id rotated r times clockwise
static uint16_t rotated(int id, int r)
{
    int n = shape_n[id];
    uint16_t m = shape[id];
    if (id == 1)
        return m;
    while (r-- > 0) {
        uint16_t o = 0;
        for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++)
                if (m & (1 << (y * 4 + x)))
                    o |= 1 << (x * 4 + (n - 1 - y));
        m = o;
    }
    return m;
}

static bool collides(const tetris_t *s, uint16_t m, int px, int py)
{
    for (int i = 0; i < 16; i++) {
        if (!(m & (1 << i)))
            continue;
        int cx = px + (i & 3), cy = py + (i >> 2);
        if (cx < 0 || cx >= COLS || cy >= ROWS)
            return true;
        if (cy >= 0 && s->board[cy][cx])
            return true;
    }
    return false;
}

// row the current piece would land on
static int land_y(const tetris_t *s)
{
    int y = s->y;
    while (!collides(s, s->mask, s->x, y + 1))
        y++;
    return y;
}

// piece colour at about 25% brightness
static uint16_t dim(uint16_t c)
{
    return (((c >> 11) >> 2) << 11) | ((((c >> 5) & 63) >> 2) << 5) | ((c & 31) >> 2);
}

// demo AI: try every rotation x column, drop straight down, score the board
static void ai_plan(tetris_t *s)
{
    float best = -1e9f;
    s->ai_x = s->x;
    s->ai_rot = 0;
    for (int r = 0; r < 4; r++) {
        uint16_t m = rotated(s->id, r);
        for (int x = -3; x < COLS; x++) {
            if (collides(s, m, x, 0))
                continue;
            int y = 0;
            while (!collides(s, m, x, y + 1))
                y++;
            uint8_t b[ROWS][COLS];
            memcpy(b, s->board, sizeof(b));
            for (int i = 0; i < 16; i++)
                if ((m & (1 << i)) && y + (i >> 2) >= 0)
                    b[y + (i >> 2)][x + (i & 3)] = 1;
            int lines = 0;
            for (int cy = 0; cy < ROWS; cy++) {
                int n = 0;
                for (int cx = 0; cx < COLS; cx++)
                    n += b[cy][cx] != 0;
                lines += n == COLS;
            }
            int height = 0, holes = 0, bump = 0, prev = 0;
            for (int cx = 0; cx < COLS; cx++) {
                int h = 0;
                for (int cy = 0; cy < ROWS; cy++) {
                    if (b[cy][cx]) {
                        if (!h)
                            h = ROWS - cy;
                    } else if (h)
                        holes++;
                }
                height += h;
                if (cx)
                    bump += h > prev ? h - prev : prev - h;
                prev = h;
            }
            float sc = -0.51f * height + 0.76f * lines - 0.36f * holes - 0.18f * bump;
            if (sc > best) {
                best = sc;
                s->ai_x = x;
                s->ai_rot = r;
            }
        }
    }
}

static void spawn(tetris_t *s)
{
    s->id = s->next;
    s->next = next_piece(s);
    s->rot = 0;
    s->x = 3;
    s->y = 0;
    s->mask = rotated(s->id, 0);
    s->g_t = s->lock_t = s->resets = s->sd_t = s->ai_t = 0;
    if (collides(s, s->mask, s->x, s->y)) {
        s->dead = true;
        // explosion over the board
        for (int i = 0; i < 6; i++)
            fx_burst(s->bx + (i % 3) * 15 + 3, s->by + (i / 3) * 30 + 15, i % 2 ? C_RED : C_YELLOW, 14);
    } else if (s->demo)
        ai_plan(s);
}

static void init(void *p, view_t v, uint8_t players, bool versus)
{
    tetris_t *s = p;
    memset(s, 0, sizeof(*s));
    s->v = v;
    s->demo = players == 0;
    s->player = s->demo ? 0 : __builtin_ctz(players);
    s->versus = versus;
    s->wide = v.w >= 128;
    if (s->player <= last_player) // first instance of a new game
        seed = esp_random() | 1;
    last_player = s->player;
    s->rng = seed;
    s->bag_i = 7;
    s->bx = s->wide ? v.x + 49 : v.x + 1;
    s->by = v.y + 2;
    s->next = next_piece(s);
    spawn(s);
}

// a successful move or rotate while resting on the stack extends the lock delay
static void moved(tetris_t *s)
{
    if (s->lock_t > 0 && s->resets < LOCK_RESETS) {
        s->lock_t = 0;
        s->resets++;
    }
}

static bool try_move(tetris_t *s, int dx)
{
    if (collides(s, s->mask, s->x + dx, s->y))
        return false;
    s->x += dx;
    moved(s);
    return true;
}

static void try_rotate(tetris_t *s)
{
    int r = (s->rot + 1) & 3;
    uint16_t m = rotated(s->id, r);
    static const int8_t kick[5] = {0, -1, 1, -2, 2};
    for (int i = 0; i < 5; i++) {
        if (collides(s, m, s->x + kick[i], s->y))
            continue;
        s->rot = r;
        s->mask = m;
        s->x += kick[i];
        moved(s);
        return;
    }
}

// demo AI input: rotate, then step sideways, then hard drop, one action per AI_STEP ticks
static void ai_input(tetris_t *s, input_t *in)
{
    memset(in, 0, sizeof(*in));
    if (++s->ai_t < AI_STEP)
        return;
    s->ai_t = 0;
    if (s->rot != s->ai_rot) {
        int r = s->rot;
        try_rotate(s);
        if (s->rot == r) // blocked, give up on this rotation
            s->ai_rot = r;
    } else if (s->x != s->ai_x) {
        if (!try_move(s, s->ai_x > s->x ? 1 : -1))
            s->ai_x = s->x;
    } else {
        in->pressed = BTN_B;
    }
}

static void lock_piece(tetris_t *s)
{
    for (int i = 0; i < 16; i++) {
        if (!(s->mask & (1 << i)))
            continue;
        int cx = s->x + (i & 3), cy = s->y + (i >> 2);
        if (cy >= 0)
            s->board[cy][cx] = s->id + 1;
    }
    s->clear_rows = 0;
    for (int y = 0; y < ROWS; y++) {
        bool full = true;
        for (int x = 0; x < COLS; x++)
            if (!s->board[y][x])
                full = false;
        if (full)
            s->clear_rows |= 1u << y;
    }
    if (s->clear_rows) {
        s->flash_t = FLASH_TICKS;
        for (int y = 0; y < ROWS; y++)
            if ((s->clear_rows >> y) & 1)
                for (int x = 0; x < COLS; x += 2)
                    fx_burst(s->bx + x * CELL + CELL, s->by + y * CELL + 1, bright[s->board[y][x] ? s->board[y][x] : 1], 3);
    } else
        spawn(s);
}

static void finish_clear(tetris_t *s)
{
    int n = 0, dst = ROWS - 1;
    for (int y = ROWS - 1; y >= 0; y--) {
        if (s->clear_rows & (1u << y)) {
            n++;
            continue;
        }
        if (dst != y)
            memcpy(s->board[dst], s->board[y], COLS);
        dst--;
    }
    for (; dst >= 0; dst--)
        memset(s->board[dst], 0, COLS);
    s->score += points[n] * (s->level + 1);
    s->lines += n;
    s->level = s->lines / 10 > 14 ? 14 : s->lines / 10;
    s->clear_rows = 0;
    spawn(s);
}

static game_status_t update(void *p, const input_t *in)
{
    tetris_t *s = p;

    if (s->dead)
        return ++s->dead_t >= DEATH_TICKS ? GAME_OVER : GAME_RUNNING;
    if (s->flash_t) {
        if (--s->flash_t == 0)
            finish_clear(s);
        return GAME_RUNNING;
    }

    input_t ai;
    if (s->demo) { // ignore the real input
        ai_input(s, &ai);
        ai.held = ai.pressed;
        in = &ai;
    }

    // horizontal move with auto-repeat
    int dir = (in->held & BTN_LEFT) ? -1 : (in->held & BTN_RIGHT) ? 1 : 0;
    if ((in->held & BTN_LEFT) && (in->held & BTN_RIGHT))
        dir = 0;
    if (in->pressed & (BTN_LEFT | BTN_RIGHT)) {
        if (dir)
            try_move(s, dir);
        s->das = 0;
    } else if (dir) {
        s->das++;
        if (s->das >= DAS_FIRST && (s->das - DAS_FIRST) % DAS_REPEAT == 0)
            try_move(s, dir);
    } else {
        s->das = 0;
    }

    if (in->pressed & BTN_A)
        try_rotate(s);

    if (in->pressed & BTN_B) {
        while (!collides(s, s->mask, s->x, s->y + 1)) {
            s->y++;
            s->score += 2;
        }
        for (int i = 0; i < 16; i++) // small burst under the piece
            if ((s->mask & (1 << i)) && !(s->mask & (1 << (i + 4))))
                fx_burst(s->bx + (s->x + (i & 3)) * CELL + 1, s->by + (s->y + (i >> 2) + 1) * CELL, bright[s->id + 1], 2);
        lock_piece(s);
        return GAME_RUNNING;
    }

    // soft drop
    if (in->held & BTN_DOWN) {
        if (++s->sd_t >= 2) {
            s->sd_t = 0;
            if (!collides(s, s->mask, s->x, s->y + 1)) {
                s->y++;
                s->score++;
                s->g_t = 0;
            }
        }
    } else {
        s->sd_t = 0;
    }

    // gravity
    if (++s->g_t >= gravity[s->level]) {
        s->g_t = 0;
        if (!collides(s, s->mask, s->x, s->y + 1))
            s->y++;
    }

    // lock delay
    if (collides(s, s->mask, s->x, s->y + 1)) {
        if (++s->lock_t >= LOCK_TICKS)
            lock_piece(s);
    } else {
        s->lock_t = 0;
    }
    return GAME_RUNNING;
}

static void cell(int ox, int oy, int x, int y, uint16_t c)
{
    gfx_rect(ox + x * CELL, oy + y * CELL, CELL, CELL, c);
}

static void draw_next(const tetris_t *s, int x, int y)
{
    uint16_t m = shape[s->next];
    for (int i = 0; i < 8; i++)
        if (m & (1 << i))
            cell(x, y, i & 3, i >> 2, col[s->next + 1]);
}

static void draw(const void *p)
{
    const tetris_t *s = p;
    const view_t *v = &s->v;
    char buf[16];

    // board frame
    gfx_rect(s->bx - 1, s->by - 1, COLS * CELL + 2, ROWS * CELL + 2, C_DARK);
    gfx_rect(s->bx, s->by, COLS * CELL, ROWS * CELL, C_BLACK);

    bool hide = s->dead && (s->dead_t / 8) % 2;
    if (!hide) {
        for (int y = 0; y < ROWS; y++) {
            bool flash = (s->clear_rows >> y) & 1;
            for (int x = 0; x < COLS; x++) {
                if (flash)
                    cell(s->bx, s->by, x, y, (s->flash_t / 3) % 2 ? C_WHITE : C_GRAY);
                else if (s->board[y][x])
                    cell(s->bx, s->by, x, y, col[s->board[y][x]]);
            }
        }
    }
    if (!s->dead && !s->flash_t) {
        int gy = land_y(s);
        for (int i = 0; i < 16; i++) // ghost under the piece
            if (s->mask & (1 << i)) {
                int cy = gy + (i >> 2);
                if (cy >= 0)
                    cell(s->bx, s->by, s->x + (i & 3), cy, dim(col[s->id + 1]));
            }
        for (int i = 0; i < 16; i++)
            if (s->mask & (1 << i)) {
                int cy = s->y + (i >> 2);
                if (cy >= 0)
                    cell(s->bx, s->by, s->x + (i & 3), cy, bright[s->id + 1]);
            }
    }

    // dark panels behind the text so it reads over the ambient background
    if (s->wide) {
        gfx_rect(v->x, v->y, 47, 64, C_BLACK);
        gfx_rect(v->x + 83, v->y, 45, 64, C_BLACK);
    } else {
        gfx_rect(v->x + 33, v->y, 31, 64, C_BLACK);
    }

    if (s->wide) {
        int lx = v->x + 2, rx = v->x + 86;
        gfx_text(lx, v->y + 4, 1, C_GRAY, "SCORE");
        snprintf(buf, sizeof(buf), "%d", s->score);
        gfx_text(lx, v->y + 12, 2, C_WHITE, buf);
        gfx_text(lx, v->y + 28, 1, C_GRAY, "LINES");
        snprintf(buf, sizeof(buf), "%d", s->lines);
        gfx_text(lx, v->y + 36, 1, C_WHITE, buf);
        gfx_text(lx, v->y + 46, 1, C_GRAY, "LEVEL");
        snprintf(buf, sizeof(buf), "%d", s->level);
        gfx_text(lx, v->y + 54, 1, C_WHITE, buf);
        gfx_text(rx, v->y + 4, 1, C_GRAY, "NEXT");
        draw_next(s, rx, v->y + 14);
    } else {
        int rx = v->x + 34;
        draw_next(s, rx + 2, v->y + 2);
        snprintf(buf, sizeof(buf), "%d", s->score);
        gfx_text(rx, v->y + 16, 1, C_WHITE, buf);
        snprintf(buf, sizeof(buf), "LV%d", s->level);
        gfx_text(rx, v->y + 28, 1, C_GRAY, buf);
        snprintf(buf, sizeof(buf), "L%d", s->lines);
        gfx_text(rx, v->y + 36, 1, C_GRAY, buf);
        if (s->versus)
        {
            snprintf(buf, sizeof(buf), "P%d", s->player + 1);
            gfx_text(rx, v->y + 56, 1, player_color[s->player], buf);
        }
    }
}

static int score(const void *p)
{
    return ((const tetris_t *)p)->score;
}

const game_t game_tetris = {
    .vs_rule = VS_FIRST_DEATH,
    .big = BIG_SCALE2,
    .init = init,
    .update = update,
    .draw = draw,
    .score = score,
};
