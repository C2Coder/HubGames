#include <assert.h>
#include <stdio.h>
#include "../main/games/fps.c"
const uint16_t player_color[MAX_PLAYERS] = {C_GREEN, C_BLUE, C_YELLOW, C_PINK};
int main(void)
{
    fps_t s; input_t input = {0};
    init(&s, (view_t){0,0,128,64}, 1, false);
    for (int i = 0; i < ENEMIES; i++) s.enemy[i].health = 0;
    s.enemy[0].health = 2; s.enemy[0].x = 10 * FP + FP / 2; s.enemy[0].y = s.y;
    input.held = BTN_A; update(&s, &input);
    assert(s.enemy[0].health == 2); // hitscan cannot shoot through the wall at x=7
    s.cooldown = 0; s.enemy[0].x = 5 * FP + FP / 2;
    update(&s, &input); assert(s.enemy[0].health == 1);
    s.cooldown = 0; update(&s, &input); assert(s.kills == 1);
    input = (input_t){.jx=100,.jy=-100,.held=BTN_B};
    int old_x = s.x, old_y = s.y, yaw = s.yaw;
    update(&s, &input); assert(s.yaw == yaw && s.x > old_x && s.y > old_y);
    init(&s, (view_t){0,0,128,64}, 1, false);
    for (int i = 0; i < ENEMIES; i++) s.enemy[i].health = 0;
    input = (input_t){.held=BTN_UP};
    for (int i = 0; i < 500; i++) update(&s, &input);
    assert(s.x < 7 * FP && free_at(s.x, s.y));
    gfx_clear(); draw(&s);
    puts("FPS occlusion, touch controls and wall collisions passed");
}
