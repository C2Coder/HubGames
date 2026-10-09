// Interface every game implements. In single mode one instance gets the whole
// 128x64 screen, in 1v1 each player gets an instance on their own 64x64 panel.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "gfx.h"
#include "input.h"

#define GAME_STATE_MAX 12288

typedef enum { GAME_RUNNING, GAME_OVER } game_status_t;

typedef enum {
    VS_FIRST_DEATH, // first player out loses
    VS_SCORE,       // both play until over, higher score wins
} vs_rule_t;

// Shared games run (in 1v1) one instance on the whole 128x64 screen, update
// gets the input array of both players and the game decides the winner.
typedef struct {
    vs_rule_t vs_rule;
    bool shared;
    void (*init)(void *s, view_t v, int player, bool versus);
    game_status_t (*update)(void *s, const input_t *in); // every 20 ms
    void (*draw)(const void *s);
    int (*score)(const void *s);
    int (*score_of)(const void *s, int player); // shared only
    int (*winner)(const void *s);               // shared only, 0 draw, 1 P1, 2 P2
} game_t;

extern const uint16_t player_color[2];

extern const game_t game_snake;
extern const game_t game_snake_arena;
