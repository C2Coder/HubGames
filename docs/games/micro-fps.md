# Micro FPS / ESP DOOM research

Investigated existing ESP32 options:

- https://github.com/w1ne/doomgeneric-esp32 — full GPLv2 Doom engine for an ESP32-S3 N16R8 (16 MB flash / 8 MB PSRAM), with a WAD in flash and a dedicated LCD backend.
- https://github.com/chayantank/esp32-doom — a small Arduino/OLED raycaster targeting 128×64. Its repository did not provide an explicit license when inspected, so its code/assets were not copied.

This project currently configures ESP-IDF 5.5.1 and 16 MB flash, with no PSRAM setup
or WAD partition. Micro FPS is an independently written lightweight raycaster
within the existing game interface and HUB75 renderer. No upstream engine or
Doom/Wolfenstein assets are included. A future full-Doom integration would need
the board's PSRAM wiring/capacity confirmed, a HUB75 backend, a WAD storage plan,
and an appropriate engine/data license.

Movement and rays use fixed point and a sine table; walls use bounded DDA.
Billboards are sorted and depth-tested against wall columns. Joystick Y moves
forward/back; X turns. Holding B changes X to strafing; A autofires with cooldown
and mild aim assistance. Arrows are an alternative. Kill six enemies, then reach
the southeast open area to win. Health is restored slightly by a kill.

1 PLAYER uses the full 128×64 screen. VERSUS runs independent 64×64 runs on the two
panels, comparing kills and remaining health, or finishing instantly on a win.
