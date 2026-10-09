// Controller input of one player, filled from the phone page.
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    BTN_UP = 1,
    BTN_DOWN = 2,
    BTN_LEFT = 4,
    BTN_RIGHT = 8,
    BTN_A = 16,
    BTN_B = 32,
    BTN_START = 64,
};

typedef struct {
    uint8_t held;    // BTN_* bits, joystick is merged into the directions
    uint8_t pressed; // bits that went down since the last input_get()
    int8_t jx, jy;   // raw joystick, -100..100, y down is positive
    bool connected;
} input_t;

// Call once per frame for each player (0 = P1, 1 = P2).
void input_get(int player, input_t *in);
