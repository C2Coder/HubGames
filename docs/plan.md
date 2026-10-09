# HubGames plan

## Controller

- ESP runs an open WiFi AP `HubGames`, page at `http://192.168.4.1`.
- The page is plain HTML/CSS/JS embedded in the firmware (no CDN, phones on the hotspot have no internet).
- Layout (landscape phone): joystick, D-pad, A and B; top bar with the player label and a pause (START) button.
- Input goes over a WebSocket: buttons bitmask + joystick x/y, sent on every change and every 100 ms as keepalive.
- First connected phone is P1, second is P2. A slot frees on disconnect or 2 s without data.
- Firmware sees one input struct per player: `up/down/left/right` (arrows OR joystick past a deadzone), `a`, `b`, analog `jx/jy`, plus "just pressed" edges.

## Menus (on the panels, 128x64)

1. **Mode** - Single / 1v1 (1v1 waits until two phones are connected).
2. **Game** - Tetris, Snake, Pong, Donkey Kong, Mario as a list, games not implemented yet are dimmed.
3. **Game** - 3 s countdown, the pause button pauses (also automatic when a needed phone disconnects), in pause A resumes, B goes back to the game menu.
4. **Result** - score / winner, A plays again, B back to the game menu.

Menus are controlled by P1. Tiny 3x5 pixel font for text.

## Screens

- Single: one game instance on the full 128x64 (both panels).
- 1v1: two instances, P1 on the left panel, P2 on the right, each 64x64.
- Each game takes a viewport (x, w, h) and lays itself out for 64x64 or 128x64, so the logic is written once.
- Exception: **Pong 1v1** is one shared court across both panels (P1 left paddle, P2 right) - two separate pong games make no sense.

## Games and scoring

| Game | Single | 1v1 decided by |
|---|---|---|
| Tetris | points + lines, speed up per level | who dies first loses (score shown) |
| Snake | length/points | who dies first loses |
| Pong | vs AI paddle, first to 5 | first to 5 |
| Donkey Kong | points (barrels jumped, reaching top), levels get faster | who gets higher / reaches the top first, 90 s limit |
| Mario | coins + distance + time bonus | who gets further / reaches the flag first, 120 s limit |

- Tetris: 10x20 board, 3 px cells (fits 64x64). Single mode adds next piece + score beside the board.
- Snake: 2 px cells, wraps or walls (walls).
- Donkey Kong: one classic level - slanted girders, ladders, rolling barrels, jump with A.
- Mario: one hand-made side-scrolling level - platforms, pits, walking enemies (stomp), coins, flag. Jump with A, run with B.
- Own simple pixel art for everything.

## Code layout

```
main/
  main.c          game loop (~50 fps), mode/game/result menus
  hub75.c/h       panel driver (done)
  gfx.c/h         pixel, rect, line, text, sprite, viewport
  font.h          3x5 font
  net.c/h         WiFi AP, HTTP server, WebSocket, player slots
  input.h         input struct
  games/          tetris.c snake.c pong.c dk.c mario.c, game.h interface
  web/index.html  controller page
```

Game interface: `init(state, viewport, mode)`, `update(state, input, dt)` returning running/dead/won, `draw(state)`, `score(state)` / `progress(state)` for 1v1.
WiFi + HTTP on core 0, game loop on core 1.

Per-game design: `docs/games/`.

## Order

1. Controller (AP, page, input) + debug screen showing input of both players - first flash/test here.
2. gfx, font, menus.
3. Snake, Tetris, Pong.
4. Donkey Kong, Mario.
