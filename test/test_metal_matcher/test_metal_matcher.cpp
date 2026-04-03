// test_metal_matcher.cpp — Unit tests for MetalMatcher (Wave 8 C-7a)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
//
// Tests MetalMatcher::matchFull(), matchQuick(), loadConfig() (defaults),
// is_ferro detection, alternatives[], dist_components[], and edge cases.
//
// Strategy: inject known CacheEntry values into FingerprintCache via UNIT_TEST
// helpers, then call MetalMatcher methods with matching/mismatching inputs and
// assert on MatchResult fields.
//
// Compile env: native-test (pio test -e native-test).
// No hardware, no SD, no LittleFS, no FreeRTOS required.

#include <unity.h>
#include "MetalMatcher.h"
#include "FingerprintCache.h"
#include "Measurement.h"
#include <math.h>
#include <string.h>

// ── Fixture ───────────────────────────────────────────────────────────────────

static FingerprintCache cache;
static MetalMatcher     matcher;

// Build a CacheEntry from raw vector components (C-5 p3 protocol values).
static CacheEntry makeEntry(const char* id, const char* metal_code, const char* coin_name,
                            float dRp1_n, float k1, float k2, float df_n, float dL1_n,
                            float df1_n = 0.0f) {
    CacheEntry e = {};
    strncpy(e.id,          id,         sizeof(e.id)          - 1);
    strncpy(e.metal_code,  metal_code, sizeof(e.metal_code)  - 1);
    strncpy(e.coin_name,   coin_name,  sizeof(e.coin_name)   - 1);
    strncpy(e.protocol_id, "p3_v0",    sizeof(e.protocol_id) - 1);
    e.dRp1_n        = dRp1_n;
    e.k1            = k1;
    e.k2            = k2;
    e.df_n          = df_n;
    e.dL1_n         = dL1_n;
    e.df1_n         = df1_n;
    e.radius_95pct  = 0.05f;
    e.records_count = 5;
    return e;
}

// Build a Measurement whose VectorCompute output exactly matches the given components.
// k1 = rp[1]/rp[0] and dRp1_n = (rp[0]-rp[1])/800 are coupled: rp[0]*(1-k1) = dRp1_n*800.
// => rp[0] derived from both; rp[1] = rp[0]*k1; rp[2] = rp[0]*k2.
// df_n is passed separately to matchFull() — not derived from Measurement fields.
// Entries' df_n field should match the value passed to matchFull() for exact-match tests.
static Measurement makeMeas(float dRp1_n_target, float k1_target,
                             float k2_target, float dL1_n_target) {
    Measurement m = {};
    // Derive rp[0] from coupled constraint: rp[0]*(1−k1) = dRp1_n*800.
    // Guard: k1 near 1.0 (near-baseline, very low conductivity change) → use fallback.
    const float rp0 = (fabsf(1.0f - k1_target) > 1e-4f)
                      ? (dRp1_n_target * 800.0f / (1.0f - k1_target))
                      : 10000.0f;
    m.rp[0] = rp0;
    m.rp[1] = rp0 * k1_target;                   // k1_actual = rp[1]/rp[0] = k1_target
    m.rp[2] = rp0 * k2_target;                   // k2_actual = rp[2]/rp[0] = k2_target
    m.rp[3] = rp0;                               // drift = 0 (rp[3]==rp[0])
    m.l[0]  = 5000.0f;
    m.l[1]  = 5000.0f - dL1_n_target * 2000.0f;
    m.pos_count = 4;
    return m;
}

void setUp() {
    cache.resetTest();
    // Re-bind matcher to fresh cache each test
    MetalMatcher::Config cfg;
    matcher.init(cache, cfg);
}

void tearDown() {}

// ── Tests ─────────────────────────────────────────────────────────────────────

// 1. matchFull() returns invalid result when cache is empty
void test_matchFull_invalid_on_empty_cache() {
    // cache not populated — isReady() returns false
    Measurement m = makeMeas(0.625f, 0.75f, 0.55f, -0.001f);
    MatchResult r = matcher.matchFull(m);
    TEST_ASSERT_FALSE(r.valid);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, r.confidence);
}

// 2. matchQuick() returns invalid result when cache is empty
void test_matchQuick_invalid_on_empty_cache() {
    MatchResult r = matcher.matchQuick(9000.0f, 10000.0f, 5100.0f, 5000.0f);
    TEST_ASSERT_FALSE(r.valid);
}

