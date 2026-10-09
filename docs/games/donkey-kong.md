# Donkey Kong

## Overview

One classic level: climb slanted girders and ladders to the top while the ape on the top girder rolls barrels down. Jump with A to hop over barrels. Single: points, levels get faster, 3 lives. 1v1: `VS_SCORE`, 90 s limit, the score is progress, so whoever gets higher (or reaches the top first) wins.

## Layout

The level is a logical 64x64 area. Origin `ox = v.x` in 64x64, `ox = v.x + 32` in 128x64.

```
y 0..5   HUD strip (64x64 only)
y 8..16  ape (12x9) on the top girder, goal figure (3x5) at the right
floor 5 (top)  surface y 17
floor 4        surface y 28
floor 3        surface y 39
floor 2        surface y 50
floor 1 (bot)  surface y 61, player starts at the left
```

- Girders are 2 px thick, 11 px apart, with a slant of 1 px per 16 px (4 px over the 64 px width). Surface height: `y(x) = base + dir * x / 16`, `dir` alternates (floor 1 slopes down to the left, floor 2 down to the right, ...), so barrels roll to the end, fall to the floor below and reverse. Stored as `base[5]`, `dir[5]` in flash. Each girder stops 3 px short of one side, leaving the gap for barrels to fall through.
- Ladders: 3 px wide, 1 or 2 per gap, staggered (const table `{floor, x}`, about 7 entries). Ladder bottom/top y = `y(x)` of the two girders.
- 128x64 single: the level sits in the centre (x+32..x+95), the free side strips (32 px each) are the HUD. Left: `SCORE`, value, `BONUS`, value. Right: `LVL n`, remaining lives as small player sprites. The top strip stays black.
- 64x64 (1v1): top 6 px HUD: progress score left (scale 1), seconds left in the middle, lives as 2x3 dots on the right, `P1`/`P2` tag in `player_color`.

Sprites (own pixel art, drawn per pixel from `const char[]` strings with a tiny palette, `.` = transparent):
- Player 5x6 (hitbox 3x5 centred), colour = `player_color` plus white head, frames: stand, walk 1, walk 2, jump, climb 1, climb 2.
- Barrel 4x4, brown disc with a lighter stripe, 2 frames (roll).
- Ape 12x9, 2 frames (arms up, arms down) when it releases a barrel. Goal figure 3x5.
- Ladder 3 px wide rungs every 2 px, girder = 2 px bar with a notch pattern.

## Controls

- LEFT / RIGHT: walk, 0.6 px/tick on the slope (follows the girder y automatically).
- UP / DOWN: climb when the player centre is within 2 px of a ladder; locks the player on the ladder (x fixed, 0.5 px/tick). Leaving at the top or bottom puts the feet on the girder. No jumping while climbing.
- A: jump, only on a girder. Horizontal velocity is fixed at takeoff (-1/0/+1).
- B: unused.

## Rules / Scoring

- Jump: initial `vy = -1.05 px/tick`, gravity 0.10 px/tick^2 (apex 5.5 px, air time about 21 ticks). Lands when the feet reach the girder surface while falling. Girder tops are solid from above only.
- Barrel: spawned at the ape every `max(50, 120 - 12 * level)` ticks. Rolls at `0.8 + 0.1 * level` px/tick (cap 1.6) along the slope, falls off the end to the floor below and flips direction. At a ladder top going down it takes the ladder with a 25% chance (rising to 50% at higher levels). Leaves the level on floor 1 (removed at the left edge). Max 8 barrels alive.
- Collision: AABB of the player hitbox against the barrel 4x4. Touch = death. Barrels that pass under a jumping player (player feet above barrel top, centres within 3 px, same floor) are counted once: +100.
- Reach the goal (feet on floor 5, x within 4 px of the goal figure): +500 plus the remaining BONUS. BONUS starts at 1000 and drops by 10 every 20 ticks (40 s), minimum 0. Then next level: level + 1 (faster barrels, shorter spawn interval), player reset, barrels cleared.
- Lives: 3. On death the player blinks/falls for 50 ticks, barrels are cleared, respawn at floor 1, the bonus continues. 0 lives = `GAME_OVER`.

## 1v1

- `vs_rule = VS_SCORE`. Each player plays their own copy, 90 s = 4500 ticks counted in `update` (starts after the countdown because main only calls `update` while playing). Time out, no lives left, or reaching the top ends that instance (`GAME_OVER`); main waits for both.
- `score()` = progress: `best_floor * 100 + height within floor (0..10)` for the highest point reached, plus if the top was reached `1000 + seconds_left * 10`. Dying never lowers progress, it only costs time. So reaching the top first scores more than reaching it later. Ties are a draw.
- Both instances use the same barrel sequence: a file-static `seed` drawn when `player == 0` and reused by `player == 1`, each instance has its own xorshift. Barrel timing differs by player position only, not luck.
- Single mode has no time limit, `score()` = points.

## Implementation notes

State struct (`dk_t`) is roughly 250 bytes:
- `view_t v`, player/versus/level/lives, state (`PLAY`, `DEAD`, `WIN`), timers
- player: `int32_t x, y` (8.8), `vx`, `vy`, `floor`, on_ladder, anim frame
- `barrel_t b[8]`: `x` 8.8, `y`, `floor`, `dir`, `state` (roll/fall/ladder), `jumped` flag - about 12 B each, ~100 B
- score, bonus, bonus_t, time_left, best progress, rng

Flash tables: `base/dir[5]`, ladders, sprites, about 300 B. Well under `GAME_STATE_MAX` (assert it as snake does).

Tick timing: `update` every 20 ms; the player moves in 8.8 fixed point, y on a girder is recomputed from x each tick (`y_on(floor, x)`), so no slope collision code is needed. Barrels use the same helper. Draw order: girders, ladders, ape, barrels, player, HUD.
