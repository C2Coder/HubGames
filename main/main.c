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

const uint16_t player_color[MAX_PLAYERS] = {C_GREEN, C_BLUE, C_YELLOW, C_PINK};

enum { MODE_SINGLE, MODE_VS, MODE_PARTY, MODE_N };
enum { M_SINGLE = 1 << MODE_SINGLE, M_VS = 1 << MODE_VS, M_PARTY = 1 << MODE_PARTY };

static const char *const mode_names[MODE_N] = {"1 PLAYER", "1V1", "PARTY"};

static const struct {
    const char *name;
    const game_t *game;
    uint8_t modes;
} menu[] = {
    {"TETRIS", &game_tetris, M_SINGLE | M_VS},
    {"SNAKE", &game_snake, M_SINGLE | M_VS},
    {"SNAKE ARENA", &game_snake_arena, M_VS | M_PARTY},
    {"PONG", &game_pong, M_SINGLE | M_VS | M_PARTY},
    {"DONKEY KONG", &game_dk, M_SINGLE | M_VS},
    {"MARIO", &game_mario, M_SINGLE | M_VS},
};
#define MENU_N (int)(sizeof(menu) / sizeof(menu[0]))

enum { S_MODE, S_GAMES, S_COUNTDOWN, S_PLAY, S_PAUSE, S_RESULT };

static int state = S_MODE, mode, game_sel = 1, timer;
static uint8_t players; // mask of players in the running game
static uint8_t winners; // mask, 0 = draw
static bool won, over[2];
static const game_t *game;
static uint8_t gs[2][GAME_STATE_MAX] __attribute__((aligned(4)));
static input_t in[MAX_PLAYERS];

static bool per_panel(void)
{
    return mode != MODE_SINGLE && !game->shared;
}

// text centered on the whole screen, or on each panel for per-panel games
static void center_text(int y, int scale, uint16_t c, const char *s)
{
    if (per_panel()) {
        gfx_text_center(32, y, scale, c, s);
        gfx_text_center(96, y, scale, c, s);
    } else {
        gfx_text_center(64, y, scale, c, s);
    }
}

static bool in_mode(int i)
{
    return menu[i].modes & (1 << mode);
}

static uint8_t connected_mask(void)
{
    uint8_t m = 0;
    for (int p = 0; p < MAX_PLAYERS; p++)
        if (in[p].connected)
            m |= 1 << p;
    return m;
}

static bool players_ok(void)
{
    return (connected_mask() & players) == players;
}

static uint8_t pressed_any(void)
{
    uint8_t b = 0;
    for (int p = 0; p < MAX_PLAYERS; p++)
        if ((players >> p) & 1)
            b |= in[p].pressed;
    return b;
}

static void start_game(void)
{
    game = menu[game_sel].game;
    players = mode == MODE_SINGLE ? 0x1 : mode == MODE_VS ? 0x3 : connected_mask();
    if (mode == MODE_SINGLE) {
        game->init(gs[0], (view_t){0, 0, 128, 64}, players, false);
    } else if (game->shared) {
        game->init(gs[0], (view_t){0, 0, 128, 64}, players, true);
    } else {
        game->init(gs[0], (view_t){0, 0, 64, 64}, 0x1, true);
        game->init(gs[1], (view_t){64, 0, 64, 64}, 0x2, true);
    }
    over[0] = over[1] = false;
    won = false;
    timer = 0;
    state = S_COUNTDOWN;
}

