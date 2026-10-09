#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "game.h"
#include "gfx.h"
#include "hub75.h"
#include "input.h"
#include "net.h"

#define BRIGHTNESS      128
#define FRAME_MS        20
#define COUNTDOWN_TICKS (3000 / FRAME_MS)

const uint16_t player_color[2] = {C_GREEN, C_BLUE};

// NULL = not implemented yet, shown dimmed
static const struct {
    const char *name;
    const game_t *game;
} menu[] = {
    {"TETRIS", NULL},
    {"SNAKE", &game_snake},
    {"PONG", NULL},
    {"DONKEY KONG", NULL},
    {"MARIO", NULL},
};
#define MENU_N (int)(sizeof(menu) / sizeof(menu[0]))

enum { S_MODE, S_GAMES, S_COUNTDOWN, S_PLAY, S_PAUSE, S_RESULT };

static int state = S_MODE, mode_sel, game_sel = 1, timer, winner;
static bool versus, over[2];
static const game_t *game;
static uint8_t gs[2][GAME_STATE_MAX] __attribute__((aligned(4)));
static input_t in[2];

// text centered on the whole screen, or on each panel in 1v1
static void center_text(int y, int scale, uint16_t c, const char *s)
{
    if (versus) {
        gfx_text_center(32, y, scale, c, s);
        gfx_text_center(96, y, scale, c, s);
    } else {
        gfx_text_center(64, y, scale, c, s);
    }
}

static bool players_ok(void)
{
    return in[0].connected && (!versus || in[1].connected);
}

static void start_game(void)
{
    game = menu[game_sel].game;
    if (versus) {
        game->init(gs[0], (view_t){0, 0, 64, 64}, 0, true);
        game->init(gs[1], (view_t){64, 0, 64, 64}, 1, true);
    } else {
        game->init(gs[0], (view_t){0, 0, 128, 64}, 0, false);
    }
    over[0] = over[1] = false;
    timer = 0;
    state = S_COUNTDOWN;
}

static void draw_game(void)
{
    game->draw(gs[0]);
    if (versus)
        game->draw(gs[1]);
}

static void mode_menu(void)
{
    if (!in[0].connected) {
        gfx_text_center(64, 12, 1, C_GRAY, "WIFI");
        gfx_text_center(64, 20, 1, C_WHITE, "HUBGAMES");
        gfx_text_center(64, 36, 1, C_GRAY, "OPEN");
        gfx_text_center(64, 44, 1, C_WHITE, "192.168.4.1");
        return;
    }

    if (in[0].pressed & (BTN_UP | BTN_DOWN))
        mode_sel ^= 1;
    if (in[0].pressed & BTN_A) {
        versus = mode_sel;
        state = S_GAMES;
        return;
    }

    gfx_text_center(64, 12, 2, mode_sel == 0 ? C_WHITE : C_DARK, "1 PLAYER");
    gfx_text_center(64, 32, 2, mode_sel == 1 ? C_WHITE : C_DARK, "1V1");
    gfx_text(52, 57, 1, in[0].connected ? player_color[0] : C_DARK, "P1");
    gfx_text(68, 57, 1, in[1].connected ? player_color[1] : C_DARK, "P2");
}

static void games_menu(void)
{
    if (in[0].pressed & BTN_UP)
        game_sel = (game_sel + MENU_N - 1) % MENU_N;
    if (in[0].pressed & BTN_DOWN)
        game_sel = (game_sel + 1) % MENU_N;
    if (in[0].pressed & BTN_B) {
        state = S_MODE;
        return;
    }

    bool ready = !versus || in[1].connected;
    if ((in[0].pressed & BTN_A) && menu[game_sel].game && ready) {
        start_game();
        return;
    }

    gfx_text_center(64, 1, 1, C_GRAY, versus ? "1V1" : "1 PLAYER");
    for (int i = 0; i < MENU_N; i++) {
        uint16_t c = menu[i].game ? (i == game_sel ? C_WHITE : C_GRAY) : (i == game_sel ? C_GRAY : C_DARK);
        int y = 11 + i * 8;
        gfx_text_center(64, y, 1, c, menu[i].name);
        if (i == game_sel) {
            int w = gfx_text_width(1, menu[i].name);
            gfx_text(64 - w / 2 - 7, y, 1, C_YELLOW, ">");
            gfx_text(64 + w / 2 + 4, y, 1, C_YELLOW, "<");
        }
    }
    if (!ready)
        gfx_text_center(64, 57, 1, C_YELLOW, "WAITING FOR P2");
}

