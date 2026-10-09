// Snake arena - 2-12 players on one board across both panels.
#include "game.h"

#include <stdio.h>
#include <string.h>
#include "esp_random.h"

#define CELL        2
#define HUD         12
#define COLS        (HUB75_W / CELL)
#define MAX_ROWS    ((HUB75_H - HUD) / CELL)
#define MAX_CELLS   (COLS * MAX_ROWS)
#define MAX_LEN     (MAX_CELLS / MAX_PLAYERS)
#define FOOD_N      3
#define DEATH_TICKS 60

typedef struct {
    uint16_t body[MAX_LEN]; // ring buffer of cell indexes, head at index head
    int head, len;
    int dx, dy;
    int8_t qx[2], qy[2];
    int qn;
    int score;
    bool dead;
} worm_t;

typedef struct {
    player_mask_t players;
    int rows;
    worm_t w[MAX_PLAYERS];
    uint8_t occ[MAX_CELLS];
    uint16_t food[FOOD_N];
    int tick, interval, dead_t;
    bool over;
} arena_t;

_Static_assert(sizeof(arena_t) <= GAME_STATE_MAX, "arena state too big");

static bool playing(const arena_t *s, int p)
{
    return (s->players >> p) & 1;
}

static int cell_at(const worm_t *w, int i)
{
    return w->body[(w->head - i + MAX_LEN) % MAX_LEN];
}

static void place_food(arena_t *s, int f)
{
    int cells = COLS * s->rows;
    int i = esp_random() % cells;
    for (int n = 0; n < cells; n++, i = (i + 1) % cells) {
        bool taken = s->occ[i];
        for (int k = 0; k < FOOD_N; k++)
            taken |= k != f && s->food[k] == i;
        if (!taken)
            break;
    }
    s->food[f] = i;
}

static void init(void *p, view_t v, player_mask_t players, bool versus)
{
    arena_t *s = p;
    memset(s, 0, sizeof(*s));
    s->players = players;
    s->rows = (v.h - HUD) / CELL;
    int count = __builtin_popcount(players), rank = 0;
    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        worm_t *w = &s->w[pl];
        if (!playing(s, pl)) {
            w->dead = true;
            continue;
        }
        w->len = 4;
        w->head = w->len - 1;
        int side = rank & 1;
        int row = (rank / 2 + 1) * s->rows / ((count + 1) / 2 + 1);
        int col = side ? COLS * 4 / 5 : COLS / 5;
        w->dx = side ? -1 : 1;
        rank++;
        for (int i = 0; i < w->len; i++) {
            // body[0] is the tail, body[len - 1] the head
            w->body[i] = row * COLS + col + (i - w->len + 1) * w->dx;
            s->occ[w->body[i]] = 1;
        }
    }
    s->interval = 10;
    for (int f = 0; f < FOOD_N; f++)
        s->food[f] = 0xffff;
    for (int f = 0; f < FOOD_N; f++)
        place_food(s, f);
}

static void queue_turn(worm_t *w, int dx, int dy)
{
    int lx = w->qn ? w->qx[w->qn - 1] : w->dx;
    int ly = w->qn ? w->qy[w->qn - 1] : w->dy;
    if ((dx == lx && dy == ly) || (dx == -lx && dy == -ly) || w->qn == 2)
        return;
    w->qx[w->qn] = dx;
    w->qy[w->qn] = dy;
    w->qn++;
}

static int alive_count(const arena_t *s)
{
    int n = 0;
    for (int pl = 0; pl < MAX_PLAYERS; pl++)
        n += !s->w[pl].dead;
    return n;
}

