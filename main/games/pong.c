// Pong: two teams, left = P1+P3, right = P2+P4, an empty team is played by the AI.
#include "game.h"

#include <stdio.h>
#include <string.h>
#include "esp_random.h"

#define PAD_W     2
#define PAD_H     12
#define WIN_PTS   5
#define SERVE_T   50
#define END_T     60
#define PAD_SPEED 461 // 1.8 px/tick in 8.8
#define AI_SPEED  282 // 1.1 px/tick
#define SPEED0    256 // 1.0 px/tick
#define SPEED_HIT 15
#define SPEED_MAX 614
#define FX(n)     ((n) * 256)

typedef struct {
    int x;       // px
    int32_t y;   // top, 8.8
    int8_t player; // -1 = AI
    uint8_t side;  // 0 left, 1 right
    uint16_t color;
    int ai_t;
    int32_t ai_target;
    int ai_err;
} pad_t;

typedef struct {
    view_t v;
    bool versus;
    uint8_t players;
    pad_t pad[4];
    int npad;
    int pts[2];
    int32_t bx, by, vx, vy; // ball top-left, 8.8
    int speed;
    int serve_t, serve_dir;
    int end_t;
    int winner; // -1 none
} pong_t;

_Static_assert(sizeof(pong_t) <= GAME_STATE_MAX, "pong state too big");

static int32_t clampi(int32_t a, int32_t lo, int32_t hi)
{
    return a < lo ? lo : a > hi ? hi : a;
}

static void serve(pong_t *s, int dir)
{
    s->serve_t = SERVE_T;
    s->serve_dir = dir;
    s->speed = SPEED0;
    s->bx = FX(s->v.w / 2 - 1);
    s->by = FX(s->v.h / 2 - 1);
    s->vx = dir * (s->speed * 240 >> 8);
    s->vy = ((int)(esp_random() % 186) - 93) * s->speed >> 8; // about +-20 degrees
    for (int i = 0; i < s->npad; i++)
        s->pad[i].ai_err = (int)(esp_random() % 13) - 6;
}

static void add_pad(pong_t *s, int side, int player, int slot)
{
    pad_t *p = &s->pad[s->npad++];
    p->side = side;
    p->player = player;
    p->x = side ? s->v.w - 4 - slot * 12 : 2 + slot * 12;
    p->y = FX((s->v.h - PAD_H) / 2);
    p->color = player_color[player >= 0 ? player : side];
}

static void init(void *st, view_t v, uint8_t players, bool versus)
{
    pong_t *s = st;
    memset(s, 0, sizeof(*s));
    s->v = v;
    s->versus = versus;
    s->players = players;
    s->winner = -1;
    for (int side = 0; side < 2; side++) {
        int slot = 0;
        for (int p = side; p < MAX_PLAYERS; p += 2)
            if (players & (1 << p))
                add_pad(s, side, p, slot++);
        if (!slot)
            add_pad(s, side, -1, 0);
    }
    serve(s, esp_random() & 1 ? 1 : -1);
}

// Ball hits paddle p moving in x, returns true on a bounce. ox = old ball x.
static bool paddle_hit(pong_t *s, pad_t *p, int32_t ox)
{
    int32_t ycross, nbx;
    if (p->side == 0 && s->vx < 0) {
        int32_t edge = FX(p->x + PAD_W);
        if (!(ox >= edge && s->bx < edge))
            return false;
        ycross = s->by - s->vy * ((ox - edge) * 256 / -s->vx) / 256;
        nbx = edge;
    } else if (p->side == 1 && s->vx > 0) {
        int32_t face = FX(p->x);
        if (!(ox + FX(2) <= face && s->bx + FX(2) > face))
            return false;
        ycross = s->by - s->vy * ((s->bx + FX(2) - face) * 256 / s->vx) / 256;
        nbx = face - FX(2);
    } else {
        return false;
    }
    if (!(ycross + FX(2) > p->y - FX(1) && ycross < p->y + FX(PAD_H + 1)))
        return false;
    s->bx = nbx;
    s->by = ycross;
    int off = (ycross + FX(1) - (p->y + FX(PAD_H / 2))) * 256 / FX(7);
    off = clampi(off, -256, 256);
    int aoff = off < 0 ? -off : off;
    s->speed = clampi(s->speed + SPEED_HIT, 0, SPEED_MAX);
    s->vy = off * s->speed * 3 / 4 >> 8;
    s->vx = (p->side ? -1 : 1) * (s->speed * (256 - 90 * aoff / 256) >> 8);
    return true;
}

