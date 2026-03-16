// MeasurementStore.h — Ring-buffer Measurement Persistence (Wave 7 P-3/P-4)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// STORAGE_ARCHITECTURE.md §8 (ring buffer + m_NNN.json format)
//
// Stores coin measurements in /data/measurements/m_NNN.json (NVS_RING_SIZE slots).
// Ring buffer index: slot = meas_count % NVS_RING_SIZE.
//
// Wave 7 P-4: SDCardManager injection (ADR-ST-009 — copy-before-overwrite).
//   When the ring wraps (meas_count >= NVS_RING_SIZE), the slot that is about to
//   be overwritten is first read from LittleFS and archived to SD, then the new
//   measurement is written. The two operations use SEPARATE mutex scopes — the
//   lfsDataMutex is released before writeMeasurement() acquires spiMutex.
//
// Threading: save() and load() MUST be called from MainLoop task only.
// ([PRE-10] same constraint as NVSManager::incrementMeasCount.)
//
// Audit guards baked in:
//   [EXT-FUTURE-5 / A-02] save():  validates rp[0] > 1.0f BEFORE computeVector().
//                                  Defense-in-depth: storage boundary cannot trust callers.
//   [EXT-FUTURE-6 / A-03] save():  writes "complete":true sentinel LAST (ADR-ST-006).
//   [EXT-FUTURE-6 / A-03] load():  validates sentinel before returning data.
//   [EXT-FUTURE-1]        save():  rejects JSON > MAX_JSON_B (LittleFS 2-block limit).
//   [ADR-ST-009]          save():  copy-before-overwrite when ring wraps (P-4).

#pragma once

#include "Measurement.h"
#include "LittleFSManager.h"
#include "NVSManager.h"

// Forward declaration — full definition in SDCardManager.h, included in .cpp.
class SDCardManager;

class MeasurementStore {
public:
    MeasurementStore(LittleFSManager& lfs, NVSManager& nvs);

    // Call once after LittleFS data is mounted.
    // Caches device_id from eFuse MAC. Logs current measurement count.
    bool begin();

    // Serialize and write m to the current ring buffer slot.
    // Increments NVS meas_count AFTER successful file close (ADR-ST-006).
    // [ADR-ST-009] When ring wraps: evicted slot is archived to SD first (if injected).
    // Returns false on validation failure, FS unavailability, or write error.
    bool save(const Measurement& m);

    // Load measurement from ring buffer slot [0, NVS_RING_SIZE).
    // Returns false if file does not exist or "complete" sentinel is absent/false.
    bool load(uint16_t slot, Measurement& out);

    uint32_t count() const;  // delegates to nvs_.getMeasCount()

    // Inject optional SD card manager for copy-before-overwrite archival (ADR-ST-009).
    // Call after tryMount() in setup(). sd may be null (disables archival silently).
    void setSDCardManager(SDCardManager* sd) { sdMgr_ = sd; }

private:
    static constexpr const char* MEAS_DIR   = "/measurements";
    static constexpr uint32_t    MAX_JSON_B = 3800;  // [EXT-FUTURE-1] ~1 LittleFS block

    LittleFSManager& lfs_;
    NVSManager&      nvs_;
    SDCardManager*   sdMgr_ = nullptr;  // optional; null = SD archival disabled
    char             deviceId_[16];     // "CoinTrace-XXYY" from factory eFuse MAC

    // Writes "/measurements/m_NNN.json" into buf.
    void slotPath(uint16_t slot, char* buf, size_t len) const;

    // Read raw bytes of slot from LittleFS into out[0..maxLen).
    // Returns number of bytes read (0 if file absent).
    // [ADR-ST-009] Caller MUST hold lfsDataMutex before calling.
    size_t readSlotRaw(uint16_t slot, uint8_t* out, size_t maxLen);
};