static void draw_game(void)
{
    game->draw(gs[0]);
    if (per_panel())
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

    if (in[0].pressed & BTN_UP)
        mode = (mode + MODE_N - 1) % MODE_N;
    if (in[0].pressed & BTN_DOWN)
        mode = (mode + 1) % MODE_N;
    if (in[0].pressed & BTN_A) {
        if (!in_mode(game_sel))
            game_sel = 2; // snake arena is in every multiplayer mode
        if (!in_mode(game_sel))
            game_sel = 1;
        state = S_GAMES;
        return;
    }

    for (int m = 0; m < MODE_N; m++)
        gfx_text_center(64, 4 + m * 17, 2, m == mode ? C_WHITE : C_DARK, mode_names[m]);
    char name[3] = {'P', 0, 0};
    for (int p = 0; p < MAX_PLAYERS; p++) {
        name[1] = '1' + p;
        gfx_text(37 + p * 15, 58, 1, in[p].connected ? player_color[p] : C_DARK, name);
    }
}

static void games_menu(void)
{
    if (in[0].pressed & BTN_UP)
        do game_sel = (game_sel + MENU_N - 1) % MENU_N; while (!in_mode(game_sel));
    if (in[0].pressed & BTN_DOWN)
        do game_sel = (game_sel + 1) % MENU_N; while (!in_mode(game_sel));
    if (in[0].pressed & BTN_B) {
        state = S_MODE;
        return;
    }

    int n = __builtin_popcount(connected_mask());
    bool ready = mode == MODE_SINGLE || (mode == MODE_VS ? in[1].connected : n >= 2);
    if ((in[0].pressed & BTN_A) && ready) {
        start_game();
        return;
    }

    gfx_text_center(64, 1, 1, C_GRAY, mode_names[mode]);
    for (int i = 0, row = 0; i < MENU_N; i++) {
        if (!in_mode(i))
            continue;
        int y = 10 + row++ * 7;
        gfx_text_center(64, y, 1, i == game_sel ? C_WHITE : C_GRAY, menu[i].name);
        if (i == game_sel) {
            int w = gfx_text_width(1, menu[i].name);
            gfx_text(64 - w / 2 - 7, y, 1, C_YELLOW, ">");
            gfx_text(64 + w / 2 + 4, y, 1, C_YELLOW, "<");
        }
    }
    if (!ready)
        gfx_text_center(64, 57, 1, C_YELLOW, mode == MODE_VS ? "WAITING FOR P2" : "NEED 2+ PLAYERS");
    else if (mode == MODE_PARTY) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d PLAYERS", n);
        gfx_text_center(64, 57, 1, C_GRAY, buf);
    }
}

static void countdown(void)
{
    draw_game();
    char buf[2] = {'3' - timer * 3 / COUNTDOWN_TICKS, 0};
    if (per_panel()) {
        gfx_rect(26, 22, 13, 22, C_BLACK);
        gfx_rect(90, 22, 13, 22, C_BLACK);
    } else {
        gfx_rect(58, 22, 13, 22, C_BLACK);
    }
    center_text(23, 4, C_WHITE, buf);
    if (++timer >= COUNTDOWN_TICKS)
        state = S_PLAY;
}

// higher score wins, equal = draw
static uint8_t by_score(int s0, int s1)
{
    return s0 > s1 ? 0x1 : s1 > s0 ? 0x2 : 0;
}

static void play(void)
{
    if (!players_ok() || (pressed_any() & BTN_START)) {
        state = S_PAUSE;
        draw_game();
        return;
    }

    if (mode == MODE_SINGLE) {
        game_status_t st = game->update(gs[0], &in[0]);
        if (st != GAME_RUNNING) {
            won = st == GAME_WON;
            state = S_RESULT;
        }
    } else if (game->shared) {
        if (game->update(gs[0], in) != GAME_RUNNING) {
            winners = game->winners(gs[0]);
            state = S_RESULT;
        }
    } else {
        game_status_t st[2] = {GAME_RUNNING, GAME_RUNNING};
        for (int p = 0; p < 2; p++)
            if (!over[p]) {
                st[p] = game->update(gs[p], &in[p]);
                over[p] = st[p] != GAME_RUNNING;
            }

        bool w0 = st[0] == GAME_WON, w1 = st[1] == GAME_WON;
        bool done = game->vs_rule == VS_FIRST_DEATH ? (over[0] || over[1]) : (over[0] && over[1]);
        if (w0 || w1) {
            winners = w0 && w1 ? by_score(game->score(gs[0]), game->score(gs[1])) : w0 ? 0x1 : 0x2;
            state = S_RESULT;
        } else if (done) {
            if (game->vs_rule == VS_FIRST_DEATH && over[0] != over[1])
                winners = over[0] ? 0x2 : 0x1;
            else
                winners = by_score(game->score(gs[0]), game->score(gs[1]));
            state = S_RESULT;
        }
    }
    draw_game();
}

