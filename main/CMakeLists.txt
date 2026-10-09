set(extra_srcs)
if(CONFIG_HUBGAMES_FULL_DOOM)
    list(APPEND extra_srcs "doom.c")
endif()

idf_component_register(SRCS ${extra_srcs} "main.c" "hub75.c" "gfx.c" "net.c" "fx.c" "ambient.c" "settings.c" "games/snake.c" "games/arena.c" "games/tetris.c" "games/pong.c" "games/dk.c" "games/mario.c" "games/bomber.c" "games/tron.c" "games/racers.c" "games/football.c" "games/towers.c" "games/fps.c" "games/tanks.c" "school.c"
                       INCLUDE_DIRS "." "games"
                       EMBED_FILES "web/index.html"
                       REQUIRES esp_driver_spi esp_timer esp_wifi esp_netif esp_event esp_http_server nvs_flash esp_psram doomgeneric)
