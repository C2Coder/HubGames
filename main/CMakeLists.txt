idf_component_register(SRCS "main.c" "hub75.c" "gfx.c" "net.c" "games/snake.c" "games/arena.c"
                       INCLUDE_DIRS "." "games"
                       EMBED_FILES "web/index.html"
                       REQUIRES esp_driver_spi esp_wifi esp_netif esp_event esp_http_server nvs_flash)
