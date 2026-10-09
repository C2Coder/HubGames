# Mario

One hand-made side-scrolling level (224 x 14 tiles, 4 px tiles, string map `level[]` in flash). Own pixel art: red-cap hero 3x7, mushroom-like walker 4x4.

## Controls

- LEFT / RIGHT: move. B held: run. A: jump, hold for a higher jump (a tap is a short hop). Small jump buffer (6 ticks) and coyote time (4 ticks).
- START: pause (main).

## Physics

8.8 fixed point per 20 ms tick, per-pixel collision against solid tiles (`#` ground, `B` brick, `X` hard block, `P` pipe).
- Walk max 0.5 px/tick, run 0.9, acceleration 0.04, friction 0.06.
- Jump `vy = -1.95`, gravity 0.12 (doubled while rising with A released), max fall 2.5.
- Measured on flat ground: rise 15 px (3.75 tiles), travel 15 px walking / 27 px running, 30 ticks in the air.
- Pits are 3-5 tiles wide, 4 and 5 wide need a run. Pipes are at most 3 tiles high (12 px), stairs go up one tile per step.
- Enemies walk at 0.25 px/tick, turn at walls, fall off ledges, only move near the view. Stomp: hero falling and feet in the upper part of the walker, it squashes and you bounce. Any other touch kills.
- Camera follows forward only (hero at 1/3 of the view), you cannot walk back off screen. Falling into a pit kills.

## Level

Tiles: ` ` air, `#`, `B`, `X`, `P`, `o` coin, `E` enemy, `F` flag pole. Flow: flat start, bricks, pipes, pits 3 to 5 wide, stairs, enemy gauntlet, pipe row with trapped walkers, 19 wide pit crossed on three brick platforms, stair up to the flag plateau at column 214. Checkpoints at columns 0, 76 and 146 (respawn at the last one passed, enemies reset, taken coins stay taken).

## Modes

- 1 PLAYER (`.big = BIG_SCALE2`): 2 panels view 128x64 (32 tiles), 4 panels view 64x64 (16 tiles) that main upscales 2x. Same rules in both: 3 lives, 200 s timer. Death freezes 1 s, then respawn. Flag: hero slides down the pole, then `GAME_WON`. 0 lives or timer 0: `GAME_OVER`. HUD: score, coins, `T` seconds left, `L` lives (compact positions in the 64 px view).
  `score() = coins*10 + stomps*10 + furthest column + (on finish) 100 + seconds_left*5`.
- VERSUS per panel (64x64 view, 16 tiles, `VS_SCORE`, not shared, 2-4 players): each instance has the same level, unlimited respawns (a death costs the 1 s freeze plus the walk back from the checkpoint), 120 s limit. Reaching the flag returns `GAME_WON` immediately, that player wins. At the limit `GAME_OVER`, and `score()` = furthest x in px (+10000 if the flag was reached), so `VS_SCORE` picks who got further. HUD: coins, seconds left, `P1`-`P4` in the player color. All drawing uses the view offset x and y (panels can be at y=64) and is clipped to the view. The level is 14 tiles + 8 px HUD = 64 px high, so it fills a 64x64 view exactly. The blue sky is kept.

## Effects

`fx_burst`: coin (gold, 6), stomp (walker brown, 8), death (red, 16), flag fireworks (rainbow, 14, every 8 ticks during the pole slide).

## Verification (host harness, not in the repo)

A depth-first search over macro inputs with the real `update()` reaches the flag in both modes (about 25 s of play, well inside 120 s). Checked that drawing in a 64 px panel never spills outside the view, idle play ends with `GAME_OVER`, and 3 deaths end a single game.
