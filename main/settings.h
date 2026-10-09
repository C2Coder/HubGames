// Settings saved in NVS (needs nvs_flash_init, done in net_init).
#pragma once

#include <stdint.h>

extern uint8_t set_panels;     // 2 or 4
extern uint8_t set_brightness; // 16..255

void settings_load(void);
void settings_save(void);
