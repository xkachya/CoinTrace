#pragma once
// Native-test mock for FreeRTOS primitives.
// Mutex operations are no-ops — tests verify logic, not concurrency.
#include <stdint.h>

typedef uint32_t TickType_t;
typedef void*    SemaphoreHandle_t;

#define portMAX_DELAY      ((TickType_t)0xFFFFFFFF)
#define pdTRUE             ((int)1)
#define pdFALSE            ((int)0)
#define pdMS_TO_TICKS(ms)  ((TickType_t)(ms))
