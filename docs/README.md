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

Twelve phones need larger WiFi, DHCP and socket limits (set in `sdkconfig.defaults`). An `sdkconfig` generated before this change keeps the old limits and the build stops with a static assert in `main/net.c`: delete `sdkconfig` and build again.

## Doom

DOOM (shareware, 1 PLAYER) runs from `components/doomgeneric` - [doomgeneric](https://github.com/ozkl/doomgeneric) as a git submodule, GPL-2.0, glue in `doom_port.c` and `main/doom.c`. Needs the 8 MB PSRAM of the N16R8 module.
Flash the WAD once (separate partition, app flashes don't touch it):

```sh
git submodule update --init
doom/flash-wad.sh   # downloads doom1.wad if missing, inside an ESP-IDF shell
```

Joystick/arrows move, A fire (and menu confirm), B use/open, pause button = Doom menu. The controller page switches to the DOOM layout by itself: joystick moves forward/back and strafes left/right, FIRE above the turn arrows, USE below. Quitting Doom restarts the board back to HubGames.

## Playing

Connect a phone to the open WiFi `HubGames` and open `http://192.168.4.1`. Up to 12 phones get P1-P12 in connect order, each with its own color, shown on the phone too.
P1 picks in the main menu: 1 PLAYER, VERSUS (own panel each), PARTY (one shared screen), AMBIENT (attract show, 14 effects), SPŠ TRUTNOV (school logo show) or SETTINGS (2 or 4 panels, brightness, panel test).
With nobody connected the panels run the ambient show with join info. 4 panels = 2x2, chain order in `panel_map` in `main/hub75.c`.

The phone labels A/B for the current game and releases all buttons when the page loses focus.

| Game | Players | Controls |
|---|---|---|
| Bomber Arena | 2-8 | move, A bomb; crates drop upgrades, walls close in after 30 s |
| Tron | 2-12 | turn, hold A to boost |
| Pocket Racers | 2-8 | left/right steer, A item, hold B while turning to drift; 3 laps |
| Pixel Football | 2-12 | move, A kick, hold B to sprint; teams alternate by slot |
| Tricky Towers | 2-8 | left/right move, A rotate, B drop, down soft drop; reach the dotted line |
| Micro FPS | 1, or VERSUS | stick moves and turns, hold A to shoot, hold B to strafe; see `docs/games/micro-fps.md` |
| Party Cup | 2-12 | five random shared games, win 3 points, others 1 |

Snake Arena, Pong and Tron take all 12 players; games limited to 8 use the first 8 connected slots (the menu shows e.g. `8/12 PLAYERS`). Party Cup keeps its starting roster and only picks games that fit all of it.

## Tests

Host tests run the real game, menu and drawing code with stubbed ESP-IDF calls:

```sh
./tools/test_host.sh
HOST_SANITIZERS=undefined,bounds,object-size ./tools/test_host.sh # if ASan hangs on macOS
```

## Code

- `main/main.c` - game loop (20 ms), menus, pause, results
- `main/net.c` - WiFi AP, HTTP page, WebSocket input, player slots
- `main/gfx.c` - drawing + 3x5 font into `hub75_fb[y][x]` (RGB565), sent by `hub75_show()`
- `main/fx.c` - particles, `main/ambient.c` - plasma and attract show, `main/settings.c` - NVS settings
- `main/school.c` - school show, logo in `main/assets/` (CC BY-SA 4.0, see its README)
- `main/games/` - games implementing `game_t` from `game.h`
- `main/web/index.html` - controller page

Design: `docs/plan.md`, `docs/plan-v2.md`, per game `docs/games/`.
