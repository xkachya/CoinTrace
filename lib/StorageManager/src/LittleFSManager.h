// LittleFSManager.h — LittleFS Dual-Partition Manager (Tier 1)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// STORAGE_ARCHITECTURE.md §8 (LittleFS sys + data partition design)
//
// Manages two independent LittleFS partition instances:
//   littlefs_sys  → VFS mount /sys  (read-only after boot: system config + web UI)
//   littlefs_data → VFS mount /data (read-write: measurements, logs, cache)
//
// Thread safety:
//   sys_  : read-only after mountSys() — mutex NOT required (ADR-ST-002)
//   data_ : use LittleFSDataGuard(lfsDataMutex(), ...) for ALL file operations
//           from multiple FreeRTOS tasks ([SA2-7], [SA2-8])
//
// Lock ordering ([SA2-6]):
//   Always acquire lfs_data_mutex_ BEFORE spi_vspi_mutex when both are needed.
//   Current callers that hold both: LittleFSTransport::rotate() (lfs → spi).
//
// Acceptance criteria (STORAGE_ARCHITECTURE.md §15 P-3):
//   mountSys()  → /sys/config/device.json readable (requires uploadfs-sys first)
//   mountData() → can create and read /data/test.txt
//
// Usage:
//   LittleFSManager lfs;
//   lfs.mountSys();         // once in setup(); warn on failure, continue
//   lfs.mountData();        // once in setup(); warn on failure, continue
//   lfs.sys().exists(...)   // use after isSysMounted() check
//   lfs.data().open(...)    // use inside LittleFSDataGuard scope

#pragma once

#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ── LittleFSDataGuard ─────────────────────────────────────────────────────────
// RAII mutex guard for LittleFS_data file operations.
//
// [SA2-8] timeout=500ms: LittleFS rotation holds mutex ~150-350ms
//         (rename + SD copy + delete). If ok()==false, caller MUST abort the
//         file operation and return an error — do NOT proceed without the guard.
//         Filesystem corruption results from unprotected concurrent writes.
//
// Example:
//   { LittleFSDataGuard g(lfs.lfsDataMutex());
//     if (!g.ok()) return false;
//     lfs.data().open(...);
//   }  // mutex released here
class LittleFSDataGuard {
public:
    explicit LittleFSDataGuard(SemaphoreHandle_t& mtx, uint32_t timeout_ms = 500)
        : mtx_(mtx),
          acquired_(xSemaphoreTake(mtx, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        // [SA2-8] Log timeout so deadlocks and priority inversions are visible.
        // Uses ESP32-native log_e() — no dependency on project Logger.
        // Caller MUST check ok() and abort the file operation on false.
        if (!acquired_) {
            log_e("LFSDataGuard: lfs_data mutex timeout (%u ms) — caller must abort file op",
                  (unsigned)timeout_ms);
        }
    }

    bool ok() const { return acquired_; }

    ~LittleFSDataGuard() { if (acquired_) xSemaphoreGive(mtx_); }

    LittleFSDataGuard(const LittleFSDataGuard&)            = delete;
    LittleFSDataGuard& operator=(const LittleFSDataGuard&) = delete;

private:
    SemaphoreHandle_t& mtx_;
    bool               acquired_;
};

// ── LittleFSManager ───────────────────────────────────────────────────────────
class LittleFSManager {
public:
    LittleFSManager()  = default;
    ~LittleFSManager();

    // ── Mount operations ────────────────────────────────────────────────────

    /**
     * @brief Mount the littlefs_sys partition (VFS: /sys).
     *
     * format_on_fail=false — never auto-format the sys partition.
     * Production web UI and config are loaded via uploadfs-sys; auto-format
     * would silently destroy them. Requires explicit `pio run -e uploadfs-sys`.
     *
     * Graceful failure: if unmounted, sys() returns an unconfigured FS handle.
     * Callers must check isSysMounted() or the return value before using.
     *
     * @return true if mounted successfully.
     */
    bool mountSys();

    /**
     * @brief Mount the littlefs_data partition (VFS: /data).
     *
     * format_on_fail=true — formats automatically on first boot (empty partition).
     * Creates required subdirectories after first mount.
     * Creates lfs_data_mutex_ for concurrent-access guard.
     *
     * Graceful failure: system continues without persistent measurements/logs
     * (RingBufferTransport keeps recent logs in RAM; no crash).
     *
     * @return true if mounted successfully.
     */
    bool mountData();

    // ── State ───────────────────────────────────────────────────────────────

    bool isSysMounted()  const { return sysMounted_; }
    bool isDataMounted() const { return dataMounted_; }

    /**
     * @brief Factory-reset LittleFS_data: erase all measurements, cache, logs.
     * Unmounts the partition before formatting and does NOT remount.
     * Caller must call esp_restart() or mountData() afterwards.
     * Used by: GPIO0 boot recovery (§17.2 [1.5]), future web factory-reset endpoint.
     * @return true if format succeeded.
     */
    bool formatData();

    // ── Filesystem handles ──────────────────────────────────────────────────

    /**
     * @brief Direct access to the sys filesystem handle.
     * Paths are relative to LittleFS internal root (no /sys prefix).
     * Example: sys().exists("/config/device.json")
     * Required: isSysMounted() == true before use; read-only after mount.
     */
    fs::LittleFSFS& sys()  { return sys_; }

    /**
     * @brief Direct access to the data filesystem handle.
     * Paths are relative to LittleFS internal root (no /data prefix).
     * Example: data().open("/measurements/m_000.json", "w")
     * Required: use exclusively inside a LittleFSDataGuard scope.
     */
    fs::LittleFSFS& data() { return data_; }

    /**
     * @brief The mutex protecting all LittleFS_data file operations.
     *
     * Pass to LittleFSDataGuard. Valid only after mountData() succeeds.
     * [SA2-6]: acquire before spi_vspi_mutex when both are needed.
     */
    SemaphoreHandle_t& lfsDataMutex() { return dataMutex_; }

    // ── Capacity ────────────────────────────────────────────────────────────

    /**
     * @brief Free bytes on the littlefs_data partition.
     * [SA2-7]: Call only within a LittleFSDataGuard scope from FreeRTOS tasks.
     * Safe to call from setup() (single-threaded context).
     * @return 0 if partition not mounted.
     */
    size_t dataFreeBytes();

    /**
     * @brief Free bytes on the littlefs_sys partition.
     * Read-only partition; no mutex required.
     * @return 0 if partition not mounted.
     */
    size_t sysFreeBytes();

private:
    fs::LittleFSFS    sys_;
    fs::LittleFSFS    data_;

    SemaphoreHandle_t dataMutex_  = nullptr;
    bool              sysMounted_  = false;
    bool              dataMounted_ = false;

    // Creates /measurements/, /cache/, /logs/ inside data partition on first boot.
    // Idempotent — safe to call on every boot (checks existence before mkdir).
    // See STORAGE_ARCHITECTURE.md §8.2 for the required directory structure.
    void createDataDirs();
};
