// MeasurementStore.cpp — Ring-buffer Measurement Persistence (Wave 7 P-3)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3

#include "MeasurementStore.h"
#include <ArduinoJson.h>
#include <esp_mac.h>     // esp_efuse_mac_get_default() [A-01]
#include <Arduino.h>     // log_i / log_e / log_w / strlcpy

MeasurementStore::MeasurementStore(LittleFSManager& lfs, NVSManager& nvs)
    : lfs_(lfs), nvs_(nvs) {
    deviceId_[0] = '\0';
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void MeasurementStore::slotPath(uint16_t slot, char* buf, size_t len) const {
    snprintf(buf, len, "%s/m_%03u.json", MEAS_DIR, (unsigned)slot);
}

// [A-01] Use esp_efuse_mac_get_default() — NEVER esp_efuse_get_custom_mac().
// get_custom_mac() returns ESP_ERR_INVALID_ARG on devices without custom eFuse MAC.
static void buildDeviceId(char* out, size_t len) {
    uint8_t mac[6] = {};
    esp_efuse_mac_get_default(mac);
    snprintf(out, len, "CoinTrace-%02X%02X", mac[4], mac[5]);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

bool MeasurementStore::begin() {
    if (!lfs_.isDataMounted()) {
        log_e("MeasStore: begin() — LittleFS data not mounted");
        return false;
    }
    buildDeviceId(deviceId_, sizeof(deviceId_));
    // /measurements/ is created by LittleFSManager::createDataDirs() on mountData()
    log_i("MeasStore: ready — %u measurements (device: %s)",
          nvs_.getMeasCount(), deviceId_);
    return true;
}

uint32_t MeasurementStore::count() const {
    return nvs_.getMeasCount();
}

// ── save() ────────────────────────────────────────────────────────────────────

bool MeasurementStore::save(const Measurement& m) {
    // [EXT-FUTURE-5 / A-02] Boundary validation FIRST — input guard.
    // Validate before touching any system state: architectural principle
    // "reject invalid input at the boundary, regardless of subsystem state".
    // Layer 1 (plugin) protects against bad hardware. Layer 2 (here) protects
    // against bad callers. They guard against different failure modes.
    if (m.rp[0] < 1.0f) {
        log_w("MeasStore: save() rejected — rp[0]=%.2f (must be >= 1.0) [A-02]",
              m.rp[0]);
        return false;
    }

    if (!lfs_.isDataMounted()) return false;

    // Build JSON document.
    // Key insertion order is preserved by ArduinoJson (linked list).
    // "complete" is added LAST — sentinel invariant (ADR-ST-006 / A-03).
    JsonDocument doc;
    doc["ts"]         = m.ts;
    doc["device_id"]  = deviceId_;
    doc["protocol"]   = "p1_UNKNOWN_013mm";  // [F-05] real fSENSOR TBD at R-01

    doc["pos_count"]  = m.pos_count;

    JsonArray rpArr = doc["rp"].to<JsonArray>();
    JsonArray lArr  = doc["l"].to<JsonArray>();
    for (int i = 0; i < 4; i++) {
        rpArr.add(m.rp[i]);
        lArr.add(m.l[i]);
    }

    doc["metal_code"] = m.metal_code;
    doc["coin_name"]  = m.coin_name;
    doc["conf"]       = m.conf;

    // [EXT-FUTURE-6 / A-03] Sentinel written LAST — power-fail guard.
    // If power fails before this line, load() will reject the partial file.
    doc["complete"]   = true;

    // [EXT-FUTURE-1] Reject oversized JSON (> 3800 B ≈ 1 LittleFS block).
    size_t jsonSize = measureJson(doc);
    if (jsonSize > MAX_JSON_B) {
        log_e("MeasStore: JSON %u B exceeds %u B limit — save() aborted",
              (unsigned)jsonSize, (unsigned)MAX_JSON_B);
        return false;
    }

    char path[32];
    slotPath(nvs_.getMeasSlot(), path, sizeof(path));

    // Write under lfsDataMutex (LittleFSTransport bg task may be active).
    LittleFSDataGuard g(lfs_.lfsDataMutex());
    if (!g.ok()) {
        log_e("MeasStore: lfsDataMutex timeout — save() aborted");
        return false;
    }

    File f = lfs_.data().open(path, "w");
    if (!f) {
        log_e("MeasStore: open(%s, w) failed", path);
        return false;
    }
    serializeJson(doc, f);
    f.close();
    // Mutex released here by g destructor.

    // [ADR-ST-006] Increment counter AFTER file is closed and data is on flash.
    nvs_.incrementMeasCount();
    return true;
}

// ── load() ────────────────────────────────────────────────────────────────────

bool MeasurementStore::load(uint16_t slot, Measurement& out) {
    if (!lfs_.isDataMounted()) return false;

    char path[32];
    slotPath(slot, path, sizeof(path));

    LittleFSDataGuard g(lfs_.lfsDataMutex());
    if (!g.ok()) return false;

    File f = lfs_.data().open(path, "r");
    if (!f) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    // Mutex released here by g destructor.

    if (err) return false;

    // [EXT-FUTURE-6 / A-03] Sentinel validation: reject partial/aborted writes.
    if (!doc["complete"].is<bool>() || !doc["complete"].as<bool>()) {
        log_w("MeasStore: slot %u — 'complete' sentinel missing or false [A-03]",
              (unsigned)slot);
        return false;
    }

    out.ts        = doc["ts"]        | static_cast<uint32_t>(0);
    out.pos_count = doc["pos_count"] | static_cast<uint8_t>(1);
    out.conf      = doc["conf"]      | 0.0f;

    JsonArrayConst rpArr = doc["rp"].as<JsonArrayConst>();
    JsonArrayConst lArr  = doc["l"].as<JsonArrayConst>();
    for (int i = 0; i < 4; i++) {
        out.rp[i] = rpArr[i] | 0.0f;
        out.l[i]  = lArr[i]  | 0.0f;
    }

    const char* mc = doc["metal_code"] | "UNKN";
    const char* cn = doc["coin_name"]  | "Unclassified";
    strlcpy(out.metal_code, mc, sizeof(out.metal_code));
    strlcpy(out.coin_name,  cn, sizeof(out.coin_name));

    return true;
}
