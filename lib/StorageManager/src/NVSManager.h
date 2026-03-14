// NVSManager.h — NVS Persistence Layer (Tier 0)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// STORAGE_ARCHITECTURE.md §7 (NVS namespace design + API)
//
// Implements IStorageManager for the NVS tier.
// Uses Preferences.h (Arduino ESP32) — thin type-safe wrapper over esp_nvs_*.
//
// Namespace layout (§7.2):
//   "wifi"    — WiFi provisioning (ssid, pass, mode, hostname)
//   "sensor"  — LDC1101 calibration + protocol_id
//   "system"  — User-facing settings (brightness, lang, log_level, dev_name)
//   "storage" — Storage bookkeeping (meas_count only)
//   "ota"     — OTA metadata
//
// Thread safety ([PRE-10]):
//   incrementMeasCount() — NOT atomic (NVS get + put). MUST be called
//   exclusively from the MainLoop task. No concurrent callers allowed.
//
// Usage:
//   NVSManager nvs;
//   nvs.begin();   // call once in setup(), after Serial
//   uint32_t n = nvs.getMeasCount();

#pragma once

#include "IStorageManager.h"
#include <Preferences.h>
#include <stdint.h>

// RING_SIZE: number of measurement slots in LittleFS_data ring buffer.
// [ADR-ST-006] RING_SIZE is immutable post-v1 — changing it invalidates
// all existing measurement files (slot index = meas_count % RING_SIZE).
//
// TODO(wave8): Add test_nvs_manager/ before incrementMeasCount() goes into production use.
//   Critical invariants to test:
//   - getMeasSlot() == getMeasCount() % NVS_RING_SIZE  (basic)
//   - slot wraps correctly at count=249 (slot=249) and count=250 (slot=0)
//   - slot wraps correctly at count=NVS_RING_SIZE, count=2*NVS_RING_SIZE
//   - incrementMeasCount() on !ready_ is a no-op (no crash, count unchanged)
//   Requires Preferences mock (test/mocks/Preferences.h) — not yet implemented.
static constexpr uint16_t NVS_RING_SIZE = 250;

// NVS key length limit (ESP-IDF): 15 characters including null terminator.
// Keys longer than 14 chars will be silently truncated — avoid.
static constexpr uint8_t NVS_KEY_MAX = 14;

class NVSManager : public IStorageManager {
public:
    // ── Calibration struct (sensor namespace) ─────────────────────────────────
    // [F-05] proto_id is a PLACEHOLDER — real fSENSOR MIKROE-3240 ≈ 200–500 kHz.
    // Value must be measured at R-01 before any community DB submission.
    // See STORAGE_ARCHITECTURE.md §7.2 and FINGERPRINT_DB_ARCHITECTURE.md §7.
    struct SensorCalibration {
        float    rp[4];         // rp0..rp3 — Rp baseline per distance (Ω)
        float    l[4];          // l0..l3  — L baseline per distance (µH)
        uint32_t freq_hz;       // Operating frequency (placeholder until R-01)
        int64_t  cal_ts;        // Unix timestamp of last calibration (0 = never)
        bool     cal_valid;     // Calibration passed validation
        char     proto_id[16];  // e.g. "p1_UNKNOWN_013mm" until R-01
    };

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    NVSManager() = default;
    ~NVSManager() override;

    /**
     * @brief Open all NVS namespaces.
     * @return true if all namespaces opened successfully.
     * Must be called once in setup() before any get/set calls.
     */
    bool begin();

    /**
     * @brief Close all NVS namespaces. Safe to call multiple times.
     */
    void end();

    bool isReady() const { return ready_; }

    // ── IStorageManager — generic NVS access used by plugins ──────────────────
    // Key format: "namespace.key" — caller is responsible for uniqueness.
    // Note: these use a dedicated "plugin" namespace to avoid collisions
    // with structured namespaces ("wifi", "sensor", etc.).

    bool nvsSaveFloat (const char* key, float    value) override;
    bool nvsSaveUInt32(const char* key, uint32_t value) override;
    bool nvsLoadFloat (const char* key, float*    out, float    defaultVal = 0.0f) override;
    bool nvsLoadUInt32(const char* key, uint32_t* out, uint32_t defaultVal = 0)   override;
    bool nvsErase     (const char* key)                                            override;

    size_t littleFsFreeBytes() const override { return 0; }  // Wave 8: LittleFSManager

    // ── WiFi namespace ────────────────────────────────────────────────────────

    /**
     * @brief Load WiFi credentials.
     * @param ssid  Output buffer ≥ 33 bytes.
     * @param pass  Output buffer ≥ 64 bytes.
     * @param mode  Output: 0=AP, 1=STA.
     * @return true if keys exist and were read.
     */
    bool loadWifi(char* ssid, size_t ssidLen,
                  char* pass, size_t passLen,
                  uint8_t& mode) const;

    bool saveWifi(const char* ssid, const char* pass, uint8_t mode);

    // ── Sensor / calibration namespace ───────────────────────────────────────

    bool isCalibrationValid() const;
    bool loadCalibration(SensorCalibration& out) const;
    bool saveCalibration(const SensorCalibration& cal);

    // ── System namespace ─────────────────────────────────────────────────────

    uint8_t  getBrightness()  const;
    void     setBrightness(uint8_t val);

    uint8_t  getLogLevel()    const;   // 0=DEBUG .. 4=FATAL
    void     setLogLevel(uint8_t val);

    String   getDevName()     const;
    void     setDevName(const char* name);

    String   getLang()        const;   // "en" | "uk"
    void     setLang(const char* lang);

    uint8_t  getDisplayRot()  const;
    void     setDisplayRot(uint8_t rot);

    // ── Storage namespace ─────────────────────────────────────────────────────

    /**
     * @return Total measurements ever (monotonic counter, never reset).
     * Slot index in ring buffer = getMeasCount() % NVS_RING_SIZE.
     */
    uint32_t getMeasCount() const;

    /**
     * @return Current ring buffer slot index [0, NVS_RING_SIZE).
     * Convenience: getMeasCount() % NVS_RING_SIZE.
     */
    uint16_t getMeasSlot() const;

    /**
     * @brief Increment measurement counter after successful file write.
     * [ADR-ST-006 write-first invariant]: call AFTER lfs_file_close(), NEVER before.
     * [PRE-10 threading]: MUST be called exclusively from MainLoop task.
     */
    void incrementMeasCount();

    // ── Factory reset ─────────────────────────────────────────────────────────

    /**
     * @brief Soft factory reset: erase wifi + system namespaces.
     * Preserves: sensor calibration, meas_count (never reset), ota metadata.
     * [PRE-5] Measurements in LittleFS_data are NOT deleted.
     */
    bool softReset();

    /**
     * @brief Hard factory reset: erase ALL namespaces.
     * Call LittleFSTransport::stop() before calling this (SA2-4).
     * After this: esp_restart() required.
     */
    bool hardReset();

private:
    bool ready_ = false;

    // Each namespace opened with readwrite=true (default).
    // Preferences closes handle on end() / destructor.
    mutable Preferences wifi_;
    mutable Preferences sensor_;
    mutable Preferences system_;
    mutable Preferences storage_;
    mutable Preferences ota_;
    mutable Preferences plugin_;  // Generic namespace for IStorageManager::nvsSave/Load
};
