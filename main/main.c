#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"

#include "ambient.h"
#include "fx.h"
#include "game.h"
#include "gfx.h"
#include "hub75.h"
#include "input.h"
#include "net.h"
#include "settings.h"
#include "school.h"
#ifdef CONFIG_HUBGAMES_FULL_DOOM
#include "doom.h"
#endif

#define FRAME_MS        20
#define COUNTDOWN_TICKS (3000 / FRAME_MS)
#define FADE_TICKS      12
#define MENU_BG         70 // ambient level behind menus
#define GAME_BG         25 // ambient level behind games

const uint16_t player_color[MAX_PLAYERS] = {C_GREEN, C_BLUE, C_YELLOW, C_PINK,
    RGB(255, 96, 0), RGB(0, 255, 255), RGB(255, 255, 255), RGB(128, 64, 255),
    RGB(176, 255, 0), RGB(255, 48, 48), RGB(0, 176, 128), RGB(255, 144, 192)};

enum { MODE_SINGLE, MODE_VS, MODE_PARTY };
enum { M_SINGLE = 1 << MODE_SINGLE, M_VS = 1 << MODE_VS, M_PARTY = 1 << MODE_PARTY };

// main menu, the first three are game modes
enum { MAIN_SINGLE, MAIN_VS, MAIN_PARTY, MAIN_AMBIENT, MAIN_SCHOOL, MAIN_SETTINGS, MAIN_N };
static const char *const main_items[MAIN_N] = {"1 PLAYER", "VERSUS", "PARTY", "AMBIENT", "SPŠ TRUTNOV", "SETTINGS"};

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
    {"TANKS", &game_tanks, M_SINGLE | M_VS | M_PARTY},
#ifdef CONFIG_HUBGAMES_FULL_DOOM
    {"DOOM", NULL, M_SINGLE},
#endif
    {"BOMBER ARENA", &game_bomber, M_VS | M_PARTY},
    {"TRON", &game_tron, M_VS | M_PARTY},
    {"POCKET RACERS", &game_racers, M_VS | M_PARTY},
    {"PIXEL FOOTBALL", &game_football, M_VS | M_PARTY},
    {"TRICKY TOWERS", &game_towers, M_VS | M_PARTY},
#ifdef CONFIG_HUBGAMES_FULL_DOOM
    {"MICRO FPS", &game_fps, M_VS}, // singleplayer uses the full Doom engine
#else
    {"MICRO FPS", &game_fps, M_SINGLE | M_VS},
#endif
    {"PARTY CUP", NULL, M_PARTY},
};
#define MENU_N (int)(sizeof(menu) / sizeof(menu[0]))

enum { SET_PANELS, SET_BRIGHT, SET_TEST, SET_N };

enum { S_MAIN, S_GAMES, S_COUNTDOWN, S_PLAY, S_PAUSE, S_RESULT, S_AMBIENT, S_SCHOOL, S_SETTINGS, S_PANELTEST, S_DOOM };

static uint32_t t; // frame counter
static int state = S_MAIN, fade, timer;
static int main_sel, mode, game_sel = 1, set_sel;
static int bar_y = -1; // animated selection bar, 1/8 px
static const game_t *game;
static player_mask_t players; // mask of players in the running game
static player_mask_t winners; // mask, 0 = draw
static bool won, scaled, cup_active;
static int cup_round, cup_last = -1;
static player_mask_t cup_players;
static uint16_t cup_points[MAX_PLAYERS];
#define CUP_ROUNDS 5

// VERSUS per panel: instance k runs on panel k for player inst_player[k]
static int n_inst;
static int inst_player[MAX_GAME_INSTANCES];
static bool over[MAX_GAME_INSTANCES];
static union {
    uint8_t shared[GAME_STATE_MAX];
    uint8_t panel[MAX_GAME_INSTANCES][PANEL_STATE_MAX];
} gs_mem __attribute__((aligned(4)));
static void *gs[MAX_GAME_INSTANCES];
static input_t in[MAX_PLAYERS];

static void start_game(void);

static controller_profile_t game_controller(void)
{
    return game == &game_bomber ? CONTROL_BOMBER : game == &game_tron ? CONTROL_TRON :
        game == &game_racers ? CONTROL_RACERS : game == &game_football ? CONTROL_FOOTBALL :
        game == &game_towers ? CONTROL_TOWERS : game == &game_fps ? CONTROL_FPS :
        game == &game_tanks ? CONTROL_TANKS : CONTROL_DEFAULT;
}

