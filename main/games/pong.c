// Placeholder, replaced by the real game.
#include "game.h"

static void init(void *s, view_t v, uint8_t players, bool versus) {}
static game_status_t update(void *s, const input_t *in) { return GAME_OVER; }
static void draw(const void *s) {}
static int score(const void *s) { return 0; }
static int score_of(const void *s, int player) { return 0; }
static uint8_t winners(const void *s) { return 0; }

const game_t game_pong = {
    .init = init,
    .update = update,
    .draw = draw,
    .score = score,
    .score_of = score_of,
    .winners = winners,
};