// 3. Exact match gives confidence = 1.0 and valid = true
void test_matchFull_exact_match_confidence_one() {
    const float dRp1_n = 0.625f, k1 = 0.75f, k2 = 0.55f, sl = -0.225f, dL1_n = -0.001f;
    cache.loadTestEntry(makeEntry("ag/coin1", "XAG925", "Test Silver", dRp1_n, k1, k2, sl, dL1_n));
    cache.beginTest();

    Measurement m = makeMeas(dRp1_n, k1, k2, dL1_n);
    MatchResult r = matcher.matchFull(m);

    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, r.confidence);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, r.distance);
    TEST_ASSERT_EQUAL_STRING("XAG925", r.metal_code);
    TEST_ASSERT_EQUAL_UINT8(ALGO_FULL, r.algo);
}

// 4. Distant match gives low confidence and valid=false (below min_confidence=0.3)
void test_matchFull_distant_match_low_confidence() {
    // Entry at dRp1_n=0.1
    cache.loadTestEntry(makeEntry("cu/coin1", "XCU", "Test Copper", 0.1f, 0.9f, 0.82f, -0.04f, 0.0f));
    cache.beginTest();

    // Query far away: dRp1_n=0.9 (silver territory)
    Measurement m = makeMeas(0.9f, 0.75f, 0.55f, 0.0f);
    MatchResult r = matcher.matchFull(m);

    // dist will be large → confidence < min_confidence → valid=false
    TEST_ASSERT_FALSE(r.valid);
    TEST_ASSERT_LESS_THAN_FLOAT(0.3f, r.confidence);
}

// 5. Correct top-1 selection from two entries
void test_matchFull_selects_closer_entry() {
    // XAG925 centroid at dRp1_n=0.65
    cache.loadTestEntry(makeEntry("ag925/c1", "XAG925", "Silver 925", 0.65f, 0.74f, 0.54f, -0.23f, -0.001f));
    // XCU centroid at dRp1_n=0.20
    cache.loadTestEntry(makeEntry("xcu/c1",   "XCU",    "Copper",     0.20f, 0.88f, 0.78f, -0.11f,  0.0f));
    cache.beginTest();

    // Query near XAG925
    Measurement m = makeMeas(0.63f, 0.74f, 0.54f, -0.001f);
    MatchResult r = matcher.matchFull(m);

    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL_STRING("XAG925", r.metal_code);
    TEST_ASSERT_EQUAL_UINT8(1, r.alt_count);
    TEST_ASSERT_EQUAL_STRING("XCU", r.alternatives[0].metal_code);
    // XCU alternative must have lower confidence than XAG925
    TEST_ASSERT_GREATER_THAN_FLOAT(r.alternatives[0].confidence, r.confidence);
}

// 6. Alternatives are populated and sorted by distance
void test_matchFull_alternatives_sorted_by_distance() {
    cache.loadTestEntry(makeEntry("ag999/c1", "XAG999", "Silver 999", 0.72f, 0.72f, 0.52f, -0.24f, -0.001f));
    cache.loadTestEntry(makeEntry("ag925/c1", "XAG925", "Silver 925", 0.65f, 0.74f, 0.54f, -0.23f, -0.001f));
    cache.loadTestEntry(makeEntry("ag833/c1", "XAG833", "Silver 833", 0.58f, 0.76f, 0.57f, -0.215f,-0.001f));
    cache.loadTestEntry(makeEntry("xcu/c1",   "XCU",    "Copper",     0.20f, 0.88f, 0.78f, -0.11f,  0.0f));
    cache.beginTest();

    // Query at XAG925 centroid
    Measurement m = makeMeas(0.65f, 0.74f, 0.54f, -0.001f);
    MatchResult r = matcher.matchFull(m);

    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL_STRING("XAG925", r.metal_code);
    TEST_ASSERT_EQUAL_UINT8(3, r.alt_count);
    // Alternatives must be in ascending distance order
    TEST_ASSERT_LESS_OR_EQUAL_FLOAT(r.alternatives[1].distance, r.alternatives[0].distance);
    TEST_ASSERT_LESS_OR_EQUAL_FLOAT(r.alternatives[2].distance, r.alternatives[1].distance);
}

