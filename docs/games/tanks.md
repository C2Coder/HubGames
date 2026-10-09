# Tanks

## Modes

Shared game (`shared = true`), one instance on the whole screen, `big = BIG_NATIVE`. Works at 128x64 and 128x128, all geometry uses the view height.

- 1 PLAYER (`versus=false`, reads only `in[0]`): waves of AI tanks (red). Wave n has min(n,3) AI tanks, min(n+1,5) on 4 panels. 3 lives, respawn after 1 s with spawn protection. Clearing wave 5: `GAME_WON`, losing all lives: `GAME_OVER`. `score()` = kills * 100 + waves cleared * 250.
- VERSUS / PARTY (2-4 players): free-for-all rounds. Last tank alive gets a round win, banner "P2 +1" for 2 s, then a new round with a new layout. If the last tanks die together nobody scores. First to 3: `GAME_OVER`, `winners()` = that player, `score_of(p)` = round wins.

## Controls

- Analog joystick (past deadzone): 8 directions. Arrows: 4 directions (with two held, the newly pressed axis wins).
- A: fire, cooldown 0.4 s, max 3 own bullets alive. B unused.

## Arena

Tile grid, 4 px tiles, 32 columns, 14 rows (128x64) or 30 rows (128x128), HUD of 6 px on top. Four hand made layouts, stored as the left half of 15 rows, mirrored left-right at init and (4 panels) top-bottom. Random layout each round/wave, never the same twice in a row. Solid walls and bricks, corridors are at least 2 tiles wide, the spawn corners are open and all spawns reach each other (checked for every layout at both sizes).

## Tanks and bullets

- Tank 5x5 px in the player color (AI red), darker treads, light barrel in the facing direction. Movement is axis separated (slides along walls), a pure move blocked by a corner is nudged up to 2 px sideways. 0.7 px/tick (AI 0.55).
- Bullet 1.8 px/tick, bounces off solid walls twice and vanishes on the third hit. A brick is destroyed and the bullet vanishes. Kills any tank, the owner's own tank only after the first bounce. Two bullets that meet cancel each other.
- Spawn protection 50 ticks, blinking. Versus corners: P1 top left, P2 bottom right, P3 top right, P4 bottom left.

## AI

Moves along one direction, re-picks every 20-60 ticks or when stuck for 6 ticks, mostly towards the player along an axis. When roughly aligned (+-4 px, also diagonal) it turns to the player and, with a clear line of sight, fires after a 12-26 tick reaction delay. Max 2 bullets. Spawns on the free spot farthest from the other tanks.

## Effects

`fx_burst`: muzzle flash (4 yellow/white), brick debris (6 brick color), wall bounce (2 white), bullets canceling (3 white), tank death 40 particles (20 player color, 10 red, 10 orange).

## HUD

Versus: round wins per player in their colors. Single: lives `L`, wave `W`, score.
