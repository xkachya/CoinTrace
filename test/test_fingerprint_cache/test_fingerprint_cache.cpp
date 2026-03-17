// test_fingerprint_cache.cpp — Unit tests for FingerprintCache::query() (Wave 8 B-2)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
//
// Tests the query() distance metric, insertion-sort ordering, confidence
// transform, and edge cases.  init() is bypassed entirely — all entries are
// injected via the #ifdef UNIT_TEST loadTestEntry()/beginTest()/resetTest()
// helpers added to FingerprintCache.h.
//
// Compile env: native-test (pio test -e native-test).
// No hardware, no SD, no LittleFS, no FreeRTOS.

#include <unity.h>
#include "FingerprintCache.h"
#include <math.h>
#include <string.h>

// ── Fixture ───────────────────────────────────────────────────────────────────

static FingerprintCache cache;

// Build a CacheEntry from 5D fingerprint components.
static CacheEntry makeEntry(const char* id, float dRp1_n, float k1, float k2,
                            float slope, float dL1_n, float radius = 0.05f,
                            uint16_t records = 10) {
    CacheEntry e = {};
    strncpy(e.id,          id,       sizeof(e.id)      - 1);
    strncpy(e.metal_code,  "XAG925", sizeof(e.metal_code) - 1);
    strncpy(e.coin_name,   "Test",   sizeof(e.coin_name) - 1);
    strncpy(e.protocol_id, "p1_v0",  sizeof(e.protocol_id) - 1);
    e.dRp1_n       = dRp1_n;
    e.k1           = k1;
    e.k2           = k2;
    e.slope        = slope;
    e.dL1_n        = dL1_n;
    e.radius_95pct = radius;
    e.records_count = records;
    return e;
}

void setUp() {
    cache.resetTest();
}

void tearDown() {}

// ── Tests ─────────────────────────────────────────────────────────────────────

// 1. query() returns 0 when cache is uninitialised (count_ == 0)
void test_query_returns_zero_on_empty_cache() {
    QueryResult results[FingerprintCache::QUERY_TOP_N];
    const uint8_t n = cache.query(0.5f, 0.75f, 0.6f, -0.13f, 0.0f,
                                   results, FingerprintCache::QUERY_TOP_N);
    TEST_ASSERT_EQUAL_UINT8(0, n);
    TEST_ASSERT_FALSE(cache.isReady());
}

// 2. Exact match: query vector == cache entry → distance = 0, confidence = 1.0
void test_exact_match_gives_full_confidence() {
    const float dRp1_n = 0.625f;
    const float k1     = 0.75f;
    const float k2     = 0.611f;
    const float slope  = -0.129f;
    const float dL1_n  = 0.0002f;

    CacheEntry e = makeEntry("ag/mtt", dRp1_n, k1, k2, slope, dL1_n);
    cache.loadTestEntry(e);
    cache.beginTest();

    QueryResult results[FingerprintCache::QUERY_TOP_N];
    const uint8_t n = cache.query(dRp1_n, k1, k2, slope, dL1_n,
                                   results, FingerprintCache::QUERY_TOP_N);
    TEST_ASSERT_EQUAL_UINT8(1, n);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, results[0].distance);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, results[0].confidence);
    TEST_ASSERT_EQUAL_STRING("ag/mtt", results[0].entry->id);
}

// 3. confidence = exp(-d²/σ²): d = σ → confidence ≈ 0.3679 (1/e)
void test_confidence_at_sigma_distance() {
    // Place a single entry at a known point.
    // Query at distance = CONFIDENCE_SIGMA from the entry.
    // For simplicity: entry at (0,0,0,0,0), query offset only in dRp1_n by σ.
    CacheEntry e = makeEntry("ref", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    cache.loadTestEntry(e);
    cache.beginTest();

    const float sigma = FingerprintCache::CONFIDENCE_SIGMA;
    QueryResult results[FingerprintCache::QUERY_TOP_N];
    cache.query(sigma, 0.0f, 0.0f, 0.0f, 0.0f,
                results, FingerprintCache::QUERY_TOP_N);

    // distance == sigma, confidence == exp(-1) ≈ 0.3679
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, sigma, results[0].distance);
    TEST_ASSERT_FLOAT_WITHIN(0.002f, expf(-1.0f), results[0].confidence);
}

// 4. Top-N ordering: best match first (smallest distance first)
void test_top_n_ordered_by_distance() {
    // Insert 5 entries at increasing distances from origin
    for (uint8_t i = 1; i <= 5; ++i) {
        char id[8]; snprintf(id, sizeof(id), "e%u", i);
        // Place entries at (0.1*i, 0, 0, 0, 0)
        cache.loadTestEntry(makeEntry(id, 0.1f * i, 0.0f, 0.0f, 0.0f, 0.0f));
    }
    cache.beginTest();

    QueryResult results[5];
    const uint8_t n = cache.query(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, results, 5);

    TEST_ASSERT_EQUAL_UINT8(5, n);
    for (uint8_t i = 1; i < n; ++i) {
        TEST_ASSERT_LESS_OR_EQUAL_FLOAT(results[i].distance, results[i-1].distance);
    }
    // Closest should be entry "e1" (dRp1_n=0.1, distance=0.1)
    TEST_ASSERT_EQUAL_STRING("e1", results[0].entry->id);
}

// 5. maxResults truncation: request 3 results from a cache of 10 → exactly 3 returned
void test_max_results_truncation() {
    for (uint8_t i = 1; i <= 10; ++i) {
        char id[8]; snprintf(id, sizeof(id), "x%u", i);
        cache.loadTestEntry(makeEntry(id, 0.1f * i, 0.5f, 0.4f, -0.1f, 0.0f));
    }
    cache.beginTest();

    QueryResult results[10];
    const uint8_t n = cache.query(0.0f, 0.5f, 0.4f, -0.1f, 0.0f, results, 3);
    TEST_ASSERT_EQUAL_UINT8(3, n);
}

// 6. Two very similar entries: lower-distance one is ranked first
void test_closer_entry_ranked_first() {
    // Entry A: close to query point
    CacheEntry a = makeEntry("near", 0.6f, 0.75f, 0.60f, -0.13f, 0.001f);
    // Entry B: far from query point
    CacheEntry b = makeEntry("far",  0.2f, 0.30f, 0.20f, -0.05f, 0.050f);
    cache.loadTestEntry(a);
    cache.loadTestEntry(b);
    cache.beginTest();

    // Query exactly at A's position
    QueryResult results[2];
    cache.query(0.6f, 0.75f, 0.60f, -0.13f, 0.001f, results, 2);
    TEST_ASSERT_EQUAL_STRING("near", results[0].entry->id);
    TEST_ASSERT_EQUAL_STRING("far",  results[1].entry->id);
    TEST_ASSERT_LESS_OR_EQUAL_FLOAT(results[1].distance, results[0].distance);
}

// ── Runner ────────────────────────────────────────────────────────────────────

void setup() {}
void loop()  {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_query_returns_zero_on_empty_cache);
    RUN_TEST(test_exact_match_gives_full_confidence);
    RUN_TEST(test_confidence_at_sigma_distance);
    RUN_TEST(test_top_n_ordered_by_distance);
    RUN_TEST(test_max_results_truncation);
    RUN_TEST(test_closer_entry_ranked_first);
    return UNITY_END();
}
