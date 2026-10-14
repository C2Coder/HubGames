#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "ambient.h"
#include "doom.h"
#include "fx.h"
#include "game.h"
#include "gfx.h"
#include "hub75.h"
#include "input.h"
#include "net.h"
#include "settings.h"

#define FRAME_MS        20
#define COUNTDOWN_TICKS (3000 / FRAME_MS)
#define FADE_TICKS      12
#define MENU_BG         70 // ambient level behind menus
#define GAME_BG         25 // ambient level behind games
#define IDLE_TICKS      (60000 / FRAME_MS) // no input in menus -> back to the show
#define ADMIN_HOLD      (1000 / FRAME_MS)  // hold pause on P1 to show SHOW / SETTINGS
#define BOOT_PIN        GPIO_NUM_0         // BOOT button kicks everyone off the WiFi

const uint16_t player_color[MAX_PLAYERS] = {C_GREEN, C_BLUE, C_YELLOW, C_PINK};

// MULTIPLAYER runs per-panel games one panel per player, shared games on the whole screen
enum { MODE_SINGLE, MODE_MULTI };
enum { M_SINGLE = 1 << MODE_SINGLE, M_MULTI = 1 << MODE_MULTI };

// main menu, SHOW and SETTINGS only after holding pause on P1
enum { MAIN_SINGLE, MAIN_MULTI, MAIN_SHOW, MAIN_SETTINGS, MAIN_N };
#define MAIN_VISITOR 2
static const char *const main_items[MAIN_N] = {"1 PLAYER", "MULTIPLAYER", "SHOW", "SETTINGS"};

static const struct {
    const char *name;
    const game_t *game;
    uint8_t modes;
    uint8_t ctx; // what the phones show as help
} menu[] = {
    {"TETRIS", &game_tetris, M_SINGLE | M_MULTI, CTX_TETRIS},
    {"SNAKE", &game_snake, M_SINGLE | M_MULTI, CTX_SNAKE},
    {"SNAKE ARENA", &game_snake_arena, M_MULTI, CTX_ARENA},
    {"PONG", &game_pong, M_SINGLE | M_MULTI, CTX_PONG},
    {"DONKEY KONG", &game_dk, M_SINGLE | M_MULTI, CTX_DK},
    {"MARIO", &game_mario, M_SINGLE | M_MULTI, CTX_MARIO},
    {"TANKS", &game_tanks, M_SINGLE | M_MULTI, CTX_TANKS},
    {"DOOM", NULL, M_SINGLE, CTX_DOOM}, // special, see games_menu
};
#define MENU_N (int)(sizeof(menu) / sizeof(menu[0]))

enum { SET_PANELS, SET_BRIGHT, SET_TEST, SET_N };

enum { S_MAIN, S_GAMES, S_COUNTDOWN, S_PLAY, S_PAUSE, S_RESULT, S_AMBIENT, S_SETTINGS, S_PANELTEST, S_DOOM };

static uint32_t t; // frame counter
static int state = S_MAIN, fade, timer;
static int main_sel, mode, game_sel = 1, set_sel;
static int idle_t, admin_hold;
static bool admin; // SHOW and SETTINGS visible
static int bar_y = -1; // animated selection bar, 1/8 px
static const game_t *game;
static uint8_t players; // mask of players in the running game
static uint8_t winners; // mask, 0 = draw
static bool won, scaled;

// VERSUS per panel: instance k runs on panel k for player inst_player[k]
static int n_inst;
static int inst_player[MAX_PLAYERS];
static bool over[MAX_PLAYERS];
// in PSRAM, internal RAM is needed for WiFi and DMA
static EXT_RAM_BSS_ATTR uint8_t gs[MAX_PLAYERS][GAME_STATE_MAX] __attribute__((aligned(4)));
static input_t in[MAX_PLAYERS];

static void start_game(void);

static void set_state(int s)
{
    state = s;
    fade = 0;
    timer = 0;
    bar_y = -1;
}

