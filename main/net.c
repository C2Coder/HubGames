#include "net.h"
#include "input.h"

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define SSID       "HubGames"
#define DEADZONE   40
#define TIMEOUT_MS 2000

_Static_assert(CONFIG_LWIP_MAX_SOCKETS >= MAX_PLAYERS + 5, "raise LWIP_MAX_SOCKETS to 20");
_Static_assert(CONFIG_LWIP_DHCPS_MAX_STATION_NUM >= MAX_PLAYERS, "raise DHCP station limit to 12");
_Static_assert(CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM <= 17 - MAX_PLAYERS,
               "reduce reserved ESP-NOW encryption keys to allow 12 WiFi clients");

static controller_profile_t controller;

static const char *TAG = "net";

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

typedef struct {
    int fd; // -1 = free
    uint8_t btn, prev;
    int8_t jx, jy;
    TickType_t seen;
} slot_t;

static slot_t slots[MAX_PLAYERS]; // initialized before the server starts
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static httpd_handle_t server;

void net_set_controller(controller_profile_t profile)
{
    portENTER_CRITICAL(&lock);
    controller = profile;
    portEXIT_CRITICAL(&lock);
}


void input_get(int p, input_t *in)
{
    int expired = -1;
    portENTER_CRITICAL(&lock);
    slot_t *s = &slots[p];
    if (s->fd >= 0 && xTaskGetTickCount() - s->seen > pdMS_TO_TICKS(TIMEOUT_MS)) {
        expired = s->fd;
        s->fd = -1;
    }
    bool conn = s->fd >= 0;
    uint8_t b = conn ? s->btn : 0;
    int jx = conn ? s->jx : 0, jy = conn ? s->jy : 0;
    bool strafe = b & BTN_STRAFE_MODE;
    b &= ~BTN_STRAFE_MODE;
    // In full Doom the stick moves/strafes; separate arrows turn.
    if (!strafe && abs(jx) > abs(jy)) {
        if (jx > DEADZONE) b |= BTN_RIGHT;
        if (jx < -DEADZONE) b |= BTN_LEFT;
    } else {
        if (jy > DEADZONE) b |= BTN_DOWN;
        if (jy < -DEADZONE) b |= BTN_UP;
    }

    in->held = b;
    in->pressed = b & ~s->prev;
    in->jx = jx;
    in->jy = jy;
    in->strafe = strafe;
    in->connected = conn;
    s->prev = b;
    portEXIT_CRITICAL(&lock);
    // Release the HTTP socket too, so a dropped phone cannot consume capacity indefinitely.
    if (expired >= 0 && server) httpd_sess_trigger_close(server, expired);
}

// Input: [buttons, jx, jy]. Reply: [player (0 = full), profile, RGB565 low, high].
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
        return ESP_OK; // handshake

    uint8_t buf[8];
    httpd_ws_frame_t f = {.payload = buf};
    esp_err_t ret = httpd_ws_recv_frame(req, &f, sizeof(buf));
    if (ret != ESP_OK)
        return ret;
    if (f.type != HTTPD_WS_TYPE_BINARY || f.len < 3)
        return ESP_OK;

    int fd = httpd_req_to_sockfd(req);
    int p = -1;
    portENTER_CRITICAL(&lock);
    for (int i = 0; i < MAX_PLAYERS && p < 0; i++)
        if (slots[i].fd == fd)
            p = i;
    for (int i = 0; i < MAX_PLAYERS && p < 0; i++)
        if (slots[i].fd < 0)
            p = i;
    if (p >= 0) {
        uint8_t prev = slots[p].fd == fd ? slots[p].prev : 0;
        slots[p] = (slot_t){
            .fd = fd,
            .btn = buf[0],
            .prev = prev,
            .jx = (int8_t)buf[1],
            .jy = (int8_t)buf[2],
            .seen = xTaskGetTickCount(),
        };
    }
    uint8_t profile = controller;
    portEXIT_CRITICAL(&lock);

    uint16_t color = p >= 0 ? player_color[p] : 0x8410;
    uint8_t reply[4] = {p + 1, profile, color & 255, color >> 8};
    httpd_ws_frame_t out = {.type = HTTPD_WS_TYPE_BINARY, .payload = reply, .len = sizeof(reply)};
    return httpd_ws_send_frame(req, &out);
}

static esp_err_t page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start);
}

static void on_close(httpd_handle_t hd, int fd)
{
    portENTER_CRITICAL(&lock);
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (slots[i].fd == fd)
            slots[i].fd = -1;
    portEXIT_CRITICAL(&lock);
    close(fd);
}

void net_init(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) slots[i].fd = -1;
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t wc = {
        .ap = {
            .ssid = SSID,
            .ssid_len = strlen(SSID),
            .channel = 6,
            .max_connection = MAX_PLAYERS,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.max_open_sockets = MAX_PLAYERS + 2; // controllers plus HTTP/reconnect headroom
    hc.backlog_conn = MAX_PLAYERS;
    hc.close_fn = on_close;
    hc.lru_purge_enable = false; // never evict an active controller to serve a page
    hc.uri_match_fn = httpd_uri_match_wildcard;
    ESP_ERROR_CHECK(httpd_start(&server, &hc));

    httpd_uri_t ws = {.uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .is_websocket = true};
    httpd_uri_t page = {.uri = "/*", .method = HTTP_GET, .handler = page_handler};
    httpd_register_uri_handler(server, &ws);
    httpd_register_uri_handler(server, &page);

    ESP_LOGI(TAG, "AP %s up, http://192.168.4.1", SSID);
}
