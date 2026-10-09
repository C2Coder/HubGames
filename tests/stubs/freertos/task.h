#pragma once
#include "FreeRTOS.h"
TickType_t xTaskGetTickCount(void);
void vTaskDelayUntil(TickType_t *, TickType_t);
void xTaskCreatePinnedToCore(void (*)(void *), const char *, unsigned, void *, unsigned, void *, int);
