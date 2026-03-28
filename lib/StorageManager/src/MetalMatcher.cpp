// MetalMatcher.cpp — Metal Classification Layer (Wave 8 C-7a)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// METAL_MATCHER_ARCHITECTURE.md v1.3.0

#include "MetalMatcher.h"
#include "VectorCompute.h"

#include <Arduino.h>       // log_i / log_w / log_e / strlcpy [SD-A-03]
#include <ArduinoJson.h>   // deserializeJson for loadConfig()
#include <math.h>          // expf, fabsf, sqrtf

// ── init() ───────────────────────────────────────────────────────────────────

void MetalMatcher::init(FingerprintCache& cache) {
    cache_ = &cache;
    cfg_   = Config{};
}

void MetalMatcher::init(FingerprintCache& cache, const Config& cfg) {
    cache_ = &cache;
    cfg_   = cfg;
}

// ── loadConfig() ─────────────────────────────────────────────────────────────
// [SPI-3] timeout 50 ms — portMAX_DELAY FORBIDDEN.
// [SD-A-03] No Logger::* during SD IO — log_i/log_w/log_e only.

bool MetalMatcher::loadConfig(SDCardManager* sd, SemaphoreHandle_t spiMutex) {
    if (!sd || !sd->isAvailable()) {
        log_w("Matcher", "SD not available — using default config");
        return false;
    }

    if (xSemaphoreTake(spiMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        log_w("Matcher", "spiMutex timeout reading matcher.json");
        return false;
    }

    File f = SD.open("/CoinTrace/matcher.json", "r");
    if (!f || f.isDirectory()) {
        xSemaphoreGive(spiMutex);
        log_w("Matcher", "matcher.json not found at /CoinTrace/matcher.json — using defaults");
        return false;
    }

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, f);
    f.close();
    xSemaphoreGive(spiMutex);

    if (err) {
        log_e("Matcher", "JSON parse error: %s", err.c_str());
        return false;
    }

    // Parse full_weights[5]
    JsonArray fw = doc["full_weights"];
    if (fw.size() == 5) {
        for (uint8_t i = 0; i < 5; i++) {
            cfg_.full_weights[i] = fw[i] | cfg_.full_weights[i];
        }
    }

    // Parse quick_weights[5]
    JsonArray qw = doc["quick_weights"];
    if (qw.size() == 5) {
        for (uint8_t i = 0; i < 5; i++) {
            cfg_.quick_weights[i] = qw[i] | cfg_.quick_weights[i];
        }
    }

    cfg_.sigma              = doc["sigma"]              | cfg_.sigma;
    cfg_.min_confidence     = doc["min_confidence"]     | cfg_.min_confidence;
    cfg_.ferro_thresh_dL1_n = doc["ferro_thresh_dL1_n"] | cfg_.ferro_thresh_dL1_n;

    log_i("Matcher", "SD config loaded — sigma=%.2f full_w=[%.1f,%.1f,%.1f,%.1f,%.1f]",
          cfg_.sigma,
          cfg_.full_weights[0], cfg_.full_weights[1], cfg_.full_weights[2],
          cfg_.full_weights[3], cfg_.full_weights[4]);
    return true;
}

// ── matchFull() ───────────────────────────────────────────────────────────────

MatchResult MetalMatcher::matchFull(const Measurement& m) const {
    // Normalization inside (ADR-M6: caller never touches 800/2000 constants)
    const float dRp1_n = VectorCompute::dRp1_n(m);
    const float k1v    = VectorCompute::k1(m);
    const float k2v    = VectorCompute::k2(m);
    const float slv    = VectorCompute::slope(m);
    const float dL1_n  = VectorCompute::dL1_n(m);
    return doMatch(dRp1_n, k1v, k2v, slv, dL1_n, cfg_.full_weights, ALGO_FULL);
}

// ── matchQuick() ──────────────────────────────────────────────────────────────

MatchResult MetalMatcher::matchQuick(float rpLive, float rpBase,
                                     float lLive,  float lBase) const {
    // Normalization inside (ADR-M6). METAL_MATCHER_ARCHITECTURE.md §6.
    // dRp1_n: (rpBase−rpLive)/800  — full Rp drop from no-coin baseline to coin on d≈0.6mm
    // dL1_n:  (lLive−lBase)/2000   — L_DATA change relative to no-coin baseline
    const float dRp1_n = (rpBase > 1.0f) ? (rpBase - rpLive) / 800.0f : 0.0f;
    const float dL1_n  = (lLive - lBase) / 2000.0f;
    // k1, k2, slope = 0.0 — unavailable in Quick (no spacers), quick_weights nullify them
    return doMatch(dRp1_n, 0.0f, 0.0f, 0.0f, dL1_n, cfg_.quick_weights, ALGO_QUICK);
}

