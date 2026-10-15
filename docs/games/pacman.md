# Pac-Man

Implemented in `main/games/pacman.c`. `VS_SCORE`, not shared, `BIG_SCALE2` (64x64 or 128x64 view only).

## Layout

- 21x21 tile maze, 3x3 px tiles = 63x63 px, own design, left-right symmetric (stored as the left half, mirrored at init). 204 dots, 4 power pellets, ghost house with a door in the middle, wrap tunnel on row 10.
- Walls are thin 1 px blue lines (tile centre pixel joined to neighbouring wall tiles), dots 1 px dim peach, power pellets blink as a plus.
- 64x64 view: maze at the view origin. Score is drawn in grey over the ghost house, lives are small yellow marks on the free pixel row under the maze.
- 128x64 view: maze centred, `SCORE` + value on the left, `LEVEL` + value and lives (yellow bars) on the right.

## Controls

Arrows or joystick. The last pressed (or held) direction is buffered and used as soon as it is possible at a tile centre. Reversing works at any point.

## Rules

- 3 lives. Dot 10, power pellet 50. Ghosts eaten in a row give 200 / 400 / 800 / 1600.
- Ghosts: red chases Pac-Man, pink aims 4 tiles ahead, cyan aims via red (mirror trick), orange chases when far and goes to its corner when within 8 tiles. At an intersection a ghost picks the direction with the shortest straight-line distance to its target, never reversing except at a phase change or pellet. Scatter / chase phases (7 s / 20 s, ...), then chase for good.
- Red starts outside, pink / cyan / orange leave the house one by one (about 1 s, 4 s, 7.5 s, earlier on higher levels).
- Power pellet: ghosts turn blue, reverse and slow down, flash white in the last 2 s. Eaten ghosts return as eyes to the house and come out again. Frightened time shrinks with the level.
- Ghosts are slowed in the tunnel.
- Touching a ghost = death animation, then ready pause and everyone back to start. After the last life the game is over.
- All dots eaten = maze flashes, refilled, next level with slightly faster Pac-Man and ghosts. No fruit.
- `fx_burst` on power pellet, eaten ghost and death.

## Demo

Players = 0: input is never read. At each tile centre the AI runs a BFS from Pac-Man to the nearest dot or close frightened ghost. Cells a non-frightened ghost can reach before Pac-Man (plus one step) are blocked. Reversing is avoided unless nothing else works, and if boxed in it moves to the neighbour farthest from the ghosts. It ends with game over normally.

## State

About 2 KB, under `GAME_STATE_MAX`.
