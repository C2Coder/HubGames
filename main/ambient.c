// Background animations: dim plasma behind menus/games and the attract show.
// Integer math and lookup tables per pixel, tables are built on first use.
#include "ambient.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>
#include "esp_random.h"
#include "gfx.h"

#define W     HUB75_W
#define SLOT  600 // frames per effect (12 s)
#define FADE  40  // fade in/out frames
#define N_FX  5

static int8_t sin8[256];        // -127..127, full circle in 256
static uint16_t hue_pal[256];   // rgb565, full saturation and value
static uint16_t fire_pal[256];
static uint16_t fall_pal[256];
static bool ready;
static uint32_t rng_s = 2463534242u;

static inline uint32_t rnd(void)
{
    rng_s ^= rng_s << 13;
    rng_s ^= rng_s >> 17;
    rng_s ^= rng_s << 5;
    return rng_s;
}

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// scale an rgb565 color, v 0..255
static inline uint16_t shade(uint16_t c, int v)
{
    int r = ((c >> 11) * v) >> 8, g = (((c >> 5) & 63) * v) >> 8, b = ((c & 31) * v) >> 8;
    return (r << 11) | (g << 5) | b;
}

static inline void put(int x, int y, uint16_t c)
{
    if ((unsigned)x < W && (unsigned)y < (unsigned)screen_h)
        hub75_fb[y][x] = c;
}

static void init_tables(void)
{
    ready = true;
    rng_s ^= esp_random() | 1;
    for (int i = 0; i < 256; i++) {
        sin8[i] = (int8_t)lroundf(127.0f * sinf(i * 6.2831853f / 256.0f));
        hue_pal[i] = gfx_hsv(i, 255, 255);
        // fire: black - red - orange - yellow - white
        fire_pal[i] = rgb565(clampi(i * 5 / 2, 0, 255), clampi((i - 90) * 2, 0, 255), clampi((i - 200) * 4, 0, 255));
        // matrix rain: dark to bright green
        fall_pal[i] = rgb565(i * i / 1020, i, i * i / 700);
    }
}

// ---------------------------------------------------------------- plasma

static const uint8_t bayer[4][4] = {
    {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5},
};

// mode 0: calm and dim-friendly, mode 1: show (faster, full color range)
static void plasma(uint32_t t, uint8_t level, int mode)
{
    int h = screen_h;
    int ts = mode ? t : t / 2;
    uint16_t pal[256][3]; // 8.8 fixed point per channel, already scaled by level
    int p1 = ts / 3, p2 = 85 + ts / 4, p3 = 170 + ts / 5;
    for (int i = 0; i < 256; i++) {
        int r, g, b;
        if (mode) {
            r = 128 + sin8[(i + p1) & 255]; // 1..255
            g = 128 + sin8[(i + p2) & 255];
            b = 128 + sin8[(i + p3) & 255];
        } else { // soft colors with a floor so they don't go black
            r = 60 + ((sin8[(i + p1) & 255] * 90) >> 7) + 90;
            g = 60 + ((sin8[(i + p2) & 255] * 90) >> 7) + 90;
            b = 60 + ((sin8[(i + p3) & 255] * 90) >> 7) + 90;
        }
        pal[i][0] = r * level;
        pal[i][1] = g * level;
        pal[i][2] = b * level;
    }
    int8_t ax[W], bx[W];
    for (int x = 0; x < W; x++) {
        ax[x] = sin8[(x * 5 + ts * 2) & 255];
        bx[x] = sin8[(x * 3 - ts) & 255];
    }
    int cx = mode ? 4 : 3;
    for (int y = 0; y < h; y++) {
        int by = sin8[(y * 6 + ts) & 255];
        int cy = sin8[(y * 4 + ts * 3 / 2) & 255];
        uint16_t *row = hub75_fb[y];
        for (int x = 0; x < W; x++) {
            int d = sin8[((x + y) * cx + ts * 2 + 64) & 255];
            int idx = (ax[x] + by + bx[x] / 2 + cy / 2 + d) >> 1;
            const uint16_t *p = pal[idx & 255];
            int dz = bayer[y & 3][x & 3]; // ordered dither keeps low levels smooth
            int r = (p[0] + dz * 128) >> 11, g = (p[1] + dz * 64) >> 10, b = (p[2] + dz * 128) >> 11;
            r -= r >> 5;
            g -= g >> 6;
            b -= b >> 5;
            row[x] = (r << 11) | (g << 5) | b;
        }
    }
}

