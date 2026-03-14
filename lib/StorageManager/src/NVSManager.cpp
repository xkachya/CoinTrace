// NVSManager.cpp — NVS Persistence Layer (Tier 0) Implementation
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// STORAGE_ARCHITECTURE.md §7

#include "NVSManager.h"
#include <Arduino.h>

// ── Namespace name constants ───────────────────────────────────────────────────
// ESP-IDF NVS: namespace names max 15 chars including null terminator.
static constexpr const char* NS_WIFI    = "wifi";
static constexpr const char* NS_SENSOR  = "sensor";
static constexpr const char* NS_SYSTEM  = "system";
static constexpr const char* NS_STORAGE = "storage";
static constexpr const char* NS_OTA     = "ota";
static constexpr const char* NS_PLUGIN  = "plugin";

// ── Lifecycle ──────────────────────────────────────────────────────────────────

NVSManager::~NVSManager() {
    end();
}

bool NVSManager::begin() {
    if (ready_) return true;

    bool ok = true;
    ok &= wifi_.begin   (NS_WIFI,    false);
    ok &= sensor_.begin (NS_SENSOR,  false);
    ok &= system_.begin (NS_SYSTEM,  false);
    ok &= storage_.begin(NS_STORAGE, false);
    ok &= ota_.begin    (NS_OTA,     false);
    ok &= plugin_.begin (NS_PLUGIN,  false);

    ready_ = ok;
    return ok;
}

void NVSManager::end() {
    if (!ready_) return;
    wifi_.end();
    sensor_.end();
    system_.end();
    storage_.end();
    ota_.end();
    plugin_.end();
    ready_ = false;
}

// ── IStorageManager — generic plugin NVS access ───────────────────────────────

bool NVSManager::nvsSaveFloat(const char* key, float value) {
    if (!ready_ || !key) return false;
    return plugin_.putFloat(key, value) != 0;
}

bool NVSManager::nvsSaveUInt32(const char* key, uint32_t value) {
    if (!ready_ || !key) return false;
    return plugin_.putUInt(key, value) != 0;
}

bool NVSManager::nvsLoadFloat(const char* key, float* out, float defaultVal) {
    if (!ready_ || !key || !out) return false;
    *out = plugin_.getFloat(key, defaultVal);
    return plugin_.isKey(key);
}

bool NVSManager::nvsLoadUInt32(const char* key, uint32_t* out, uint32_t defaultVal) {
    if (!ready_ || !key || !out) return false;
    *out = plugin_.getUInt(key, defaultVal);
    return plugin_.isKey(key);
}

bool NVSManager::nvsErase(const char* key) {
    if (!ready_ || !key) return false;
    return plugin_.remove(key);
}

// ── WiFi namespace ─────────────────────────────────────────────────────────────

bool NVSManager::loadWifi(char* ssid, size_t ssidLen,
                           char* pass, size_t passLen,
                           uint8_t& mode) const {
    if (!ready_ || !ssid || !pass) return false;
    wifi_.getString("ssid", ssid, ssidLen);
    wifi_.getString("pass", pass, passLen);
    mode = wifi_.getUChar("mode", 0);  // default: AP mode
    return wifi_.isKey("ssid");
}

bool NVSManager::saveWifi(const char* ssid, const char* pass, uint8_t mode) {
    if (!ready_ || !ssid || !pass) return false;
    bool ok = true;
    ok &= (wifi_.putString("ssid", ssid)   != 0);
    ok &= (wifi_.putString("pass", pass)   != 0);
    ok &= (wifi_.putUChar ("mode", mode)   != 0);
    return ok;
}

// ── Sensor / calibration namespace ────────────────────────────────────────────

bool NVSManager::isCalibrationValid() const {
    if (!ready_) return false;
    return sensor_.getBool("cal_valid", false);
}

bool NVSManager::loadCalibration(SensorCalibration& out) const {
    if (!ready_) return false;

    out.rp[0]     = sensor_.getFloat("rp0", 0.0f);
    out.rp[1]     = sensor_.getFloat("rp1", 0.0f);
    out.rp[2]     = sensor_.getFloat("rp2", 0.0f);
    out.rp[3]     = sensor_.getFloat("rp3", 0.0f);
    out.l[0]      = sensor_.getFloat("l0",  0.0f);
    out.l[1]      = sensor_.getFloat("l1",  0.0f);
    out.l[2]      = sensor_.getFloat("l2",  0.0f);
    out.l[3]      = sensor_.getFloat("l3",  0.0f);
    out.freq_hz   = sensor_.getUInt ("freq_hz",  0);
    out.cal_ts    = sensor_.getLong ("cal_ts",   0);
    out.cal_valid = sensor_.getBool ("cal_valid", false);
    sensor_.getString("proto_id", out.proto_id, sizeof(out.proto_id));

    // If proto_id was never written, initialise with the known placeholder.
    if (out.proto_id[0] == '\0') {
        strncpy(out.proto_id, "p1_UNKNOWN_013mm", sizeof(out.proto_id) - 1);
        out.proto_id[sizeof(out.proto_id) - 1] = '\0';
    }

    return out.cal_valid;
}

