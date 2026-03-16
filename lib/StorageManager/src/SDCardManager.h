// SDCardManager.h — SD Card Archive Tier (Wave 7 P-4)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// STORAGE_ARCHITECTURE.md §13 (SD card archive tier)
//
// Manages writes to SD card over the shared VSPI bus (SD_CS = GPIO12).
// All SD operations are guarded by spiMutex (FreeRTOS semaphore,
// passed in via tryMount() from PluginContext).
//
// Design constraints (ADR-ST-008):
//   [SPI-1]  SD_CS    = GPIO12 (hardware-fixed on M5Cardputer-Adv).
//   [SPI-2]  SPI_FREQ = 20 MHz (33 Ω series resistors on M5Cardputer SPI bus).
//   [SPI-3]  MUTEX_TIMEOUT_MS = 50 ms — portMAX_DELAY is FORBIDDEN.
//   [SPI-4]  Mutex scope covers the ENTIRE atomic SD operation (open → write → close).
//
// Concurrency rules (ADR-ST-009, LittleFSManager.h [SA2-6]):
//   writeMeasurement():  lfsDataMutex ALREADY released by caller before invoking.
//                        Acquires only spiMutex. Two separate scopes in the caller.
//   copyLogToSD():       Alternating per-chunk scopes: LFS mutex (read) → release
//                        → SPI mutex (write) → release. Never holds both at once.
//
// Audit guards:
//   [SD-A-01] tryMount():           null-guard on spi_ / spiMutex_ before SD.begin().
//   [SD-A-02] All SD.open() calls:  guarded by spiMutex_ with MUTEX_TIMEOUT_MS.
//   [SD-A-03] No Logger::* calls during SD IO — esp32 native log_e/log_i/log_w only.
//   [SD-A-04] ensureDirLocked():    caller MUST hold spiMutex_ (SD.mkdir uses SPI).
//
// SD directory layout:
//   /CoinTrace/measurements/m_<globalId:06d>.json  — archived ring-buffer evictions
//   /CoinTrace/logs/log_<uptime_s:010d>.jsonl      — rotated LittleFS log archives

#pragma once

#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "LittleFSManager.h"

class SDCardManager {
public:
    SDCardManager() = default;

    // Mount the SD card. Call once in setup() AFTER SPI.begin() and after
    // gCtx.spi / gCtx.spiMutex are initialised.
    // Returns true if the card is present and readable; false is non-fatal
    // (SD tier becomes unavailable — device continues with LittleFS only).
    // [SD-A-01] Null-guards spi_ and spiMutex_ before SD.begin().
    bool tryMount(SPIClass* spi, SemaphoreHandle_t spiMutex);

    // Gracefully unmount the SD card (flush + SD.end()).
    // Call during soft shutdown BEFORE power-off or SPI re-use.
    void umount();

    // Returns true if the card was successfully mounted and is still available.
    bool isAvailable() const { return available_; }

    // Archive one measurement JSON to SD.
    // SD path: /CoinTrace/measurements/m_<globalId:06d>.json
    // buf must point to serialised JSON; len is the byte count (no null terminator
    // required, but accepted).
    // [SD-A-02] Acquires spiMutex_ for the full open → write → close scope.
    // Caller must NOT hold lfsDataMutex when calling this (ADR-ST-009 [SA2-6]).
    bool writeMeasurement(uint32_t globalId, const uint8_t* buf, size_t len);

    // Stream-copy lfsPath from LittleFS to /CoinTrace/logs/log_<ts>.jsonl on SD.
    // Designed for the LittleFSTransport rotate hook: call AFTER lfsDataMutex is
    // released so that log.1.jsonl is stable for reading.
    //
    // Alternating per-chunk mutex scopes (COPY_CHUNK bytes at a time):
    //   Acquire lfsDataMutex → read chunk → release
    //   Acquire spiMutex_    → write chunk → release
    // [SD-A-02] [SA2-6] Never holds both mutexes simultaneously.
    bool copyLogToSD(LittleFSManager& lfs, const char* lfsPath);

private:
    static constexpr uint8_t  SD_CS            = 12;
    static constexpr uint32_t SPI_FREQ         = 20000000UL;  // 20 MHz
    static constexpr uint32_t MUTEX_TIMEOUT_MS = 50;            // [SPI-3]
    static constexpr size_t   COPY_CHUNK       = 512;           // bytes per stream chunk

    SPIClass*         spi_      = nullptr;
    SemaphoreHandle_t spiMutex_ = nullptr;
    bool              available_ = false;

    // Create directory (and all required parents) on SD if it does not exist.
    // [SD-A-04] Caller MUST hold spiMutex_ before calling — SD.mkdir uses SPI.
    void ensureDirLocked(const char* dir);
};