void ambient_plasma(uint32_t t, uint8_t level)
{
    if (!ready) init_tables();
    plasma(t, level, 0);
}

// ---------------------------------------------------------------- state shared by the show effects

typedef struct { int16_t x, y, z; uint8_t hue; } star_t;
typedef struct { int16_t x, y; int8_t vx, vy; uint8_t life, hue, kind; } spark_t;
typedef struct { int16_t head; uint8_t speed, len; } drop_t;

#define N_STARS  300
#define N_SPARKS 600
#define N_DROPS  (W / 3)
#define LIFE_W   64
#define LIFE_H   64

static union {
    uint8_t heat[HUB75_H][W]; // fire
    star_t stars[N_STARS];
    drop_t drops[N_DROPS];
    struct {
        uint8_t a[2][LIFE_H][LIFE_W];
        uint8_t glow[LIFE_H][LIFE_W];
        uint8_t hue[LIFE_H][LIFE_W];
        int cur, hist[8], hn;
    } life;
    struct {
        int32_t dx2[5][W];
        int bx[5], by[5];
    } ball;
    struct {
        spark_t p[N_SPARKS];
        int next;
    } fw;
} st;

// ---------------------------------------------------------------- fire

static void fx_fire(uint32_t t, bool fresh)
{
    int h = screen_h;
    int dmax = 384 / h - 1; // flames reach about 3/4 of the height
    if (fresh) memset(st.heat, 0, sizeof(st.heat));
    for (int x = 0; x < W; x++) {
        int v = 255 - (int)(rnd() % 70) - ((sin8[(x * 9 + t * 4) & 255] + 127) >> 3) - ((sin8[(x * 5 - t * 3) & 255] + 127) >> 3);
        st.heat[h - 1][x] = v;
    }
    for (int y = 0; y < h - 1; y++) {
        int y2 = y + 2 < h ? y + 2 : y + 1;
        for (int x = 0; x < W; x++) {
            uint32_t r = rnd();
            int xa = x + (int)(r & 3) - 1, xb = x + (int)((r >> 2) & 3) - 1;
            xa = clampi(xa, 0, W - 1);
            xb = clampi(xb, 0, W - 1);
            int v = (st.heat[y + 1][xa] + st.heat[y + 1][xb] + st.heat[y + 1][x] + st.heat[y2][x]) >> 2;
            v -= 1 + (int)((r >> 8) % dmax);
            st.heat[y][x] = v < 0 ? 0 : v;
        }
    }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < W; x++)
            hub75_fb[y][x] = fire_pal[st.heat[y][x]];
}

// ---------------------------------------------------------------- starfield

static void star_new(star_t *s, bool far)
{
    s->x = (int)(rnd() % 800) - 400;
    s->y = (int)(rnd() % 800) - 400;
    s->z = far ? 300 + rnd() % 212 : 20 + rnd() % 490;
    s->hue = rnd() & 255;
}

static void fx_stars(uint32_t t, bool fresh)
{
    int h = screen_h, cy = h / 2;
    if (fresh)
        for (int i = 0; i < N_STARS; i++) star_new(&st.stars[i], false);
    memset(hub75_fb, 0, sizeof(uint16_t) * W * h);
    int speed = 8 + (sin8[(t >> 1) & 255] >> 5); // 4..12
    for (int i = 0; i < N_STARS; i++) {
        star_t *s = &st.stars[i];
        s->z -= speed;
        if (s->z < 8) { star_new(s, true); continue; }
        int sx = 64 + s->x * 64 / s->z, sy = cy + s->y * 64 / s->z;
        if ((unsigned)sx >= W || (unsigned)sy >= (unsigned)h) { star_new(s, true); continue; }
        int v = clampi(480 - s->z * 7 / 10, 90, 255);
        uint16_t hc = hue_pal[s->hue]; // tint toward white
        int r = ((hc >> 11) * 3 + 31 * 5) >> 3, g = (((hc >> 5) & 63) * 3 + 63 * 5) >> 3, b = ((hc & 31) * 3 + 31 * 5) >> 3;
        uint16_t c = shade((r << 11) | (g << 5) | b, v);
        put(sx, sy, c);
        if (s->z < 220) { // near stars are bigger
            put(sx + 1, sy, shade(c, 200));
            put(sx, sy + 1, shade(c, 200));
            put(sx + 1, sy + 1, shade(c, 160));
        }
        for (int k = 1; k <= 3 && s->z < 300; k++) { // trail toward the center
            int z2 = s->z + k * speed * 2;
            put(64 + s->x * 64 / z2, cy + s->y * 64 / z2, shade(c, 160 / (k + 1)));
        }
    }
}

