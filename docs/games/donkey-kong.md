# Donkey Kong

## Overview

One 64x64 level with 5 slanted girders and 6 ladders. An ape on the top girder (right side) throws barrels that roll down. Jump with A, climb with UP/DOWN. Goal is the pink figure on the top girder at x 40. File: `main/games/dk.c`, own pixel art.

## Layout

- Level is 64 px wide. 64x64 view: `ox = v.x`, top 6 px are the HUD. 128x64 view: level centred (`ox = v.x + 32`), HUD in the 32 px side strips (left SCORE/BONUS, right LVL and lives as green blocks).
- Girder surfaces: `base[] = {59, 48, 37, 26, 15}` plus 0..3 px slope. Even floors (0, 2, 4) slope down to the left, odd floors down to the right. Spans: even floors x 3..63 (floor 0 x 0..63), odd floors x 0..60. The 3 px gap at the downhill end is where barrels fall through.
- Ladders `{gap, x}`: `{0,44} {1,14} {1,50} {2,44} {2,20} {3,16}`.

## Controls

- LEFT / RIGHT: walk (0.6 px/tick), follows the girder slope.
- UP / DOWN: climb when within 2 px of a ladder (snaps to its x, 0.5 px/tick).
- A: jump (apex about 5 px, about 21 ticks). Horizontal direction fixed at takeoff. Girders are solid from above only.

## Rules

- Barrels spawn at the ape every `max(50, 120 - 12*level)` ticks, roll downhill at `0.8 + 0.1*level` px/tick (cap 1.6), fall through the gap to the floor below and reverse. At a ladder top a barrel takes the ladder down with 25% chance (+10% per level, cap 50%). Max 8 barrels. They leave the level at the left of floor 0.
- Touching a barrel kills. Jumping over a rolling barrel on the same floor: +100.
- Single: 3 lives, 3 levels. Reaching the goal gives +500 + BONUS (starts 1000, -10 every 20 ticks). Level 3 cleared returns `GAME_WON`, no lives returns `GAME_OVER`. `score()` = points. On death barrels are cleared and the player restarts at the bottom, bonus keeps running.
- 1v1 (per panel, `VS_SCORE`, level 2 speed): reaching the goal returns `GAME_WON` at once. Dying respawns at the bottom after 50 ticks, no lives. 90 s limit shown on the HUD, then `GAME_OVER`. `score()` = `best_height * 10 + points` (best height = highest girder point reached, never lowered by dying), so VS_SCORE picks who got higher.
- Both panels get the same barrels: `player 0` draws a file-static seed in `init`, player 1 reuses it. Barrel spawns are tick-timed and the ladder choice is `hash(seed, barrel serial, ladder)`, no RNG state, so sequences stay identical.

## Verification

Host harness with a bot (walks to ladders, jumps barrels, waits at ladder ends) reaches the top in all 1v1 runs (about 6 s, 0.9 deaths on average) and clears all 3 single levels in about 40% of runs with 3 lives.
