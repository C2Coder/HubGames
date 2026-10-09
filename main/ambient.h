// Background animations: dim plasma behind menus/games and the attract show.
#pragma once

#include <stdint.h>

// full screen plasma, level = brightness 0..255, t = frame counter
void ambient_plasma(uint32_t t, uint8_t level);

// attract show: cycles through animations, full screen, t = frame counter
void ambient_show(uint32_t t);
