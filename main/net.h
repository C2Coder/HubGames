#pragma once

#include <stdint.h>

// what is on the panels, so the phones can show matching help
enum {
    CTX_MENU = 1,
    CTX_PAUSE,
    CTX_RESULT,
    CTX_SHOW,
    CTX_TETRIS = 10,
    CTX_SNAKE,
    CTX_ARENA,
    CTX_PONG,
    CTX_DK,
    CTX_MARIO,
    CTX_TANKS,
    CTX_DOOM,
    CTX_PACMAN,
};

// Open WiFi AP "Mecha Arcade", controller page at http://192.168.4.1
void net_init(void);

// current screen and the mask of players in the running game
void net_set_context(uint8_t ctx, uint8_t playing);

// drop every phone from the WiFi and free all player slots
void net_kick_all(void);