static void set_state(int s)
{
    if (s == S_RESULT && cup_active) {
        for (int p = 0; p < MAX_PLAYERS; p++)
            if (players & (1u << p)) cup_points[p] += (winners & (1u << p)) ? 3 : 1;
    }
    state = s;
    if (s == S_MAIN || s == S_GAMES) cup_active = false;
    if (s == S_MAIN || s == S_GAMES) net_set_controller(CONTROL_DEFAULT);
    else if (s == S_PAUSE) net_set_controller(CONTROL_PAUSE);
    else if (s == S_RESULT) net_set_controller(cup_active && cup_round < CUP_ROUNDS ? CONTROL_NEXT : CONTROL_RESULT);
    fade = 0;
    timer = 0;
    bar_y = -1;
}

static int text_scale(void)
{
    return screen_h == 128 ? 2 : 1;
}

static player_mask_t connected_mask(void)
{
    player_mask_t m = 0;
    for (int p = 0; p < MAX_PLAYERS; p++)
        if (in[p].connected)
            m |= 1 << p;
    return m;
}

static bool players_ok(void)
{
    return (connected_mask() & players) == players;
}

static uint8_t pressed_any(player_mask_t mask)
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

// list over a darkened background with a sliding highlight bar
static void draw_list(const char *const *items, int n, int sel, int y0)
{
    int scale = text_scale();
    int pitch = scale == 2 ? 16 : 8, bar_h = 5 * scale + 3;
    int visible = (screen_h - 14 - y0) / pitch;
    if (visible < 1) visible = 1;
    int first = sel >= visible ? sel - visible + 1 : 0;
    int count = n - first < visible ? n - first : visible;
    int target = (y0 + (sel - first) * pitch - 2) * 8;
    if (bar_y < 0)
        bar_y = target;
    bar_y += (target - bar_y) / 3;
    if (abs(target - bar_y) < 3)
        bar_y = target;

    gfx_darken(0, y0 - 2, HUB75_W, count * pitch + 1);
    gfx_rect(8, bar_y / 8, HUB75_W - 16, bar_h, gfx_hsv(t / 2, 200, 90));
    for (int i = 0; i < count; i++)
        gfx_text_center(64, y0 + i * pitch, scale, first + i == sel ? C_WHITE : C_GRAY, items[first + i]);
}

static void draw_players_row(int y)
{
    (void)y;
    char name[8];
    for (int p = 0; p < MAX_PLAYERS; p++) {
        snprintf(name, sizeof(name), "P%d", p + 1);
        gfx_text_center(11 + (p % 6) * 21, screen_h - 12 + (p / 6) * 6,
                        1, in[p].connected ? player_color[p] : C_DARK, name);
    }
}

// rainbow HUBGAMES and how to join, over the ambient show
static void join_overlay(void)
{
    int big = screen_h == 128 ? 3 : 2;
    int cy = screen_h / 2;
    gfx_darken(0, cy - 15 - big * 5, HUB75_W, 34 + big * 5);
    const char *title = "HUBGAMES";
    int x = 64 - gfx_text_width(big, title) / 2;
    for (int i = 0; title[i]; i++) {
        char ch[2] = {title[i], 0};
        gfx_text(x + i * 4 * big, cy - 12 - big * 5, big, gfx_hsv(t * 3 + i * 24, 220, 255), ch);
    }
    gfx_text_center(64, cy + 2, 1, C_WHITE, "JOIN WIFI HUBGAMES");
    gfx_text_center(64, cy + 10, 1, (t / 25) % 2 ? C_YELLOW : C_WHITE, "OPEN 192.168.4.1");
}

