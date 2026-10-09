# Mario

## Overview

One hand-made side-scrolling level, 256 tiles long: ground with pits, brick platforms, walking enemies you can stomp, coins, a flag at the end. Single: coins + distance + time bonus. 1v1: `VS_SCORE`, 120 s limit, whoever gets further (or reaches the flag first) wins. Names and art are our own (a plain hero and a mushroom-like walker).

## Layout

Tile = 4x4 px. The level is 256 columns x 14 rows = 1024 x 56 px, plus an 8 px HUD strip on top (64 px total).

- 64x64 (1v1): view is 16 tiles wide (64 px). HUD: coins left, time left (seconds) middle, `P1`/`P2` right.
- 128x64 (single): view is 32 tiles wide (128 px), twice the look-ahead, so the same level is easier to read. HUD: `SCORE`, coins, `TIME`, lives.
- Same code for both: `view_w = v.w`, the camera and the draw loop use it, the level and physics do not change. (The wider view makes single slightly easier, intended.)

Sprites (own pixel art, `const char[]` strings with a small palette, `.` transparent):
- Hero 4x7 (hitbox 3x7, 2 tiles high fits a 2-tile gap): frames stand, run 1, run 2, jump, plus dead. Player colour from `player_color` for the shirt in 1v1.
- Walker 4x4 (two feet frames, squashed frame 4x2).
- Coin 2x4 on the 4x4 tile. Brick tile with mortar lines, ground tile with a lighter top row, flag pole 2x24 with a 6x4 triangle flag.

## Controls

- LEFT / RIGHT: accelerate. UP/DOWN unused.
- A: jump. Variable height: while rising and A is released, gravity is doubled, so a tap is a short hop.
- B held: run (max speed 0.9 instead of 0.5 px/tick, higher acceleration).
- START: pause (main).

## Rules / Scoring

Physics in 8.8 fixed point (256 = 1 px), per tick of 20 ms:
- Walk max 0.5 px/tick (6 tiles/s), run max 0.9 px/tick. Acceleration 0.04 px/tick^2, friction 0.06 when no direction is held.
- Jump `vy = -1.95 px/tick`, gravity 0.12 px/tick^2: about 4 tiles high, 33 ticks of air, covers 4 tiles at walk and 7 tiles at run. Max fall speed 2.5 px/tick.
- Collision per axis: move x, resolve against solid tiles (`#`, `B`), then move y and resolve. Tile lookup `level[row][col]` with out-of-range columns treated as solid on the left edge, empty on the right. The player cannot walk left of the camera.
- Pits: fall below the level bottom = death. Design rule: pits at most 3 tiles wide on the walk path, up to 5 tiles with a run.
- Enemies: speed 0.25 px/tick, walk until a solid tile in front then turn. They fall off ledges. Spawned from `E` markers at init; only updated while within the view + 6 tiles to the right, frozen otherwise.
- Stomp: player `vy > 0` and the player's feet are in the upper half of the enemy. The enemy squashes (20 ticks, then removed), +100, bounce `vy = -1.2` (-1.9 if A held). Any other touch = death.
- Coins: 'o' tile, +50 points. Collected flags are one bit per tile (`coin_taken[256*14/8]`), so the map stays const.
- Flag: touching the pole column ends the level: +1000, plus `seconds_left * 20`. The hero slides down for 40 ticks, then `GAME_OVER`.
- Lives: 3 in single. Death freezes 50 ticks (hero hops up and falls), respawn at the start or the midway checkpoint (column 128) if it has been passed. 0 lives = `GAME_OVER`. Single time limit 150 s counting down (7500 ticks); at 0 it is `GAME_OVER`.
- Single `score()` = `coins*50 + stomps*100 + furthest_tile*10 + flag bonus + time bonus`.

## Level storage

A `const char *const level[14]` in flash, one 256-character string per row (about 3.6 KB flash, no RAM). Characters: ` ` air, `#` ground, `B` brick (solid platform), `o` coin, `E` enemy start, `F` flag pole (the full column). Plus a `X` hard block for stairs, treated like `#` with a different shade. Rows 12-13 are ground (`#`) except at pits, which are columns of spaces in both rows.

Hand-made flow of the level (columns): 0-20 flat start with coins, 21-40 first bricks + 2 walkers, 41-60 first pit (3 wide) and a floating brick row, 61-100 stairs and a 5-wide pit, 101-127 enemy gauntlet, 128 checkpoint, 129-200 mixed pits and platforms, 201-240 stair run of hard blocks, 245-250 flag.

## 1v1

- `vs_rule = VS_SCORE`; 120 s = 6000 ticks of `update`. Time out, 0 lives or reaching the flag ends that instance. Main waits for both to finish.
- `score()` = progress = `furthest_tile * 10` + if the flag was reached `2000 + seconds_left * 10`. Coins and stomps do not count, they would hide the race. The flag finisher with more time left beats the later one.
- Both players get the same level, enemies are deterministic (no RNG needed).

## Implementation notes

State struct (`mario_t`), about 700 bytes:
- `view_t v`, player/versus, status, timers, lives, score parts (coins, stomps, furthest, time)
- player: `x, y, vx, vy` (8.8, `int32_t`), facing, on_ground, anim
- `cam_x` pixels: `cam = max(cam, px - view_w/3)`, clamped to `[0, 1024 - view_w]`, never scrolls back
- `enemy_t e[24]` (`x, y, vx` 8.8, state, timer; about 14 B) = ~340 B
- `uint8_t coin_taken[448]` (1 bit per tile of 256x14)

Under `GAME_STATE_MAX` (assert it). Tick timing: `update` every 20 ms, order: input -> player x -> player y -> enemies -> pickups/flag -> camera -> timers. `draw`: loop over visible columns `cam/4 .. (cam+view_w)/4 + 1` x 14 rows (up to 33x14 = 462 tile checks, filled with `gfx_rect`), then coins, enemies, hero, HUD. Sub-pixel camera offset is `cam % 4`.
