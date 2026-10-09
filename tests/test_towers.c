#include <assert.h>
#include <stdio.h>
#include "../main/games/towers.c"
const uint16_t player_color[MAX_PLAYERS] = {C_GREEN, C_BLUE};

int main(void)
{
    // An overhanging piece blocked by the neighbour's blocks locks in place; it
    // only falls off (costing a life) when it would really leave the well.
    towers_t t; init(&t, (view_t){0, 0, 128, 64}, 3, true);
    int w = t.width, r = t.rows;
    t.tower[0].kind = 3; t.tower[0].rot = 0; t.tower[0].x = w - 5; t.tower[0].y = r - 4;
    t.board[r - 1][w - 5] = 1; t.board[r - 2][w - 2] = 1;
    land(&t, 0);
    assert(t.tower[0].lives == 3);
    assert(t.board[r - 2][w - 5] && t.board[r - 2][w - 4] && t.board[r - 2][w - 3]);
    init(&t, (view_t){0, 0, 128, 64}, 3, true);
    t.tower[0].kind = 3; t.tower[0].rot = 0; t.tower[0].x = w - 4; t.tower[0].y = r - 4;
    t.board[r - 1][w - 4] = 1;
    land(&t, 0); // slides right to cols w-3..w-1, the last is outside the well
    land(&t, 0);
    assert(t.tower[0].lives == 2);
    puts("Tower edge falls passed");
}
