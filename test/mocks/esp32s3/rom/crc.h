// test/mocks/esp32s3/rom/crc.h — Native-test mock for ESP32-S3 ROM CRC32.
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
//
// FingerprintCache.cpp includes <esp32s3/rom/crc.h> for crc32_le() —
// the hardware-accelerated ROM CRC32 function on ESP32-S3.
// On native, we provide a correct software implementation using the same
// polynomial (IEEE 802.3 / CRC-32b, 0xEDB88320).
//
// ComputeLFSCrc32() is never called during native unit tests (init() is not
// invoked — only query() math is tested). This mock exists solely so that
// FingerprintCache.cpp compiles in the native-test environment.

#pragma once

#include <stdint.h>
#include <stddef.h>

// Software CRC32 — same polynomial as ESP32-S3 ROM crc32_le().
// Input/output convention: raw (un-inverted) running CRC, matching
// how FingerprintCache calls it (initial value = 0).
inline uint32_t crc32_le(uint32_t crc, const uint8_t* buf, size_t len) {
    crc = ~crc;
    while (len--) {
        crc ^= *buf++;
        for (int i = 0; i < 8; ++i) {
            crc = (crc >> 1) ^ (0xEDB88320U & static_cast<uint32_t>(-(crc & 1)));
        }
    }
    return ~crc;
}
