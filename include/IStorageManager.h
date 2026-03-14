// IStorageManager.h — Storage Manager Interface
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// STORAGE_ARCHITECTURE.md §5 (Tier 0 — NVS interface)
//
// Phase 1: interface only — PluginContext::storage = nullptr (allowed by contract).
// Phase 2 (Wave 7): NVS + LittleFS implementation in lib/StorageManager/.
//
// Plugins must guard all IStorageManager calls with a null-check:
//   if (ctx->storage) { ctx->storage->nvsSaveFloat(...); }

#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Minimal persistence interface exposed to plugins via PluginContext.
 *
 * Provides access to Tier 0 (NVS) key-value store and Tier 1 (LittleFS) file
 * operations. Plugins receive this via ctx->storage; they must check for nullptr
 * before use — storage is optional and may be unavailable during early boot.
 */
class IStorageManager {
public:
    virtual ~IStorageManager() = default;

    // ── Tier 0: NVS key-value store ───────────────────────────────────────────
    // Keys are namespaced by the caller: "ldc1101.calibration_rp_baseline"
    // Max key length: 15 chars (NVS limitation).

    virtual bool nvsSaveFloat (const char* key, float    value) = 0;
    virtual bool nvsSaveUInt32(const char* key, uint32_t value) = 0;

    virtual bool nvsLoadFloat (const char* key, float*    out, float    defaultVal = 0.0f) = 0;
    virtual bool nvsLoadUInt32(const char* key, uint32_t* out, uint32_t defaultVal = 0)   = 0;

    /** @brief Remove a single NVS key. */
    virtual bool nvsErase(const char* key) = 0;

    // ── Tier 1: LittleFS simple file access ───────────────────────────────────
    // Used by MeasurementStore (Wave 7). Plugins generally use NVS, not files.

    /** @return Free bytes on LittleFS_data partition; 0 if unavailable. */
    virtual size_t littleFsFreeBytes() const = 0;
};