static void countdown(void)
{
    draw_game();
    char buf[2] = {'3' - timer * 3 / COUNTDOWN_TICKS, 0};
    if (versus) {
        gfx_rect(26, 22, 13, 22, C_BLACK);
        gfx_rect(90, 22, 13, 22, C_BLACK);
    } else {
        gfx_rect(58, 22, 13, 22, C_BLACK);
    }
    center_text(23, 4, C_WHITE, buf);
    if (++timer >= COUNTDOWN_TICKS)
        state = S_PLAY;
}

static void play(void)
{
    if (!players_ok() || ((in[0].pressed | in[1].pressed) & BTN_START)) {
        state = S_PAUSE;
        draw_game();
        return;
    }

    if (!versus) {
        if (game->update(gs[0], &in[0]) == GAME_OVER)
            state = S_RESULT;
    } else {
        for (int p = 0; p < 2; p++)
            if (!over[p])
                over[p] = game->update(gs[p], &in[p]) == GAME_OVER;

        bool done = game->vs_rule == VS_FIRST_DEATH ? (over[0] || over[1]) : (over[0] && over[1]);
        if (done) {
            if (game->vs_rule == VS_FIRST_DEATH && over[0] != over[1]) {
                winner = over[0] ? 2 : 1;
            } else {
                int s0 = game->score(gs[0]), s1 = game->score(gs[1]);
                winner = s0 > s1 ? 1 : s1 > s0 ? 2 : 0;
            }
            state = S_RESULT;
        }
    }
    draw_game();
}

static void pause_screen(void)
{
    uint8_t pressed = in[0].pressed | in[1].pressed;
    if ((pressed & (BTN_A | BTN_START)) && players_ok()) {
        state = S_PLAY;
        return;
    }
    if (pressed & BTN_B) {
        state = S_GAMES;
        return;
    }

    draw_game();
    gfx_rect(0, 18, HUB75_W, 30, C_BLACK);
    const char *title = players_ok() ? "PAUSED" : !in[0].connected ? "P1 LOST" : "P2 LOST";
    center_text(20, 1, players_ok() ? C_WHITE : C_RED, title);
    center_text(32, 1, C_GRAY, "A RESUME");
    center_text(40, 1, C_GRAY, "B QUIT");
}

static void result_screen(void)
{
    uint8_t pressed = in[0].pressed | in[1].pressed;
    if (pressed & BTN_A) {
        start_game();
        return;
    }
    if (pressed & BTN_B) {
        state = S_GAMES;
        return;
    }

    char buf[16];
    if (!versus) {
        gfx_text_center(64, 12, 2, C_RED, "GAME OVER");
        snprintf(buf, sizeof(buf), "SCORE %d", game->score(gs[0]));
        gfx_text_center(64, 32, 1, C_WHITE, buf);
    } else {
        for (int p = 0; p < 2; p++) {
            int cx = 32 + p * 64;
            if (winner == 0)
                gfx_text_center(cx, 12, 2, C_YELLOW, "DRAW");
            else if (winner == p + 1)
                gfx_text_center(cx, 12, 2, C_GREEN, "WIN");
            else
                gfx_text_center(cx, 12, 2, C_RED, "LOSE");
            snprintf(buf, sizeof(buf), "SCORE %d", game->score(gs[p]));
            gfx_text_center(cx, 32, 1, player_color[p], buf);
        }
    }
    center_text(52, 1, C_GRAY, "A AGAIN B MENU");
}

static void game_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    while (1) {
        input_get(0, &in[0]);
        input_get(1, &in[1]);
        gfx_clear();

        switch (state) {
        case S_MODE: mode_menu(); break;
        case S_GAMES: games_menu(); break;
        case S_COUNTDOWN: countdown(); break;
        case S_PLAY: play(); break;
        case S_PAUSE: pause_screen(); break;
        case S_RESULT: result_screen(); break;
        }

        hub75_show(BRIGHTNESS);
        vTaskDelayUntil(&last, pdMS_TO_TICKS(FRAME_MS));
    }
}

void app_main(void)
{
    hub75_init();
    net_init();
    xTaskCreatePinnedToCore(game_task, "game", 8192, NULL, 5, NULL, 1);
}
