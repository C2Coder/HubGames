#include "game.h"

#include <stdio.h>
#include <string.h>
#include "esp_random.h"

#define CELL      2
#define HUD       6 // score line on top
#define MAX_CELLS (HUB75_W / CELL * (HUB75_H - HUD) / CELL)
#define DEATH_TICKS 60

typedef struct {
    view_t v;
    int player;
    bool versus;
    int cols, rows;
    uint8_t occ[MAX_CELLS];          // occupied cells, y * cols + x
    uint8_t x[MAX_CELLS], y[MAX_CELLS]; // body ring buffer, head at index head
    int head, len;
    int dx, dy;
    int8_t qx[2], qy[2]; // queued turns
    int qn;
    int fx, fy;
    int score, tick, interval, dead_t;
    bool dead;
} snake_t;

_Static_assert(sizeof(snake_t) <= GAME_STATE_MAX, "snake state too big");

static bool place_food(snake_t *s)
{
    int n = s->cols * s->rows;
    if (s->len >= n)
        return false;
    int i = esp_random() % n;
    while (s->occ[i])
        i = (i + 1) % n;
    s->fx = i % s->cols;
    s->fy = i / s->cols;
    return true;
}

static void init(void *p, view_t v, uint8_t players, bool versus)
{
    snake_t *s = p;
    memset(s, 0, sizeof(*s));
    s->v = v;
    s->player = __builtin_ctz(players);
    s->versus = versus;
    s->cols = v.w / CELL;
    s->rows = (v.h - HUD) / CELL;
    s->len = 4;
    s->head = s->len - 1;
    for (int i = 0; i < s->len; i++) {
        s->x[i] = s->cols / 4 + i;
        s->y[i] = s->rows / 2;
        s->occ[s->y[i] * s->cols + s->x[i]] = 1;
    }
    s->dx = 1;
    s->interval = 10;
    place_food(s);
}

static void queue_turn(snake_t *s, int dx, int dy)
{
    int lx = s->qn ? s->qx[s->qn - 1] : s->dx;
    int ly = s->qn ? s->qy[s->qn - 1] : s->dy;
    if ((dx == lx && dy == ly) || (dx == -lx && dy == -ly) || s->qn == 2)
        return;
    s->qx[s->qn] = dx;
    s->qy[s->qn] = dy;
    s->qn++;
}

static game_status_t update(void *p, const input_t *in)
{
    snake_t *s = p;

    if (s->dead)
        return ++s->dead_t >= DEATH_TICKS ? GAME_OVER : GAME_RUNNING;

    if (in->pressed & BTN_UP) queue_turn(s, 0, -1);
    if (in->pressed & BTN_DOWN) queue_turn(s, 0, 1);
    if (in->pressed & BTN_LEFT) queue_turn(s, -1, 0);
    if (in->pressed & BTN_RIGHT) queue_turn(s, 1, 0);

    if (++s->tick < s->interval)
        return GAME_RUNNING;
    s->tick = 0;

    if (s->qn) {
        s->dx = s->qx[0];
        s->dy = s->qy[0];
        s->qx[0] = s->qx[1];
        s->qy[0] = s->qy[1];
        s->qn--;
    }

    int hx = s->x[s->head] + s->dx, hy = s->y[s->head] + s->dy;
    if (hx < 0 || hy < 0 || hx >= s->cols || hy >= s->rows) {
        s->dead = true;
        return GAME_RUNNING;
    }

    bool eat = hx == s->fx && hy == s->fy;
    if (!eat) {
        int t = (s->head - s->len + 1 + MAX_CELLS) % MAX_CELLS;
        s->occ[s->y[t] * s->cols + s->x[t]] = 0;
    }
    if (s->occ[hy * s->cols + hx]) {
        s->dead = true;
        return GAME_RUNNING;
    }

    s->head = (s->head + 1) % MAX_CELLS;
    s->x[s->head] = hx;
    s->y[s->head] = hy;
    s->occ[hy * s->cols + hx] = 1;

    if (eat) {
        s->len++;
        s->score++;
        s->interval = 10 - s->score / 8;
        if (s->interval < 3)
            s->interval = 3;
        if (!place_food(s))
            return GAME_OVER; // board full
    }
    return GAME_RUNNING;
}

static void draw(const void *p)
{
    const snake_t *s = p;
    const view_t *v = &s->v;
    uint16_t c = player_color[s->player];

    char buf[12];
    snprintf(buf, sizeof(buf), "%d", s->score);
    gfx_text(v->x + 1, v->y, 1, C_WHITE, buf);
    if (s->versus)
        gfx_text(v->x + v->w - 8, v->y, 1, c, s->player ? "P2" : "P1");
    gfx_rect(v->x, v->y + HUD - 1, v->w, 1, C_DARK);

    int ox = v->x, oy = v->y + HUD;
    gfx_rect(ox + s->fx * CELL, oy + s->fy * CELL, CELL, CELL, C_RED);

    if (s->dead && (s->dead_t / 8) % 2)
        return; // blink when dead
    for (int i = 0; i < s->len; i++) {
        int idx = (s->head - i + MAX_CELLS) % MAX_CELLS;
        gfx_rect(ox + s->x[idx] * CELL, oy + s->y[idx] * CELL, CELL, CELL, i ? c : C_WHITE);
    }
}

static int score(const void *p)
{
    return ((const snake_t *)p)->score;
}

const game_t game_snake = {
    .vs_rule = VS_FIRST_DEATH,
    .init = init,
    .update = update,
    .draw = draw,
    .score = score,
};
