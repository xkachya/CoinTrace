#pragma once
// Native-test mock for ESP-IDF esp_mac.h.
// Provides esp_efuse_mac_get_default() used by MeasurementStore::begin()
// to build a device ID string from the last two MAC octets [A-01].
//
// Returns a deterministic test MAC: 00:11:22:33:AA:BB
// so device_id = "CoinTrace-AABB" in native test builds.

#include <stdint.h>

typedef int esp_err_t;
#define ESP_OK 0

inline esp_err_t esp_efuse_mac_get_default(uint8_t mac[6]) {
    mac[0] = 0x00;
    mac[1] = 0x11;
    mac[2] = 0x22;
    mac[3] = 0x33;
    mac[4] = 0xAA;
    mac[5] = 0xBB;
    return ESP_OK;
}
