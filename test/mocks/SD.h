// test/mocks/SD.h — Native-test mock for Arduino SD library (ESP32 variant).
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
//
// SDClass methods are no-ops; SD.begin() always returns false (SD tier
// unavailable on native — no real SPI bus).
// File reuses the stub from LittleFS.h (in real ESP32 Arduino, both SD and
// LittleFS use the same fs::File type from FS.h).
//
// SPIClass is forward-declared here (normally in SPI.h, pulled in by SD.h
// in the Arduino framework). Defined minimally for SDCardManager.h compilation.
//
// SD card type constants and FILE_* mode constants match the real ESP32 SD.h.

#pragma once

#include "LittleFS.h"   // File stub (same type as real sd/lfs File on ESP32)
#include <stdint.h>

// ── SPIClass stub ─────────────────────────────────────────────────────────────
// (Normally declared in <SPI.h>; included transitively by real SD.h.)
// SDCardManager.h uses SPIClass* — this stub satisfies the declaration.
class SPIClass {
public:
    void begin(int8_t = -1, int8_t = -1, int8_t = -1, int8_t = -1) {}
    void end() {}
};

// ── SD open mode constants ────────────────────────────────────────────────────
#ifndef FILE_READ
    #define FILE_READ   "r"
#endif
#ifndef FILE_WRITE
    #define FILE_WRITE  "w"
#endif
#ifndef FILE_APPEND
    #define FILE_APPEND "a"
#endif

// ── SD card type constants ────────────────────────────────────────────────────
static constexpr uint8_t CARD_NONE    = 0;
static constexpr uint8_t CARD_MMC     = 1;
static constexpr uint8_t CARD_SD      = 2;
static constexpr uint8_t CARD_SDHC    = 3;
static constexpr uint8_t CARD_UNKNOWN = 4;

// ── SDClass stub ──────────────────────────────────────────────────────────────
class SDClass {
public:
    // begin() always returns false — SD tier unavailable on native.
    bool     begin(uint8_t, SPIClass&, uint32_t = 4000000, const char* = "/sd") { return false; }
    void     end()                                    {}
    uint8_t  cardType()                               { return CARD_NONE; }
    uint64_t cardSize()                               { return 0; }
    bool     exists(const char*)                      { return false; }
    bool     mkdir(const char*)                       { return false; }
    bool     remove(const char*)                      { return false; }
    File     open(const char*, const char* = FILE_READ) { return File{}; }
};

// Each translation unit gets its own SD instance (static).
// Safe because available_ is always false — no real SD operations are reached.
static SDClass SD;
