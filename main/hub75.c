#include "hub75.h"

#include <string.h>
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

// Saturn J6 -> RP-HUB75 connector
#define PIN_D0  38
#define PIN_D1  39
#define PIN_D2  41
#define PIN_D3  45
#define PIN_CS  1
#define PIN_CLK 3

#define SPI_HZ (20 * 1000 * 1000)

// Protocol from RP-HUB75/sw/fw/src/proto.h
static const uint16_t sync_seq[16] = {0xac92, 0x3bca, 0x41bf, 0x393d, 0xa74a, 0xae01, 0x155d, 0xfb70,
                                      0xf681, 0x2f6d, 0x4931, 0x0fa3, 0x77bf, 0xd756, 0x26f9, 0x4eb6};

struct disp_mode {
    uint8_t magic;
    uint8_t flags;
    uint8_t format;
    uint8_t brightness;
    uint16_t width;
    uint16_t padding;
};

#define MODE_MAGIC    0xfb
#define FORMAT_RGB565 7

#define HDR_SIZE (sizeof(sync_seq) + sizeof(struct disp_mode))
#define MAX_FRAME (4 * 64 * 64 * 2)
#define BUF_SIZE (HDR_SIZE + MAX_FRAME)
#define MAX_TRANS 16384 // bytes per SPI transaction, below the S3 DMA limit

// Logical panel (left to right, top to bottom) -> position in the chain and
// whether it is mounted upside down.
static const struct {
    uint8_t chain;
    uint8_t rot180;
} panel_map[4] = {{0, 0}, {1, 0}, {2, 0}, {3, 0}};

uint16_t hub75_fb[HUB75_H][HUB75_W];
int screen_h = 64;
int hub75_panels = 2;

static spi_device_handle_t spi;
static uint8_t *txbuf;

void hub75_set_panels(int panels)
{
    hub75_panels = panels == 4 ? 4 : 2;
    screen_h = hub75_panels == 4 ? 128 : 64;
}

void hub75_init(void)
{
    spi_bus_config_t bus = {
        .data0_io_num = PIN_D0,
        .data1_io_num = PIN_D1,
        .data2_io_num = PIN_D2,
        .data3_io_num = PIN_D3,
        .sclk_io_num = PIN_CLK,
        .max_transfer_sz = MAX_TRANS,
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev = {
        .mode = 0,
        .clock_speed_hz = SPI_HZ,
        .spics_io_num = PIN_CS,
        .flags = SPI_DEVICE_HALFDUPLEX,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &spi));

    txbuf = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA);
    assert(txbuf);
}

void hub75_show(uint8_t brightness)
{
    int chain_w = 64 * hub75_panels;
    struct disp_mode mode = {
        .magic = MODE_MAGIC,
        .format = FORMAT_RGB565,
        .brightness = brightness,
        .width = chain_w,
    };
    memcpy(txbuf, sync_seq, sizeof(sync_seq));
    memcpy(txbuf + sizeof(sync_seq), &mode, sizeof(mode));

    // copy each 64x64 panel of the screen to its place in the chain
    uint16_t *frame = (uint16_t *)(txbuf + HDR_SIZE);
    for (int p = 0; p < hub75_panels; p++) {
        int sx = (p % 2) * 64, sy = (p / 2) * 64;
        uint16_t *dst = frame + panel_map[p].chain * 64;
        for (int y = 0; y < 64; y++) {
            uint16_t *row = dst + y * chain_w;
            if (panel_map[p].rot180) {
                const uint16_t *src = &hub75_fb[sy + 63 - y][sx + 63];
                for (int x = 0; x < 64; x++)
                    row[x] = *src--;
            } else {
                memcpy(row, &hub75_fb[sy + y][sx], 64 * 2);
            }
        }
    }
    int size = HDR_SIZE + chain_w * 64 * 2;

    // The display samples the low nibble of each byte first, the ESP sends
    // the high nibble first, so swap nibbles in every byte.
    uint32_t *w = (uint32_t *)txbuf;
    for (int i = 0; i < size / 4; i++)
        w[i] = ((w[i] & 0x0f0f0f0f) << 4) | ((w[i] >> 4) & 0x0f0f0f0f);

    // split into transactions, the display is fine with CS toggles on 4 byte boundaries
    for (int off = 0; off < size; off += MAX_TRANS) {
        int len = size - off < MAX_TRANS ? size - off : MAX_TRANS;
        spi_transaction_t t = {
            .flags = SPI_TRANS_MODE_QIO,
            .length = len * 8,
            .tx_buffer = txbuf + off,
        };
        ESP_ERROR_CHECK(spi_device_transmit(spi, &t));
    }
}
