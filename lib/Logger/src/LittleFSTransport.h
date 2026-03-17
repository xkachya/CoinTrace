// LittleFSTransport.h — Asynchronous LittleFS_data Log Transport (Wave 7 P-3/P-4)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// LOGGER_ARCHITECTURE.md §6.6
//
// Hot-log tier: keeps the last 2 × maxLogKB_ of log entries on LittleFS_data.
//   log.0.jsonl — current log (append, open-once)
//   log.1.jsonl — previous log (kept after rotation)
//
// Design:
//   Open-once pattern: log.0.jsonl is kept open between write()s (1 sync/entry).
//   Background FreeRTOS task drains the LogEntry queue and performs rotation.
//   Rotation at maxLogKB_: log.1 deleted, log.0 → log.1, new log.0 opened.
//
// Wave 7 P-4 — SD rotate hook:
//   After rotation, log.1.jsonl (the just-filled log) is stream-copied to SD
//   via SDCardManager::copyLogToSD(). Called AFTER lfsDataMutex is released
//   so that alternating mutex scopes can be used (no simultaneous hold).
//   [SA2-6] see LittleFSManager.h lock-ordering rules.
//
// Audit guards:
//   [LF-FUTURE-7 / A-05] write():       null-guard on queue_ (task may not be started).
//   [LF-FUTURE-7 / A-05] isActive():    returns queue_ != nullptr && taskRunning_.
//   [LF-FUTURE-6 / A-04] processEntry(): pdMS_TO_TICKS(200) — no portMAX_DELAY.
//   [LF-FUTURE-6 / A-04] rotate():       pdMS_TO_TICKS(1000) — no portMAX_DELAY.
//
// Usage:
//   static LittleFSTransport gLfsTransport(gLFS);
//   gLfsTransport.startTask(/*core=*/0, /*prio=*/2);   // BEFORE addTransport()
//   gLogger.addTransport(&gLfsTransport);

#pragma once

#include "ILogTransport.h"
#include "LittleFSManager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// Forward declaration — SDCardManager.h is in StorageManager; included in .cpp.
class SDCardManager;

class LittleFSTransport : public ILogTransport {
public:
    explicit LittleFSTransport(LittleFSManager& lfs,
                                uint32_t         maxLogKB  = 200,
                                uint16_t         queueSize = 64);

    // begin() returns true immediately — actual init happens in startTask().
    bool        begin()  override { return true; }
    void        end()    override;

    // Non-blocking: enqueues entry. [A-05] null-guard: drops if queue_ == nullptr.
    void        write(const LogEntry& entry) override;

    const char* getName()  const override { return "LittleFS"; }

    // [A-05 / LF-FUTURE-7] Active only when task is running with a valid queue.
    bool        isActive() const override;

    uint32_t    getDroppedCount() const override { return droppedCount_; }

    // Create queue and FreeRTOS task. Call BEFORE Logger::addTransport().
    // coreId=0 (PRO_CPU): keeps log I/O off APP_CPU (core 1) where plugins run.
    void startTask(uint8_t coreId = 0, UBaseType_t priority = 2);
    void stopTask();

    // Diagnostic: returns minimum free stack space observed since task creation,
    // in bytes. Call ≥10 s after startTask() for a representative value.
    // Returns 0 if task has not been started. Useful for tuning stack size.
    uint32_t stackWatermarkBytes() const;

    // Inject optional SD card manager for log archival on rotation (P-4).
    // Call in setup() after gSDCard.tryMount(). sd may be null (hook is a no-op).
    void setSDCardManager(SDCardManager* sd) { sdMgr_ = sd; }

private:
    static constexpr const char* LOG_CURRENT = "/logs/log.0.jsonl";
    static constexpr const char* LOG_ARCHIVE = "/logs/log.1.jsonl";

    LittleFSManager&  lfs_;
    uint32_t          maxLogKB_;
    QueueHandle_t     queue_       = nullptr;
    TaskHandle_t      taskHandle_  = nullptr;
    uint16_t          queueSize_;
    volatile uint32_t droppedCount_ = 0;
    volatile bool     taskRunning_  = false;
    SDCardManager*    sdMgr_        = nullptr;  // optional P-4 rotate hook

    // File kept open between writes (open-once pattern — LOGGER_ARCHITECTURE §6.6).
    File             currentFile_;
    bool             fileOpen_         = false;
    uint32_t         currentSizeBytes_ = 0;

    static void taskFunc(void* param);

    // Open (or re-open) LOG_CURRENT in append mode. Acquires lfsDataMutex_.
    bool openCurrentFile();

    // Write one entry to currentFile_ under lfsDataMutex_.
    // [A-04] Uses pdMS_TO_TICKS(200) — no portMAX_DELAY.
    void processEntry(const LogEntry& entry);

    // Rotate logs: log.1 deleted, log.0 → log.1, new log.0 opened.
    // [A-04] Uses pdMS_TO_TICKS(1000) — no portMAX_DELAY.
    void rotate();
};
