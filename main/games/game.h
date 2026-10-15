// Interface every game implements. The screen is 128x64 (2 panels) or
// 128x128 (4 panels, 2x2), see screen_h.
//
// 1 PLAYER: players = 0x1, versus = false.
//   2 panels: view 128x64.
//   4 panels: big == BIG_NATIVE -> view 128x128, BIG_SCALE2 -> view 64x64 that
//   main upscales 2x (fx_scale is 2 then, fx coordinates stay in game space).
// VERSUS (not shared): one instance per player, each on its own 64x64 panel,
//   players = 1 << p, versus = true. Panels are filled in player order, so
//   the instance of the lowest player is inited first. 2-4 players.
// Shared (VERSUS and PARTY): one instance on the whole screen (128 x screen_h),
//   players = mask of who plays, update gets the whole input array.
//
// DEMO (attract show): players = 0, versus = false, view as in 1 PLAYER. The
//   game plays itself (AI), input is ignored. Only games in the show need it
//   (pong, tetris, pacman).
//
// Games call fx_burst() for effects. The background is black, don't clear it.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "gfx.h"
#include "fx.h"
#include "input.h"

#define GAME_STATE_MAX 16384

typedef enum { GAME_RUNNING, GAME_OVER, GAME_WON } game_status_t;

typedef enum {
    VS_FIRST_DEATH, // per panel: last player still in wins
    VS_SCORE,       // per panel: all play until over, highest score wins
} vs_rule_t;
// In VERSUS per panel, GAME_WON (flag, top reached) ends the match, that player wins.

typedef enum { BIG_NATIVE, BIG_SCALE2 } big_t;

typedef struct {
    vs_rule_t vs_rule;
    bool shared;
    big_t big;
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
extern const game_t game_tanks;
extern const game_t game_pacman;
