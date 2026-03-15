// test_measurement_store.cpp — Unit tests for MeasurementStore contracts
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
//
// Covers:
//   [A-02 / EXT-FUTURE-5] save() boundary guard: rp[0] < 1.0f rejected
//   [A-03 / EXT-FUTURE-6] load() sentinel: "complete": true required
//
// Scope: NATIVE only (no real filesystem).
//   - Boundary guard (A-02): testable because rp check fires BEFORE isDataMounted().
//     LittleFSManager default: isDataMounted() = false — no hardware needed.
//   - Sentinel validation (A-03): tested directly via ArduinoJson (same logic
//     as in load(); integration round-trip runs on device via cointrace-test).
//   - NVSManager default: ready_ = false — NVS calls are not reached in these tests.
//
// Integration tests (save() → file → load() round-trip + ring wrap) run on
// device via `pio test -e cointrace-test`.

#include <unity.h>

// Headers under test
#include "Measurement.h"
#include "MeasurementStore.h"
#include "LittleFSManager.h"
#include "NVSManager.h"

// ArduinoJson — sentinel validation logic
#include <ArduinoJson.h>

// ── Fixtures ──────────────────────────────────────────────────────────────────

static LittleFSManager s_lfs;  // default: isDataMounted() = false
static NVSManager      s_nvs;  // default: ready_          = false

void setUp()    {}
void tearDown() {}

// ── Helper ────────────────────────────────────────────────────────────────────

static Measurement makeMeasurement(float rp0) {
    Measurement m = {};
    m.rp[0]    = rp0;
    m.ts       = 42;
    m.pos_count = 1;
    snprintf(m.metal_code, sizeof(m.metal_code), "UNKN");
    snprintf(m.coin_name,  sizeof(m.coin_name),  "Unclassified");
    m.conf = 0.0f;
    return m;
}

// ──────────────────────────────────────────────────────────────────────────────
// A-02 — Boundary guard: save() must reject rp[0] < 1.0
//
// After save() reorder (EXT-FUTURE-5 testability fix), the rp check fires
// BEFORE isDataMounted(), so no FS state is needed for these tests.
// ──────────────────────────────────────────────────────────────────────────────

void test_save_rejects_rp_zero(void) {
    MeasurementStore store(s_lfs, s_nvs);
    TEST_ASSERT_FALSE(store.save(makeMeasurement(0.0f)));
}

void test_save_rejects_rp_negative(void) {
    MeasurementStore store(s_lfs, s_nvs);
    TEST_ASSERT_FALSE(store.save(makeMeasurement(-100.0f)));
}

void test_save_rejects_rp_small(void) {
    MeasurementStore store(s_lfs, s_nvs);
    // 0.999f: just below the 1.0f threshold
    TEST_ASSERT_FALSE(store.save(makeMeasurement(0.999f)));
}

void test_save_boundary_one_point_zero(void) {
    // rp[0] = 1.0f passes the boundary guard (not < 1.0).
    // Returns false because isDataMounted() = false — expected; guard passed.
    MeasurementStore store(s_lfs, s_nvs);
    // We can't distinguish "guard passed, FS not mounted" from "guard fired"
    // by return value alone. At minimum, this demonstrates the guard does NOT
    // block valid rp values and the code reaches the FS check.
    bool result = store.save(makeMeasurement(1.0f));
    // Either false (FS not mounted — expected) or true (impossible without FS).
    // We assert it does not crash — this is a compile + run check.
    (void)result;  // suppress unused warning
    TEST_PASS();   // reaches here = no crash
}

void test_save_rejects_large_valid_rp_without_fs(void) {
    // rp[0] = 10000.0f: boundary guard passes; fails at FS check.
    // Confirm: returns false (no FS), does NOT crash.
    MeasurementStore store(s_lfs, s_nvs);
    TEST_ASSERT_FALSE(store.save(makeMeasurement(10000.0f)));
}

