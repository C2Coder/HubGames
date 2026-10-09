# HubGames

Games for two daisy chained 64x64 HUB75 panels driven by an RP-HUB75 board, controlled by a Saturn (ESP32-S3) board.

RP-HUB75 must run the firmware built with QSPI input (`RoboCamp/RP-HUB75/sw/fw`).

## Wiring (Saturn J6 -> RP-HUB75)

| Signal | ESP32-S3 |
|---|---|
| D0  | IO38 |
| D1  | IO39 |
| D2  | IO41 |
| D3  | IO45 |
| CS  | IO1  |
| CLK | IO3  |

## Build and flash

```sh
. ~/esp/esp-idf-v5.5.1/export.sh
idf.py build flash monitor
```

Flashing goes over native USB (USB-Serial-JTAG), no boot button needed.

## Playing

Connect a phone to the open WiFi `HubGames` and open `http://192.168.4.1`. First phone is P1, second P2.
P1 picks the mode (1 player on both panels, 1v1 on one panel each) and the game.

## Code

- `main/main.c` - game loop (20 ms), menus, pause, results
- `main/net.c` - WiFi AP, HTTP page, WebSocket input, player slots
- `main/gfx.c` - drawing + 3x5 font into `hub75_fb[y][x]` (RGB565), sent by `hub75_show()`
- `main/games/` - games implementing `game_t` from `game.h`
- `main/web/index.html` - controller page

Design: `docs/plan.md`, per game `docs/games/`.
