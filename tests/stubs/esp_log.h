#pragma once
#include <stdio.h>
#define ESP_LOGI(tag, ...) do { (void)(tag); if (0) fprintf(stderr, __VA_ARGS__); } while (0)