static game_status_t update(void *p, const input_t *in)
{
    arena_t *s = p;

    if (s->over)
        return ++s->dead_t >= DEATH_TICKS ? GAME_OVER : GAME_RUNNING;

    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        worm_t *w = &s->w[pl];
        if (w->dead)
            continue;
        if (in[pl].pressed & BTN_UP) queue_turn(w, 0, -1);
        if (in[pl].pressed & BTN_DOWN) queue_turn(w, 0, 1);
        if (in[pl].pressed & BTN_LEFT) queue_turn(w, -1, 0);
        if (in[pl].pressed & BTN_RIGHT) queue_turn(w, 1, 0);
    }

    if (++s->tick < s->interval)
        return GAME_RUNNING;
    s->tick = 0;

    int nh[MAX_PLAYERS], eat[MAX_PLAYERS];
    bool died[MAX_PLAYERS] = {0};
    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        worm_t *w = &s->w[pl];
        nh[pl] = -1;
        eat[pl] = -1;
        if (w->dead)
            continue;
        if (w->qn) {
            w->dx = w->qx[0];
            w->dy = w->qy[0];
            w->qx[0] = w->qx[1];
            w->qy[0] = w->qy[1];
            w->qn--;
        }
        int h = cell_at(w, 0);
        int x = h % COLS + w->dx, y = h / COLS + w->dy;
        if (x < 0 || y < 0 || x >= COLS || y >= s->rows) {
            died[pl] = true;
            continue;
        }
        nh[pl] = y * COLS + x;
        for (int f = 0; f < FOOD_N; f++)
            if (s->food[f] == nh[pl])
                eat[pl] = f;
    }

    // tails move first, so following a tail is fine
    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        worm_t *w = &s->w[pl];
        if (!w->dead && (eat[pl] < 0 || w->len >= MAX_LEN))
            s->occ[cell_at(w, w->len - 1)] = 0;
    }

    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        if (nh[pl] < 0)
            continue;
        if (s->occ[nh[pl]])
            died[pl] = true;
        for (int o = 0; o < MAX_PLAYERS; o++)
            if (o != pl && nh[o] == nh[pl])
                died[pl] = true;
    }

    for (int pl = 0; pl < MAX_PLAYERS; pl++)
        if (died[pl]) {
            int h = cell_at(&s->w[pl], 0);
            fx_burst(h % COLS * CELL + 1, HUD + h / COLS * CELL + 1, player_color[pl], 30);
            s->w[pl].dead = true;
        }

    if (alive_count(s) <= 1) {
        s->over = true; // keep the last dead bodies on screen, they blink
        return GAME_RUNNING;
    }

    // dead snakes leave the board when the game goes on, head included
    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        if (!died[pl])
            continue;
        worm_t *w = &s->w[pl];
        for (int i = 0; i < w->len; i++)
            s->occ[cell_at(w, i)] = 0;
        w->len = 0;
    }

    int total = 0;
    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        worm_t *w = &s->w[pl];
        total += w->score;
        if (w->dead)
            continue;
        if (eat[pl] >= 0) {
            fx_burst(nh[pl] % COLS * CELL + 1, HUD + nh[pl] / COLS * CELL + 1, C_RED, 10);
            if (w->len < MAX_LEN)
                w->len++;
            w->score++;
        }
        w->head = (w->head + 1) % MAX_LEN;
        w->body[w->head] = nh[pl];
        s->occ[nh[pl]] = 1;
    }
    for (int pl = 0; pl < MAX_PLAYERS; pl++)
        if (!s->w[pl].dead && eat[pl] >= 0)
            place_food(s, eat[pl]);

    s->interval = 10 - total / 12;
    if (s->interval < 3)
        s->interval = 3;
    return GAME_RUNNING;
}

static void draw(const void *p)
{
    const arena_t *s = p;
    char buf[12];

    // scores in two HUD rows, six slots per row
    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        if (!playing(s, pl))
            continue;
        snprintf(buf, sizeof(buf), "%d", s->w[pl].score);
        gfx_text_center(11 + (pl % 6) * 21, (pl / 6) * 6, 1, s->w[pl].dead ? C_DARK : player_color[pl], buf);
    }
    gfx_rect(0, HUD - 1, HUB75_W, 1, C_DARK);

    for (int f = 0; f < FOOD_N; f++)
        gfx_rect(s->food[f] % COLS * CELL, HUD + s->food[f] / COLS * CELL, CELL, CELL, C_RED);

    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        const worm_t *w = &s->w[pl];
        if (w->dead && (s->dead_t / 8) % 2)
            continue; // blink when dead
        for (int i = 0; i < w->len; i++) {
            int c = cell_at(w, i);
            gfx_rect(c % COLS * CELL, HUD + c / COLS * CELL, CELL, CELL, i ? player_color[pl] : C_WHITE);
        }
    }
}

static int score_of(const void *p, int player)
{
    return ((const arena_t *)p)->w[player].score;
}

static player_mask_t winners(const void *p)
{
    const arena_t *s = p;
    for (int pl = 0; pl < MAX_PLAYERS; pl++)
        if (!s->w[pl].dead)
            return 1 << pl;
    return 0; // the last ones died together
}

const game_t game_snake_arena = {
    .shared = true,
    .big = BIG_NATIVE,
    .init = init,
    .update = update,
    .draw = draw,
    .score_of = score_of,
    .winners = winners,
};