// 7. dist_components[] non-zero for active weight axes, zero for w=0 axis
void test_matchFull_dist_components_respect_weights() {
    MetalMatcher::Config cfg;
    cfg.full_weights[3] = 0.0f;  // df_n weight = 0
    matcher.init(cache, cfg);

    cache.loadTestEntry(makeEntry("ag925/c1", "XAG925", "Silver 925", 0.65f, 0.74f, 0.54f, -0.23f, -0.001f));
    cache.beginTest();

    // Query exactly at centroid
    Measurement m = makeMeas(0.65f, 0.74f, 0.54f, -0.001f);
    // slope component is w=0, so dist_components[3] must be 0
    MatchResult r = matcher.matchFull(m);

    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, r.dist_components[3]);
}

// 8. matchQuick() uses quick_weights (k1/k2/slope=0) and ALGO_QUICK
void test_matchQuick_algo_and_weights() {
    // Entry with distinctive dRp1_n and dL1_n
    cache.loadTestEntry(makeEntry("ag925/c1", "XAG925", "Silver 925", 0.65f, 0.74f, 0.54f, -0.23f, -0.001f));
    cache.beginTest();

    // rpBase=10000, rpLive=4800 → dRp1_n=(10000-4800)/800=6.5 — far from centroid
    // Use values matching entry: dRp1_n=0.65 → rpBase-rpLive=0.65*800=520 → rpLive=rpBase-520
    const float rpBase = 10000.0f;
    const float rpLive = rpBase - 0.65f * 800.0f;  // dRp1_n = 0.65
    const float lBase  = 5000.0f;
    const float lLive  = lBase + (-0.001f) * 2000.0f; // dL1_n = -0.001

    MatchResult r = matcher.matchQuick(rpLive, rpBase, lLive, lBase);

    TEST_ASSERT_EQUAL_UINT8(ALGO_QUICK, r.algo);
    TEST_ASSERT_TRUE(r.valid);
    // dist_components[1],[2],[3] must be 0 (quick_weights for k1/k2/slope = 0)
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, r.dist_components[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, r.dist_components[2]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, r.dist_components[3]);
}

// 9. is_ferro flag: DISABLED at default threshold (99.0) — never fires for typical metals
void test_is_ferro_disabled_at_default_threshold() {
    cache.loadTestEntry(makeEntry("xfe/c1", "XFE", "Steel", 0.20f, 0.88f, 0.78f, -0.11f, -2.4f));
    cache.beginTest();

    // Query near XFE centroid (dL1_n ≈ -2.4, typical from C-5 data)
    Measurement m = makeMeas(0.20f, 0.88f, 0.78f, -2.4f);
    MatchResult r = matcher.matchFull(m);

    // ferro_thresh_dL1_n = 99.0 → |dL1_n|=2.4 < 99.0 → is_ferro must be false
    TEST_ASSERT_FALSE(r.is_ferro);
}

// 10. is_ferro fires when thresh is lowered below actual |dL1_n|
void test_is_ferro_fires_when_thresh_lowered() {
    MetalMatcher::Config cfg;
    cfg.ferro_thresh_dL1_n = 2.0f;  // lower than typical |dL1_n| = 2.4
    matcher.init(cache, cfg);

    cache.loadTestEntry(makeEntry("xfe/c1", "XFE", "Steel", 0.20f, 0.88f, 0.78f, -0.11f, -2.4f));
    cache.beginTest();

    Measurement m = makeMeas(0.20f, 0.88f, 0.78f, -2.4f);
    MatchResult r = matcher.matchFull(m);

    TEST_ASSERT_TRUE(r.is_ferro);  // |dL1_n|=2.4 > thresh=2.0
}

// 11. Confidence formula: exp(−dist²/σ²) with sigma=0.35
void test_confidence_formula_matches_expected() {
    // Use a realistic centroid (k1<1 required for consistent makeMeas derivation)
    // Entry at {dRp1_n=0.5, k1=0.8, k2=0.6, slope=(0.6-1)/2=-0.2, dL1_n=0.0}
    const float dRp1 = 0.5f, k1v = 0.8f, k2v = 0.6f, dL1 = 0.0f;
    const float slopev = (k2v - 1.0f) / 2.0f;  // = -0.2 (VectorCompute OLS for p3)
    cache.loadTestEntry(makeEntry("test/c1", "XAG999", "Test", dRp1, k1v, k2v, slopev, dL1));
    cache.beginTest();

    // Exact match → confidence = exp(0) = 1.0
    MatchResult r_exact = matcher.matchFull(makeMeas(dRp1, k1v, k2v, dL1));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, r_exact.confidence);

    // Offset on dRp1_n axis by 0.35 (keeping k1/k2/dL1 identical):
    //   dist = sqrt(w0*0.35² + w1*0 + w2*0 + w3*0 + w4*0) = 0.35 (w0=1.0)
    //   conf = exp(−0.35²/0.35²) = exp(−1) ≈ 0.3679
    MatchResult r_off = matcher.matchFull(makeMeas(dRp1 + 0.35f, k1v, k2v, dL1));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, expf(-1.0f), r_off.confidence);
    TEST_ASSERT_GREATER_THAN_FLOAT(r_off.confidence, r_exact.confidence);
}

