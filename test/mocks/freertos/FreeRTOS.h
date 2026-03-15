#pragma once
// Native-test mock for FreeRTOS primitives.
// Mutex/queue/task operations are no-ops — tests verify logic, not concurrency.
#include <stdint.h>

typedef uint32_t  TickType_t;
typedef int       BaseType_t;
typedef uint32_t  UBaseType_t;
typedef void*     SemaphoreHandle_t;
typedef void*     QueueHandle_t;
typedef void*     TaskHandle_t;
typedef void (*TaskFunction_t)(void*);

#define portMAX_DELAY      ((TickType_t)0xFFFFFFFF)
#define pdTRUE             ((BaseType_t)1)
#define pdFALSE            ((BaseType_t)0)
#define pdPASS             pdTRUE
#define pdFAIL             pdFALSE
#define pdMS_TO_TICKS(ms)  ((TickType_t)(ms))
