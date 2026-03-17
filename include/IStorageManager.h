// IStorageManager.h — Storage Manager Interface
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// STORAGE_ARCHITECTURE.md §5 (3-tier architecture facade)
//
// Phase 1: interface only — PluginContext::storage = nullptr (allowed by contract).
// Wave 7 P-2..P-4: NVS + LittleFS + SD implementation in lib/StorageManager/.
// Wave 7 P-5: StorageManager Facade is the canonical implementation injected
//             into PluginContext::storage. Plugins receive a unified entry point
//             regardless of which tiers are available.
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

    // ── Tier 1: LittleFS status ────────────────────────────────────────────────

    /** @return Free bytes on LittleFS_data partition; 0 if unavailable. */
    virtual size_t littleFsFreeBytes() const = 0;

    /** @return true if LittleFS_data partition is mounted and operational. */
    virtual bool isLfsMounted() const = 0;

    // ── Tier 2: SD status ─────────────────────────────────────────────────────

    /** @return true if an SD card is mounted and available. */
    virtual bool isSdAvailable() const = 0;

    // ── Measurement count ─────────────────────────────────────────────────────

    /** @return Total measurements ever written (NVS monotonic counter); 0 if unavailable. */
    virtual uint32_t measurementCount() const = 0;

    // ── FingerprintCache query ────────────────────────────────────────────────

    /**
     * @brief One ranked identification candidate returned by queryFingerprint().
     * Field sizes match CacheEntry — see FingerprintCache.h for full DB schema.
     */
    struct FPMatch {
        char  id[32];         // e.g. "xag925/at_thaler_1780"
        char  coin_name[48];  // e.g. "Austrian Maria Theresa Thaler"
        char  metal_code[8];  // e.g. "XAG925"
        float confidence;     // [0.0 .. 1.0]  exp(−dist² / σ²)
    };

    /**
     * @brief Query the fingerprint index for the closest coin matches.
     *
     * Input vector must be PRE-NORMALIZED by the caller:
     *   dRp1_n = measured_dRp1 / 800.0f
     *   dL1_n  = measured_dL1  / 2000.0f
     *
     * @param out        Caller-allocated array with capacity >= maxResults.
     * @param maxResults Maximum number of candidates to return.
     * @return           Number of results written to out[] (0 if unavailable).
     */
    virtual uint8_t queryFingerprint(float    dRp1_n,
                                     float    k1,
                                     float    k2,
                                     float    slope,
                                     float    dL1_n,
                                     FPMatch* out,
                                     uint8_t  maxResults) = 0;
};
