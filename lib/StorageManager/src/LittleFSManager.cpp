// LittleFSManager.cpp — LittleFS Dual-Partition Manager (Tier 1) Implementation
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// STORAGE_ARCHITECTURE.md §8

#include "LittleFSManager.h"
#include <Arduino.h>

// ── Partition labels (must match CSV Name column exactly) ─────────────────────
static constexpr const char* PARTITION_SYS  = "littlefs_sys";
static constexpr const char* PARTITION_DATA = "littlefs_data";

// VFS mount points (base_path argument to LittleFSFS::begin())
static constexpr const char* MOUNT_SYS      = "/sys";
static constexpr const char* MOUNT_DATA     = "/data";

// Maximum simultaneously open file handles per partition.
// Headroom for concurrent reads from WebServer + MeasurementStore + LittleFSTransport.
static constexpr uint8_t MAX_OPEN_FILES = 10;

// ── Lifecycle ─────────────────────────────────────────────────────────────────

LittleFSManager::~LittleFSManager() {
    if (dataMounted_) data_.end();
    if (sysMounted_)  sys_.end();
    if (dataMutex_)   vSemaphoreDelete(dataMutex_);
}

// ── Mount operations ──────────────────────────────────────────────────────────

bool LittleFSManager::mountSys() {
    if (sysMounted_) return true;

    // format_on_fail=false: production content (web UI, device.json) loaded via
    // uploadfs-sys. Auto-format would silently destroy it. Require explicit upload.
    sysMounted_ = sys_.begin(/*formatOnFail=*/false, MOUNT_SYS, MAX_OPEN_FILES, PARTITION_SYS);
    return sysMounted_;
}

bool LittleFSManager::mountData() {
    if (dataMounted_) return true;

    // Create mutex before mount so the guard can be used immediately after.
    dataMutex_ = xSemaphoreCreateMutex();
    if (!dataMutex_) {
        // Insufficient heap — cannot create mutex. Treat as mount failure.
        return false;
    }

    // format_on_fail=true: automatically format on first boot (empty partition).
    // Safe: data partition contains user data only (no pre-loaded system content).
    dataMounted_ = data_.begin(/*formatOnFail=*/true, MOUNT_DATA, MAX_OPEN_FILES, PARTITION_DATA);
    if (dataMounted_) {
        createDataDirs();
    }
    return dataMounted_;
}

// ── Private helpers ───────────────────────────────────────────────────────────

void LittleFSManager::createDataDirs() {
    // Required directory structure (STORAGE_ARCHITECTURE.md §8.2):
    //   /measurements/  — ring buffer m_000.json .. m_249.json (ADR-ST-006)
    //   /cache/         — fingerprint index cache (index.json, sd_generation.bin)
    //   /logs/          — log rotation (log.0.jsonl, log.1.jsonl)
    //
    // Idempotent: check existence before each mkdir to avoid false error returns
    // on subsequent boots when directories are already present.
    static const char* const kDirs[] = { "/measurements", "/cache", "/logs" };
    for (const char* dir : kDirs) {
        if (!data_.exists(dir)) {
            data_.mkdir(dir);
        }
    }
}

// ── Capacity ──────────────────────────────────────────────────────────────────

size_t LittleFSManager::dataFreeBytes() {
    if (!dataMounted_) return 0;
    const size_t total = data_.totalBytes();
    const size_t used  = data_.usedBytes();
    return (total > used) ? (total - used) : 0;
}

size_t LittleFSManager::sysFreeBytes() {
    if (!sysMounted_) return 0;
    const size_t total = sys_.totalBytes();
    const size_t used  = sys_.usedBytes();
    return (total > used) ? (total - used) : 0;
}
