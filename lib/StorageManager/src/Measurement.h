// Measurement.h — Coin Measurement Struct (Wave 7 P-3)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// STORAGE_ARCHITECTURE.md §8.3 (m_NNN.json format)
//
// A single coin measurement as stored in /data/measurements/m_NNN.json.
//
// P-3 scope (single-position):
//   rp[0], l[0]     — populated from LDC1101Plugin::read()
//   rp[1..3], l[1..3] — zero-filled (reserved for P-5+ multi-position fixture)
//
// JSON layout (write order matters — ADR-ST-006 sentinel-last invariant):
//   { "ts":…, "device_id":…, "protocol":…, "pos_count":1,
//     "rp":[…], "l":[…], "metal_code":"UNKN", "coin_name":"Unclassified",
//     "conf":0.0, "complete":true }   ← "complete" MUST be last

#pragma once
#include <stdint.h>

struct Measurement {
    uint32_t ts;             // millis()/1000 at capture (uptime-seconds; not Unix in P-3)
    float    rp[4];          // Rp raw codes at 4 coil positions (LDC1101: 0–65535)
    float    l[4];           // L  raw codes at 4 coil positions
    uint8_t  pos_count;      // Number of valid positions (1 in P-3, up to 4 in P-5+)
    char     metal_code[8];  // "UNKN" until P-5+ classification
    char     coin_name[48];  // "Unclassified" until P-5+
    float    conf;           // 0.0–1.0 classification confidence (0.0 in P-3)
};
