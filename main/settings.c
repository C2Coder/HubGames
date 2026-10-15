#include "settings.h"

#include "nvs.h"

uint8_t set_panels = 2;
uint8_t set_brightness = 128;
uint8_t set_show = 0;

void settings_load(void)
{
    nvs_handle_t h;
    if (nvs_open("hubgames", NVS_READONLY, &h) != ESP_OK)
        return;
    nvs_get_u8(h, "panels", &set_panels);
    nvs_get_u8(h, "bright", &set_brightness);
    nvs_get_u8(h, "show", &set_show);
    nvs_close(h);
    if (set_panels != 4)
        set_panels = 2;
    if (set_brightness < 16)
        set_brightness = 16;
}

void settings_save(void)
{
    nvs_handle_t h;
    if (nvs_open("hubgames", NVS_READWRITE, &h) != ESP_OK)
        return;
    nvs_set_u8(h, "panels", set_panels);
    nvs_set_u8(h, "bright", set_brightness);
    nvs_set_u8(h, "show", set_show);
    nvs_commit(h);
    nvs_close(h);
}
