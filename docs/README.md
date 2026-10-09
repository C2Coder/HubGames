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

## Drawing

Draw RGB565 pixels into `hub75_fb[y][x]` (128x64) and call `hub75_show(brightness)`.
