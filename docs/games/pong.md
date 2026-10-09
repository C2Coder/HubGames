# Pong

## Overview

Single: you (left paddle) against an AI paddle (right), first to 5 points wins. 1v1: ONE shared court across both panels, P1 left paddle, P2 right paddle, first to 5. This is the only game that is not "one instance per player", so it needs a small interface change (below).

## Layout

The court is always the full 128x64 (`view_t{0,0,128,64}`), in single mode and in 1v1.

- Paddles: 2x12 px, left at x=2..3, right at x=124..125. P1 `player_color[0]` (green), P2 / AI `player_color[1]` (blue).
- Ball: 2x2 px, white.
- Net: dotted line at x=63..64 (every second pixel), `C_DARK`.
- Scores: scale 1 digits at y=1, left one right-aligned to x=58, right one at x=70, in the player colours. No title text, no walls drawn (top/bottom are the screen edge).
- The ball crosses the panel seam with no visual break.

## Controls

- UP / DOWN held moves the paddle at full speed (1.8 px/tick).
- If `|jy| > 15` the analog joystick is used instead: `speed = jy * 1.8 / 100`, so small deflections move slowly. (`held` also contains the joystick merged into the directions, so analog is checked first.)
- A: skip the serve pause. Everything else unused.
- In 1v1 P1 uses `in[0]`, P2 uses `in[1]`. In single mode the AI replaces P2.

## Rules / Scoring

- First to 5 points. A point is scored when the ball leaves the court left or right. After a point: 50 tick pause (ball sits in the centre, blinking), then the ball is served towards the player who just conceded, at a random angle within +-20 degrees.
- Ball in fixed point 8.8 (`int32_t`, 256 = 1 px). Speed magnitude starts at 1.0 px/tick (crosses the court in 2.5 s), +0.06 px/tick per paddle hit, capped at 2.4 px/tick. Reset on every serve.
- Top/bottom: `vy = -vy`, clamp to the court.
- Paddle hit angle: `off = (ball_cy - paddle_cy) / 7` clamped to -1..1 (7 = half paddle plus ball). Then `vy = off * speed * 0.75`, `vx = sign * speed * (1 - 0.35 * |off|)` (flipped away from the paddle). Centre hits are flat and fast, edge hits are steep and slightly slower. Computed with 8.8 multiplies, no sqrt, speed drift is acceptable.
- Collision is swept: when the ball's next x passes the paddle face plane in this tick, interpolate y at the crossing and test against the paddle rect (+1 px tolerance). Max ball speed 2.4 px/tick is above the 2 px thickness, so a plain overlap test could tunnel.
- Scoring for the result screen: single `score()` = P1 points (0..5), the generic GAME OVER screen is shown, win or lose. 1v1: see `score_of` below.

### AI

- Re-targets every 6 ticks (120 ms reaction delay). It only tracks when the ball moves towards it and is past x=32; otherwise it drifts to the court centre.
- Target y = ball y + `err`, `err` is re-rolled after each of its own hits and each serve, uniform in +-6 px.
- Max speed 1.1 px/tick (player 1.8), so steep, fast balls get past it. It moves in a dead zone of 2 px to avoid jitter.
- Easy tuning knobs: reaction ticks, error range, max speed.

## 1v1 (shared court)

Interface change in `game.h`:
- New field `bool shared` in `game_t`. If true and `versus`, main creates ONE instance.
- For shared games `init` is called once as `init(gs[0], (view_t){0,0,128,64}, 0, true)`. `update` receives a pointer to the 2-element `input_t` array (`in`, i.e. `in[0]` = P1, `in[1]` = P2). The signature stays the same. In single mode it receives `&in[0]` as before and the game ignores the second element (it must not read it when `!versus`).
- New optional `int (*score_of)(const void *s, int player)` returning that player's points. Needed because `score()` has one value.

Changes in `main.c`:
- `start_game`: `if (versus && game->shared)` call init once (full view), else the current per-panel path.
- `draw_game`: draw `gs[1]` only if `versus && !game->shared`.
- `play` (versus branch): if shared, call `update(gs[0], in)` once; `GAME_OVER` goes to `S_RESULT`; winner = higher `score_of(gs[0], p)` (never a draw in first-to-5). The `over[]` / `vs_rule` logic is skipped.
- `result_screen`: for shared games use `score_of(gs[0], p)` per panel instead of `score(gs[p])`; the WIN / LOSE text per panel is unchanged.
- Menu entry: `{"PONG", &game_pong}`.
- Pause and countdown already work, they only call `draw_game`.

## Implementation notes

State struct (`pong_t`), about 80 bytes:

- `view_t v`, `bool versus`, `int pts[2]`
- `int32_t py[2]` (paddle top, 8.8), `int32_t bx, by, vx, vy`
- `int speed` (8.8), `int serve_t`, `int serve_dir`
- AI: `int ai_t`, `int32_t ai_target`, `int ai_err`
- `int end_t` (blink 60 ticks after the 5th point, then `GAME_OVER`)

Tick timing: `update` every 20 ms; order is paddles, ball move + collisions, scoring, AI. `draw` converts 8.8 to pixels with `>> 8`. Speeds above are px per tick (1 px/tick = 50 px/s). `vs_rule` is unused for the shared mode but must still be set (`VS_SCORE`).
