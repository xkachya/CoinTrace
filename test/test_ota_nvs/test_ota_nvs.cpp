// test_ota_nvs.cpp — Unit tests for NVSManager OTA metadata (Wave 8 A-4)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
//
// Tests the NVS "ota" namespace methods: save/load/confirm/clear.
// All invariants defined in NVSManager.h §OTA namespace (ADR-007).
//
// Compile env: native-test (pio test -e native-test)
// No hardware, no SPI, no FreeRTOS.

#include <unity.h>
#include "NVSManager.h"
#include "Preferences.h"  // for Preferences::resetAll()
#include <string.h>

// ── Fixture ───────────────────────────────────────────────────────────────────

static NVSManager nvs;

void setUp() {
    Preferences::resetAll();
    nvs.end();
    nvs.begin();
}

void tearDown() {
    nvs.end();
}

// ── Tests ─────────────────────────────────────────────────────────────────────

// 1. loadOtaMeta() on a fresh namespace returns false (no OTA ever applied).
void test_load_fresh_returns_false() {
    NVSManager::OtaMeta m;
    TEST_ASSERT_FALSE(nvs.loadOtaMeta(m));
}

// 2. Fresh load: out struct is zero-initialised — no garbage in fields.
void test_load_fresh_zeroes_struct() {
    NVSManager::OtaMeta m;
    m.pending   = true;   // prefill with noise
    m.confirmed = true;
    m.pre_version[0] = 'X';
    nvs.loadOtaMeta(m);
    TEST_ASSERT_FALSE(m.pending);
    TEST_ASSERT_FALSE(m.confirmed);
    TEST_ASSERT_EQUAL_CHAR('\0', m.pre_version[0]);
}

// 3. saveOtaMeta() returns true when NVS is ready.
void test_save_returns_true() {
    TEST_ASSERT_TRUE(nvs.saveOtaMeta("1.0.0-dev"));
}

// 4. After saveOtaMeta(): loadOtaMeta() returns true.
void test_save_then_load_returns_true() {
    nvs.saveOtaMeta("1.0.0-dev");
    NVSManager::OtaMeta m;
    TEST_ASSERT_TRUE(nvs.loadOtaMeta(m));
}

// 5. After saveOtaMeta(): pending=true, confirmed=false.
void test_save_sets_pending_not_confirmed() {
    nvs.saveOtaMeta("1.0.0-dev");
    NVSManager::OtaMeta m;
    nvs.loadOtaMeta(m);
    TEST_ASSERT_TRUE(m.pending);
    TEST_ASSERT_FALSE(m.confirmed);
}

// 6. saveOtaMeta() persists the pre_version string correctly.
void test_save_persists_pre_version() {
    nvs.saveOtaMeta("1.0.0-dev");
    NVSManager::OtaMeta m;
    nvs.loadOtaMeta(m);
    TEST_ASSERT_EQUAL_STRING("1.0.0-dev", m.pre_version);
}

// 7. pre_version longer than 15 chars is stored truncated (NVS getString behaviour).
//    Ensures no buffer overflow in the 16-byte OtaMeta::pre_version field.
void test_save_long_version_does_not_overflow() {
    nvs.saveOtaMeta("1.2.3-very-long-version-string-overflow");
    NVSManager::OtaMeta m;
    nvs.loadOtaMeta(m);
    // Buffer is 16 bytes; result must be null-terminated within those 16 bytes.
    // The exact truncation length depends on getString() implementation; just
    // verify the buffer is null-terminated (no UB).
    TEST_ASSERT_EQUAL_CHAR('\0', m.pre_version[sizeof(m.pre_version) - 1]);
}

// 8. setOtaConfirmed() sets confirmed=true while pending stays true.
void test_confirm_sets_confirmed() {
    nvs.saveOtaMeta("1.0.0-dev");
    TEST_ASSERT_TRUE(nvs.setOtaConfirmed());
    NVSManager::OtaMeta m;
    nvs.loadOtaMeta(m);
    TEST_ASSERT_TRUE(m.confirmed);
    TEST_ASSERT_TRUE(m.pending);  // pending unchanged by confirm
}

