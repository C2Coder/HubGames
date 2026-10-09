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
#define BUF_SIZE (HDR_SIZE + sizeof(hub75_fb))

uint16_t hub75_fb[HUB75_H][HUB75_W];

static spi_device_handle_t spi;
static uint8_t *txbuf;

void hub75_init(void)
{
    spi_bus_config_t bus = {
        .data0_io_num = PIN_D0,
        .data1_io_num = PIN_D1,
        .data2_io_num = PIN_D2,
        .data3_io_num = PIN_D3,
        .sclk_io_num = PIN_CLK,
        .max_transfer_sz = BUF_SIZE,
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
    struct disp_mode mode = {
        .magic = MODE_MAGIC,
        .format = FORMAT_RGB565,
        .brightness = brightness,
        .width = HUB75_W,
    };
    memcpy(txbuf, sync_seq, sizeof(sync_seq));
    memcpy(txbuf + sizeof(sync_seq), &mode, sizeof(mode));
    memcpy(txbuf + HDR_SIZE, hub75_fb, sizeof(hub75_fb));

    // The display samples the low nibble of each byte first, the ESP sends
    // the high nibble first, so swap nibbles in every byte.
    uint32_t *w = (uint32_t *)txbuf;
    for (int i = 0; i < BUF_SIZE / 4; i++)
        w[i] = ((w[i] & 0x0f0f0f0f) << 4) | ((w[i] >> 4) & 0x0f0f0f0f);

    spi_transaction_t t = {
        .flags = SPI_TRANS_MODE_QIO,
        .length = BUF_SIZE * 8,
        .tx_buffer = txbuf,
    };
    ESP_ERROR_CHECK(spi_device_transmit(spi, &t));
}
