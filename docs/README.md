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

Connect a phone to the open WiFi `HubGames` and open `http://192.168.4.1`. Phones get P1-P4 in connect order.
P1 picks in the main menu: 1 PLAYER, VERSUS (own panel each), PARTY (2-4 on one shared screen), AMBIENT (attract show) or SETTINGS (2 or 4 panels, brightness, panel test).
With nobody connected the panels run the ambient show with join info. 4 panels = 2x2, chain order in `panel_map` in `main/hub75.c`.

## Code

- `main/main.c` - game loop (20 ms), menus, pause, results
- `main/net.c` - WiFi AP, HTTP page, WebSocket input, player slots
- `main/gfx.c` - drawing + 3x5 font into `hub75_fb[y][x]` (RGB565), sent by `hub75_show()`
- `main/fx.c` - particles, `main/ambient.c` - plasma and attract show, `main/settings.c` - NVS settings
- `main/games/` - games implementing `game_t` from `game.h`
- `main/web/index.html` - controller page

Design: `docs/plan.md`, `docs/plan-v2.md`, per game `docs/games/`.