static void ai_move(pong_t *s, pad_t *p)
{
    bool toward = p->side ? s->vx > 0 : s->vx < 0;
    int bxp = s->bx >> 8;
    bool near = p->side ? bxp > s->v.w / 4 : bxp < s->v.w * 3 / 4;
    if (--p->ai_t <= 0) {
        p->ai_t = 6;
        if (toward && near)
            p->ai_target = s->by + FX(1) + FX(p->ai_err) - FX(PAD_H / 2);
        else
            p->ai_target = FX((s->v.h - PAD_H) / 2);
    }
    int32_t d = p->ai_target - p->y;
    if (d > FX(2))
        p->y += d > AI_SPEED ? AI_SPEED : d;
    else if (d < -FX(2))
        p->y += d < -AI_SPEED ? -AI_SPEED : d;
}

static game_status_t update(void *st, const input_t *in)
{
    pong_t *s = st;
    int np = s->versus ? MAX_PLAYERS : 1; // single mode only has in[0]

    if (s->winner >= 0) {
        if (++s->end_t < END_T)
            return GAME_RUNNING;
        return !s->versus && s->winner == 0 ? GAME_WON : GAME_OVER;
    }

    // paddles
    for (int i = 0; i < s->npad; i++) {
        pad_t *p = &s->pad[i];
        if (p->player < 0) {
            ai_move(s, p);
        } else if (p->player < np) {
            const input_t *k = &in[p->player];
            int32_t sp = 0;
            if (k->jy > 15 || k->jy < -15)
                sp = k->jy * PAD_SPEED / 100;
            else if (k->held & BTN_UP)
                sp = -PAD_SPEED;
            else if (k->held & BTN_DOWN)
                sp = PAD_SPEED;
            p->y += sp;
        }
        p->y = clampi(p->y, 0, FX(s->v.h - PAD_H));
    }

    // serve pause
    if (s->serve_t > 0) {
        for (int p = 0; p < np; p++)
            if ((s->players & (1 << p)) && (in[p].pressed & BTN_A))
                s->serve_t = 1;
        s->serve_t--;
        return GAME_RUNNING;
    }

    // ball
    int32_t ox = s->bx;
    s->bx += s->vx;
    s->by += s->vy;
    if (s->by < 0) {
        s->by = -s->by;
        s->vy = -s->vy;
    } else if (s->by > FX(s->v.h - 2)) {
        s->by = FX(2 * (s->v.h - 2)) - s->by;
        s->vy = -s->vy;
    }
    for (int i = 0; i < s->npad; i++)
        if (paddle_hit(s, &s->pad[i], ox))
            break;

    // points
    int scorer = -1;
    if (s->bx < -FX(2))
        scorer = 1;
    else if (s->bx > FX(s->v.w))
        scorer = 0;
    if (scorer >= 0) {
        if (++s->pts[scorer] >= WIN_PTS) {
            s->winner = scorer;
            s->end_t = 0;
            s->vx = s->vy = 0;
        } else {
            serve(s, scorer ? -1 : 1);
        }
    }
    return GAME_RUNNING;
}

static void draw(const void *st)
{
    const pong_t *s = st;
    const view_t *v = &s->v;

    for (int y = 0; y < v->h; y += 2)
        gfx_rect(v->x + v->w / 2 - 1, v->y + y, 2, 1, C_DARK);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", s->pts[0]);
    gfx_text(v->x + v->w / 2 - 6 - gfx_text_width(1, buf) + 1, v->y + 1, 1, player_color[0], buf);
    snprintf(buf, sizeof(buf), "%d", s->pts[1]);
    gfx_text(v->x + v->w / 2 + 6, v->y + 1, 1, player_color[1], buf);

    bool blink = (s->winner >= 0 && (s->end_t / 8) % 2) || (s->serve_t > 0 && (s->serve_t / 8) % 2);
    for (int i = 0; i < s->npad; i++) {
        const pad_t *p = &s->pad[i];
        if (s->winner >= 0 && p->side != s->winner && blink)
            continue;
        gfx_rect(v->x + p->x, v->y + (p->y >> 8), PAD_W, PAD_H, p->color);
    }
    if (!blink || s->winner >= 0)
        gfx_rect(v->x + (s->bx >> 8), v->y + (s->by >> 8), 2, 2, C_WHITE);
}

static int score(const void *st)
{
    return ((const pong_t *)st)->pts[0];
}

static int score_of(const void *st, int player)
{
    return ((const pong_t *)st)->pts[player & 1];
}

static uint8_t winners(const void *st)
{
    const pong_t *s = st;
    if (s->winner < 0)
        return 0;
    return s->players & (s->winner ? 0xA : 0x5);
}

const game_t game_pong = {
    .vs_rule = VS_SCORE,
    .shared = true,
    .init = init,
    .update = update,
    .draw = draw,
    .score = score,
    .score_of = score_of,
    .winners = winners,
};
