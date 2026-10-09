#include "game.h"
#include "settings.h"
#include "net.h"
#include "doom.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>
uint16_t hub75_fb[HUB75_H][HUB75_W];
int screen_h = 64;
int hub75_panels = 2;
uint8_t set_panels = 2, set_brightness = 128;
static uint32_t rng = 42;
uint32_t esp_random(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
#ifndef TEST_NETWORK
TickType_t xTaskGetTickCount(void) { return 0; }
#endif
void vTaskDelay(TickType_t d) { (void)d; }
void vTaskDelayUntil(TickType_t *t, TickType_t d) { *t += d; }
void xTaskCreatePinnedToCore(void (*f)(void *), const char *n, unsigned stack, void *p, unsigned pri, void *out, int c)
{ (void)f; (void)n; (void)stack; (void)p; (void)pri; (void)out; (void)c; }
int64_t esp_timer_get_time(void) { struct timespec ts; timespec_get(&ts, TIME_UTC); return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000; }
void hub75_init(void) {}
void hub75_show(uint8_t b) { (void)b; }
void hub75_set_panels(int n) { hub75_panels = n; screen_h = n == 4 ? 128 : 64; }
void settings_load(void) {}
void settings_save(void) {}
#ifndef TEST_NETWORK
void net_init(void) {}
void net_set_controller(controller_profile_t p) { (void)p; }
void input_get(int p, input_t *in) { (void)p; memset(in, 0, sizeof(*in)); }
#endif
uint32_t esp_get_free_heap_size(void) { return 0; }
uint32_t esp_get_minimum_free_heap_size(void) { return 0; }
bool doom_available(void) { return false; }
bool doom_run(void) { return false; }
