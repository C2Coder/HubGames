# Micro FPS

A small original raycaster (no engine code or WAD data), so a first person
shooter also runs per panel in VERSUS, next to the full DOOM in 1 PLAYER.

Movement and rays use fixed point and a sine table; walls use bounded DDA.
Enemy billboards are sorted and depth-tested against the wall columns.
Joystick Y moves forward/back, X turns; holding B turns X into strafing; A
autofires with a cooldown and mild aim assist. Arrows work too. Kill six
enemies, then reach the open area in the south-east corner to win. A kill
restores a little health.

1 PLAYER uses the whole screen. VERSUS runs an independent 64x64 game per
panel: the first to escape wins, otherwise kills and remaining health decide.

Existing ESP32 options looked at first:

- https://github.com/w1ne/doomgeneric-esp32 - full GPLv2 Doom engine for an ESP32-S3 N16R8 with the WAD in flash (the same approach as `components/doomgeneric`).
- https://github.com/chayantank/esp32-doom - a small Arduino/OLED raycaster for 128x64. No license, so nothing was copied.
