#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hub75.h"

// HSV with h in 0..255
static uint16_t hue(uint8_t h)
{
    uint8_t region = h / 43, rem = (h - region * 43) * 6;
    uint8_t q = 255 - rem, t = rem;
    switch (region) {
    case 0: return rgb565(255, t, 0);
    case 1: return rgb565(q, 255, 0);
    case 2: return rgb565(0, 255, t);
    case 3: return rgb565(0, q, 255);
    case 4: return rgb565(t, 0, 255);
    default: return rgb565(255, 0, q);
    }
}

void app_main(void)
{
    hub75_init();

    for (uint8_t frame = 0;; frame++) {
        for (int y = 0; y < HUB75_H; y++)
            for (int x = 0; x < HUB75_W; x++)
                hub75_fb[y][x] = hue(x * 2 + y + frame);

        // white border on the first panel to see which one is x = 0..63
        for (int i = 0; i < 64; i++) {
            hub75_fb[0][i] = hub75_fb[63][i] = 0xffff;
            hub75_fb[i][0] = hub75_fb[i][63] = 0xffff;
        }

        hub75_show(128);
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}
