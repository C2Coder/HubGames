// Attract show animations.
#pragma once

#include <stdint.h>

#define AMBIENT_CYCLE (5 * 600) // frames until every animation was shown once

// attract show: cycles through animations, full screen, t = frame counter
void ambient_show(uint32_t t);

// start the next ambient_show from scratch (t may jump)
void ambient_reset(void);
