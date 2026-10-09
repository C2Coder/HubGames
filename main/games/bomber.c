// Bomber arena: grid movement, chain reactions, upgrades, sudden-death walls.
#include "game.h"
#include <stdio.h>
#include <string.h>
#include "esp_random.h"

#define CELL 6
#define COLS 21
#define ROWS 19
#define HUD 7
#define BOMBS 24
#define FUSE 110
#define FIRE 22
// Tile 0 floor, 1 wall, 2 crate, 3 extra bomb, 4 longer blast.
typedef struct { uint8_t x, y, owner, range; uint16_t fuse; } bomb_t;
typedef struct {
    int rows, oy, tick, end_t, ring;
    player_mask_t players, alive;
    uint8_t tile[ROWS][COLS], flame[ROWS][COLS];
    bomb_t bombs[BOMBS];
    struct { uint8_t x, y, range, capacity, cooldown; int score; } man[MAX_PLAYERS];
} bomber_t;
_Static_assert(sizeof(bomber_t) <= GAME_STATE_MAX, "bomber state too big");
static const int8_t dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};

static int bomb_at(const bomber_t *s, int x, int y)
{
    for (int i = 0; i < BOMBS; i++)
        if (s->bombs[i].fuse && s->bombs[i].x == x && s->bombs[i].y == y) return i;
    return -1;
}
static void init(void *p, view_t v, player_mask_t players, bool versus)
{
    (void)versus;
    bomber_t *s = p;
    memset(s, 0, sizeof(*s));
    s->players = s->alive = players;
    s->rows = (v.h - HUD) / CELL;
    if (!(s->rows & 1)) s->rows--;
    s->oy = HUD + (v.h - HUD - s->rows * CELL) / 2;
    for (int y = 0; y < s->rows; y++)
        for (int x = 0; x < COLS; x++)
            s->tile[y][x] = (!x || !y || x == COLS - 1 || y == s->rows - 1 ||
                (!(x & 1) && !(y & 1))) ? 1 : (esp_random() % 100 < 65 ? 2 : 0);
    // Eight evenly spaced spawns; always reserve an escape cross.
    int rank = 0;
    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        if (!(players & (1u << pl))) continue;
        int x = 1 + (rank % 4) * 6;
        int y = rank < 4 ? 1 : s->rows - 2;
        s->man[pl].x = x; s->man[pl].y = y;
        s->man[pl].range = 2; s->man[pl].capacity = 1;
        s->tile[y][x] = 0;
        for (int d = 0; d < 4; d++)
            if (s->tile[y + dy[d]][x + dx[d]] != 1) s->tile[y + dy[d]][x + dx[d]] = 0;
        rank++;
    }
}
static void detonate(bomber_t *s, int i)
{
    bomb_t b = s->bombs[i];
    s->bombs[i].fuse = 0; // mark before propagation to stop recursive cycles
    s->flame[b.y][b.x] = FIRE;
    for (int d = 0; d < 4; d++)
        for (int k = 1; k <= b.range; k++) {
            int x = b.x + dx[d] * k, y = b.y + dy[d] * k;
            if (x < 0 || y < 0 || x >= COLS || y >= s->rows || s->tile[y][x] == 1) break;
            s->flame[y][x] = FIRE;
            int chain = bomb_at(s, x, y);
            if (chain >= 0) s->bombs[chain].fuse = 1; // process iteratively, no deep recursion
            if (s->tile[y][x] == 2) {
                uint32_t r = esp_random() % 8;
                s->tile[y][x] = r < 2 ? 3 + r : 0;
                s->man[b.owner].score++;
                break;
            }
            if (s->tile[y][x] >= 3) s->tile[y][x] = 0;
        }
    fx_burst(b.x * CELL + 3, s->oy + b.y * CELL + 3, C_YELLOW, 6);
}
static game_status_t update(void *p, const input_t *in)
{
    bomber_t *s = p;
    if (__builtin_popcount(s->alive) <= 1)
        return ++s->end_t >= 60 ? GAME_OVER : GAME_RUNNING;
    s->tick++;
    for (int y = 0; y < s->rows; y++)
        for (int x = 0; x < COLS; x++)
            if (s->flame[y][x]) s->flame[y][x]--;
    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        if (!(s->alive & (1u << pl))) continue;
        if (s->man[pl].cooldown) s->man[pl].cooldown--;
        if (!s->man[pl].cooldown) {
            int x = s->man[pl].x, y = s->man[pl].y;
            if (in[pl].held & BTN_LEFT) x--;
            else if (in[pl].held & BTN_RIGHT) x++;
            else if (in[pl].held & BTN_UP) y--;
            else if (in[pl].held & BTN_DOWN) y++;
            if (x >= 0 && y >= 0 && x < COLS && y < s->rows &&
                s->tile[y][x] != 1 && s->tile[y][x] != 2 && bomb_at(s, x, y) < 0) {
                s->man[pl].x = x; s->man[pl].y = y;
                s->man[pl].cooldown = 7;
                if (s->tile[y][x] == 3 && s->man[pl].capacity < 3) s->man[pl].capacity++;
                if (s->tile[y][x] == 4 && s->man[pl].range < 6) s->man[pl].range++;
                if (s->tile[y][x] >= 3) s->tile[y][x] = 0;
            }
        }
        if (in[pl].pressed & BTN_A) {
            int count = 0, free = -1;
            for (int i = 0; i < BOMBS; i++) {
                if (s->bombs[i].fuse && s->bombs[i].owner == pl) count++;
                if (!s->bombs[i].fuse) free = i;
            }
            int x = s->man[pl].x, y = s->man[pl].y;
            if (free >= 0 && count < s->man[pl].capacity && bomb_at(s, x, y) < 0)
                s->bombs[free] = (bomb_t){x, y, pl, s->man[pl].range, FUSE};
        }
    }
    for (int i = 0; i < BOMBS; i++)
        if (s->bombs[i].fuse > 1) s->bombs[i].fuse--;
    // A chain can target an earlier index; bounded passes resolve the whole chain this frame.
    for (int pass = 0; pass < BOMBS; pass++) {
        bool found = false;
        for (int i = 0; i < BOMBS; i++)
            if (s->bombs[i].fuse == 1) { detonate(s, i); found = true; }
        if (!found) break;
    }
    if (s->tick >= 1500 && s->tick % 250 == 0) {
        int r = ++s->ring;
        for (int y = r; y < s->rows - r; y++)
            for (int x = r; x < COLS - r; x++)
                if (x == r || x == COLS - r - 1 || y == r || y == s->rows - r - 1)
                    s->tile[y][x] = 1;
    }
    for (int pl = 0; pl < MAX_PLAYERS; pl++) {
        if (!(s->alive & (1u << pl))) continue;
        int x = s->man[pl].x, y = s->man[pl].y;
        if (s->flame[y][x] || s->tile[y][x] == 1) {
            s->alive &= ~(1u << pl);
            fx_burst(x * CELL + 3, s->oy + y * CELL + 3, player_color[pl], 18);
        } else if (s->tick % 50 == 0) s->man[pl].score++;
    }
    return GAME_RUNNING;
}
static void draw(const void *p)
{
    const bomber_t *s = p;
    char buf[32];
    snprintf(buf, sizeof(buf), "A BOMB   ALIVE %d", __builtin_popcount(s->alive));
    gfx_text(1, 0, 1, C_WHITE, buf);
    for (int y = 0; y < s->rows; y++)
        for (int x = 0; x < COLS; x++) {
            int px = x * CELL + 1, py = s->oy + y * CELL + 1;
            int tile = s->tile[y][x];
            if (tile == 1) gfx_rect(px, py, 5, 5, RGB(64, 80, 112));
            else if (tile == 2) {
                gfx_rect(px, py, 5, 5, RGB(128, 64, 24));
                gfx_pixel(px + 1, py + 1, C_YELLOW);
            } else if (tile >= 3) gfx_rect(px + 1, py + 1, 3, 3, tile == 3 ? C_BLUE : C_GREEN);
            if (s->flame[y][x]) gfx_rect(px, py, 5, 5, s->flame[y][x] & 2 ? C_YELLOW : RGB(255, 96, 0));
        }
    for (int i = 0; i < BOMBS; i++)
        if (s->bombs[i].fuse) {
            int x = s->bombs[i].x * CELL + 2, y = s->oy + s->bombs[i].y * CELL + 2;
            gfx_rect(x, y, 3, 3, (s->bombs[i].fuse / 8) & 1 ? C_WHITE : player_color[s->bombs[i].owner]);
            gfx_pixel(x + 1, y - 1, C_RED);
        }
    for (int pl = 0; pl < MAX_PLAYERS; pl++)
        if (s->alive & (1u << pl)) {
            int x = s->man[pl].x * CELL + 2, y = s->oy + s->man[pl].y * CELL + 2;
            gfx_rect(x, y, 3, 4, player_color[pl]);
            gfx_pixel(x + 1, y, C_WHITE);
        }
}
static int score_of(const void *p, int pl) { return ((const bomber_t *)p)->man[pl].score; }
static player_mask_t winners(const void *p) { return ((const bomber_t *)p)->alive; }
const game_t game_bomber = {.shared = true, .opaque = true, .big = BIG_NATIVE, .max_players = 8,
    .init = init, .update = update, .draw = draw, .score_of = score_of, .winners = winners};
