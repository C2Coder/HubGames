#include <assert.h>
#include <stdio.h>
#include "../main/games/tanks.c"
const uint16_t player_color[MAX_PLAYERS] = {1,2,3,4,5,6,7,8,9,10,11,12};
int main(void)
{
    tanks_t s; input_t in[MAX_PLAYERS] = {0};
    player_mask_t mask = (1u << 9) | (1u << 11);
    init(&s, (view_t){0,0,128,64}, mask, true);
    assert(s.t[9].alive && s.t[11].alive && !s.t[0].alive);
    assert(s.t[9].x != s.t[11].x || s.t[9].y != s.t[11].y);
    assert(tank_color(&s, 11) == player_color[11]);
    s.t[9].alive = false; s.wins[11] = WIN_ROUNDS - 1;
    check_versus(&s); assert(winners(&s) == (1u << 11));
    init(&s, (view_t){0,0,128,64}, 1u << 11, false);
    assert(s.human == 11 && tank_color(&s, 0) == player_color[11]);
    in[11].held = BTN_A; update(&s, in);
    bool fired = false;
    for (int j = 0; j < NB; j++) if (s.b[j].alive && s.b[j].owner == 0) fired = true;
    assert(fired);
    puts("Tank high slots, colors, single controls and winner masks passed");
}
