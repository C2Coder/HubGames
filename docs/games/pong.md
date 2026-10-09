# Pong

## Modes

Shared game (`shared = true`), one instance on the full 128x64 court.

- 1 PLAYER (`versus=false`, reads only `in[0]`): P1 left paddle vs AI right paddle, first to 5. P1 wins: `GAME_WON`, AI wins: `GAME_OVER`. `score()` = P1 points.
- 1V1 (mask 0x3) and PARTY (any mask of 2-4 players): teams, left = P1+P3, right = P2+P4 (those in the mask). A team with two players gets two paddles, the first at the goal line (x=2 / x=124), the second 12 px further forward. An empty team (e.g. mask 0x5) is played by the AI. First team to 5: `GAME_OVER`, `winners()` = human players of the winning team (0 if the AI wins), `score_of(p)` = points of p's team.

## Controls

- UP / DOWN held: paddle at 1.8 px/tick.
- Analog joystick `|jy| > 15`: proportional speed, up to 1.8 px/tick.
- A: skip the serve pause.

## Layout

- Paddles 2x12 px in the player color (AI paddle in the side's color: P1 color left, P2 color right). Ball 2x2 white. Dotted dark net in the middle.
- Score centered at the top, left team points in the P1 color, right team in the P2 color.
- Ball blinks during the 50 tick serve pause. After the 5th point the losing paddles and the ball blink for 60 ticks, then the game ends.

## Physics

Ball in fixed point 8.8. Speed starts at 1.0 px/tick, +0.06 per paddle hit, capped at 2.4, reset on each serve. Serve goes towards the team that conceded, within about +-20 degrees. Paddle hit is swept (no tunnelling): hit position gives the bounce angle (centre flat and fast, edges steep and slower). Left paddles only hit a ball moving left, right paddles only one moving right, so a ball passes through a forward paddle when coming from behind it. Top and bottom bounce.

AI: retargets every 6 ticks, only tracks when the ball comes towards it and is in its half-plus, otherwise returns to centre. Aim error +-6 px re-rolled per serve, max speed 1.1 px/tick.
