// FingerprintCache.h — Fingerprint Index Cache (Wave 7 P-4)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// STORAGE_ARCHITECTURE.md §17.2 boot [7], FINGERPRINT_DB_ARCHITECTURE.md §6.3
//
// Loads the fingerprint index (SD:/CoinTrace/database/index.json) into RAM on
// boot, with CRC32 integrity check and generation-based staleness detection.
//
// Boot scenarios (§17.2 [7]):
//   a) SD + cache + CRC32 OK + generation match  → use cache (fast path, ~1 ms)
//   b) SD + cache + CRC32 fail or generation ≠   → rebuild from SD
//   c) SD + no cache                              → build from SD, persist to LittleFS
//   d) No SD  + cache                             → load cache (offline mode)
//   e) No SD  + no cache                          → LOG_WARN, matching unavailable
//
// RAM budget: CacheEntry ≈ 80 bytes × 1000 entries = 80 KB.
// ESP32-S3FN8 free heap ≈ 337 KB — safe without PSRAM.
//
// Thread safety:
//   init() — call once from setup() (single-threaded boot context, no mutex needed).
//   query() — read-only after init(); concurrent reads from multiple tasks are safe
//             (no mutation of entries_ after init returns).
//   Writes to LittleFS cache (rebuildFromSD / saveCache) happen only inside init()
//   under LittleFSDataGuard per [SA2-7].
//
// Lock ordering ([SA2-6]):
//   buildCache() acquires lfsDataMutex BEFORE spiMutex when both are needed.
//   In practice: SD reads (spiMutex only) → LittleFS writes (lfsDataMutex only)
//   as separate phases, never simultaneously.
//
// Constraints:
//   [SPI-3]  spiMutex timeout = 50 ms — portMAX_DELAY FORBIDDEN.
//   [BOOT-2] esp_task_wdt_reset() inside SD iteration loop (prevents TWDT on large DB).
//   [SD-A-03] No Logger::* during SD IO — use log_e/log_i/log_w (ESP32 native) only.

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "LittleFSManager.h"
#include "SDCardManager.h"

// ── CacheEntry ────────────────────────────────────────────────────────────────
// In-RAM representation of one index.json entry.
// Fields mirror FINGERPRINT_DB_ARCHITECTURE.md §6.3.
// Normalized values (dRp1_n, dL1_n) are stored as-is from index.json —
// CI already divided by dRp1_MAX=800 and dL1_MAX=2000 at build time.
struct CacheEntry {
    char     id[32];            // "xag925/at_thaler_1780"
    char     metal_code[8];     // "XAG925"
    char     coin_name[48];     // "Austrian Maria Theresa Thaler"
    char     protocol_id[24];   // "p1_1mhz_013mm"
    float    dRp1_n;            // centroid, normalized = dRp1 / 800 Ohm
    float    k1;
    float    k2;
    float    slope;
    float    dL1_n;             // normalized = dL1 / 2000 µH
    float    radius_95pct;
    uint16_t records_count;
};

// ── QueryResult ───────────────────────────────────────────────────────────────
// Single candidate returned by query().
struct QueryResult {
    const CacheEntry* entry;    // points into entries_ — valid for lifetime of cache
    float             distance; // weighted Euclidean distance (lower = better match)
    float             confidence; // exp(-dist² / σ²), [0.0 .. 1.0]
};

// ── FingerprintCache ─────────────────────────────────────────────────────────
class FingerprintCache {
public:
    // Maximum entries held in RAM (FINGERPRINT_DB_ARCHITECTURE.md §6.3 RAM budget).
    static constexpr uint16_t MAX_ENTRIES = 1000;

    // Sigma for confidence transform: conf = exp(−dist² / σ²).
    // σ = 0.3 covers typical inter-class distances for v1 normalized 5D space.
    // Adjust after empirical validation (FINGERPRINT_DB_ARCHITECTURE.md §3.4).
    static constexpr float CONFIDENCE_SIGMA = 0.3f;

    // Number of top candidates returned by query().
    static constexpr uint8_t QUERY_TOP_N = 10;

    // SD source path for index.json.
    static constexpr const char* SD_INDEX_PATH = "/CoinTrace/database/index.json";