bool NVSManager::saveCalibration(const SensorCalibration& cal) {
    if (!ready_) return false;

    bool ok = true;
    ok &= (sensor_.putFloat ("rp0",      cal.rp[0])    != 0);
    ok &= (sensor_.putFloat ("rp1",      cal.rp[1])    != 0);
    ok &= (sensor_.putFloat ("rp2",      cal.rp[2])    != 0);
    ok &= (sensor_.putFloat ("rp3",      cal.rp[3])    != 0);
    ok &= (sensor_.putFloat ("l0",       cal.l[0])     != 0);
    ok &= (sensor_.putFloat ("l1",       cal.l[1])     != 0);
    ok &= (sensor_.putFloat ("l2",       cal.l[2])     != 0);
    ok &= (sensor_.putFloat ("l3",       cal.l[3])     != 0);
    ok &= (sensor_.putUInt  ("freq_hz",  cal.freq_hz)  != 0);
    ok &= (sensor_.putLong  ("cal_ts",   cal.cal_ts)   != 0);
    ok &= (sensor_.putBool  ("cal_valid",cal.cal_valid) != 0);
    ok &= (sensor_.putString("proto_id", cal.proto_id) != 0);
    return ok;
}

// ── System namespace ──────────────────────────────────────────────────────────

uint8_t NVSManager::getBrightness() const {
    if (!ready_) return 128;
    return system_.getUChar("brightness", 128);
}

void NVSManager::setBrightness(uint8_t val) {
    if (!ready_) return;
    system_.putUChar("brightness", val);
}

uint8_t NVSManager::getLogLevel() const {
    if (!ready_) return 2;  // default: INFO
    return system_.getUChar("log_level", 2);
}

void NVSManager::setLogLevel(uint8_t val) {
    if (!ready_) return;
    system_.putUChar("log_level", val);
}

String NVSManager::getDevName() const {
    if (!ready_) return String("CoinTrace");
    return system_.getString("dev_name", "CoinTrace");
}

void NVSManager::setDevName(const char* name) {
    if (!ready_ || !name) return;
    system_.putString("dev_name", name);
}

String NVSManager::getLang() const {
    if (!ready_) return String("en");
    return system_.getString("lang", "en");
}

void NVSManager::setLang(const char* lang) {
    if (!ready_ || !lang) return;
    system_.putString("lang", lang);
}

uint8_t NVSManager::getDisplayRot() const {
    if (!ready_) return 1;
    return system_.getUChar("display_rot", 1);
}

void NVSManager::setDisplayRot(uint8_t rot) {
    if (!ready_) return;
    system_.putUChar("display_rot", rot);
}

// ── Storage namespace ─────────────────────────────────────────────────────────

uint32_t NVSManager::getMeasCount() const {
    if (!ready_) return 0;
    return storage_.getUInt("meas_count", 0);
}

uint16_t NVSManager::getMeasSlot() const {
    return static_cast<uint16_t>(getMeasCount() % NVS_RING_SIZE);
}

void NVSManager::incrementMeasCount() {
    // [PRE-10] NOT atomic — two NVS operations (read + write).
    // Safe only because this is called exclusively from MainLoop task.
    if (!ready_) return;
    uint32_t n = storage_.getUInt("meas_count", 0);
    storage_.putUInt("meas_count", n + 1);
}

// ── Factory reset ─────────────────────────────────────────────────────────────

bool NVSManager::softReset() {
    // Erase user-facing settings only. Preserves calibration + meas_count.
    if (!ready_) return false;
    bool ok = true;
    ok &= wifi_.clear();    // wipe SSID, password
    ok &= system_.clear();  // wipe brightness, lang, log_level, dev_name
    return ok;
}

bool NVSManager::hardReset() {
    // Erase everything. Caller must ensure LittleFSTransport is stopped first (SA2-4).
    if (!ready_) return false;
    bool ok = true;
    ok &= wifi_.clear();
    ok &= sensor_.clear();
    ok &= system_.clear();
    ok &= storage_.clear();
    ok &= ota_.clear();
    ok &= plugin_.clear();
    return ok;
}
