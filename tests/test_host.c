// Drive the real menu/game code with a simulated framebuffer and phone inputs.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../main/main.c"

static void snapshot(const char *path)
{
    FILE *f = fopen(path, "wb"); assert(f);
    fprintf(f, "P6\n%d %d\n255\n", HUB75_W, screen_h);
    for (int y = 0; y < screen_h; y++) for (int x = 0; x < HUB75_W; x++) {
        uint16_t c = hub75_fb[y][x];
        unsigned char rgb[] = {(c >> 11) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static void controller_count(int n)
{
    memset(in, 0, sizeof(in));
    for (int p = 0; p < n; p++) in[p].connected = true;
}
static void check_game(const game_t *g, int n)
{
    static uint8_t storage[GAME_STATE_MAX] __attribute__((aligned(8)));
    input_t controls[MAX_PLAYERS] = {0};
    player_mask_t mask = (1u << n) - 1;
    g->init(storage, (view_t){0, 0, 128, screen_h}, mask, true);
    for (int frame = 0; frame < 6200; frame++) {
        for (int p = 0; p < n; p++) {
            uint8_t old = controls[p].held;
            controls[p].held = frame % 13 ? old : (esp_random() & 63);
            controls[p].pressed = controls[p].held & ~old;
            controls[p].jx = controls[p].jy = 0;
        }
        game_status_t result = g->update(storage, controls);
        if (frame % 5 == 0) { gfx_clear(); g->draw(storage); }
        if (result != GAME_RUNNING) {
            assert(!(g->winners(storage) & ~mask));
            break;
        }
    }
    for (int p = 0; p < n; p++) (void)g->score_of(storage, p);
}
int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "/tmp";
    char path[256];
    assert(sizeof(gs_mem) == MAX_GAME_INSTANCES * PANEL_STATE_MAX);
    assert(gfx_text_width(1, "SPŠ TRUTNOV") == 43);
    for (int p = 0; p < MAX_PLAYERS; p++)
        for (int q = p + 1; q < MAX_PLAYERS; q++) assert(player_color[p] != player_color[q]);
    controller_count(12);
    assert(connected_mask() == ALL_PLAYERS);
    in[11].pressed = BTN_A;
    assert(pressed_any(1u << 11) == BTN_A);
    in[11].pressed = 0;
    mode = MODE_PARTY;
    for (game_sel = 0; game_sel < MENU_N; game_sel++) {
        const game_t *g = menu[game_sel].game;
        if (!g || !g->shared) continue;
        start_game();
        int max = g->max_players ? g->max_players : MAX_PLAYERS;
        assert(__builtin_popcount(players) == max);
        gfx_clear(); draw_game();
        snprintf(path, sizeof(path), "%s/game-%d.ppm", dir, game_sel); snapshot(path);
        check_game(g, max);
    }
    // Full Doom stays in 1 PLAYER; without a WAD, A keeps the menu open.
    mode = MODE_SINGLE; controller_count(1);
    for (game_sel = 0; menu[game_sel].game || strcmp(menu[game_sel].name, "DOOM"); game_sel++) assert(game_sel < MENU_N);
    state = S_GAMES; in[0].pressed = BTN_A; games_menu(); assert(state == S_GAMES);
    in[0].pressed = 0;
    game_sel = MENU_N - 2; mode = MODE_SINGLE; controller_count(1);
    start_game(); gfx_clear(); draw_game();
    snprintf(path, sizeof(path), "%s/fps.ppm", dir); snapshot(path);
    // Sparse high slots must work in individual versus panels too.
    controller_count(0); in[9].connected = in[11].connected = true;
    mode = MODE_VS; game_sel = 1;
    start_game(); assert(players == ((1u << 9) | (1u << 11))); assert(n_inst == 2);
    assert(inst_player[0] == 9 && inst_player[1] == 11);
    play(); assert(state == S_COUNTDOWN); // play() doesn't change a running game state
    // Every party cup round must include the complete twelve-player roster.
    controller_count(12); mode = MODE_PARTY; game_sel = MENU_N - 1;
    cup_active = false;
    for (int round = 1; round <= CUP_ROUNDS; round++) {
        if (round > 1) cup_round = round;
        start_game(); assert(cup_active && players == ALL_PLAYERS);
        assert(!game->max_players || game->max_players >= 12);
        winners = 1u << 11; set_state(S_RESULT);
    }
    assert(cup_points[11] == 15 && cup_points[0] == 5);
    timer = 50; gfx_clear(); result_screen();
    snprintf(path, sizeof(path), "%s/cup.ppm", dir); snapshot(path);
    cup_active = false;
    for (int height = 64; height <= 128; height += 64) {
        hub75_set_panels(height == 64 ? 2 : 4);
        // Cover complete effect lifetimes and transitions, including the reused union state.
        int64_t begin = esp_timer_get_time();
        for (uint32_t frame = 0; frame < 14 * 600; frame++) {
            gfx_clear(); ambient_show(frame);
            if (frame % 600 == 200) {
                snprintf(path, sizeof(path), "%s/ambient-%d-%d.ppm", dir, height, frame / 600); snapshot(path);
            }
        }
        printf("Ambient %d: mean %.3f ms/frame on host\n", height,
               (esp_timer_get_time() - begin) / (14 * 600 * 1000.0));
        for (uint32_t frame = 0; frame < 5 * 450; frame++) {
            gfx_clear(); school_show(frame);
            for (int y = 0; y < height; y++) for (int x = 0; x < HUB75_W; x++)
                assert(hub75_fb[y][x] == SCHOOL_BLUE || hub75_fb[y][x] == C_WHITE);
            if (frame % 450 == 200) {
                snprintf(path, sizeof(path), "%s/school-%d-%d.ppm", dir, height, frame / 450); snapshot(path);
            }
        }
        for (int i = 0; i < MENU_N - 1; i++) {
            game_sel = i;
            if (!menu[i].game || menu[i].game->shared) continue;
            controller_count(12); mode = MODE_VS; start_game();
            assert(n_inst == (height == 128 ? hub75_panels : 2));
            for (int frame = 0; frame < 1500; frame++) {
                for (int p = 0; p < MAX_PLAYERS; p++) {
                    in[p].held = esp_random() & 63; in[p].pressed = in[p].held;
                }
                play(); gfx_clear(); draw_game();
                if (state == S_RESULT) break;
            }
        }
    }
    puts("Host game/menu/palette checks passed");
    return 0;
}
