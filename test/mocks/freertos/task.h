#pragma once
// Native-test mock for FreeRTOS task API.
// Used by LittleFSTransport (xTaskCreatePinnedToCore / vTaskDelete / vTaskDelay).
//
// xTaskCreatePinnedToCore — always fails (pdFAIL = 0): no real tasks on native.
//   LittleFSTransport::startTask() checks return value and sets taskRunning_=false.
// vTaskDelete / vTaskDelay — no-ops.

#include "FreeRTOS.h"

inline BaseType_t xTaskCreatePinnedToCore(
    TaskFunction_t,   // pvTaskCode
    const char*,      // pcName
    uint32_t,         // usStackDepth
    void*,            // pvParameters
    UBaseType_t,      // uxPriority
    TaskHandle_t*,    // pvCreatedTask
    BaseType_t        // xCoreID
) {
    return pdFAIL;
}

inline void vTaskDelete(TaskHandle_t) {}
inline void vTaskDelay(TickType_t)    {}
