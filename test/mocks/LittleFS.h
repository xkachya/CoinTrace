#pragma once
// Native-test mock for LittleFS Arduino library.
// Include Arduino.h first so log_e() is available for LittleFSDataGuard
// (defined inline in LittleFSManager.h, uses log_e without including Arduino.h).
#include <Arduino.h>
// Provides minimal File + fs::LittleFSFS stubs that satisfy all headers
// in lib/StorageManager/ (LittleFSManager, MeasurementStore) and
// lib/Logger/ (LittleFSTransport) without any real filesystem.
//
// ArduinoJson v7 compatibility:
//   serializeJson(doc, f)   — calls f.write(uint8_t) / f.write(const uint8_t*, size_t)
//   deserializeJson(doc, f) — calls f.read() / f.peek() / f.available()
// All are no-ops here; integration tests run on device via cointrace-test env.

#include <stddef.h>
#include <stdint.h>

// ── File stub ─────────────────────────────────────────────────────────────────
class File {
public:
    File() = default;

    // Boolean validity check — always false (no real FS on native)
    operator bool() const { return false; }

    void   close()  {}
    void   flush()  {}
    size_t size() const { return 0; }

    // ArduinoJson v7 Print interface (serializeJson output)
    size_t write(uint8_t)                        { return 0; }
    size_t write(const uint8_t*, size_t)         { return 0; }

    // ArduinoJson v7 Stream interface (deserializeJson input)
    int    read()                           { return -1; }
    int    peek()                           { return -1; }
    int    available()                      { return 0;  }

    // Bulk read — used by MeasurementStore, SDCardManager, FingerprintCache
    size_t read(uint8_t*, size_t)           { return 0;  }

    // Directory check — used by FingerprintCache (always false on native)
    bool   isDirectory() const              { return false; }

    // Arduino Print helpers — used by LittleFSTransport and FingerprintCache
    size_t print(const char*)               { return 0; }
    size_t print(char)                      { return 0; }
};

// ── fs::LittleFSFS stub ───────────────────────────────────────────────────────
namespace fs {

class LittleFSFS {
public:
    // begin(formatOnFail, mountPoint, maxFiles, partitionLabel)
    bool begin(bool, const char*, uint8_t, const char*) { return false; }
    void end() {}

    bool   exists(const char*)                { return false; }
    bool   mkdir(const char*)                 { return false; }
    bool   remove(const char*)                { return false; }
    bool   rename(const char*, const char*)   { return false; }
    size_t totalBytes()                       { return 0; }
    size_t usedBytes()                        { return 0; }
    File   open(const char*, const char*)     { return File{}; }
};

} // namespace fs
