#include <assert.h>
#include <stdio.h>
#include "../main/games/racers.c"
const uint16_t player_color[MAX_PLAYERS] = {C_GREEN, C_BLUE};

// A car center is legal where update() accepts it; -1 = corner box, else straight 0..3.
static bool legal(const racers_t *s, int x, int y)
{
    return road(s, x - 2, y) && road(s, x + 2, y) && road(s, x, y - 2) && road(s, x, y + 2);
}
static int straight(const racers_t *s, int x, int y)
{
    for (int k = 0; k < 4; k++) if (at_checkpoint(s, k, x, y)) return -1;
    return y < HUD + 17 ? 0 : x >= 106 ? 1 : y >= s->h - 17 ? 2 : 3;
}
int main(void)
{
    for (int h = 64; h <= 128; h += 64) {
        racers_t s; memset(&s, 0, sizeof(s)); s.h = h;
        // Cars move at most 1.25 px per frame (2 with a collision push), so a
        // car can never step from one straight to the next around a corner box.
        for (int y = 0; y < h; y++) for (int x = 0; x < 128; x++) {
            if (!legal(&s, x, y) || straight(&s, x, y) < 0) continue;
            for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) {
                if (!legal(&s, x + dx, y + dy)) continue;
                int a = straight(&s, x, y), b = straight(&s, x + dx, y + dy);
                assert(b < 0 || b == a);
            }
        }
    }
    puts("Racer checkpoints passed");
}