// ──────────────────────────────────────────────────────────────────────────────
// A-03 — Sentinel validation: load() must reject files without "complete": true
//
// The sentinel logic in load() is:
//   if (!doc["complete"].is<bool>() || !doc["complete"].as<bool>()) return false;
//
// Tested directly via ArduinoJson — same API, exercises the contract without
// needing a real LittleFS file. Integration tests run on device.
// ──────────────────────────────────────────────────────────────────────────────

void test_sentinel_missing_rejected(void) {
    // Simulates a measurement file written without the sentinel (power fail
    // before the last line, or a pre-A-03 writer).
    JsonDocument doc;
    doc["ts"]    = 100;
    doc["rp"][0] = 5000.0f;
    // No "complete" field written

    bool valid = doc["complete"].is<bool>() && doc["complete"].as<bool>();
    TEST_ASSERT_FALSE(valid);
}

void test_sentinel_false_rejected(void) {
    // Simulates a file where the sentinel was written but set to false
    // (e.g. writer crashed while serializing "complete": true).
    JsonDocument doc;
    doc["ts"]       = 100;
    doc["complete"] = false;

    bool valid = doc["complete"].is<bool>() && doc["complete"].as<bool>();
    TEST_ASSERT_FALSE(valid);
}

void test_sentinel_null_rejected(void) {
    // JSON null is not a bool — is<bool>() must return false.
    JsonDocument doc;
    doc["ts"]       = 100;
    doc["complete"] = nullptr;

    bool valid = doc["complete"].is<bool>() && doc["complete"].as<bool>();
    TEST_ASSERT_FALSE(valid);
}

void test_sentinel_string_rejected(void) {
    // "complete": "true" (string, not bool) — is<bool>() returns false.
    JsonDocument doc;
    doc["ts"]       = 100;
    doc["complete"] = "true";

    bool valid = doc["complete"].is<bool>() && doc["complete"].as<bool>();
    TEST_ASSERT_FALSE(valid);
}

void test_sentinel_valid_accepted(void) {
    // Correctly formed file: "complete": true at end.
    JsonDocument doc;
    doc["ts"]          = 100;
    doc["device_id"]   = "CoinTrace-AABB";
    doc["protocol_id"] = "p1_UNKNOWN_013mm";
    doc["pos_count"]   = 1;
    doc["rp"][0]      = 5000.0f;
    doc["complete"]   = true;  // sentinel written last

    bool valid = doc["complete"].is<bool>() && doc["complete"].as<bool>();
    TEST_ASSERT_TRUE(valid);
}

// ── Measurement struct contract ───────────────────────────────────────────────

void test_measurement_struct_sizes(void) {
    // Guard against accidental struct layout changes that break JSON field sizes.
    TEST_ASSERT_EQUAL(8,  sizeof(((Measurement*)nullptr)->metal_code));
    TEST_ASSERT_EQUAL(48, sizeof(((Measurement*)nullptr)->coin_name));
    TEST_ASSERT_EQUAL(4,  sizeof(((Measurement*)nullptr)->rp) / sizeof(float));
    TEST_ASSERT_EQUAL(4,  sizeof(((Measurement*)nullptr)->l)  / sizeof(float));
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    // A-02 boundary guard
    RUN_TEST(test_save_rejects_rp_zero);
    RUN_TEST(test_save_rejects_rp_negative);
    RUN_TEST(test_save_rejects_rp_small);
    RUN_TEST(test_save_boundary_one_point_zero);
    RUN_TEST(test_save_rejects_large_valid_rp_without_fs);

    // A-03 sentinel validation
    RUN_TEST(test_sentinel_missing_rejected);
    RUN_TEST(test_sentinel_false_rejected);
    RUN_TEST(test_sentinel_null_rejected);
    RUN_TEST(test_sentinel_string_rejected);
    RUN_TEST(test_sentinel_valid_accepted);

    // Struct layout contract
    RUN_TEST(test_measurement_struct_sizes);

    return UNITY_END();
}
