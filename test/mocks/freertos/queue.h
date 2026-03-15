#pragma once
// Native-test mock for FreeRTOS queue API.
// Used by LittleFSTransport (xQueueCreate / xQueueSend / xQueueReceive).
//
// xQueueCreate  — returns a non-null sentinel so queue_ != nullptr checks pass
// xQueueSend    — always fails (pdFALSE): no real queue on native
// xQueueReceive — always returns pdFALSE (empty queue)
// vQueueDelete  — no-op

#include "FreeRTOS.h"

// Sentinel value for a "created but empty" queue
static int g_mock_queue_sentinel = 0;

inline QueueHandle_t xQueueCreate(uint32_t, uint32_t) {
    return (QueueHandle_t)&g_mock_queue_sentinel;
}

inline BaseType_t xQueueSend(QueueHandle_t, const void*, TickType_t) {
    return pdFALSE;
}

inline BaseType_t xQueueSendToBack(QueueHandle_t, const void*, TickType_t) {
    return pdFALSE;
}

inline BaseType_t xQueueReceive(QueueHandle_t, void*, TickType_t) {
    return pdFALSE;
}

inline void vQueueDelete(QueueHandle_t) {}
