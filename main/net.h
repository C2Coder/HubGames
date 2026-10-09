#pragma once

// Open WiFi AP "HubGames", controller page at http://192.168.4.1
void net_init(void);

// The reply includes a controller profile so phones can label A/B for each game.
typedef enum { CONTROL_DEFAULT, CONTROL_BOMBER, CONTROL_TRON, CONTROL_RACERS,
               CONTROL_FOOTBALL, CONTROL_TOWERS, CONTROL_FPS } controller_profile_t;
void net_set_controller(controller_profile_t profile);
