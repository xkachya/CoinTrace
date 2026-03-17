// StorageManager.h — Unified Storage Facade (Wave 7 P-5)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// STORAGE_ARCHITECTURE.md §5 (3-tier architecture diagram), §15 P-5
//
// Facade over the three storage tiers (ADR-ST-005 Variant B):
//   Tier 0 — NVSManager    : WiFi, calibration, system settings, meas_count
//   Tier 1 — LittleFSManager: measurements, logs, fingerprint cache
//   Tier 2 — SDCardManager : optional archive + fingerprint DB source
//
// Implements IStorageManager and is injected into PluginContext::storage.
// Replaces the previous direct NVSManager injection — plugins no longer need
// to know which tier serves their request.
//
// Graceful degradation:
//   Each method checks tier availability before routing and returns a safe
//   default (false / 0 / nullptr) if the tier is not ready. No tier failure
//   causes a crash or a hard fault.
//
// Thread safety:
//   Methods delegate to the same thread-safety contracts as the underlying
//   tier classes (NVSManager.[PRE-10], LittleFSManager.[SA2-6..SA2-8],
//   SDCardManager.[SPI-3..SPI-4]).
//   StorageManager itself holds no mutable state — it is safe to call from
//   any task subject to the individual tier constraints.

#pragma once

#include "IStorageManager.h"
#include "NVSManager.h"
#include "LittleFSManager.h"
#include "SDCardManager.h"
#include "FingerprintCache.h"
#include "MeasurementStore.h"

class StorageManager : public IStorageManager {
public:
    StorageManager(NVSManager& nvs, LittleFSManager& lfs,
                   SDCardManager& sd, FingerprintCache& fp,
                   MeasurementStore& meas);

    // ── IStorageManager — Tier 0: NVS key-value ──────────────────────────────
    // Routes to NVSManager; returns false/0 if NVS is not ready.
    bool nvsSaveFloat (const char* key, float    value)                            override;
    bool nvsSaveUInt32(const char* key, uint32_t value)                            override;
    bool nvsLoadFloat (const char* key, float*    out, float    defaultVal = 0.0f) override;
    bool nvsLoadUInt32(const char* key, uint32_t* out, uint32_t defaultVal = 0)   override;
    bool nvsErase     (const char* key)                                            override;

    // ── IStorageManager — Tier 1: LittleFS status ────────────────────────────
    /** @return Free bytes on LittleFS_data; 0 if unmounted. */
    size_t   littleFsFreeBytes() const override;

    /** @return true if LittleFS_data is mounted and operational. */
    bool     isLfsMounted()      const override;

    // ── IStorageManager — Tier 2: SD status ──────────────────────────────────
    /** @return true if an SD card is mounted and available. */
    bool     isSdAvailable()     const override;

    // ── IStorageManager — Measurement count ──────────────────────────────────
    /** @return Total measurements ever written (NVS monotonic counter). */
    uint32_t measurementCount()  const override;

    // ── IStorageManager — FingerprintCache query ──────────────────────────────
    // Routes to FingerprintCache::query() and bridges QueryResult → FPMatch.
    // Returns 0 if cache is not ready or maxResults == 0.
    // [SD-A-03] No Logger::* calls — FingerprintCache uses log_e/log_i/log_w.
    uint8_t queryFingerprint(float    dRp1_n,
                             float    k1,
                             float    k2,
                             float    slope,
                             float    dL1_n,
                             FPMatch* out,
                             uint8_t  maxResults) override;

private:
    NVSManager&        nvs_;
    LittleFSManager&   lfs_;
    SDCardManager&     sd_;
    FingerprintCache&  fp_;
    MeasurementStore&  meas_;
};
