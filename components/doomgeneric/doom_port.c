#include "doom_port.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include "doomgeneric/doomgeneric/doomgeneric.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

// The WAD partition is memory mapped and served through a tiny read-only VFS
// at /wad, so Doom's normal stdio file code works unchanged. Mapped reads
// don't disable the cache, which matters because the Doom stack is in PSRAM.

static const esp_partition_t *part;
static const uint8_t *wad;
static uint32_t wad_size, pos;
static doom_hooks_t hooks;

static uint32_t rd32(const uint8_t *b)
{
    return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24;
}

bool doom_wad_ok(void)
{
    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "doomwad");
    if (!part)
        return false;
    uint8_t h[12];
    if (esp_partition_read(part, 0, h, sizeof(h)) != ESP_OK || memcmp(h, "IWAD", 4))
        return false;
    // the lump directory is at the end of the file
    wad_size = rd32(h + 8) + rd32(h + 4) * 16;
    return wad_size <= part->size;
}

static bool wad_map(void)
{
    esp_partition_mmap_handle_t handle;
    const void *ptr;
    if (esp_partition_mmap(part, 0, wad_size, ESP_PARTITION_MMAP_DATA, &ptr, &handle) != ESP_OK)
        return false;
    wad = ptr;
    return true;
}

static int wad_open(const char *path, int flags, int mode)
{
    if (strcmp(path, "/doom1.wad") || (flags & O_ACCMODE) != O_RDONLY) {
        errno = ENOENT;
        return -1;
    }
    pos = 0;
    return 0;
}

static ssize_t wad_read(int fd, void *dst, size_t size)
{
    if (pos >= wad_size)
        return 0;
    if (size > wad_size - pos)
        size = wad_size - pos;
    memcpy(dst, wad + pos, size);
    pos += size;
    return size;
}

static off_t wad_lseek(int fd, off_t off, int whence)
{
    if (whence == SEEK_SET)
        pos = off;
    else if (whence == SEEK_CUR)
        pos += off;
    else
        pos = wad_size + off;
    return pos;
}

static int wad_close(int fd)
{
    return 0;
}

static int wad_fstat(int fd, struct stat *st)
{
    memset(st, 0, sizeof(*st));
    st->st_size = wad_size;
    st->st_mode = S_IFREG;
    return 0;
}

static void doom_task(void *arg)
{
    static char *argv[] = {"doom", "-iwad", "/wad/doom1.wad", NULL};
    ESP_LOGI("doom", "starting, wad %u bytes", (unsigned)wad_size);
    doomgeneric_Create(3, argv);
    while (1)
        doomgeneric_Tick();
}

void doom_start(const doom_hooks_t *h)
{
    hooks = *h;
    if (!wad_map()) {
        ESP_LOGE("doom", "wad mmap failed");
        return;
    }
    esp_vfs_t vfs = {
        .flags = ESP_VFS_FLAG_DEFAULT,
        .open = wad_open,
        .read = wad_read,
        .lseek = wad_lseek,
        .close = wad_close,
        .fstat = wad_fstat,
    };
    ESP_ERROR_CHECK(esp_vfs_register("/wad", &vfs, NULL));
    // big stack in PSRAM, internal RAM is tight
    if (xTaskCreatePinnedToCoreWithCaps(doom_task, "doom", 65536, NULL, 5, NULL, 1, MALLOC_CAP_SPIRAM) != pdPASS)
        ESP_LOGE("doom", "task create failed");
}

// ---- doomgeneric platform ----

void DG_Init(void)
{
}

void DG_DrawFrame(void)
{
    hooks.draw(DG_ScreenBuffer);
}

void DG_SleepMs(uint32_t ms)
{
    vTaskDelay(ms < portTICK_PERIOD_MS ? 1 : pdMS_TO_TICKS(ms));
}

uint32_t DG_GetTicksMs(void)
{
    return esp_timer_get_time() / 1000;
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    return hooks.get_key(pressed, key);
}

void DG_SetWindowTitle(const char *title)
{
}
