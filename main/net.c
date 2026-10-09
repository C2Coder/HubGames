#include "net.h"
#include "input.h"

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"

#define SSID       "HubGames"
#define DEADZONE   40
#define TIMEOUT_MS 2000

static const char *TAG = "net";

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

typedef struct {
    int fd; // -1 = free
    uint8_t btn;
    int8_t jx, jy;
    TickType_t seen;
} slot_t;

static slot_t slots[MAX_PLAYERS] = {{.fd = -1}, {.fd = -1}, {.fd = -1}, {.fd = -1}};
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

void input_get(int p, input_t *in)
{
    static uint8_t prev[MAX_PLAYERS];

    portENTER_CRITICAL(&lock);
    slot_t *s = &slots[p];
    if (s->fd >= 0 && xTaskGetTickCount() - s->seen > pdMS_TO_TICKS(TIMEOUT_MS))
        s->fd = -1;
    bool conn = s->fd >= 0;
    uint8_t b = conn ? s->btn : 0;
    int jx = conn ? s->jx : 0, jy = conn ? s->jy : 0;
    portEXIT_CRITICAL(&lock);

    bool strafe = b & BTN_STRAFE_MODE;
    b &= ~BTN_STRAFE_MODE;

    // joystick gives only the dominant direction, in strafe mode only up/down
    if (!strafe && abs(jx) > abs(jy)) {
        if (jx > DEADZONE) b |= BTN_RIGHT;
        if (jx < -DEADZONE) b |= BTN_LEFT;
    } else {
        if (jy > DEADZONE) b |= BTN_DOWN;
        if (jy < -DEADZONE) b |= BTN_UP;
    }

    in->held = b;
    in->pressed = b & ~prev[p];
    in->jx = jx;
    in->jy = jy;
    in->strafe = strafe;
    in->connected = conn;
    prev[p] = b;
}

// Frame from the page: [buttons, jx, jy], reply: [player number, 0 = full]
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
        slots[p] = (slot_t){
            .fd = fd,
            .btn = buf[0],
            .jx = (int8_t)buf[1],
            .jy = (int8_t)buf[2],
            .seen = xTaskGetTickCount(),
        };
    }
    portEXIT_CRITICAL(&lock);

    uint8_t reply = p + 1;
    httpd_ws_frame_t out = {.type = HTTPD_WS_TYPE_BINARY, .payload = &reply, .len = 1};
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
            .max_connection = 8,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    httpd_handle_t server;
    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.close_fn = on_close;
    hc.lru_purge_enable = true;
    hc.uri_match_fn = httpd_uri_match_wildcard;
    ESP_ERROR_CHECK(httpd_start(&server, &hc));

    httpd_uri_t ws = {.uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .is_websocket = true};
    httpd_uri_t page = {.uri = "/*", .method = HTTP_GET, .handler = page_handler};
    httpd_register_uri_handler(server, &ws);
    httpd_register_uri_handler(server, &page);

    ESP_LOGI(TAG, "AP %s up, http://192.168.4.1", SSID);
}
