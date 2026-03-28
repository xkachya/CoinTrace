// MetalMatcher.h — Metal Classification Layer (Wave 8 C-7a)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// METAL_MATCHER_ARCHITECTURE.md v1.3.0
//
// Thin algorithmic layer over FingerprintCache that:
//   - Accepts configurable per-axis weights from matcher.json (no rebuild required)
//   - Implements Full 5D match (matchFull) and Quick 2D match (matchQuick)
//   - Returns top-1 + 3 alternatives with per-component distances for tuning
//   - Leaves FingerprintCache as a pure storage class (separation of concerns)
//
// Placement: lib/StorageManager/src/ — alongside FingerprintCache (ADR-M1).
//
// Memory: ~48 B BSS (gMatcher global). MatchResult lives on caller stack (~248 B).
// Thread safety: matchFull/matchQuick are read-only after init(); safe from MainLoop.
//   loadConfig() acquires spiMutex — call once from setup(), not from update().

#pragma once

#include "FingerprintCache.h"
#include "SDCardManager.h"
#include "Measurement.h"
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ── Algorithm identifiers ─────────────────────────────────────────────────────
static constexpr uint8_t ALGO_FULL  = 0;  // matchFull() — 5D weighted search
static constexpr uint8_t ALGO_QUICK = 1;  // matchQuick() — 2D projection

// ── Alternative ──────────────────────────────────────────────────────────────
// Runner-up candidate (#2..#4) returned alongside the top match.
struct Alternative {
    char  metal_code[8];   // e.g. "XAG833"
    char  coin_name[48];   // e.g. "Test Silver 833"
    float confidence;      // exp(−dist²/σ²), [0.0..1.0]
    float distance;        // weighted Euclidean distance
};
// sizeof(Alternative) = 8+48+4+4 = 64 B

// ── MatchResult ───────────────────────────────────────────────────────────────
// Complete result from matchFull() or matchQuick(). Lives on caller stack.
struct MatchResult {
    // ── Top match ──────────────────────────────────────────────────────────
    char    metal_code[8];      // "XAG925" | "" if no valid match
    char    coin_name[48];      // full coin name
    float   confidence;         // exp(−dist²/σ²), [0.0..1.0]
    float   distance;           // weighted Euclidean distance (lower = better)

    // ── Classification flags ───────────────────────────────────────────────
    bool    is_ferro;           // fabsf(dL1_n) > cfg.ferro_thresh_dL1_n
                                // ⚠️ DISABLED in v1: ferro_thresh_dL1_n=99.0 (pending Wave 9 S-5)
    bool    valid;              // true if confidence >= cfg.min_confidence AND cache ready
    uint8_t algo;               // ALGO_FULL=0 or ALGO_QUICK=1

    // ── Debug / tuning ─────────────────────────────────────────────────────
    // Per-axis weighted contribution: √(wi·Δi²) for each component.
    // Zero for axes where wi = 0 (e.g. slope with full_weights[3]=0, or k1/k2/slope in Quick).
    // Use these for tuning: compare non-zero axes to identify which dimension drives mismatch.
    float   dist_components[5]; // [dRp1_n, k1, k2, slope, dL1_n]

    // ── Alternatives ──────────────────────────────────────────────────────
    Alternative alternatives[3];
    uint8_t     alt_count;      // 0..3 valid entries in alternatives[]
};
// sizeof(MatchResult) ≈ 8+48+4+4+1+1+1+3(pad)+20+192+1 ≈ 283 B — stack only, no heap

// ── MetalMatcher ─────────────────────────────────────────────────────────────
class MetalMatcher {
public:

    // ── Configuration (loaded from SD:/CoinTrace/matcher.json) ───────────────
    struct Config {
        // Weights for 5D vector: [dRp1_n, k1, k2, slope, dL1_n]
        // C-5 defaults (METAL_MATCHER_ARCHITECTURE.md §7):
        float full_weights[5]    = {1.0f, 1.0f, 1.0f, 0.0f, 1.0f};
        // Quick Screen: k1/k2/slope=0.0 → only dRp1_n and dL1_n contribute
        float quick_weights[5]   = {1.5f, 0.0f, 0.0f, 0.0f, 2.5f};
        // Gaussian confidence: conf = exp(−dist²/σ²). σ=0.35 validated on C-5 dataset.
        float sigma              = 0.35f;
        // Below min_confidence → valid=false
        float min_confidence     = 0.3f;
        // Ferro threshold on |dL1_n|. 99.0 = DISABLED (p3: all metals |dL1_n|≈2.4).
        // Re-enable after Wave 9 S-5 ferro hw-session.
        float ferro_thresh_dL1_n = 99.0f;
    };

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    // Bind to a FingerprintCache instance with default Config (filled by loadConfig).
    // Call AFTER FingerprintCache::init() in setup(). loadConfig() updates cfg_ in-place.
    void init(FingerprintCache& cache);

    // Bind to a FingerprintCache instance with explicit config override.
    // Typically used in unit tests; production always uses init(cache)+loadConfig().
    void init(FingerprintCache& cache, const Config& cfg);

    // Load Config from SD:/CoinTrace/matcher.json.
    // On missing file or parse error: Config unchanged (defaults retained). Logs warning.
    // [SPI-3] spiMutex timeout 50 ms — portMAX_DELAY FORBIDDEN.
    // [SD-A-03] Uses log_i/log_w/log_e (not Logger::*) during SD IO.
    // Returns true if file found and parsed successfully.
    bool loadConfig(SDCardManager* sd, SemaphoreHandle_t spiMutex);

    // ── Matching ──────────────────────────────────────────────────────────────

    // Full 5D match. Normalizes Measurement internally via VectorCompute.
    // Requires complete 4-position cycle (rp[0..2] + l[0..1] valid).
    // Call from doMeasCompute() after STEP_DRIFT capture.
    MatchResult matchFull(const Measurement& m) const;

    // Quick 2D match. Normalizes raw sensor values internally (ADR-M6).
    //   rpLive, lLive  — current live readings  (getLiveRp(), getLiveL())
    //   rpBase, lBase  — no-coin baseline        (getBaseline(), getLBaseline())
    // Uses quick_weights[5] = [1.5, 0, 0, 0, 2.5].
    // ⚠️ Phase 1 does NOT call this — it uses threshold-based classifyQuick() instead.
    //    Phase 2 (after Wave 9 C-5 quick_centroid data) will call this from drawQuickScreen().
    MatchResult matchQuick(float rpLive, float rpBase,
                           float lLive,  float lBase) const;

    // ── Debug ─────────────────────────────────────────────────────────────────

    // Log top-1 + alternatives with per-component distances via log_i.
    // Call after matchFull/matchQuick for manual tuning sessions.
    // [SD-A-03] Uses log_i (not Logger::*).
    void logTopCandidates(const MatchResult& r) const;

    // ── Config access ─────────────────────────────────────────────────────────
    void          resetConfig()        { cfg_ = Config{}; }
    const Config& config()       const { return cfg_; }
    bool          isReady()      const { return cache_ != nullptr && cache_->isReady(); }

private:
    FingerprintCache* cache_ = nullptr;
    Config            cfg_;

    // Shared implementation for matchFull and matchQuick.
    // Calls cache_->query() with the given weights, builds MatchResult with
    // dist_components[], is_ferro, alternatives[], and our cfg_.sigma confidence.
    MatchResult doMatch(float dRp1_n, float k1, float k2, float slope,
                        float dL1_n, const float* weights, uint8_t algo) const;
};
