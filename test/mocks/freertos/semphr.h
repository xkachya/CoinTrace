#pragma once
#include "FreeRTOS.h"

// Mock: all mutex ops succeed instantly (no real scheduling needed for unit tests)
inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    static int sentinel = 1;
    return (SemaphoreHandle_t)&sentinel;
}
inline int  xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
inline void xSemaphoreGive(SemaphoreHandle_t)             {}
inline void vSemaphoreDelete(SemaphoreHandle_t)           {}