static void main_menu(void)
{
    if (!in[0].connected) {
        ambient_show(t);
        join_overlay();
        return;
    }

    if (in[0].pressed & BTN_UP)
        main_sel = (main_sel + MAIN_N - 1) % MAIN_N;
    if (in[0].pressed & BTN_DOWN)
        main_sel = (main_sel + 1) % MAIN_N;
    if (in[0].pressed & BTN_A) {
        if (main_sel == MAIN_AMBIENT) {
            set_state(S_AMBIENT);
        } else if (main_sel == MAIN_SCHOOL) {
            fx_clear();
            set_state(S_SCHOOL);
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
    draw_list(main_items, MAIN_N, main_sel, scale == 2 ? 20 : 6);
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
#ifdef CONFIG_HUBGAMES_FULL_DOOM
    bool full_doom = !menu[game_sel].game && mode == MODE_SINGLE;
    if (full_doom && (in[0].pressed & BTN_A)) {
        if (doom_available()) {
            state = S_DOOM;
            net_set_controller(CONTROL_DOOM);
            if (!doom_run()) set_state(S_GAMES);
        }
        return;
    }
#endif
    if ((in[0].pressed & BTN_A) && ready) {
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

    char buf[32];
    int y = screen_h - 6 * scale;
#ifdef CONFIG_HUBGAMES_FULL_DOOM
    if (full_doom) {
        gfx_text_center(64, y, scale, C_GRAY, doom_available() ? "START = DOOM MENU" : "NO PSRAM / WAD");
    } else
#endif
    if (!ready) {
        gfx_text_center(64, y, scale, C_YELLOW, "NEED 2+ PLAYERS");
    } else if (mode != MODE_SINGLE) {
        const game_t *g = menu[game_sel].game;
        int max = !g || g->shared ? (g && g->max_players ? g->max_players : MAX_PLAYERS) : hub75_panels;
        snprintf(buf, sizeof(buf), "%d/%d PLAYERS", n < max ? n : max, n);
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
    if (pressed_any(ALL_PLAYERS)) {
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
    if (pressed_any(ALL_PLAYERS)) {
        set_state(S_MAIN);
        return;
    }
    ambient_show(t);
    if (t % 750 < 250) // join info 5 s out of every 15 s
        join_overlay();
}

// ---- games ----

static void start_game(void)
{
    game = menu[game_sel].game;
    player_mask_t conn = connected_mask();
    if (!game) {
        if (!cup_active) {
            cup_active = true; cup_round = 1; cup_last = -1;
            cup_players = conn;
            memset(cup_points, 0, sizeof(cup_points));
        }
        conn = cup_players;
        int eligible[MENU_N], count = 0;
        for (int i = 0; i < MENU_N; i++) {
            const game_t *g = menu[i].game;
            if (!g || !g->shared || !(menu[i].modes & M_PARTY) || i == cup_last) continue;
            if (g->max_players && g->max_players < __builtin_popcount(conn)) continue;
            eligible[count++] = i;
        }
        cup_last = eligible[esp_random() % count];
        game = menu[cup_last].game;
    } else cup_active = false;
    for (int k = 0; k < MAX_GAME_INSTANCES; k++)
        gs[k] = mode != MODE_SINGLE && !game->shared ? gs_mem.panel[k] : gs_mem.shared;
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
        players = 0;
        int limit = game->max_players ? game->max_players : MAX_PLAYERS;
        for (int p = 0, n = 0; p < MAX_PLAYERS && n < limit; p++)
            if (conn & (1u << p)) { players |= 1u << p; n++; }
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
    net_set_controller(game_controller());
    won = false;
    winners = 0;
    set_state(S_COUNTDOWN);
}

static void draw_game(void)
{
    if (!game->opaque) ambient_plasma(t, GAME_BG);
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
static player_mask_t best_score(void)
{
    int best = 0;
    player_mask_t m = 0;
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
    if (cup_active) {
        char label[32];
        snprintf(label, sizeof(label), "CUP %d/%d %s", cup_round, CUP_ROUNDS, menu[cup_last].name);
        gfx_darken(0, 0, HUB75_W, 7);
        gfx_text_center(64, 1, 1, C_WHITE, label);
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
        player_mask_t won_mask = 0;
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
        net_set_controller(game_controller());
        state = S_PLAY;
        return;
    }
    if (pressed & BTN_B) {
        set_state(S_GAMES);
        return;
    }

    draw_game();
    char title[10] = "PAUSED";
    player_mask_t lost = players & ~connected_mask();
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
        if (cup_active) {
            if (cup_round < CUP_ROUNDS) cup_round++;
            else cup_active = false;
        }
        start_game();
        return;
    }
    if (timer > 25 && (pressed & BTN_B)) {
        set_state(S_GAMES);
        return;
    }

    char buf[32];
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
            int n = __builtin_popcount(winners);
            if (n == 1) {
                int p = __builtin_ctz(winners);
                snprintf(buf, sizeof(buf), "P%d WIN", p + 1);
                gfx_text_center(64, cy - 18, 2, player_color[p], buf);
            } else {
                gfx_text_center(64, cy - 18, 2, C_GREEN, "TEAM WIN");
            }
        }
        int slot = 0;
        for (int p = 0; p < MAX_PLAYERS; p++) {
            if (!(players & (1u << p))) continue;
            snprintf(buf, sizeof(buf), "P%d %d", p + 1, player_score(p));
            gfx_text_center(16 + (slot % 4) * 32, cy - 5 + (slot / 4) * 7,
                            1, winners & (1u << p) ? C_WHITE : player_color[p], buf);
            slot++;
        }
    }
    if (cup_active) {
        ambient_plasma(t, MENU_BG);
        snprintf(buf, sizeof(buf), "CUP %d/%d", cup_round, CUP_ROUNDS);
        gfx_text_center(64, 2, 2, C_WHITE, buf);
        int best = -1, slot = 0;
        for (int p = 0; p < MAX_PLAYERS; p++)
            if ((cup_players & (1u << p)) && cup_points[p] > best) best = cup_points[p];
        for (int p = 0; p < MAX_PLAYERS; p++) if (cup_players & (1u << p)) {
            snprintf(buf, sizeof(buf), "P%d %d", p + 1, cup_points[p]);
            int y = 19 + (slot / 4) * 9;
            gfx_text_center(16 + (slot % 4) * 32, y, 1,
                            cup_points[p] == best ? C_WHITE : player_color[p], buf);
            slot++;
        }
        gfx_text_center(64, screen_h - 18, 1, C_YELLOW,
                        cup_round == CUP_ROUNDS ? "FINAL STANDINGS" : "WIN 3 PTS REST 1");
    }
    if (timer > 25) {
        gfx_darken(0, screen_h - 9, HUB75_W, 9);
        gfx_text_center(64, screen_h - 7, 1, C_GRAY, cup_active && cup_round < CUP_ROUNDS ? "A NEXT   B MENU" : "A AGAIN   B MENU");
    }
}

static void game_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    int64_t worst = 0;
    int late = 0;
    while (1) {
        int64_t start = esp_timer_get_time();
#ifdef CONFIG_HUBGAMES_FULL_DOOM
        if (state == S_DOOM) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
#endif
        for (int p = 0; p < MAX_PLAYERS; p++)
            input_get(p, &in[p]);
        gfx_clear();

        switch (state) {
        case S_MAIN: main_menu(); break;
        case S_GAMES: games_menu(); break;
        case S_COUNTDOWN: countdown(); break;
        case S_PLAY: play(); break;
        case S_PAUSE: pause_screen(); break;
        case S_RESULT: result_screen(); break;
        case S_AMBIENT: ambient_mode(); break;
        case S_SCHOOL:
            if (pressed_any(ALL_PLAYERS)) set_state(S_MAIN);
            else school_show(t);
            break;
        case S_SETTINGS: settings_menu(); break;
        case S_PANELTEST: panel_test(); break;
        }
        fx_update_draw();

        if (fade < FADE_TICKS)
            fade++;
        hub75_show(set_brightness * fade / FADE_TICKS);

        int64_t took = esp_timer_get_time() - start;
        if (took > worst)
            worst = took;
        if (took > FRAME_MS * 1000) late++;
        if (t % 500 == 0) {
            ESP_LOGI("main", "worst frame %d us, late %d/500, heap %u, min %u",
                     (int)worst, late, (unsigned)esp_get_free_heap_size(),
                     (unsigned)esp_get_minimum_free_heap_size());
            worst = 0;
            late = 0;
        }
        t++;
        vTaskDelayUntil(&last, pdMS_TO_TICKS(FRAME_MS));
    }
}

void app_main(void)
{
    hub75_init();
    net_init();
    settings_load();
    hub75_set_panels(set_panels);
    xTaskCreatePinnedToCore(game_task, "game", 8192, NULL, 5, NULL, 1);
}
