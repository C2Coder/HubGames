#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define CONFIG_LWIP_MAX_SOCKETS 20
#define CONFIG_LWIP_DHCPS_MAX_STATION_NUM 12
#define CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM 2
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NVS_NO_FREE_PAGES 1
#define ESP_ERR_NVS_NEW_VERSION_FOUND 2
#define ESP_ERROR_CHECK(e) do { if ((e) != ESP_OK) __builtin_trap(); } while (0)
#define HTTP_GET 1
#define HTTP_POST 2
#define HTTPD_WS_TYPE_BINARY 2
#define WIFI_MODE_AP 2
#define WIFI_IF_AP 1
#define WIFI_AUTH_OPEN 0
typedef void *httpd_handle_t;
typedef struct { int method, fd; } httpd_req_t;
typedef struct { uint8_t *payload; size_t len; int type; } httpd_ws_frame_t;
typedef struct { int max_open_sockets, backlog_conn; bool lru_purge_enable; void (*close_fn)(httpd_handle_t,int); void *uri_match_fn; } httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
typedef struct { const char *uri; int method; esp_err_t (*handler)(httpd_req_t *); bool is_websocket; } httpd_uri_t;
typedef struct { int unused; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
typedef struct { struct { char ssid[32]; int ssid_len, channel, max_connection, authmode; } ap; } wifi_config_t;
extern uint8_t fake_frame[3], fake_reply[4];
extern httpd_config_t fake_http;
extern wifi_config_t fake_wifi;
extern int fake_closed;
static inline int esp_event_loop_create_default(void) { return 0; }
static inline int esp_netif_init(void) { return 0; }
static inline void esp_netif_create_default_wifi_ap(void) {}
static inline int esp_wifi_init(wifi_init_config_t *c) { (void)c; return 0; }
static inline int esp_wifi_set_mode(int m) { (void)m; return 0; }
static inline int esp_wifi_set_config(int i, wifi_config_t *c) { (void)i; fake_wifi = *c; return 0; }
static inline int esp_wifi_start(void) { return 0; }
static inline int nvs_flash_init(void) { return 0; }
static inline int nvs_flash_erase(void) { return 0; }
static inline int httpd_req_to_sockfd(httpd_req_t *r) { return r->fd; }
static inline int httpd_ws_recv_frame(httpd_req_t *r, httpd_ws_frame_t *f, size_t max)
{ (void)r; if (max < 3) return -1; memcpy(f->payload, fake_frame, 3); f->len=3; f->type=2; return 0; }
static inline int httpd_ws_send_frame(httpd_req_t *r, httpd_ws_frame_t *f)
{ (void)r; if (f->len != 4) return -1; memcpy(fake_reply, f->payload, 4); return 0; }
static inline int httpd_sess_trigger_close(httpd_handle_t s, int fd) { (void)s; fake_closed = fd; return 0; }
static inline int httpd_resp_set_type(httpd_req_t *r, const char *s) { (void)r; (void)s; return 0; }
static inline int httpd_resp_send(httpd_req_t *r, const char *s, size_t n) { (void)r; (void)s; (void)n; return 0; }
static inline int httpd_start(httpd_handle_t *s, httpd_config_t *c) { *s = (void *)1; fake_http = *c; return 0; }
static inline int httpd_register_uri_handler(httpd_handle_t s, httpd_uri_t *u) { (void)s; (void)u; return 0; }
static void *httpd_uri_match_wildcard;
