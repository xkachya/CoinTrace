// PluginContext.h — Shared Context passed to every plugin
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// PLUGIN_CONTRACT.md §1.3 / PLUGIN_ARCHITECTURE.md
//
// PluginContext is assembled by PluginSystem::begin() and passed to every
// plugin::initialize().  All pointer fields (except storage) are guaranteed
// non-null at the time initialize() is called.

#pragma once

#include <Wire.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Forward declarations — avoid circular includes
class Logger;
class ConfigManager;
class IStorageManager;
class WiFiManager;      // Wave 8 A-1 — CONNECTIVITY_ARCHITECTURE.md §4
class HttpServer;       // Wave 8 A-2 — CONNECTIVITY_ARCHITECTURE.md §5.2

/**
 * @brief Container for all shared system resources.
 *
 * Passed by pointer to IPlugin::initialize().  Plugins must NOT store
 * the pointer beyond shutdown() — the context may be destroyed after all
 * plugins have shut down.
 *
 * Thread-safety:
 *   - wire   / spi:    protected by wireMutex / spiMutex for async tasks
 *   - config / log:    thread-safe by their own design
 *   - storage:         may be nullptr — check before use
 */
struct PluginContext {
    // ── Hardware buses ────────────────────────────────────────────────────────
    TwoWire*          wire;       // Initialised I2C bus (Wire / Wire1)
    SemaphoreHandle_t wireMutex;  // FreeRTOS mutex — take before every I2C burst in async tasks

    SPIClass*         spi;        // Initialised SPI bus (SPI / VSPI)
    SemaphoreHandle_t spiMutex;   // FreeRTOS mutex — take before every SPI burst in async tasks
                                  // ≡ spi_vspi_mutex in STORAGE_ARCHITECTURE — same object, one mutex for the entire VSPI bus

    // ── Services ──────────────────────────────────────────────────────────────
    ConfigManager*    config;     // Plugin configuration access (never nullptr)
    Logger*           log;        // Logging (never nullptr)
    IStorageManager*  storage;    // Persistence (may be nullptr — check before use)
    WiFiManager*      wifi;        // WiFi state — AP/STA, getIP(), isConnected() (Wave 8 A-1)
                                   // nullptr before WiFiManager::begin(); always check.
    HttpServer*       http;        // REST API server (Wave 8 A-2) — nullptr before begin().

    PluginContext()
        : wire(nullptr), wireMutex(nullptr)
        , spi(nullptr),  spiMutex(nullptr)
        , config(nullptr), log(nullptr), storage(nullptr)
        , wifi(nullptr), http(nullptr) {}
};
