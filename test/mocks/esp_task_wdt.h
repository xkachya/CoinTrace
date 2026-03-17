// test/mocks/esp_task_wdt.h — Native-test mock for ESP-IDF watchdog API.
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
//
// FingerprintCache.cpp calls esp_task_wdt_reset() in its SD iteration loop
// [BOOT-2] to prevent TWDT reset on large databases.
// On native this is a no-op — no watchdog exists.

#pragma once

inline void esp_task_wdt_reset() {}
