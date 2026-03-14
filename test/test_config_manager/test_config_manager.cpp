// test_config_manager.cpp — ConfigManager Unit Tests
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
//
// Covers:
//   - All getXxx() return defaultVal when key is absent
//   - set+get round-trip for every type (int, uint8, uint32, float, bool, string)
//   - setXxx() upserts (second set overwrites first)
//   - getUInt8 clamps values > 255 to defaultVal
//   - getBool accepted literals: "true","1" → true; "false","0","" → false
//   - getBool rejects partial matches: "1abc","10","trueval" → false (exact-match only)
//   - Capacity: 64th entry accepted; 65th silently dropped without crash

#include <cstdio>
#include <unity.h>
#include "ConfigManager.h"
#include <string.h>

// ── Helpers ───────────────────────────────────────────────────────────────────

static ConfigManager cm;

void setUp(void) {
    // Reconstruct a fresh ConfigManager before each test via placement-like reset.
    // Simplest portable approach on embedded: use a new object each time.
    cm = ConfigManager();
}

void tearDown(void) {}

// ── Default values (missing key → caller's default) ──────────────────────────

void test_getInt_missing_key_returns_default(void) {
    TEST_ASSERT_EQUAL_INT32(42, cm.getInt("missing", 42));
}

void test_getInt_negative_default(void) {
    TEST_ASSERT_EQUAL_INT32(-100, cm.getInt("x", -100));
}

void test_getUInt8_missing_key_returns_default(void) {
    TEST_ASSERT_EQUAL_UINT8(7, cm.getUInt8("missing", 7));
}

void test_getUInt32_missing_key_returns_default(void) {
    TEST_ASSERT_EQUAL_UINT32(16000000UL, cm.getUInt32("freq", 16000000UL));
}

void test_getFloat_missing_key_returns_default(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.85f, cm.getFloat("missing", 0.85f));
}

void test_getBool_missing_key_returns_default_false(void) {
    TEST_ASSERT_FALSE(cm.getBool("missing", false));
}

void test_getBool_missing_key_returns_default_true(void) {
    TEST_ASSERT_TRUE(cm.getBool("missing", true));
}

void test_getString_missing_key_returns_default(void) {
    TEST_ASSERT_EQUAL_STRING("defval", cm.getString("missing", "defval"));
}

// ── Round-trip: set + get ─────────────────────────────────────────────────────

void test_setInt_getInt_roundtrip(void) {
    cm.setInt("ldc1101.spi_cs_pin", 5);
    TEST_ASSERT_EQUAL_INT32(5, cm.getInt("ldc1101.spi_cs_pin", 0));
}

void test_setInt_negative_roundtrip(void) {
    cm.setInt("neg", -999);
    TEST_ASSERT_EQUAL_INT32(-999, cm.getInt("neg", 0));
}

void test_setUInt32_getUInt32_roundtrip(void) {
    cm.setUInt32("ldc1101.clkin_freq_hz", 16000000UL);
    TEST_ASSERT_EQUAL_UINT32(16000000UL, cm.getUInt32("ldc1101.clkin_freq_hz", 0));
}

void test_setFloat_getFloat_roundtrip(void) {
    cm.setFloat("ldc1101.coin_detect_threshold", 0.85f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.85f, cm.getFloat("ldc1101.coin_detect_threshold", 0.0f));
}

void test_setFloat_release_threshold_roundtrip(void) {
    cm.setFloat("ldc1101.coin_release_threshold", 0.92f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.92f, cm.getFloat("ldc1101.coin_release_threshold", 0.0f));
}

void test_setBool_true_stores_true(void) {
    cm.setBool("enabled", true);
    TEST_ASSERT_TRUE(cm.getBool("enabled", false));
}

void test_setBool_false_stores_false(void) {
    cm.setBool("enabled", false);
    TEST_ASSERT_FALSE(cm.getBool("enabled", true));
}

void test_setString_getString_roundtrip(void) {
    cm.setString("name", "LDC1101");
    TEST_ASSERT_EQUAL_STRING("LDC1101", cm.getString("name", ""));
}

// ── getUInt8 type clamping ────────────────────────────────────────────────────

void test_getUInt8_value_255_accepted(void) {
    cm.setInt("u8max", 255);
    TEST_ASSERT_EQUAL_UINT8(255, cm.getUInt8("u8max", 0));
}

void test_getUInt8_value_0_accepted(void) {
    cm.setInt("u8zero", 0);
    TEST_ASSERT_EQUAL_UINT8(0, cm.getUInt8("u8zero", 99));
}

void test_getUInt8_value_256_returns_default(void) {
    // 256 exceeds uint8 range — getUInt8 should return the default, not 256 truncated to 0
    cm.setInt("u8ovf", 256);
    TEST_ASSERT_EQUAL_UINT8(42, cm.getUInt8("u8ovf", 42));
}

// ── getBool accepted literals ─────────────────────────────────────────────────

void test_getBool_string_true_returns_true(void) {
    cm.setString("b", "true");
    TEST_ASSERT_TRUE(cm.getBool("b", false));
}

void test_getBool_string_1_returns_true(void) {
    cm.setString("b", "1");
    TEST_ASSERT_TRUE(cm.getBool("b", false));
}

void test_getBool_string_false_returns_false(void) {
    cm.setString("b", "false");
    TEST_ASSERT_FALSE(cm.getBool("b", true));
}

void test_getBool_string_0_returns_false(void) {
    cm.setString("b", "0");
    TEST_ASSERT_FALSE(cm.getBool("b", true));
}

void test_getBool_empty_string_returns_false(void) {
    cm.setString("b", "");
    TEST_ASSERT_FALSE(cm.getBool("b", true));
}