// 9. clearOtaMeta() sets pending=false, confirmed=false.
void test_clear_resets_flags() {
    nvs.saveOtaMeta("1.0.0-dev");
    nvs.setOtaConfirmed();
    TEST_ASSERT_TRUE(nvs.clearOtaMeta());
    NVSManager::OtaMeta m;
    nvs.loadOtaMeta(m);
    TEST_ASSERT_FALSE(m.pending);
    TEST_ASSERT_FALSE(m.confirmed);
}

// 10. loadOtaMeta() after clearOtaMeta(): loadOtaMeta still returns true
//     (the "pending" key exists in NVS, just set to false).
void test_load_after_clear_returns_true() {
    nvs.saveOtaMeta("1.0.0-dev");
    nvs.clearOtaMeta();
    NVSManager::OtaMeta m;
    TEST_ASSERT_TRUE(nvs.loadOtaMeta(m));
}

// 11. Full OTA lifecycle: save → confirm → clear → load.
//     Simulates the A-4 happy path end-to-end.
void test_full_ota_happy_path() {
    // OTA applied
    TEST_ASSERT_TRUE(nvs.saveOtaMeta("1.0.0-dev"));

    // Boot: check state
    NVSManager::OtaMeta m;
    TEST_ASSERT_TRUE(nvs.loadOtaMeta(m));
    TEST_ASSERT_TRUE(m.pending);
    TEST_ASSERT_FALSE(m.confirmed);
    TEST_ASSERT_EQUAL_STRING("1.0.0-dev", m.pre_version);

    // User presses 'O' to confirm
    TEST_ASSERT_TRUE(nvs.setOtaConfirmed());
    TEST_ASSERT_TRUE(nvs.loadOtaMeta(m));
    TEST_ASSERT_TRUE(m.confirmed);

    // Teardown: clearOtaMeta() after acceptance
    TEST_ASSERT_TRUE(nvs.clearOtaMeta());
    TEST_ASSERT_TRUE(nvs.loadOtaMeta(m));
    TEST_ASSERT_FALSE(m.pending);
    TEST_ASSERT_FALSE(m.confirmed);
}

// 12. Full OTA rollback lifecycle: save → timeout → clearOtaMeta.
void test_full_ota_rollback_path() {
    TEST_ASSERT_TRUE(nvs.saveOtaMeta("1.0.0-dev"));

    NVSManager::OtaMeta m;
    TEST_ASSERT_TRUE(nvs.loadOtaMeta(m));
    TEST_ASSERT_TRUE(m.pending);
    TEST_ASSERT_FALSE(m.confirmed);

    // 60-second timer fires — main.cpp calls clearOtaMeta() then restart
    TEST_ASSERT_TRUE(nvs.clearOtaMeta());

    // After restart: no pending OTA triggers the rollback path again
    TEST_ASSERT_TRUE(nvs.loadOtaMeta(m));
    TEST_ASSERT_FALSE(m.pending);
    TEST_ASSERT_FALSE(m.confirmed);
}

// 13. saveOtaMeta() on !ready NVS returns false (robustness).
void test_save_not_ready_returns_false() {
    nvs.end();
    TEST_ASSERT_FALSE(nvs.saveOtaMeta("1.0.0-dev"));
    // Re-open for tearDown()
    nvs.begin();
}

// 14. loadOtaMeta() on !ready NVS returns false (robustness).
void test_load_not_ready_returns_false() {
    nvs.end();
    NVSManager::OtaMeta m;
    TEST_ASSERT_FALSE(nvs.loadOtaMeta(m));
    nvs.begin();
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();

    RUN_TEST(test_load_fresh_returns_false);
    RUN_TEST(test_load_fresh_zeroes_struct);
    RUN_TEST(test_save_returns_true);
    RUN_TEST(test_save_then_load_returns_true);
    RUN_TEST(test_save_sets_pending_not_confirmed);
    RUN_TEST(test_save_persists_pre_version);
    RUN_TEST(test_save_long_version_does_not_overflow);
    RUN_TEST(test_confirm_sets_confirmed);
    RUN_TEST(test_clear_resets_flags);
    RUN_TEST(test_load_after_clear_returns_true);
    RUN_TEST(test_full_ota_happy_path);
    RUN_TEST(test_full_ota_rollback_path);
    RUN_TEST(test_save_not_ready_returns_false);
    RUN_TEST(test_load_not_ready_returns_false);

    return UNITY_END();
}
