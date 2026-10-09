#include <assert.h>
#include <stdio.h>
#include "../main/games/arena.c"
const uint16_t player_color[MAX_PLAYERS] = {C_GREEN, C_BLUE, C_YELLOW, C_PINK};
static void check_occupancy(const arena_t *s)
{
    uint8_t occupied[MAX_CELLS] = {0};
    for (int p = 0; p < MAX_PLAYERS; p++) if (!s->w[p].dead)
        for (int i = 0; i < s->w[p].len; i++) occupied[cell_at(&s->w[p], i)] = 1;
    assert(memcmp(occupied, s->occ, s->rows * COLS) == 0);
}
int main(void)
{
    arena_t s; input_t input[MAX_PLAYERS] = {0};
    for (int height = 64; height <= 128; height += 64) {
        screen_h = height;
        init(&s, (view_t){0,0,128,height}, ALL_PLAYERS, true);
        check_occupancy(&s);
        for (int i = 0; i < 200 && !s.over; i++) {
            update(&s, input);
            if (!s.over) check_occupancy(&s); // the final bodies stay to blink
        }
        // A wall death with two other survivors must free head, body and tail.
        init(&s, (view_t){0,0,128,height}, 7, true);
        worm_t *w = &s.w[0];
        memset(s.occ, 0, sizeof(s.occ));
        for (int p = 1; p < 3; p++) for (int i = 0; i < s.w[p].len; i++) s.occ[cell_at(&s.w[p],i)] = 1;
        w->head = 3; w->dx = -1;
        for (int i = 0; i < 4; i++) { w->body[i] = 4 * COLS + 3 - i; s.occ[w->body[i]] = 1; }
        int old_head = cell_at(w, 0);
        s.tick = s.interval - 1;
        update(&s, input);
        assert(w->dead && !s.over && !s.occ[old_head]);
        for (int i = 0; i < 4; i++) assert(!s.occ[w->body[i]]);
        check_occupancy(&s);
        // A surviving snake can cross the former head cell on the next move.
        worm_t *f = &s.w[1];
        for (int i = 0; i < f->len; i++) s.occ[cell_at(f, i)] = 0;
        f->head = 3; f->dx = -1; f->dy = 0;
        for (int i = 0; i < 4; i++) { f->body[i] = old_head + 4 - i; s.occ[f->body[i]] = 1; }
        s.tick = s.interval - 1; update(&s, input);
        assert(!f->dead && cell_at(f, 0) == old_head);
        check_occupancy(&s);
        // The final death keeps the bodies on screen, they blink during the result delay.
        init(&s, (view_t){0,0,128,height}, 3, true);
        w = &s.w[0]; memset(s.occ, 0, sizeof(s.occ));
        for (int i = 0; i < s.w[1].len; i++) s.occ[cell_at(&s.w[1],i)] = 1;
        w->head = 3; w->dx = -1;
        for (int i = 0; i < 4; i++) { w->body[i] = 4 * COLS + 3 - i; s.occ[w->body[i]] = 1; }
        s.tick = s.interval - 1; update(&s, input);
        assert(s.over && w->dead && w->len == 4);
    }
    puts("Snake corpse regressions passed");
}
