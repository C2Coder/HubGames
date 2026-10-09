// Interface every game implements. In single mode one instance gets the whole
// 128x64 screen, in 1v1 each player gets an instance on their own 64x64 panel.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "gfx.h"
#include "input.h"

#define GAME_STATE_MAX 8192

typedef enum { GAME_RUNNING, GAME_OVER } game_status_t;

typedef enum {
    VS_FIRST_DEATH, // first player out loses
    VS_SCORE,       // both play until over, higher score wins
} vs_rule_t;

typedef struct {
    vs_rule_t vs_rule;
    void (*init)(void *s, view_t v, int player, bool versus);
    game_status_t (*update)(void *s, const input_t *in); // every 20 ms
    void (*draw)(const void *s);
    int (*score)(const void *s);
} game_t;

extern const uint16_t player_color[2];

extern const game_t game_snake;
