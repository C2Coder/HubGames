#include <assert.h>
#include <stdio.h>
#include "../main/net.c"
uint8_t fake_frame[3], fake_reply[4];
httpd_config_t fake_http;
wifi_config_t fake_wifi;
int fake_closed = -1;
static TickType_t now;
TickType_t xTaskGetTickCount(void) { return now; }
const char index_html_start[] = "", index_html_end[] = "";
const uint16_t player_color[MAX_PLAYERS] = {1,2,3,4,5,6,7,8,9,10,11,0xf498};
static void packet(int fd, uint8_t held)
{
    fake_frame[0] = held; fake_frame[1] = 80; fake_frame[2] = 0;
    httpd_req_t req = {.method=HTTP_POST,.fd=fd};
    assert(ws_handler(&req) == ESP_OK);
}
int main(void)
{
    input_t input;
    net_init();
    assert(fake_wifi.ap.max_connection == 12);
    assert(fake_http.max_open_sockets >= 14 && !fake_http.lru_purge_enable);
    for (int p = 0; p < MAX_PLAYERS; p++) {
        input_get(p, &input); assert(!input.connected);
        packet(100 + p, 0); assert(fake_reply[0] == p + 1);
    }
    packet(150, 0); assert(fake_reply[0] == 0); // thirteenth controller is full
    net_set_controller(CONTROL_FPS);
    packet(111, BTN_A); assert(fake_reply[0] == 12 && fake_reply[1] == CONTROL_FPS);
    assert((fake_reply[2] | (fake_reply[3] << 8)) == player_color[11]);
    input_get(11, &input);
    assert(input.connected && (input.pressed & BTN_A) && (input.held & BTN_RIGHT));
    input_get(11, &input); assert(!input.pressed);
    on_close(server, 111); input_get(11, &input); assert(!input.connected);
    packet(151, BTN_A); assert(fake_reply[0] == 12);
    input_get(11, &input); assert(input.pressed & BTN_A); // new player cannot inherit old edges
    now = TIMEOUT_MS + 1;
    input_get(11, &input); assert(!input.connected && fake_closed == 151);
    puts("Twelve controller slots, colors, reconnect and stale sockets passed");
}