// Reject partial matches — these must return false (exact-match only, see getBool fix)
void test_getBool_string_10_returns_false(void) {
    cm.setString("b", "10");
    TEST_ASSERT_FALSE(cm.getBool("b", false));
}

void test_getBool_string_1abc_returns_false(void) {
    cm.setString("b", "1abc");
    TEST_ASSERT_FALSE(cm.getBool("b", false));
}

void test_getBool_string_trueval_returns_false(void) {
    cm.setString("b", "trueval");
    TEST_ASSERT_FALSE(cm.getBool("b", false));
}

// ── Upsert: second setXxx() overwrites first ─────────────────────────────────

void test_setInt_upsert_overwrites(void) {
    cm.setInt("pin", 5);
    cm.setInt("pin", 12);
    TEST_ASSERT_EQUAL_INT32(12, cm.getInt("pin", 0));
}

void test_setFloat_upsert_overwrites(void) {
    cm.setFloat("thresh", 0.80f);
    cm.setFloat("thresh", 0.85f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.85f, cm.getFloat("thresh", 0.0f));
}

void test_upsert_does_not_grow_count(void) {
    // Setting the same key twice should NOT add a second entry
    cm.setInt("key1", 1);
    cm.setInt("key2", 2);
    cm.setInt("key1", 99);  // upsert
    // Both keys still accessible with correct values
    TEST_ASSERT_EQUAL_INT32(99, cm.getInt("key1", 0));
    TEST_ASSERT_EQUAL_INT32(2,  cm.getInt("key2", 0));
}

// ── Multiple independent keys ─────────────────────────────────────────────────

void test_multiple_keys_independent(void) {
    cm.setInt("ldc1101.spi_cs_pin",    5);
    cm.setInt("ldc1101.resp_time",     7);  // uint8 stored as int — getUInt8 reads it back
    cm.setFloat("ldc1101.detect",  0.85f);
    cm.setBool ("ldc1101.enabled", true);

    TEST_ASSERT_EQUAL_INT32 (5,     cm.getInt  ("ldc1101.spi_cs_pin", 0));
    TEST_ASSERT_EQUAL_UINT8 (7,     cm.getUInt8("ldc1101.resp_time",  0));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.85f, cm.getFloat("ldc1101.detect",  0.0f));
    TEST_ASSERT_TRUE (cm.getBool("ldc1101.enabled", false));
}

// ── Capacity boundary ─────────────────────────────────────────────────────────

void test_capacity_fills_to_64_entries(void) {
    char key[16];
    for (int i = 0; i < 64; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        cm.setInt(key, i);
    }
    // All 64 entries must be retrievable
    for (int i = 0; i < 64; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        TEST_ASSERT_EQUAL_INT32(i, cm.getInt(key, -1));
    }
}

void test_capacity_overflow_silent_drop(void) {
    char key[16];
    // Fill to capacity
    for (int i = 0; i < 64; i++) {
        snprintf(key, sizeof(key), "slot%d", i);
        cm.setInt(key, i * 10);
    }
    // 65th entry — must not crash; existing entries must not be corrupted
    cm.setInt("overflow_key", 999);
    TEST_ASSERT_EQUAL_INT32(0, cm.getInt("overflow_key", 0));  // dropped → returns default
    // Earlier entries still intact
    TEST_ASSERT_EQUAL_INT32(0,  cm.getInt("slot0",  -1));
    TEST_ASSERT_EQUAL_INT32(630, cm.getInt("slot63", -1));
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // Defaults
    RUN_TEST(test_getInt_missing_key_returns_default);
    RUN_TEST(test_getInt_negative_default);
    RUN_TEST(test_getUInt8_missing_key_returns_default);
    RUN_TEST(test_getUInt32_missing_key_returns_default);
    RUN_TEST(test_getFloat_missing_key_returns_default);
    RUN_TEST(test_getBool_missing_key_returns_default_false);
    RUN_TEST(test_getBool_missing_key_returns_default_true);
    RUN_TEST(test_getString_missing_key_returns_default);

    // Round-trip
    RUN_TEST(test_setInt_getInt_roundtrip);
    RUN_TEST(test_setInt_negative_roundtrip);
    RUN_TEST(test_setUInt32_getUInt32_roundtrip);
    RUN_TEST(test_setFloat_getFloat_roundtrip);
    RUN_TEST(test_setFloat_release_threshold_roundtrip);
    RUN_TEST(test_setBool_true_stores_true);
    RUN_TEST(test_setBool_false_stores_false);
    RUN_TEST(test_setString_getString_roundtrip);

    // UInt8 clamping
    RUN_TEST(test_getUInt8_value_255_accepted);
    RUN_TEST(test_getUInt8_value_0_accepted);
    RUN_TEST(test_getUInt8_value_256_returns_default);

    // getBool exact-match
    RUN_TEST(test_getBool_string_true_returns_true);
    RUN_TEST(test_getBool_string_1_returns_true);
    RUN_TEST(test_getBool_string_false_returns_false);
    RUN_TEST(test_getBool_string_0_returns_false);
    RUN_TEST(test_getBool_empty_string_returns_false);
    RUN_TEST(test_getBool_string_10_returns_false);
    RUN_TEST(test_getBool_string_1abc_returns_false);
    RUN_TEST(test_getBool_string_trueval_returns_false);

    // Upsert
    RUN_TEST(test_setInt_upsert_overwrites);
    RUN_TEST(test_setFloat_upsert_overwrites);
    RUN_TEST(test_upsert_does_not_grow_count);

    // Multiple keys
    RUN_TEST(test_multiple_keys_independent);

    // Capacity
    RUN_TEST(test_capacity_fills_to_64_entries);
    RUN_TEST(test_capacity_overflow_silent_drop);

    return UNITY_END();
}