// 12. resetConfig() restores defaults
void test_resetConfig_restores_defaults() {
    MetalMatcher::Config custom;
    custom.sigma = 0.10f;
    matcher.init(cache, custom);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.10f, matcher.config().sigma);

    matcher.resetConfig();
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.35f, matcher.config().sigma);  // default
}

// 13. isReady() reflects cache state
void test_isReady_false_on_empty_cache() {
    TEST_ASSERT_FALSE(matcher.isReady());

    cache.loadTestEntry(makeEntry("ag925/c1", "XAG925", "Silver", 0.65f, 0.74f, 0.54f, -0.23f, -0.001f));
    cache.beginTest();

    TEST_ASSERT_TRUE(matcher.isReady());
}

// 14. df1_n weight in matchFull() — non-zero w5 separates entries identical in 5D
void test_matchFull_df1n_weight_separates_identical_5d_entries() {
    // Two centroids: identical 5D position, differ only in df1_n (Kennedy_B vs USSR_A scenario)
    const float dRp1 = 0.65f, k1v = 0.74f, k2v = 0.54f, df_nv = 0.80f, dL1 = -0.001f;
    cache.loadTestEntry(makeEntry("kennedy", "XKENED", "Kennedy Half",
                                  dRp1, k1v, k2v, df_nv, dL1, /*df1_n=*/0.6f));
    cache.loadTestEntry(makeEntry("ussr",    "XUSSR",  "USSR 10R",
                                  dRp1, k1v, k2v, df_nv, dL1, /*df1_n=*/0.7f));
    cache.beginTest();

    // Config with df1_n weight=2.0 active
    MetalMatcher::Config cfg;
    cfg.full_weights[5] = 2.0f;
    matcher.init(cache, cfg);

    // Query at df1_n=0.6 (kennedy position) — identical 5D, df1_n distinguishes
    Measurement m = makeMeas(dRp1, k1v, k2v, dL1);
    MatchResult r = matcher.matchFull(m, df_nv, /*df1_n=*/0.6f);

    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL_STRING("XKENED", r.metal_code);
    TEST_ASSERT_EQUAL_UINT8(1, r.alt_count);
    TEST_ASSERT_EQUAL_STRING("XUSSR", r.alternatives[0].metal_code);
    // Kennedy exact match on all 6 dims → confidence=1.0; USSR separates by df1_n
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, r.confidence);
    TEST_ASSERT_GREATER_THAN_FLOAT(r.alternatives[0].confidence, r.confidence);
    // dist_components[5] for KENNEDY = 0 (exact match on df1_n)
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, r.dist_components[5]);
}

// ── Runner ────────────────────────────────────────────────────────────────────

void setup() {}
void loop()  {}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_matchFull_invalid_on_empty_cache);
    RUN_TEST(test_matchQuick_invalid_on_empty_cache);
    RUN_TEST(test_matchFull_exact_match_confidence_one);
    RUN_TEST(test_matchFull_distant_match_low_confidence);
    RUN_TEST(test_matchFull_selects_closer_entry);
    RUN_TEST(test_matchFull_alternatives_sorted_by_distance);
    RUN_TEST(test_matchFull_dist_components_respect_weights);
    RUN_TEST(test_matchQuick_algo_and_weights);
    RUN_TEST(test_is_ferro_disabled_at_default_threshold);
    RUN_TEST(test_is_ferro_fires_when_thresh_lowered);
    RUN_TEST(test_confidence_formula_matches_expected);
    RUN_TEST(test_resetConfig_restores_defaults);
    RUN_TEST(test_isReady_false_on_empty_cache);
    RUN_TEST(test_matchFull_df1n_weight_separates_identical_5d_entries);
    return UNITY_END();
}
