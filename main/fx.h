// Particle effects, drawn on top of everything by main. Games call fx_burst
// in their own coordinates, main sets fx_scale when it upscales a game.
#pragma once

#include <stdint.h>

extern int fx_scale;

void fx_burst(int x, int y, uint16_t color, int n); // sparks flying out
void fx_confetti(int n);                            // falling from the top, whole screen
void fx_clear(void);
void fx_update_draw(void);
