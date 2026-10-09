# HubGames v2 - 4 panels, ambient, animations

## Panels

- Setting `PANELS 2 / 4` in a new SETTINGS menu, saved in NVS. Also `BRIGHTNESS` and a `PANEL TEST` screen (big 1-4 on each panel to check the wiring).
- 2 panels: screen 128x64 (as now). 4 panels: 2x2, screen 128x128.
- The RP-HUB75 gets one 256x64 chain. `hub75.c` maps the logical screen to the chain with a table (chain position + 180° rotation per panel), default: top-left, top-right, bottom-left, bottom-right, all upright. If the wiring differs, only that table changes.
- Frame at 4 panels is 32 KB, sent as 2 SPI transactions (S3 DMA limit is 32 KB per transaction).
- `hub75_fb` becomes 128x128, `gfx` clips to the current screen size (`screen_h` 64 or 128).

## Modes on 4 panels

- **1 PLAYER** - bigger: Snake and Pong use the full 128x128 natively, Tetris, Donkey Kong and Mario run their 64x64 layout scaled 2x (chunky pixels, fills all 4 panels).
- **VERSUS** (was 1V1) - per-panel games for 2-4 players, each on their own panel (on 2 panels max 2 = 1v1 as now). First-death games: last one alive wins. Score games: highest wins. GAME_WON (flag/top) still wins instantly.
- **PARTY** - Snake Arena and Pong on the full 128x128 (Pong court taller, teams as now).
- On 2 panels everything stays as it is now.

## Ambient + animations

- **Menus**: slow dim plasma background in shifting colors, menu items on dark bars, selection bar slides smoothly to the new item, short fade (brightness ramp) between screens.
- **Idle screen** (nobody connected): brighter plasma, `HUBGAMES` title with a rainbow shimmer, `WIFI HUBGAMES` and `192.168.4.1` scrolling/blinking.
- **In games**: very dim ambient tint behind the game where the game leaves black (gameplay stays readable).
- **Effects** (`fx.c`, small particle system drawn on top by main): games call `fx_burst(x, y, color, n)` on events:
  - Snake - eat sparkle, death explosion
  - Tetris - line clear sparkle across the row
  - Pong - paddle hit spark, goal burst
  - Donkey Kong - barrel jumped, death
  - Mario - coin, stomp, death, flag fireworks
- **Results**: confetti for the winner, red fade for game over / lose.

## Interface changes

- `init(s, view, players, versus)` stays, views can be 64x64, 128x64 or 128x128.
- `game_t` gets `big`: `BIG_NATIVE` (game handles 128x128 itself) or `BIG_SCALE2` (main gives a 64x64 view and upscales 2x). 
- `MAX_PLAYERS` stays 4, per-panel instances up to 4 (`gs[4]`), `GAME_STATE_MAX` 16 KB.

## Work split

1. Me: hub75 mapping + split transfer, gfx screen size, settings + NVS, main (modes, N-player versus, scale2, fades), fx, plasma, menus, idle screen, results.
2. Then 4 parallel subagents (one per game file, like last time): adapt to big/scale2 + N players + fx calls. Snake/Arena by me.
3. Build, flash, commit.
