// Doom on HubGames: main provides drawing and input, Doom runs in its own task.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Doom key codes (from doomkeys.h)
#define DOOM_KEY_RIGHTARROW 0xae
#define DOOM_KEY_LEFTARROW  0xac
#define DOOM_KEY_UPARROW    0xad
#define DOOM_KEY_DOWNARROW  0xaf
#define DOOM_KEY_STRAFE_L   0xa0
#define DOOM_KEY_STRAFE_R   0xa1
#define DOOM_KEY_USE        0xa2
#define DOOM_KEY_FIRE       0xa3
#define DOOM_KEY_ESCAPE     27
#define DOOM_KEY_ENTER      13
#define DOOM_KEY_RSHIFT     (0x80 + 0x36)

typedef struct {
    void (*draw)(const uint32_t *fb); // 320x200 XRGB8888
    int (*get_key)(int *pressed, unsigned char *key); // 1 if an event was returned
} doom_hooks_t;

bool doom_wad_ok(void);                   // doom1.wad flashed to the doomwad partition
void doom_start(const doom_hooks_t *hooks); // starts the Doom task, quitting Doom reboots
