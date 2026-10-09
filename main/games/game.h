// Interface every game implements.
//
// Single: one instance on the whole 128x64 screen, players = 0x1.
// 1v1 (not shared): one instance per player on their own 64x64 panel, players = 1 << p.
// Shared (1v1 and party): one instance on 128x64, players = mask of who plays,
// update gets the whole input array indexed by player number.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "gfx.h"
#include "input.h"

#define GAME_STATE_MAX 12288

typedef enum { GAME_RUNNING, GAME_OVER, GAME_WON } game_status_t;

typedef enum {
    VS_FIRST_DEATH, // 1v1 per panel: first player out loses
    VS_SCORE,       // 1v1 per panel: both play until over, higher score wins
} vs_rule_t;
// In 1v1 per panel, GAME_WON (flag, top reached) ends the match, that player wins.

typedef struct {
    vs_rule_t vs_rule;
    bool shared;
    void (*init)(void *s, view_t v, uint8_t players, bool versus);
    game_status_t (*update)(void *s, const input_t *in); // every 20 ms
    void (*draw)(const void *s);
    int (*score)(const void *s);                // not shared
    int (*score_of)(const void *s, int player); // shared
    uint8_t (*winners)(const void *s);          // shared, mask of winning players, 0 = draw
} game_t;

extern const uint16_t player_color[MAX_PLAYERS];

extern const game_t game_snake;
extern const game_t game_snake_arena;
extern const game_t game_tetris;
extern const game_t game_pong;
extern const game_t game_dk;
extern const game_t game_mario;
