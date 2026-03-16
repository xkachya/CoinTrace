// LittleFSTransport.cpp — Asynchronous LittleFS_data Log Transport (Wave 7 P-3/P-4)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3

#include "LittleFSTransport.h"
#include "SDCardManager.h"  // P-4: SD rotate hook (forward-declared in .h)
#include <Arduino.h>  // log_e / log_w / strlen

LittleFSTransport::LittleFSTransport(LittleFSManager& lfs,
                                      uint32_t         maxLogKB,
                                      uint16_t         queueSize)
    : lfs_(lfs), maxLogKB_(maxLogKB), queueSize_(queueSize) {}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void LittleFSTransport::startTask(uint8_t coreId, UBaseType_t priority) {
    queue_ = xQueueCreate(queueSize_, sizeof(LogEntry));
    if (!queue_) {
        log_e("LFSTransport: xQueueCreate(%u) failed — out of heap", queueSize_);
        return;
    }
    taskRunning_ = true;
    BaseType_t rc = xTaskCreatePinnedToCore(
        taskFunc, "lfs_log", /*stack=*/4096, this, priority, &taskHandle_, coreId);
    if (rc != pdPASS) {
        log_e("LFSTransport: xTaskCreate failed");
        taskRunning_ = false;
        vQueueDelete(queue_);
        queue_ = nullptr;
    }
}

void LittleFSTransport::stopTask() {
    taskRunning_ = false;
    // Allow task time to drain remaining entries and close the file.
    // Task checks taskRunning_ every 100 ms (xQueueReceive timeout).
    // Worst case: 100 ms wait + final processEntry() ~200 ms mutex = ~300 ms.
    vTaskDelay(pdMS_TO_TICKS(500));
    taskHandle_ = nullptr;
}

void LittleFSTransport::end() {
    stopTask();
    if (queue_) {
        vQueueDelete(queue_);
        queue_ = nullptr;
    }
}

// ── write() / isActive() ──────────────────────────────────────────────────────

void LittleFSTransport::write(const LogEntry& entry) {
    // [LF-FUTURE-7 / A-05] null-guard: queue_ is nullptr until startTask() succeeds.
    // Caller (Logger::dispatch) must NOT be aware of this — we silently count drops.
    if (!queue_) {
        droppedCount_++;
        return;
    }
    // Non-blocking (timeout=0): if queue is full, drop entry rather than stalling
    // Logger::dispatch() which holds the Logger mutex.
    if (xQueueSend(queue_, &entry, 0) != pdTRUE) {
        droppedCount_++;
    }
}

bool LittleFSTransport::isActive() const {
    // [LF-FUTURE-7 / A-05] base class returns true unconditionally.
    // Override: active only when task is running with a valid queue.
    return queue_ != nullptr && taskRunning_;
}

// ── File management ───────────────────────────────────────────────────────────

bool LittleFSTransport::openCurrentFile() {
    if (!lfs_.isDataMounted()) return false;

    // /logs/ is created by LittleFSManager::createDataDirs() on first mountData().
    LittleFSDataGuard g(lfs_.lfsDataMutex());
    if (!g.ok()) return false;

    currentFile_ = lfs_.data().open(LOG_CURRENT, "a");
    if (!currentFile_) {
        log_e("LFSTransport: open(%s, a) failed", LOG_CURRENT);
        return false;
    }
    fileOpen_         = true;
    currentSizeBytes_ = currentFile_.size();
    return true;
}

// ── processEntry() ────────────────────────────────────────────────────────────

void LittleFSTransport::processEntry(const LogEntry& entry) {
    if (!fileOpen_ && !openCurrentFile()) {
        droppedCount_++;
        return;
    }

    // Serialize to JSON Lines format (one JSON object per line).
    char line[260];
    entry.toJSON(line, sizeof(line) - 2);
    size_t len = strlen(line);
    line[len++] = '\n';
    line[len]   = '\0';

    // [LF-FUTURE-6 / A-04] SA2-8: portMAX_DELAY ЗАБОРОНЕНИЙ.
    // MainLoop holds lfsDataMutex_ max ~10 ms (MeasurementStore::save()).
    // 200 ms is a generous margin.
    if (xSemaphoreTake(lfs_.lfsDataMutex(), pdMS_TO_TICKS(200)) != pdTRUE) {
        droppedCount_++;
        return;  // skip entry — do not block the bg task
    }
    currentFile_.print(line);
    currentFile_.flush();  // lfs_file_sync() equivalent — power-fail safe
    currentSizeBytes_ += len;
    xSemaphoreGive(lfs_.lfsDataMutex());

    if (currentSizeBytes_ >= maxLogKB_ * 1024U) {
        rotate();
    }
}

// ── rotate() ─────────────────────────────────────────────────────────────────

void LittleFSTransport::rotate() {
    // [LF-FUTURE-6 / A-04] SA2-8: timeout 1000 ms (not portMAX_DELAY).
    // Covers: file close + remove log.1 + rename log.0 → log.1.
    // If timeout: rotation deferred — log.0 continues growing past watermark.
    if (xSemaphoreTake(lfs_.lfsDataMutex(), pdMS_TO_TICKS(1000)) != pdTRUE) {
        log_w("LFSTransport: rotate() — mutex timeout, rotation deferred");
        return;
    }

    currentFile_.close();
    fileOpen_ = false;

    lfs_.data().remove(LOG_ARCHIVE);                       // delete log.1 (if exists)
    lfs_.data().rename(LOG_CURRENT, LOG_ARCHIVE);          // log.0 → log.1
    currentSizeBytes_ = 0;

    xSemaphoreGive(lfs_.lfsDataMutex());

    // [ADR-ST-009 rotate hook] Archive the just-rotated log.1 to SD.
    // lfsDataMutex is NOW RELEASED — copyLogToSD() uses alternating chunk scopes.
    // log.1.jsonl is stable: only modified at the NEXT rotation (200 KB away).
    if (sdMgr_ != nullptr && sdMgr_->isAvailable()) {
        sdMgr_->copyLogToSD(lfs_, LOG_ARCHIVE);
    }

    openCurrentFile();  // open fresh log.0.jsonl
}

// ── Background task ───────────────────────────────────────────────────────────

void LittleFSTransport::taskFunc(void* param) {
    auto* self = static_cast<LittleFSTransport*>(param);

    // Open log file at task start (LittleFS already mounted by this point).
    self->openCurrentFile();

    LogEntry entry;
    while (self->taskRunning_) {
        if (xQueueReceive(self->queue_, &entry, pdMS_TO_TICKS(100)) == pdTRUE) {
            self->processEntry(entry);
        }
    }

    // Graceful shutdown: drain remaining queued entries before closing.
    while (xQueueReceive(self->queue_, &entry, 0) == pdTRUE) {
        self->processEntry(entry);
    }

    // Close file cleanly.
    if (self->fileOpen_) {
        if (xSemaphoreTake(self->lfs_.lfsDataMutex(), pdMS_TO_TICKS(500)) == pdTRUE) {
            self->currentFile_.flush();
            self->currentFile_.close();
            self->fileOpen_ = false;
            xSemaphoreGive(self->lfs_.lfsDataMutex());
        }
    }

    vTaskDelete(nullptr);
}
