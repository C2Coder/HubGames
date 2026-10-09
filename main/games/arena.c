// Snake arena - both players on one board across both panels.
#include "game.h"

#include <stdio.h>
#include <string.h>
#include "esp_random.h"

#define CELL        2
#define HUD         6
#define COLS        (HUB75_W / CELL)
#define ROWS        ((HUB75_H - HUD) / CELL)
#define MAX_CELLS   (COLS * ROWS)
#define FOOD_N      2
#define DEATH_TICKS 60

typedef struct {
    uint16_t body[MAX_CELLS]; // ring buffer of cell indexes, head at index head
    int head, len;
    int dx, dy;
    int8_t qx[2], qy[2];
    int qn;
    int score;
    bool dead;
} worm_t;

typedef struct {
    view_t v;
    worm_t w[2];
    uint8_t occ[MAX_CELLS];
    uint16_t food[FOOD_N];
    int tick, interval, dead_t;
    bool over;
} arena_t;

_Static_assert(sizeof(arena_t) <= GAME_STATE_MAX, "arena state too big");

static void place_food(arena_t *s, int f)
{
    int i = esp_random() % MAX_CELLS;
    for (int n = 0; n < MAX_CELLS; n++, i = (i + 1) % MAX_CELLS) {
        bool taken = s->occ[i];
        for (int k = 0; k < FOOD_N; k++)
            taken |= k != f && s->food[k] == i;
        if (!taken)
            break;
    }
    s->food[f] = i;
}

static void init(void *p, view_t v, int player, bool versus)
{
    arena_t *s = p;
    memset(s, 0, sizeof(*s));
    s->v = v;
    for (int pl = 0; pl < 2; pl++) {
        worm_t *w = &s->w[pl];
        w->len = 4;
        w->head = w->len - 1;
        w->dx = pl ? -1 : 1;
        int y = pl ? ROWS * 2 / 3 : ROWS / 3;
        for (int i = 0; i < w->len; i++) {
            int x = pl ? COLS - COLS / 6 - i : COLS / 6 + i;
            w->body[i] = y * COLS + x;
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

static game_status_t update(void *p, const input_t *in)
{
    arena_t *s = p;

    if (s->over)
        return ++s->dead_t >= DEATH_TICKS ? GAME_OVER : GAME_RUNNING;

    for (int pl = 0; pl < 2; pl++) {
        worm_t *w = &s->w[pl];
        if (in[pl].pressed & BTN_UP) queue_turn(w, 0, -1);
        if (in[pl].pressed & BTN_DOWN) queue_turn(w, 0, 1);
        if (in[pl].pressed & BTN_LEFT) queue_turn(w, -1, 0);
        if (in[pl].pressed & BTN_RIGHT) queue_turn(w, 1, 0);
    }

    if (++s->tick < s->interval)
        return GAME_RUNNING;
    s->tick = 0;

    int nh[2], eat[2];
    for (int pl = 0; pl < 2; pl++) {
        worm_t *w = &s->w[pl];
        if (w->qn) {
            w->dx = w->qx[0];
            w->dy = w->qy[0];
            w->qx[0] = w->qx[1];
            w->qy[0] = w->qy[1];
            w->qn--;
        }
        int h = w->body[w->head];
        int x = h % COLS + w->dx, y = h / COLS + w->dy;
        nh[pl] = -1;
        eat[pl] = -1;
        if (x < 0 || y < 0 || x >= COLS || y >= ROWS) {
            w->dead = true;
            continue;
        }
        nh[pl] = y * COLS + x;
        for (int f = 0; f < FOOD_N; f++)
            if (s->food[f] == nh[pl])
                eat[pl] = f;
    }

    // tails move first, so following a tail is fine
    for (int pl = 0; pl < 2; pl++) {
        worm_t *w = &s->w[pl];
        if (eat[pl] < 0)
            s->occ[w->body[(w->head - w->len + 1 + MAX_CELLS) % MAX_CELLS]] = 0;
    }

    for (int pl = 0; pl < 2; pl++)
        if (nh[pl] >= 0 && (s->occ[nh[pl]] || nh[pl] == nh[!pl]))
            s->w[pl].dead = true;

    if (s->w[0].dead || s->w[1].dead) {
        s->over = true;
        return GAME_RUNNING;
    }

    for (int pl = 0; pl < 2; pl++) {
        worm_t *w = &s->w[pl];
        if (eat[pl] >= 0) {
            w->len++;
            w->score++;
        }
        w->head = (w->head + 1) % MAX_CELLS;
        w->body[w->head] = nh[pl];
        s->occ[nh[pl]] = 1;
    }
    for (int pl = 0; pl < 2; pl++)
        if (eat[pl] >= 0)
            place_food(s, eat[pl]);

    s->interval = 10 - (s->w[0].score + s->w[1].score) / 12;
    if (s->interval < 3)
        s->interval = 3;
    return GAME_RUNNING;
}

static void draw(const void *p)
{
    const arena_t *s = p;
    char buf[12];

    for (int pl = 0; pl < 2; pl++) {
        snprintf(buf, sizeof(buf), "%d", s->w[pl].score);
        int x = pl ? HUB75_W - 1 - gfx_text_width(1, buf) : 1;
        gfx_text(x, 0, 1, player_color[pl], buf);
    }
    gfx_rect(0, HUD - 1, HUB75_W, 1, C_DARK);

    for (int f = 0; f < FOOD_N; f++)
        gfx_rect(s->food[f] % COLS * CELL, HUD + s->food[f] / COLS * CELL, CELL, CELL, C_RED);

    for (int pl = 0; pl < 2; pl++) {
        const worm_t *w = &s->w[pl];
        if (w->dead && (s->dead_t / 8) % 2)
            continue; // blink when dead
        for (int i = 0; i < w->len; i++) {
            int c = w->body[(w->head - i + MAX_CELLS) % MAX_CELLS];
            gfx_rect(c % COLS * CELL, HUD + c / COLS * CELL, CELL, CELL, i ? player_color[pl] : C_WHITE);
        }
    }
}

static int score_of(const void *p, int player)
{
    return ((const arena_t *)p)->w[player].score;
}

static int winner(const void *p)
{
    const arena_t *s = p;
    if (s->w[0].dead == s->w[1].dead)
        return 0;
    return s->w[0].dead ? 2 : 1;
}

const game_t game_snake_arena = {
    .shared = true,
    .init = init,
    .update = update,
    .draw = draw,
    .score_of = score_of,
    .winner = winner,
};