// ---------------------------------------------------------------- matrix rain

static void drop_reset(drop_t *d)
{
    d->len = 6 + rnd() % 18;
    d->speed = 6 + rnd() % 14; // 1/16 cell per frame
    d->head = -(int)(rnd() % 300);
}

static void fx_matrix(uint32_t t, bool fresh)
{
    int h = screen_h, rows = h / 4;
    if (fresh)
        for (int i = 0; i < N_DROPS; i++) {
            drop_reset(&st.drops[i]);
            st.drops[i].head = (int)(rnd() % (rows * 16 + 300)) - 300;
        }
    memset(hub75_fb, 0, sizeof(uint16_t) * W * h);
    for (int c = 0; c < N_DROPS; c++) {
        drop_t *d = &st.drops[c];
        d->head += d->speed;
        int hc = d->head >> 4;
        if (hc - d->len > rows) { drop_reset(d); continue; }
        for (int k = 0; k < d->len; k++) {
            int row = hc - k;
            if (row < 0 || row >= rows) continue;
            uint32_t hh = (c * 2654435761u) ^ (row * 40503u);
            if ((hh >> 29) == 0) hh += (t >> 4) * 977u; // some glyphs change
            hh *= 2246822519u;
            hh ^= hh >> 15;
            uint16_t col = k == 0 ? rgb565(220, 255, 220) : fall_pal[255 - k * 235 / d->len];
            int px = c * 3, py = row * 4;
            for (int b = 0; b < 6; b++)
                if (hh & (1u << b)) put(px + (b & 1), py + (b >> 1), col);
        }
    }
}

// ---------------------------------------------------------------- life

static void life_seed(int lw, int lh)
{
    memset(&st.life, 0, sizeof(st.life));
    for (int y = 0; y < lh; y++)
        for (int x = 0; x < lw; x++) {
            st.life.a[0][y][x] = (rnd() % 100) < 30;
            st.life.hue[y][x] = (x + y) * 2;
        }
}

static void life_step(int lw, int lh)
{
    int c = st.life.cur, n = c ^ 1, pop = 0;
    for (int y = 0; y < lh; y++) {
        int y0 = (y + lh - 1) % lh, y1 = (y + 1) % lh;
        for (int x = 0; x < lw; x++) {
            int x0 = (x + lw - 1) % lw, x1 = (x + 1) % lw;
            int s = st.life.a[c][y0][x0] + st.life.a[c][y0][x] + st.life.a[c][y0][x1] +
                    st.life.a[c][y][x0] + st.life.a[c][y][x1] +
                    st.life.a[c][y1][x0] + st.life.a[c][y1][x] + st.life.a[c][y1][x1];
            int alive = st.life.a[c][y][x];
            int nv = (s == 3) || (alive && s == 2);
            st.life.a[n][y][x] = nv;
            if (nv) {
                pop++;
                if (!alive) st.life.hue[y][x] = (x + y) * 2; // newborn color by position
                st.life.glow[y][x] = 255;
            }
        }
    }
    st.life.cur = n;
    // stagnant: population repeats with period 1..4 for a while, or nearly dead
    int hn = st.life.hn++ & 7;
    st.life.hist[hn] = pop;
    bool stuck = pop < 12;
    if (!stuck && st.life.hn > 12) {
        for (int p = 1; p <= 4 && !stuck; p++) {
            bool same = true;
            for (int k = 0; k < 8; k++)
                if (st.life.hist[(hn - k + 16) & 7] != st.life.hist[(hn - k - p + 16) & 7]) same = false;
            stuck = same;
        }
    }
    if (stuck) { // sprinkle new cells instead of a hard reset
        for (int i = 0; i < lw * lh / 3; i++)
            st.life.a[n][rnd() % lh][rnd() % lw] = 1;
        st.life.hn = 0;
        memset(st.life.hist, 0, sizeof(st.life.hist));
    }
}

static void fx_life(uint32_t t, bool fresh)
{
    int lw = LIFE_W, lh = screen_h / 2;
    if (fresh) life_seed(lw, lh);
    if (t % 5 == 0) life_step(lw, lh);
    int c = st.life.cur;
    for (int y = 0; y < lh; y++)
        for (int x = 0; x < lw; x++) {
            int g = st.life.glow[y][x];
            if (!st.life.a[c][y][x]) {
                g = g * 15 >> 4;
                st.life.glow[y][x] = g;
            }
            uint16_t col = shade(hue_pal[(st.life.hue[y][x] + (t >> 2)) & 255], g);
            uint16_t *p = &hub75_fb[y * 2][x * 2];
            p[0] = p[1] = p[W] = p[W + 1] = col;
        }
}

