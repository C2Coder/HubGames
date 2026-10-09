#include "fx.h"

#include "esp_random.h"
#include "gfx.h"

#define MAX_P 160

typedef struct {
    int32_t x, y, vx, vy; // 8.8 fixed point, screen pixels
    uint16_t color;
    uint8_t life;
    uint8_t gravity;
} particle_t;

static particle_t ps[MAX_P];
int fx_scale = 1;

static int rnd(int lo, int hi)
{
    return lo + (int)(esp_random() % (uint32_t)(hi - lo + 1));
}

static particle_t *slot(void)
{
    particle_t *best = &ps[0];
    for (int i = 0; i < MAX_P; i++) {
        if (!ps[i].life)
            return &ps[i];
        if (ps[i].life < best->life)
            best = &ps[i];
    }
    return best; // steal the oldest
}

void fx_burst(int x, int y, uint16_t color, int n)
{
    for (int i = 0; i < n; i++) {
        particle_t *p = slot();
        p->x = (x * fx_scale) << 8;
        p->y = (y * fx_scale) << 8;
        p->vx = rnd(-300, 300) * fx_scale;
        p->vy = rnd(-400, 200) * fx_scale;
        p->color = i % 3 ? color : C_WHITE;
        p->life = rnd(15, 35);
        p->gravity = 1;
    }
}

void fx_confetti(int n)
{
    for (int i = 0; i < n; i++) {
        particle_t *p = slot();
        p->x = rnd(0, HUB75_W - 1) << 8;
        p->y = rnd(-40, 0) << 8;
        p->vx = rnd(-60, 60);
        p->vy = rnd(80, 200);
        p->color = gfx_hsv(esp_random(), 255, 255);
        p->life = 120;
        p->gravity = 0;
    }
}

void fx_clear(void)
{
    for (int i = 0; i < MAX_P; i++)
        ps[i].life = 0;
}

void fx_update_draw(void)
{
    for (int i = 0; i < MAX_P; i++) {
        particle_t *p = &ps[i];
        if (!p->life)
            continue;
        p->life--;
        p->x += p->vx;
        p->y += p->vy;
        p->vy += p->gravity * 25;
        gfx_pixel(p->x >> 8, p->y >> 8, p->color);
    }
}
