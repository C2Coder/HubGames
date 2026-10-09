# Snake

Implemented in `main/games/snake.c`.

## Layout

- 2 px cells, 6 px HUD on top (score, in 1v1 also `P1`/`P2` in the player color) with a dim line under it.
- 64x64 (1v1): 32x29 grid. 128x64 (single): 64x29 grid.
- Edges of the play area are walls.

## Controls

Arrows or joystick turn. Up to 2 turns are queued so quick double turns work, reversing is ignored.

## Rules

- Starts with length 4 in the middle-left, moving right.
- Food (red) gives +1 score and +1 length.
- Moves every 7 ticks (140 ms), 1 tick faster every 5 food, minimum 3 ticks (60 ms).
- Wall or own body = death. The snake blinks for 1.2 s, then the game is over.
- Filling the whole board ends the game.

## 1v1

`VS_FIRST_DEATH` - first player to die loses. Both dying on the same tick is decided by score, equal score is a draw.

## State

About 6 KB (occupancy grid + body ring buffer for the 64x29 single board), under `GAME_STATE_MAX`.

## Snake Arena (1v1 only)

Implemented in `main/games/arena.c`, a shared game (`game_t.shared`).

- Both snakes on one 64x29 board across both panels, P1 starts top-left moving right, P2 bottom-right moving left.
- 2 food items at once, scores shown in the player colors left and right on the HUD.
- Wall, own body or the other snake = death. Head-on into the same cell kills both.
- Speed goes up every 6 food eaten in total.
- Survivor wins, both dying on the same step is a draw.