// ---------------------------------------------------------------- fireworks

// positions in 1/16 pixel, kind 0 spark, 1 rocket, 2 rocket trail
static void spark_add(int x, int y, int vx, int vy, int life, int hue, int kind)
{
    spark_t *s = &st.fw.p[st.fw.next];
    st.fw.next = (st.fw.next + 1) % N_SPARKS;
    s->x = x; s->y = y; s->vx = vx; s->vy = vy; s->life = life; s->hue = hue; s->kind = kind;
}

static void fx_fireworks(uint32_t t, bool fresh)
{
    int h = screen_h;
    if (fresh) memset(&st.fw, 0, sizeof(st.fw));
    if (rnd() % 28 == 0) {
        int v0 = 16 + h / 8 + (int)(rnd() % 8);
        spark_add((20 + rnd() % 88) * 16, (h - 1) * 16, (int)(rnd() % 7) - 3, -v0, 2 * v0 - (int)(rnd() % 16), rnd() & 255, 1);
    }
    memset(hub75_fb, 0, sizeof(uint16_t) * W * h);
    for (int i = 0; i < N_SPARKS; i++) {
        spark_t *s = &st.fw.p[i];
        if (!s->life) continue;
        s->life--;
        s->x += s->vx;
        s->y += s->vy;
        if (s->kind == 1) {
            s->vy += (t & 1);
            if (!s->life) { // burst
                int n = 100 + rnd() % 40, sp = 22 + rnd() % 14, hue = s->hue, two = rnd() & 1;
                for (int k = 0; k < n; k++) {
                    int a = rnd() & 255, v = sp * (50 + rnd() % 78) / 127;
                    if (two && (k & 1)) v = v * 6 / 10;
                    spark_add(s->x, s->y, sin8[(a + 64) & 255] * v >> 7, sin8[a] * v >> 7, 45 + rnd() % 35,
                              two && (k & 1) ? hue + 100 : hue + (rnd() & 15), 0);
                }
                continue;
            }
            put(s->x >> 4, s->y >> 4, rgb565(255, 220, 160));
            spark_add(s->x, s->y, (int)(rnd() % 5) - 2, 2, 8 + rnd() % 8, 20, 2);
            continue;
        }
        if (s->kind == 0) {
            s->vx -= s->vx >> 4;
            s->vy -= s->vy >> 4;
            if (t & 1) s->vy++;
        }
        int x = s->x >> 4, y = s->y >> 4;
        int v = clampi(s->kind == 2 ? s->life * 14 : s->life * 8, 0, 255);
        uint16_t c = s->kind == 2 ? shade(rgb565(255, 140, 40), v) : shade(gfx_hsv(s->hue, s->life > 20 ? 255 : 200, 255), v);
        put(x, y, c);
        if (s->kind == 0 && s->life > 10) {
            put(x - (s->vx >> 3), y - (s->vy >> 3), shade(c, 110));
            if (s->life > 25) { put(x + 1, y, shade(c, 140)); put(x, y + 1, shade(c, 140)); }
        }
    }
}

// ---------------------------------------------------------------- show

static void fade_fb(int f) // f 0..256
{
    int n = W * screen_h;
    uint16_t *p = &hub75_fb[0][0];
    for (int i = 0; i < n; i++) {
        uint16_t c = p[i];
        p[i] = (((c >> 11) * f >> 8) << 11) | ((((c >> 5) & 63) * f >> 8) << 5) | ((c & 31) * f >> 8);
    }
}

void ambient_show(uint32_t t)
{
    static int cur = -1, last_h;
    static const uint8_t order[N_FX] = {5, 2, 1, 3, 7};
    if (!ready) init_tables();
    int slot = (t / SLOT) % N_FX;
    uint32_t lt = t % SLOT;
    bool fresh = (slot != cur) || (screen_h != last_h);
    cur = slot;
    last_h = screen_h;
    switch (order[slot]) {
    case 1: fx_fire(t, fresh); break;
    case 2: fx_stars(t, fresh); break;
    case 3: fx_matrix(t, fresh); break;
    case 5: fx_life(t, fresh); break;
    default: fx_fireworks(t, fresh); break;
    }
    if (lt < FADE) fade_fb(lt * 256 / FADE);
    else if (lt >= SLOT - FADE) fade_fb((SLOT - 1 - lt) * 256 / FADE);
}
