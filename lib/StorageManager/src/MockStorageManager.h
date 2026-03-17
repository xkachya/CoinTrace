// MockStorageManager.h — Test Double for IStorageManager (Wave 7 P-5)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// STORAGE_ARCHITECTURE.md §15 P-5 (MockStorage for unit tests)
//
// Header-only stub implementing IStorageManager.
// All methods return safe no-op defaults — designed for use in unit tests
// where no real storage is needed.
//
// Usage:
//   MockStorageManager mock;
//   plugin.initialize(&ctx_with_mock);
//   // assertions on plugin behaviour without real NVS / LittleFS / SD
//
// To test storage-dependent behaviour, extend this class and override
// specific methods (e.g. make nvsLoadFloat() return a test value).

#pragma once

#include "IStorageManager.h"

class MockStorageManager : public IStorageManager {
public:
    // ── Tier 0: NVS ──────────────────────────────────────────────────────────
    bool nvsSaveFloat (const char*, float)                              override { return false; }
    bool nvsSaveUInt32(const char*, uint32_t)                           override { return false; }
    bool nvsLoadFloat (const char*, float*    out, float    def = 0.0f) override { if (out) *out = def; return false; }
    bool nvsLoadUInt32(const char*, uint32_t* out, uint32_t def = 0)   override { if (out) *out = def; return false; }
    bool nvsErase     (const char*)                                     override { return false; }

    // ── Tier 1: LittleFS ─────────────────────────────────────────────────────
    size_t   littleFsFreeBytes() const override { return 0; }
    bool     isLfsMounted()      const override { return false; }

    // ── Tier 2: SD ───────────────────────────────────────────────────────────
    bool     isSdAvailable()     const override { return false; }

    // ── Measurement count ─────────────────────────────────────────────────────
    uint32_t measurementCount()  const override { return 0; }

    // ── FingerprintCache query ────────────────────────────────────────────────
    uint8_t queryFingerprint(float, float, float, float, float,
                             FPMatch*, uint8_t)                         override { return 0; }
};