// ── doMatch() ─────────────────────────────────────────────────────────────────
// Shared implementation for matchFull and matchQuick.
// Calls cache_->query() with given weights, then rebuilds MatchResult with:
//   - our cfg_.sigma (not FingerprintCache::CONFIDENCE_SIGMA)
//   - per-component dist_components[] for tuning
//   - alternatives[] from top-2..top-4 cache results
//
// Note on double-expf (W-QS1 tech debt, METAL_MATCHER_ARCHITECTURE.md §8):
//   cache_->query() computes confidence internally with CONFIDENCE_SIGMA.
//   doMatch() ignores that confidence and recomputes with cfg_.sigma.
//   This means 2 expf() per cache entry. Acceptable overhead for v1 (<1% CPU).
//   Optimisation path: cache_->query(…, return_distances_only) in Phase 2.

MatchResult MetalMatcher::doMatch(float dRp1_n, float k1, float k2, float slope,
                                  float dL1_n, const float* weights, uint8_t algo) const {
    MatchResult result = {};
    result.algo  = algo;
    result.valid = false;

    if (!isReady()) {
        return result;  // cache not ready — caller shows "DB N/A"
    }

    // Fetch top-4 from cache using our weights.
    // top-1 → result, top-2..top-4 → alternatives[].
    QueryResult qr[4] = {};
    const uint8_t n = cache_->query(dRp1_n, k1, k2, slope, dL1_n, qr, 4, weights);

    if (n == 0) {
        return result;
    }

    // ── Top match ─────────────────────────────────────────────────────────────
    const CacheEntry* top = qr[0].entry;

    strlcpy(result.metal_code, top->metal_code, sizeof(result.metal_code));
    strlcpy(result.coin_name,  top->coin_name,  sizeof(result.coin_name));

    result.distance = qr[0].distance;  // already weighted by cache_->query()

    // Recompute confidence with cfg_.sigma (matcher.json tunable, not FingerprintCache const)
    const float sigma2  = cfg_.sigma * cfg_.sigma;
    result.confidence   = expf(-(result.distance * result.distance) / sigma2);
    result.is_ferro     = (fabsf(dL1_n) > cfg_.ferro_thresh_dL1_n);
    result.valid        = (result.confidence >= cfg_.min_confidence);

    // Per-axis weighted contribution: √(wi·Δi²)  — for logTopCandidates() debug output
    const float d0 = dRp1_n - top->dRp1_n;
    const float d1 = k1     - top->k1;
    const float d2 = k2     - top->k2;
    const float d3 = slope  - top->slope;
    const float d4 = dL1_n  - top->dL1_n;
    result.dist_components[0] = sqrtf(weights[0] * d0 * d0);
    result.dist_components[1] = sqrtf(weights[1] * d1 * d1);
    result.dist_components[2] = sqrtf(weights[2] * d2 * d2);
    result.dist_components[3] = sqrtf(weights[3] * d3 * d3);
    result.dist_components[4] = sqrtf(weights[4] * d4 * d4);

    // ── Alternatives (top-2..top-4) ───────────────────────────────────────────
    result.alt_count = 0;
    for (uint8_t i = 1; i < n && result.alt_count < 3; ++i) {
        Alternative& a = result.alternatives[result.alt_count++];
        strlcpy(a.metal_code, qr[i].entry->metal_code, sizeof(a.metal_code));
        strlcpy(a.coin_name,  qr[i].entry->coin_name,  sizeof(a.coin_name));
        a.distance   = qr[i].distance;
        a.confidence = expf(-(a.distance * a.distance) / sigma2);
    }

    return result;
}

// ── logTopCandidates() ────────────────────────────────────────────────────────
// METAL_MATCHER_ARCHITECTURE.md §10.
// [SD-A-03] log_i only — no Logger::*

void MetalMatcher::logTopCandidates(const MatchResult& r) const {
    log_i("Meas", "#1 %-8s conf=%.2f dist=%.4f [%.3f|%.3f|%.3f|%.3f|%.3f] ferro=%s",
          r.metal_code, r.confidence, r.distance,
          r.dist_components[0], r.dist_components[1],
          r.dist_components[2], r.dist_components[3], r.dist_components[4],
          r.is_ferro ? "Y" : "n");
    for (uint8_t i = 0; i < r.alt_count; ++i) {
        log_i("Meas", "#%u %-8s conf=%.2f dist=%.4f",
              i + 2,
              r.alternatives[i].metal_code,
              r.alternatives[i].confidence,
              r.alternatives[i].distance);
    }
}