static int text_scale(void)
{
    return screen_h == 128 ? 2 : 1;
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

static uint8_t pressed_any(uint8_t mask)
{
    uint8_t b = 0;
    for (int p = 0; p < MAX_PLAYERS; p++)
        if ((mask >> p) & 1)
            b |= in[p].pressed;
    return b;
}

// ---- regions: one per panel instance, or the whole screen ----

static bool per_panel(void)
{
    return mode != MODE_SINGLE && !game->shared;
}

static int n_regions(void)
{
    return per_panel() ? n_inst : 1;
}

static view_t region(int r)
{
    if (!per_panel())
        return (view_t){0, 0, HUB75_W, screen_h};
    return (view_t){(r % 2) * 64, (r / 2) * 64, 64, 64};
}

// text centered horizontally in the region, dy from its vertical center
static void region_text(int r, int dy, int scale, uint16_t c, const char *s)
{
    view_t v = region(r);
    gfx_text_center(v.x + v.w / 2, v.y + v.h / 2 + dy, scale, c, s);
}

static void region_band(int r, int dy, int h)
{
    view_t v = region(r);
    gfx_darken(v.x, v.y + v.h / 2 + dy, v.w, h);
}

// ---- menus ----

// list over a darkened background with a sliding highlight bar, scrolls
// when it doesn't fit above the status line
static void draw_list(const char *const *items, int n, int sel, int y0)
{
    int scale = text_scale();
    int pitch = scale == 2 ? 16 : 8, bar_h = 5 * scale + 3;
    int visible = (screen_h - 7 * scale - y0) / pitch;
    int first = 0;
    if (n > visible) {
        first = sel - visible / 2;
        if (first < 0)
            first = 0;
        if (first > n - visible)
            first = n - visible;
        n = first + visible;
    }

    int target = (y0 + (sel - first) * pitch - 2) * 8;
    if (bar_y < 0)
        bar_y = target;
    bar_y += (target - bar_y) / 3;
    if (abs(target - bar_y) < 3)
        bar_y = target;

    gfx_darken(0, y0 - 2, HUB75_W, (n - first) * pitch + 1);
    gfx_rect(8, bar_y / 8, HUB75_W - 16, bar_h, gfx_hsv(t / 2, 200, 90));
    for (int i = first; i < n; i++)
        gfx_text_center(64, y0 + (i - first) * pitch, scale, i == sel ? C_WHITE : C_GRAY, items[i]);
}

static void draw_players_row(int y)
{
    int scale = text_scale();
    char name[3] = {'P', 0, 0};
    for (int p = 0; p < MAX_PLAYERS; p++) {
        name[1] = '1' + p;
        gfx_text(64 - 29 * scale + p * 15 * scale, y, scale, in[p].connected ? player_color[p] : C_DARK, name);
    }
}

static void rainbow_text(int y, int scale, const char *s)
{
    int x = 64 - gfx_text_width(scale, s) / 2;
    for (int i = 0; s[i]; i++) {
        char ch[2] = {s[i], 0};
        gfx_text(x + i * 4 * scale, y, scale, gfx_hsv(t * 3 + i * 24, 220, 255), ch);
    }
}

// MECHA ARCADE and how to join, over the darkened ambient show
static void join_overlay(void)
{
    uint16_t blink = (t / 25) % 2 ? C_YELLOW : C_WHITE;
    gfx_darken(0, 0, HUB75_W, screen_h);
    if (screen_h == 128) {
        rainbow_text(4, 3, "MECHA");
        rainbow_text(22, 3, "ARCADE");
        gfx_text_center(64, 46, 2, C_GRAY, "1. JOIN WIFI");
        gfx_text_center(64, 58, 2, C_YELLOW, "MECHA ARCADE");
        gfx_text_center(64, 78, 2, C_GRAY, "2. OPEN");
        gfx_text_center(64, 90, 2, blink, "192.168.4.1");
        gfx_text_center(64, 112, 1, C_WHITE, "THEN PLAY ON YOUR PHONE");
    } else {
        rainbow_text(2, 2, "MECHA ARCADE");
        gfx_text_center(64, 17, 1, C_GRAY, "1. JOIN WIFI");
        gfx_text_center(64, 24, 1, C_YELLOW, "MECHA ARCADE");
        gfx_text_center(64, 34, 1, C_GRAY, "2. OPEN");
        gfx_text_center(64, 41, 2, blink, "192.168.4.1");
        gfx_text_center(64, 57, 1, C_WHITE, "THEN PLAY ON YOUR PHONE");
    }
}

static void main_menu(void)
{
    if (!in[0].connected) {
        admin = false;
        ambient_show(t);
        if (t % 750 < 500) // join info 10 s out of every 15 s
            join_overlay();
        return;
    }

    // hold pause on P1 for the hidden items
    admin_hold = (in[0].held & BTN_START) ? admin_hold + 1 : 0;
    if (admin_hold == ADMIN_HOLD)
        admin = !admin;
    int n = admin ? MAIN_N : MAIN_VISITOR;
    if (main_sel >= n)
        main_sel = 0;

    if (in[0].pressed & BTN_UP)
        main_sel = (main_sel + n - 1) % n;
    if (in[0].pressed & BTN_DOWN)
        main_sel = (main_sel + 1) % n;
    if (in[0].pressed & BTN_A) {
        if (main_sel == MAIN_SHOW) {
            set_state(S_AMBIENT);
        } else if (main_sel == MAIN_SETTINGS) {
            set_state(S_SETTINGS);
        } else {
            mode = main_sel;
            if (!(menu[game_sel].modes & (1 << mode)))
                for (game_sel = 0; !(menu[game_sel].modes & (1 << mode)); game_sel++)
                    ;
            set_state(S_GAMES);
        }
        return;
    }

    ambient_plasma(t, MENU_BG);
    int scale = text_scale();
    rainbow_text(scale == 2 ? 4 : 1, scale, "MECHA ARCADE");
    draw_list(main_items, n, main_sel, scale == 2 ? 30 : 14);
    draw_players_row(screen_h - 6 * scale);
}

static bool in_mode(int i)
{
    return menu[i].modes & (1 << mode);
}

static void games_menu(void)
{
    if (in[0].pressed & BTN_UP)
        do game_sel = (game_sel + MENU_N - 1) % MENU_N; while (!in_mode(game_sel));
    if (in[0].pressed & BTN_DOWN)
        do game_sel = (game_sel + 1) % MENU_N; while (!in_mode(game_sel));
    if (in[0].pressed & BTN_B) {
        set_state(S_MAIN);
        return;
    }

    int n = __builtin_popcount(connected_mask());
    bool ready = mode == MODE_SINGLE || n >= 2;
    bool doom = !menu[game_sel].game;
    if ((in[0].pressed & BTN_A) && doom && doom_available()) {
        state = S_DOOM;
        net_set_context(CTX_DOOM, 0x1);
        doom_run();
        return;
    }
    if ((in[0].pressed & BTN_A) && ready && !doom) {
        start_game();
        return;
    }

    // visible items and the selected row
    const char *items[MENU_N];
    int cnt = 0, sel = 0;
    for (int i = 0; i < MENU_N; i++) {
        if (!in_mode(i))
            continue;
        if (i == game_sel)
            sel = cnt;
        items[cnt++] = menu[i].name;
    }

    int scale = text_scale();
    ambient_plasma(t, MENU_BG);
    gfx_text_center(64, scale, scale, C_GRAY, main_items[mode]);
    draw_list(items, cnt, sel, scale == 2 ? 20 : 9);

    char buf[20];
    int y = screen_h - 6 * scale;
    if (doom) {
        gfx_text_center(64, y, scale, C_GRAY, doom_available() ? "START = DOOM MENU" : "NO WAD FLASHED");
    } else if (!ready) {
        gfx_text_center(64, y, scale, C_YELLOW, "NEED 2+ PLAYERS");
    } else if (mode == MODE_MULTI) {
        int max = menu[game_sel].game->shared ? MAX_PLAYERS : hub75_panels;
        snprintf(buf, sizeof(buf), "%d PLAYERS", n < max ? n : max);
        gfx_text_center(64, y, scale, C_GRAY, buf);
    }
}

static void settings_menu(void)
{
    if (in[0].pressed & BTN_UP)
        set_sel = (set_sel + SET_N - 1) % SET_N;
    if (in[0].pressed & BTN_DOWN)
        set_sel = (set_sel + 1) % SET_N;
    if (in[0].pressed & BTN_B) {
        set_state(S_MAIN);
        return;
    }

    int dir = (in[0].pressed & BTN_RIGHT) ? 1 : (in[0].pressed & BTN_LEFT) ? -1 : 0;
    bool a = in[0].pressed & BTN_A;
    if (set_sel == SET_PANELS && (dir || a)) {
        set_panels = set_panels == 4 ? 2 : 4;
        hub75_set_panels(set_panels);
        settings_save();
        bar_y = -1;
    } else if (set_sel == SET_BRIGHT && dir) {
        int b = set_brightness + dir * 16;
        set_brightness = b < 16 ? 16 : b > 255 ? 255 : b;
        settings_save();
    } else if (set_sel == SET_TEST && a) {
        set_state(S_PANELTEST);
        return;
    }

    char panels[16], bright[20];
    snprintf(panels, sizeof(panels), "< PANELS %d >", set_panels);
    snprintf(bright, sizeof(bright), "< LIGHT %d >", set_brightness * 100 / 255);
    const char *items[SET_N] = {panels, bright, "PANEL TEST"};

    int scale = text_scale();
    ambient_plasma(t, MENU_BG);
    gfx_text_center(64, scale, scale, C_GRAY, "SETTINGS");
    draw_list(items, SET_N, set_sel, scale == 2 ? 30 : 16);
}

static void panel_test(void)
{
    if (pressed_any(0xf)) {
        set_state(S_SETTINGS);
        return;
    }
    for (int p = 0; p < hub75_panels; p++) {
        int x = (p % 2) * 64, y = (p / 2) * 64;
        uint16_t c = gfx_hsv(p * 64, 255, 255);
        gfx_rect(x, y, 64, 1, c);
        gfx_rect(x, y + 63, 64, 1, c);
        gfx_rect(x, y, 1, 64, c);
        gfx_rect(x + 63, y, 1, 64, c);
        char num[2] = {'1' + p, 0};
        gfx_text_center(x + 32, y + 14, 7, c, num);
        gfx_text_center(x + 32, y + 3, 1, C_WHITE, "TOP");
    }
}

static void ambient_mode(void)
{
    if (pressed_any(0xf)) {
        set_state(S_MAIN);
        return;
    }
    admin = false;
    ambient_show(t);
    if (t % 750 < 500) // join info 10 s out of every 15 s
        join_overlay();
}

// ---- games ----

static void start_game(void)
{
    game = menu[game_sel].game;
    uint8_t conn = connected_mask();
    scaled = false;
    fx_scale = 1;
    fx_clear();

    if (mode == MODE_SINGLE) {
        players = 0x1;
        view_t v = {0, 0, HUB75_W, screen_h};
        if (screen_h == 128 && game->big == BIG_SCALE2) {
            v = (view_t){0, 0, 64, 64};
            scaled = true;
            fx_scale = 2;
        }
        game->init(gs[0], v, players, false);
    } else if (game->shared) {
        players = conn; // everyone connected plays
        game->init(gs[0], (view_t){0, 0, HUB75_W, screen_h}, players, true);
    } else {
        // one panel per player, as many as there are panels
        players = 0;
        n_inst = 0;
        for (int p = 0; p < MAX_PLAYERS && n_inst < hub75_panels; p++) {
            if (!((conn >> p) & 1))
                continue;
            players |= 1 << p;
            inst_player[n_inst] = p;
            over[n_inst] = false;
            game->init(gs[n_inst], region(n_inst), 1 << p, true);
            n_inst++;
        }
    }
    won = false;
    winners = 0;
    set_state(S_COUNTDOWN);
}

static void draw_game(void)
{
    ambient_plasma(t, GAME_BG);
    if (per_panel()) {
        for (int k = 0; k < n_inst; k++)
            game->draw(gs[k]);
    } else {
        game->draw(gs[0]);
        if (scaled)
            gfx_scale2();
    }
}

static int player_score(int p)
{
    if (game->shared)
        return game->score_of(gs[0], p);
    if (mode == MODE_SINGLE)
        return game->score(gs[0]);
    for (int k = 0; k < n_inst; k++)
        if (inst_player[k] == p)
            return game->score(gs[k]);
    return 0;
}

// players with the best score, 0 if everyone ties
static uint8_t best_score(void)
{
    int best = 0;
    uint8_t m = 0;
    for (int k = 0; k < n_inst; k++) {
        int s = game->score(gs[k]);
        if (!m || s > best) {
            best = s;
            m = 0;
        }
        if (s == best)
            m |= 1 << inst_player[k];
    }
    return m == players ? 0 : m;
}

static void countdown(void)
{
    draw_game();
    char buf[2] = {'3' - timer * 3 / COUNTDOWN_TICKS, 0};
    int scale = screen_h == 128 && !per_panel() ? 6 : 4;
    for (int r = 0; r < n_regions(); r++) {
        region_band(r, -5 * scale / 2 - 2, 5 * scale + 4);
        region_text(r, -5 * scale / 2, scale, gfx_hsv(timer * 3, 200, 255), buf);
    }
    if (++timer >= COUNTDOWN_TICKS) {
        state = S_PLAY; // no fade
        timer = 0;
    }
}

static void play(void)
{
    if (!players_ok() || (pressed_any(players) & BTN_START)) {
        set_state(S_PAUSE);
        fade = FADE_TICKS;
        draw_game();
        return;
    }

    if (mode == MODE_SINGLE) {
        game_status_t st = game->update(gs[0], &in[0]);
        if (st != GAME_RUNNING) {
            won = st == GAME_WON;
            set_state(S_RESULT);
        }
    } else if (game->shared) {
        if (game->update(gs[0], in) != GAME_RUNNING) {
            winners = game->winners(gs[0]);
            set_state(S_RESULT);
        }
    } else {
        uint8_t won_mask = 0;
        int alive = 0, last = 0;
        for (int k = 0; k < n_inst; k++) {
            if (!over[k]) {
                game_status_t st = game->update(gs[k], &in[inst_player[k]]);
                if (st == GAME_WON)
                    won_mask |= 1 << inst_player[k];
                over[k] = st != GAME_RUNNING;
            }
            if (!over[k]) {
                alive++;
                last = inst_player[k];
            }
        }

        if (won_mask) {
            winners = won_mask;
            set_state(S_RESULT);
        } else if (game->vs_rule == VS_FIRST_DEATH && alive <= 1) {
            winners = alive == 1 ? 1 << last : best_score();
            set_state(S_RESULT);
        } else if (alive == 0) {
            winners = best_score();
            set_state(S_RESULT);
        }
    }
    draw_game();
}

static void pause_screen(void)
{
    uint8_t pressed = pressed_any(players);
    if ((pressed & (BTN_A | BTN_START)) && players_ok()) {
        state = S_PLAY;
        return;
    }
    if (pressed & BTN_B) {
        set_state(S_GAMES);
        return;
    }

    draw_game();
    char title[10] = "PAUSED";
    uint8_t lost = players & ~connected_mask();
    if (lost)
        snprintf(title, sizeof(title), "P%d LOST", __builtin_ctz(lost) + 1);
    for (int r = 0; r < n_regions(); r++) {
        region_band(r, -13, 27);
        region_text(r, -11, 1, lost ? C_RED : C_WHITE, title);
        region_text(r, 0, 1, C_GRAY, "A RESUME");
        region_text(r, 7, 1, C_GRAY, "B QUIT");
    }
}

static void result_screen(void)
{
    bool party_win = won || winners;
    if (party_win && (timer == 0 || timer == 50))
        fx_confetti(60);
    timer++;

    uint8_t pressed = pressed_any(players);
    if (timer > 25 && (pressed & BTN_A)) {
        start_game();
        return;
    }
    if (timer > 25 && (pressed & BTN_B)) {
        set_state(S_GAMES);
        return;
    }

    char buf[20];
    int big = screen_h == 128 ? 3 : 2;
    int pulse = 30 + (int)(t % 50 < 25 ? t % 25 : 25 - t % 25) * 2;

    if (mode == MODE_SINGLE) {
        if (won) {
            ambient_plasma(t, MENU_BG);
            region_band(0, -18, 34);
            region_text(0, -15, big, C_GREEN, "YOU WIN");
        } else {
            gfx_rect(0, 0, HUB75_W, screen_h, RGB(pulse, 0, 0));
            region_text(0, -15, big, C_RED, "GAME OVER");
        }
        snprintf(buf, sizeof(buf), "SCORE %d", game->score(gs[0]));
        region_text(0, 5 * big - 8, 1, C_WHITE, buf);
    } else if (per_panel()) {
        ambient_plasma(t, MENU_BG);
        for (int k = 0; k < n_inst; k++) {
            int p = inst_player[k];
            view_t v = region(k);
            bool w = (winners >> p) & 1;
            if (winners && !w)
                gfx_rect(v.x, v.y, 64, 64, RGB(pulse, 0, 0));
            else
                gfx_darken(v.x, v.y + 16, 64, 28);
            const char *res = winners == 0 ? "DRAW" : w ? "WIN" : "LOSE";
            region_text(k, -14, 2, winners == 0 ? C_YELLOW : w ? C_GREEN : C_RED, res);
            snprintf(buf, sizeof(buf), "SCORE %d", player_score(p));
            region_text(k, 2, 1, player_color[p], buf);
        }
    } else {
        ambient_plasma(t, MENU_BG);
        int cy = screen_h / 2;
        gfx_darken(0, cy - 22, HUB75_W, 34);
        if (winners == 0) {
            gfx_text_center(64, cy - 18, 2, C_YELLOW, "DRAW");
        } else {
            // "P1 P3 WIN", each name in its color
            int n = __builtin_popcount(winners);
            int x = 64 - (n * 12 + gfx_text_width(2, "WIN")) / 2;
            for (int p = 0; p < MAX_PLAYERS; p++) {
                if (!((winners >> p) & 1))
                    continue;
                snprintf(buf, sizeof(buf), "P%d", p + 1);
                gfx_text(x, cy - 15, 1, player_color[p], buf);
                x += 12;
            }
            gfx_text(x, cy - 18, 2, C_GREEN, "WIN");
        }
        for (int p = 0; p < MAX_PLAYERS; p++) {
            if (!((players >> p) & 1))
                continue;
            snprintf(buf, sizeof(buf), "P%d %d", p + 1, player_score(p));
            gfx_text_center(16 + p * 32, cy + 2, 1, player_color[p], buf);
        }
    }
    if (timer > 25) {
        gfx_darken(0, screen_h - 9, HUB75_W, 9);
        gfx_text_center(64, screen_h - 7, 1, C_GRAY, "A AGAIN   B MENU");
    }
}

// what the phones show as help for the current screen
static uint8_t screen_ctx(void)
{
    switch (state) {
    case S_MAIN: return in[0].connected ? CTX_MENU : CTX_SHOW;
    case S_COUNTDOWN:
    case S_PLAY: return menu[game_sel].ctx;
    case S_PAUSE: return CTX_PAUSE;
    case S_RESULT: return CTX_RESULT;
    case S_AMBIENT: return CTX_SHOW;
    case S_DOOM: return CTX_DOOM;
    default: return CTX_MENU;
    }
}

// BOOT button: kick every phone off the WiFi and start over
static void boot_button(void)
{
    static int last = 1;
    int level = gpio_get_level(BOOT_PIN);
    if (last && !level) {
        net_kick_all();
        if (state != S_DOOM)
            set_state(S_MAIN);
    }
    last = level;
}

// no input in menus for a while -> back to the attract show
static void idle_return(void)
{
    if (pressed_any(0xf))
        idle_t = 0;
    bool menu_like = state == S_GAMES || state == S_SETTINGS || state == S_PANELTEST || state == S_RESULT ||
                     state == S_PAUSE || (state == S_MAIN && in[0].connected);
    if (menu_like && ++idle_t > IDLE_TICKS) {
        idle_t = 0;
        set_state(S_AMBIENT);
    }
}

static void game_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    int64_t worst = 0;
    while (1) {
        int64_t start = esp_timer_get_time();
        boot_button();
        if (state == S_DOOM) { // Doom draws from its own task
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        for (int p = 0; p < MAX_PLAYERS; p++)
            input_get(p, &in[p]);
        idle_return();
        gfx_clear();

        switch (state) {
        case S_MAIN: main_menu(); break;
        case S_GAMES: games_menu(); break;
        case S_COUNTDOWN: countdown(); break;
        case S_PLAY: play(); break;
        case S_PAUSE: pause_screen(); break;
        case S_RESULT: result_screen(); break;
        case S_AMBIENT: ambient_mode(); break;
        case S_SETTINGS: settings_menu(); break;
        case S_PANELTEST: panel_test(); break;
        }
        fx_update_draw();
        bool in_game = state == S_COUNTDOWN || state == S_PLAY || state == S_PAUSE || state == S_RESULT;
        net_set_context(screen_ctx(), in_game ? players : 0xf);

        if (fade < FADE_TICKS)
            fade++;
        hub75_show(set_brightness * fade / FADE_TICKS);

        int64_t took = esp_timer_get_time() - start;
        if (took > worst)
            worst = took;
        if (t % 500 == 0) {
            ESP_LOGI("main", "worst frame %d us", (int)worst);
            worst = 0;
        }
        t++;
        vTaskDelayUntil(&last, pdMS_TO_TICKS(FRAME_MS));
    }
}

void app_main(void)
{
    hub75_init();
    gpio_config_t boot = {.pin_bit_mask = 1ULL << BOOT_PIN, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&boot);
    net_init();
    settings_load();
    hub75_set_panels(set_panels);
    xTaskCreatePinnedToCore(game_task, "game", 8192, NULL, 5, NULL, 1);
}