    // LittleFS cache file paths (relative to /data partition root).
    static constexpr const char* LFS_CACHE_INDEX      = "/cache/index.json";
    static constexpr const char* LFS_CACHE_CRC32      = "/cache/index_crc32.bin";
    static constexpr const char* LFS_CACHE_GENERATION = "/cache/sd_generation.bin";

    FingerprintCache() = default;

    // ── Lifecycle ──────────────────────────────────────────────────────────

    // Load or build the fingerprint cache.
    // Call once from setup() AFTER SDCardManager::tryMount() (boot step 4f).
    //   lfs         — mounted LittleFSManager instance
    //   sdCard      — may be null or unavailable (handled gracefully)
    //   spiMutex    — shared VSPI mutex from PluginContext
    //
    // Returns true if cache is ready for queries (scenarios a–d).
    // Returns false if neither SD nor cache is available (scenario e).
    //
    // [BOOT-2] Calls esp_task_wdt_reset() during SD iteration (prevents TWDT reset).
    bool init(LittleFSManager& lfs, SDCardManager* sdCard,
              SemaphoreHandle_t spiMutex);

    // ── Query ──────────────────────────────────────────────────────────────

    // Returns true if the cache is loaded and query() can be called.
    bool isReady() const { return ready_; }

    // Number of entries currently loaded.
    uint16_t entryCount() const { return count_; }

    // Find the top-N closest matches for an input measurement vector.
    //
    // Input must be PRE-NORMALIZED by caller:
    //   dRp1_n = measured_dRp1 / 800.0f
    //   dL1_n  = measured_dL1  / 2000.0f
    //
    // results[] must have capacity >= QUERY_TOP_N.
    // Returns the number of results written (may be < QUERY_TOP_N if count_ < QUERY_TOP_N).
    //
    // Thread safe for concurrent reads (no writes after init()).
    uint8_t query(float dRp1_n, float k1, float k2, float slope, float dL1_n,
                  QueryResult* results, uint8_t maxResults = QUERY_TOP_N) const;

private:
    // ── Storage ────────────────────────────────────────────────────────────

    CacheEntry        entries_[MAX_ENTRIES];
    uint16_t          count_   = 0;
    bool              ready_   = false;

    // ── CRC32 helpers ──────────────────────────────────────────────────────

    // Compute CRC32 of a file on LittleFS.
    // Caller must hold lfsDataMutex before calling.
    // Returns 0 on file read error (also a valid CRC, but collisions negligible).
    uint32_t computeLFSCrc32(fs::LittleFSFS& fs, const char* path);

    // Read a 4-byte LE uint32 from a LittleFS binary file.
    // Caller must hold lfsDataMutex.
    bool readU32LE(fs::LittleFSFS& fs, const char* path, uint32_t& out);

    // Write a 4-byte LE uint32 to a LittleFS binary file (creates or overwrites).
    // Caller must hold lfsDataMutex.
    bool writeU32LE(fs::LittleFSFS& fs, const char* path, uint32_t value);

    // ── SD helpers ─────────────────────────────────────────────────────────

    // Read the "generation" field from SD:/CoinTrace/database/index.json header.
    // Acquires spiMutex_ with timeout [SPI-3]. [SD-A-03] No Logger::* during IO.
    // Returns false on parse failure or mutex timeout.
    bool readSDGeneration(SDCardManager& sd, SemaphoreHandle_t spiMutex,
                          uint32_t& out);

    // ── Cache build / load ─────────────────────────────────────────────────

    // Parse SD:/CoinTrace/database/index.json and populate entries_.
    // Acquires spiMutex for SD read, then lfsDataMutex for LittleFS write.
    // [BOOT-2] Calls esp_task_wdt_reset() once per parse iteration.
    // [SD-A-03] No Logger::* during SD IO sections.
    bool buildFromSD(LittleFSManager& lfs, SDCardManager& sd,
                     SemaphoreHandle_t spiMutex);

    // Load entries_ from LittleFS /cache/index.json.
    // Caller must hold lfsDataMutex.
    bool loadFromLFS(fs::LittleFSFS& fs);

    // Serialize entries_ → /cache/index.json + index_crc32.bin + sd_generation.bin.
    // Caller must hold lfsDataMutex.
    bool saveToLFS(fs::LittleFSFS& fs, uint32_t generation);

    // Remove all three LittleFS cache files (called when CRC32 fails).
    // Caller must hold lfsDataMutex.
    void invalidateCache(fs::LittleFSFS& fs);
};
