# Tetris

## Overview

Classic falling-block game. 10x20 board, 3 px cells (30x60 px). Single: points + lines, levels speed up. 1v1: `VS_FIRST_DEATH`, whoever tops out first loses (score only shown). No garbage lines between players (keeps it simple, can be added later).

## Layout

Cell = 3 px, board = 30x60 px, 1 px dark border around it. Locked cells are drawn in the piece colour, the active piece brighter. Text uses the 3x5 font at scale 1 (4 px per char).

64x64 (1v1 and nothing else uses it):
- Board at x+1, y+2 (border at x, y+1; 62 px of 64 used).
- Right column x+34..x+63 (29 px): `NEXT` preview at y+2 (4x2 cells of 3 px = 12x6 px), then score (up to 6 digits = 24 px) at y+16, `LV n` at y+28, lines at y+36. Player tag `P1`/`P2` at the bottom in `player_color`.

128x64 (single):
- Board centered: x+49, y+2.
- Left (x+2..x+44): `SCORE`, value at scale 2 (5 digits = 40 px), `LINES`, `LEVEL`.
- Right (x+84..x+126): `NEXT` with preview in 3 px cells.

Built as described, with these details: rotation uses a runtime 4x4 box rotation (O does not rotate, I uses a 4 box, others 3) instead of a table; kicks tried are 0, -1, +1, -2, +2. The 64x64 view shows NEXT preview, score, `LVn`, `Ln` and the P1/P2 tag; `score_of`/`winners` are not set (not shared). Result is `GAME_OVER` only (no `GAME_WON`). Active piece uses a brighter colour than locked cells.

Piece colours: I cyan, O yellow, T purple, S green, Z red, J blue, L orange (all RGB565 constants in the file).

## Controls

| Input | Action |
|---|---|
| LEFT / RIGHT | move, first step on press, then auto-repeat (DAS 9 ticks, then every 3 ticks) |
| DOWN | soft drop, one row per 2 ticks while held, +1 point per row |
| A | rotate clockwise |
| B | hard drop, +2 points per row, locks immediately |
| UP | unused (the joystick hits it by accident too often) |
| START | pause (handled by main) |

## Rules / Scoring

- Rotation: clockwise only, 4 precomputed rotation masks per piece (4x4 bitmask, `uint16_t rot[7][4]` in flash). Simple wall kick: if the rotated piece collides try dx = -1, +1, then -2, +2 (the I piece only needs the 2-cell kicks). No floor kicks, no SRS tables. If nothing fits the rotation is ignored.
- Randomizer: 7-bag. Shuffle the 7 piece ids (Fisher-Yates) and deal them in order, refill when empty. One `next` piece is shown.
- Lock: when the gravity step fails the piece gets a 10 tick lock delay, reset by any successful move or rotate (max 8 resets), hard drop skips it.
- Line clear: full rows flash for 15 ticks (input ignored), then the rows are removed.
- Points per clear, times (level + 1): 1 line 40, 2 lines 100, 3 lines 300, 4 lines 1200.
- Level = lines / 10, capped at 14.
- Game over: a new piece cannot spawn (overlaps the stack). Locked cells blink for 60 ticks, then `GAME_OVER`.

Speed curve (ticks per row at gravity, 20 ms per tick):

| Level | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ticks/row | 40 | 34 | 28 | 23 | 19 | 15 | 12 | 9 | 7 | 5 | 4 | 3 | 2 | 2 | 1 |

(0.8 s per row at the start, 20 rows/s at level 14.) Const table `gravity[15]`.

## 1v1

- `vs_rule = VS_FIRST_DEATH`. Main sets `over[p]` when `update` returns `GAME_OVER` and the other player loses at once. Score is only shown on the result screen.
- Both players must get the same piece sequence: a file-static `seed` is drawn from `esp_random()` in `init` when `player == 0` and reused by `player == 1`, each instance runs its own bag RNG (small xorshift in the state) from that seed.
- Right column of the 64x64 view carries NEXT + score so the opponent's progress is visible on their panel.

## Implementation notes

State struct (`tetris_t`), all in the 8192 byte `GAME_STATE_MAX`:

| Field | Size |
|---|---|
| `view_t v`, player, versus, flags | ~28 B |
| `uint8_t board[20][10]` (0 = empty, else colour index 1..7) | 200 B |
| bag[7], bag_i, next, cur piece (id, rot, x, y) | ~16 B |
| timers: gravity, lock, DAS, clear flash, death, soft drop | ~16 B |
| score, lines, level, rng | ~16 B |
| clear-row list | 4 B |

Total about 300 B, a few percent of the limit; `_Static_assert(sizeof(tetris_t) <= GAME_STATE_MAX)` like snake.

Tick timing: `update` runs every 20 ms. Order per tick: input (rotate/hard drop on `pressed`, move on `held` with DAS counter) -> gravity counter -> lock delay -> clear/spawn. `draw` is read-only, uses `gfx_rect` per locked cell (up to 200 rects, fine). Preview and HUD positions are computed once in `init` from `v.w` (`v.w == 128` selects the wide layout).

## v2

- `.big = BIG_SCALE2`: 1 PLAYER on 4 panels gets a 64x64 view (versus = false, no P tag), upscaled 2x by main.
- VERSUS 2-4 players: all drawing uses `v.x`/`v.y`, tag is `P<player+1>` in `player_color[player]`. Seed is redrawn when an instance is inited with a player number <= the previous one (start of a new game), so it works for any player set.
- Text columns sit on black panels, the well is black, so the ambient background shows only in the gaps.
- Effects: `fx_burst` sparkles along each cleared row (row colours), small burst under the piece on hard drop, explosion over the board on game over.

## Ghost and demo

- Ghost: the hard drop position of the active piece is drawn under it in the piece colour at 25% brightness (`dim()`). Not shown during line-clear flash or death.
- Demo (attract show): `init` with `players == 0` sets `demo`, player is treated as 0, the input pointer is never read. On every spawn `ai_plan` tries every rotation x column (straight drop) and scores the board with aggregate height -0.51, lines +0.76, holes -0.36, bumpiness -0.18. `ai_input` then builds a fake `input_t` for the normal update path: one action per 4 ticks (rotate, then one step sideways, then hard drop), so rules, levels and scoring are unchanged. A blocked rotation or move drops the plan to the current state. Ends with `GAME_OVER` when it tops out.
