#pragma once
// Native-test mock for Arduino ESP32 Preferences library.
// NVSManager.h includes <Preferences.h> — this stub satisfies that dependency
// so NVSManager (and via it MeasurementStore) compile on native.
//
// All operations are no-ops. NVSManager::begin() returns false (ready_=false)
// so NVS-dependent code paths are skipped in tests that only exercise storage
// logic (MeasurementStore boundary guard, sentinel validation).
//
// TODO(wave8): Add test_nvs_manager/ when Preferences mock supports
// in-memory key-value storage. See NVSManager.h TODO for test list.

#include <Arduino.h>   // String class — NVSManager returns String from getDevName/getLang
#include <stdint.h>
#include <string.h>

class Preferences {
public:
    Preferences()  = default;
    ~Preferences() = default;

    // Lifecycle
    bool begin(const char*, bool) { return false; }
    void end() {}

    // ── put* — store value, return bytes written (0 = fail) ─────────────────
    size_t   putFloat  (const char*, float)          { return 0; }
    size_t   putDouble (const char*, double)         { return 0; }
    size_t   putBool   (const char*, bool)           { return 0; }
    size_t   putInt    (const char*, int32_t)        { return 0; }
    size_t   putUInt   (const char*, uint32_t)       { return 0; }
    size_t   putLong   (const char*, int32_t)        { return 0; }
    size_t   putLong64 (const char*, int64_t)        { return 0; }
    size_t   putULong  (const char*, uint32_t)       { return 0; }
    size_t   putULong64(const char*, uint64_t)       { return 0; }
    size_t   putShort  (const char*, int16_t)        { return 0; }
    size_t   putUShort (const char*, uint16_t)       { return 0; }
    size_t   putChar   (const char*, int8_t)         { return 0; }
    size_t   putUChar  (const char*, uint8_t)        { return 0; }
    size_t   putString (const char*, const char*)    { return 0; }

    // ── get* — return stored value or default ────────────────────────────────
    float    getFloat  (const char*, float    d=0.0f)   const { return d; }
    double   getDouble (const char*, double   d=0.0)    const { return d; }
    bool     getBool   (const char*, bool     d=false)  const { return d; }
    int32_t  getInt    (const char*, int32_t  d=0)      const { return d; }
    uint32_t getUInt   (const char*, uint32_t d=0)      const { return d; }
    int32_t  getLong   (const char*, int32_t  d=0)      const { return d; }
    int64_t  getLong64 (const char*, int64_t  d=0)      const { return d; }
    uint32_t getULong  (const char*, uint32_t d=0)      const { return d; }
    uint64_t getULong64(const char*, uint64_t d=0)      const { return d; }
    int16_t  getShort  (const char*, int16_t  d=0)      const { return d; }
    uint16_t getUShort (const char*, uint16_t d=0)      const { return d; }
    int8_t   getChar   (const char*, int8_t   d=0)      const { return d; }
    uint8_t  getUChar  (const char*, uint8_t  d=0)      const { return d; }

    // getString with buffer fills empty string, returns 0
    size_t getString(const char*, char* buf, size_t maxLen) const {
        if (buf && maxLen > 0) buf[0] = '\0';
        return 0;
    }

    // getString returning String (used by NVSManager::getDevName, getLang)
    String getString(const char*, const char* def = "") const {
        return String(def);
    }

    // ── Key management ───────────────────────────────────────────────────────
    bool isKey(const char*) const { return false; }
    bool remove(const char*)      { return false; }
    bool clear()                  { return true; }
};
