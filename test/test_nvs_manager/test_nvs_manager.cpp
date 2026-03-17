// test_nvs_manager.cpp — Unit tests for NVSManager (Wave 8 B-1)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
//
// Tests the NVS persistence layer against the in-memory Preferences mock.
// Validates all invariants from the NVSManager.h TODO(wave8) block +
// STORAGE_ARCHITECTURE.md §7 (namespace design, factory reset contracts).
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
    // Wipe the global Preferences store between tests so tests are independent.
    Preferences::resetAll();
    // Ensure NVSManager starts from a clean, open state.
    nvs.end();
    nvs.begin();
}

void tearDown() {
    nvs.end();
}

// ── Tests ─────────────────────────────────────────────────────────────────────

// 1. begin() → isReady() = true
void test_begin_sets_ready() {
    TEST_ASSERT_TRUE(nvs.isReady());
}

// 2. getMeasCount() starts at 0 on a fresh namespace
void test_meas_count_starts_at_zero() {
    TEST_ASSERT_EQUAL_UINT32(0, nvs.getMeasCount());
}

// 3. incrementMeasCount() × 3 → getMeasCount() = 3
void test_increment_meas_count() {
    nvs.incrementMeasCount();
    nvs.incrementMeasCount();
    nvs.incrementMeasCount();
    TEST_ASSERT_EQUAL_UINT32(3, nvs.getMeasCount());
}

// 4. getMeasSlot() = getMeasCount() % NVS_RING_SIZE
//    Tests ring-buffer wraparound at count = NVS_RING_SIZE.
//    Increments 251 times: slot at 250 = 0; at 251 = 1.
void test_meas_slot_wraps_correctly() {
    for (uint16_t i = 0; i < NVS_RING_SIZE; ++i) {
        nvs.incrementMeasCount();
    }
    // count == NVS_RING_SIZE → slot == 0 (full wrap)
    TEST_ASSERT_EQUAL_UINT32(NVS_RING_SIZE, nvs.getMeasCount());
    TEST_ASSERT_EQUAL_UINT16(0, nvs.getMeasSlot());

    nvs.incrementMeasCount();
    // count == NVS_RING_SIZE + 1 → slot == 1
    TEST_ASSERT_EQUAL_UINT16(1, nvs.getMeasSlot());
}

// 5. incrementMeasCount() on !ready_ is a safe no-op
void test_increment_on_not_ready_is_noop() {
    nvs.end();  // sets ready_=false
    nvs.incrementMeasCount();  // must not crash or change count
    nvs.begin();
    TEST_ASSERT_EQUAL_UINT32(0, nvs.getMeasCount());  // storage ns still clean
}

// 6. saveWifi / loadWifi round-trip
void test_wifi_round_trip() {
    const char* ssid = "TestNet_5G";
    const char* pass = "hunter2!@#";
    const uint8_t mode = 1;  // STA

    TEST_ASSERT_TRUE(nvs.saveWifi(ssid, pass, mode));

    char gotSsid[64] = {};
    char gotPass[64] = {};
    uint8_t gotMode = 0xFF;

    TEST_ASSERT_TRUE(nvs.loadWifi(gotSsid, sizeof(gotSsid),
                                  gotPass, sizeof(gotPass), gotMode));
    TEST_ASSERT_EQUAL_STRING(ssid, gotSsid);
    TEST_ASSERT_EQUAL_STRING(pass, gotPass);
    TEST_ASSERT_EQUAL_UINT8(mode, gotMode);
}

// 7. saveCalibration / loadCalibration round-trip
void test_calibration_round_trip() {
    NVSManager::SensorCalibration cal = {};
    cal.rp[0] = 1800.0f;  cal.rp[1] = 1350.0f;
    cal.rp[2] = 1100.0f;  cal.rp[3] = 1802.0f;
    cal.l[0]  = 22.0f;    cal.l[1]  = 21.0f;
    cal.freq_hz  = 1000000;
    cal.cal_ts   = 1700000000L;  // 2023-11-14 (fits in int32_t range)
    cal.cal_valid = true;
    // proto_id[16] holds 15 chars max; use a string that fits cleanly.
    strncpy(cal.proto_id, "p1_test_013mm", sizeof(cal.proto_id) - 1);

    TEST_ASSERT_TRUE(nvs.saveCalibration(cal));

    NVSManager::SensorCalibration out = {};
    const bool loaded = nvs.loadCalibration(out);
    TEST_ASSERT_TRUE(loaded);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, cal.rp[0], out.rp[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, cal.rp[1], out.rp[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, cal.l[0],  out.l[0]);
    TEST_ASSERT_TRUE(out.cal_valid);
    TEST_ASSERT_EQUAL_STRING("p1_test_013mm", out.proto_id);
}

// 8. softReset() clears wifi + system namespaces, preserves sensor (calibration)
void test_soft_reset_preserves_sensor() {
    // Set up WiFi, system, and calibration
    nvs.saveWifi("CoinNet", "secret", 0);
    nvs.setDevName("MyDevice");
    nvs.setBrightness(200);

    NVSManager::SensorCalibration cal = {};
    cal.rp[0] = 1600.0f;
    cal.cal_valid = true;
    strncpy(cal.proto_id, "p1_test_013mm", sizeof(cal.proto_id) - 1);
    nvs.saveCalibration(cal);

    TEST_ASSERT_TRUE(nvs.softReset());

    // WiFi should be cleared (loadWifi returns false — ssid key gone)
    char ssid[64] = {}; char pass[64] = {}; uint8_t mode = 0xFF;
    TEST_ASSERT_FALSE(nvs.loadWifi(ssid, sizeof(ssid), pass, sizeof(pass), mode));

    // System should be cleared — getDevName falls back to default "CoinTrace"
    TEST_ASSERT_EQUAL_STRING("CoinTrace", nvs.getDevName().c_str());

    // Sensor (calibration) must survive softReset
    NVSManager::SensorCalibration out = {};
    nvs.loadCalibration(out);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1600.0f, out.rp[0]);
    TEST_ASSERT_TRUE(out.cal_valid);
}

// 9. hardReset() clears all namespaces
void test_hard_reset_clears_all() {
    nvs.saveWifi("CoinNet", "secret", 0);
    nvs.setDevName("MyDevice");

    NVSManager::SensorCalibration cal = {};
    cal.cal_valid = true;
    strncpy(cal.proto_id, "test", sizeof(cal.proto_id) - 1);
    nvs.saveCalibration(cal);

    TEST_ASSERT_TRUE(nvs.hardReset());

    // WiFi gone
    char ssid[64] = {}; char pass[64] = {}; uint8_t mode = 0xFF;
    TEST_ASSERT_FALSE(nvs.loadWifi(ssid, sizeof(ssid), pass, sizeof(pass), mode));

    // Calibration gone
    NVSManager::SensorCalibration out = {};
    TEST_ASSERT_FALSE(nvs.loadCalibration(out));
}

// ── Runner ────────────────────────────────────────────────────────────────────

void setup() {}   // Required by Arduino framework — no-op on native
void loop()  {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_begin_sets_ready);
    RUN_TEST(test_meas_count_starts_at_zero);
    RUN_TEST(test_increment_meas_count);
    RUN_TEST(test_meas_slot_wraps_correctly);
    RUN_TEST(test_increment_on_not_ready_is_noop);
    RUN_TEST(test_wifi_round_trip);
    RUN_TEST(test_calibration_round_trip);
    RUN_TEST(test_soft_reset_preserves_sensor);
    RUN_TEST(test_hard_reset_clears_all);
    return UNITY_END();
}