static void pause_screen(void)
{
    uint8_t pressed = pressed_any();
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
    char title[10] = "PAUSED";
    uint8_t lost = players & ~connected_mask();
    if (lost)
        snprintf(title, sizeof(title), "P%d LOST", __builtin_ctz(lost) + 1);
    center_text(20, 1, lost ? C_RED : C_WHITE, title);
    center_text(32, 1, C_GRAY, "A RESUME");
    center_text(40, 1, C_GRAY, "B QUIT");
}

static int player_score(int p)
{
    return game->shared ? game->score_of(gs[0], p) : game->score(gs[p]);
}

static void result_screen(void)
{
    uint8_t pressed = pressed_any();
    if (pressed & BTN_A) {
        start_game();
        return;
    }
    if (pressed & BTN_B) {
        state = S_GAMES;
        return;
    }

    char buf[16];
    if (mode == MODE_SINGLE) {
        if (won)
            gfx_text_center(64, 12, 2, C_GREEN, "YOU WIN");
        else
            gfx_text_center(64, 12, 2, C_RED, "GAME OVER");
        snprintf(buf, sizeof(buf), "SCORE %d", game->score(gs[0]));
        gfx_text_center(64, 32, 1, C_WHITE, buf);
        gfx_text_center(64, 52, 1, C_GRAY, "A AGAIN B MENU");
    } else if (mode == MODE_VS) {
        for (int p = 0; p < 2; p++) {
            int cx = 32 + p * 64;
            if (winners == 0)
                gfx_text_center(cx, 12, 2, C_YELLOW, "DRAW");
            else if ((winners >> p) & 1)
                gfx_text_center(cx, 12, 2, C_GREEN, "WIN");
            else
                gfx_text_center(cx, 12, 2, C_RED, "LOSE");
            snprintf(buf, sizeof(buf), "SCORE %d", player_score(p));
            gfx_text_center(cx, 32, 1, player_color[p], buf);
            gfx_text_center(cx, 52, 1, C_GRAY, "A AGAIN B MENU");
        }
    } else {
        if (winners == 0) {
            gfx_text_center(64, 8, 2, C_YELLOW, "DRAW");
        } else {
            // "P1 P3 WIN", each name in its color
            int n = __builtin_popcount(winners);
            int w = n * 12 + gfx_text_width(2, "WIN");
            int x = 64 - w / 2;
            for (int p = 0; p < MAX_PLAYERS; p++) {
                if (!((winners >> p) & 1))
                    continue;
                snprintf(buf, sizeof(buf), "P%d", p + 1);
                gfx_text(x, 11, 1, player_color[p], buf);
                x += 12;
            }
            gfx_text(x, 8, 2, C_GREEN, "WIN");
        }
        for (int p = 0; p < MAX_PLAYERS; p++) {
            if (!((players >> p) & 1))
                continue;
            snprintf(buf, sizeof(buf), "P%d %d", p + 1, player_score(p));
            gfx_text_center(16 + p * 32, 32, 1, player_color[p], buf);
        }
        gfx_text_center(64, 52, 1, C_GRAY, "A AGAIN B MENU");
    }
}

static void game_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    while (1) {
        for (int p = 0; p < MAX_PLAYERS; p++)
            input_get(p, &in[p]);
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
