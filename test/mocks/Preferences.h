#pragma once
// Native-test mock for Arduino ESP32 Preferences library.
// NVSManager.h includes <Preferences.h> — this stub satisfies that dependency
// so NVSManager compiles and runs correctly on native.
//
// Wave 8 B-1: upgraded from no-op stubs to a full in-memory key-value store
// keyed by namespace name. Each Preferences instance owns a namespace name
// (set by begin()), and reads/writes go into a process-global static map so
// that separate instances sharing the same namespace (if any) see the same data.
//
// This makes NVSManager::begin() return true (ready_=true), enabling all
// test_nvs_manager/ tests to exercise real NVSManager logic.

#include <Arduino.h>   // String — NVSManager returns String from getDevName/getLang
#include <stdint.h>
#include <string.h>
#include <string>
#include <map>
#include <sstream>
#include <algorithm>   // std::min

class Preferences {
public:
    Preferences()  = default;
    ~Preferences() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────
    bool begin(const char* ns, bool /*readOnly*/) {
        ns_ = ns ? ns : "";
        return true;
    }
    void end() { ns_.clear(); }

    // ── put* ─────────────────────────────────────────────────────────────
    size_t putFloat  (const char* k, float    v) { set(k, ftos(v));              return sizeof(v); }
    size_t putDouble (const char* k, double   v) { set(k, dtos(v));              return sizeof(v); }
    size_t putBool   (const char* k, bool     v) { set(k, v ? "1" : "0");        return 1; }
    size_t putInt    (const char* k, int32_t  v) { set(k, std::to_string(v));   return sizeof(v); }
    size_t putUInt   (const char* k, uint32_t v) { set(k, std::to_string(v));   return sizeof(v); }
    size_t putLong   (const char* k, int32_t  v) { set(k, std::to_string(v));   return sizeof(v); }
    size_t putLong64 (const char* k, int64_t  v) { set(k, std::to_string(v));   return sizeof(v); }
    size_t putULong  (const char* k, uint32_t v) { set(k, std::to_string(v));   return sizeof(v); }
    size_t putULong64(const char* k, uint64_t v) { set(k, std::to_string(v));   return sizeof(v); }
    size_t putShort  (const char* k, int16_t  v) { set(k, std::to_string(v));   return sizeof(v); }
    size_t putUShort (const char* k, uint16_t v) { set(k, std::to_string(v));   return sizeof(v); }
    size_t putChar   (const char* k, int8_t   v) { set(k, std::to_string(v));   return sizeof(v); }
    size_t putUChar  (const char* k, uint8_t  v) { set(k, std::to_string(v));   return sizeof(v); }
    size_t putString (const char* k, const char* s) {
        std::string v = s ? s : "";
        set(k, v);
        return v.size();
    }

    // ── get* ─────────────────────────────────────────────────────────────
    float    getFloat  (const char* k, float    d = 0.0f) const { return has(k) ? stof_(k)   : d; }
    double   getDouble (const char* k, double   d = 0.0)  const { return has(k) ? stod_(k)   : d; }
    bool     getBool   (const char* k, bool     d = false) const { return has(k) ? (cns().at(k) == "1") : d; }
    int32_t  getInt    (const char* k, int32_t  d = 0)    const { return has(k) ? (int32_t)stoi64_(k) : d; }
    uint32_t getUInt   (const char* k, uint32_t d = 0)    const { return has(k) ? (uint32_t)stou64_(k) : d; }
    int32_t  getLong   (const char* k, int32_t  d = 0)    const { return has(k) ? (int32_t)stoi64_(k) : d; }
    int64_t  getLong64 (const char* k, int64_t  d = 0)    const { return has(k) ? stoi64_(k) : d; }
    uint32_t getULong  (const char* k, uint32_t d = 0)    const { return has(k) ? (uint32_t)stou64_(k) : d; }
    uint64_t getULong64(const char* k, uint64_t d = 0)    const { return has(k) ? stou64_(k) : d; }
    int16_t  getShort  (const char* k, int16_t  d = 0)    const { return has(k) ? (int16_t)stoi64_(k) : d; }
    uint16_t getUShort (const char* k, uint16_t d = 0)    const { return has(k) ? (uint16_t)stou64_(k) : d; }
    int8_t   getChar   (const char* k, int8_t   d = 0)    const { return has(k) ? (int8_t)stoi64_(k) : d; }
    uint8_t  getUChar  (const char* k, uint8_t  d = 0)    const { return has(k) ? (uint8_t)stou64_(k) : d; }

    // getString(key, buf, maxLen) — fills buf, returns chars written (excluding NUL)
    size_t getString(const char* k, char* buf, size_t maxLen) const {
        if (!buf || maxLen == 0) return 0;
        if (!has(k)) { buf[0] = '\0'; return 0; }
        const std::string& s = cns().at(k);
        const size_t n = std::min(s.size(), maxLen - 1);
        memcpy(buf, s.data(), n);
        buf[n] = '\0';
        return n;
    }

    // getString(key, default) → String — used by NVSManager::getDevName / getLang
    String getString(const char* k, const char* def = "") const {
        return String(has(k) ? cns().at(k).c_str() : def);
    }

    // ── Key management ───────────────────────────────────────────────────
    bool isKey (const char* k) const { return has(k); }
    bool remove(const char* k)       {
        if (!has(k)) return false;
        g()[ns_].erase(k);
        return true;
    }
    bool clear() { g()[ns_].clear(); return true; }

    // ── Test helpers ─────────────────────────────────────────────────────
    // Remove all data for all namespaces. Call from setUp() in NVS tests.
    static void resetAll() { g().clear(); }

private:
    std::string ns_;

    // Global store:  namespace-name → key → string-encoded value
    static std::map<std::string, std::map<std::string, std::string>>& g() {
        static std::map<std::string, std::map<std::string, std::string>> s;
        return s;
    }

    // Writable reference to this namespace's key-value map
    std::map<std::string, std::string>& ns()  { return g()[ns_]; }
    // Read-only reference (auto-vivifies an empty map — ok for reads)
    const std::map<std::string, std::string>& cns() const { return g()[ns_]; }

    void set(const char* k, const std::string& v) { g()[ns_][k] = v; }

    bool has(const char* k) const {
        const auto& store = g();
        auto it = store.find(ns_);
        if (it == store.end()) return false;
        return it->second.count(k) > 0;
    }

    // ── Serialization helpers ─────────────────────────────────────────────
    static std::string ftos(float v) {
        std::ostringstream os; os.precision(9); os << v; return os.str();
    }
    static std::string dtos(double v) {
        std::ostringstream os; os.precision(17); os << v; return os.str();
    }

    float    stof_  (const char* k) const { return std::stof (cns().at(k)); }
    double   stod_  (const char* k) const { return std::stod (cns().at(k)); }
    int64_t  stoi64_(const char* k) const { return std::stoll(cns().at(k)); }
    uint64_t stou64_(const char* k) const { return std::stoull(cns().at(k)); }
};
