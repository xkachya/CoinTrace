// ISensorPlugin.h — Sensor Plugin Interface
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// PLUGIN_INTERFACES_EXTENDED.md §1

#pragma once

#include "IPlugin.h"
#include <stdint.h>

/**
 * @brief Interface for all physical measurement sensors.
 *
 * Extends IPlugin with sensor-specific type metadata and a universal
 * SensorData struct. The primary measurement is in value1; secondary
 * (dual-output sensors such as LDC1101) uses value2.
 *
 * Thread safety for read(): implementations must protect cached data with an
 * internal mutex if read() may be called from tasks other than the main loop.
 */
class ISensorPlugin : public IPlugin {
public:

    // ── Sensor type ───────────────────────────────────────────────────────────

    enum class SensorType : uint8_t {
        INDUCTIVE,      // LDC1101 — RP + L (eddy-current metal ID)
        WEIGHT,         // HX711 — coin mass
        MAGNETIC,       // QMC5883L — ferromagnetic signature
        TEMPERATURE,    // BME280 / NTC — ambient temperature
        LIGHT,          // BH1750 — illuminance
        IMU,            // BMI270 — acceleration / gyro
        STORAGE,        // SDCard used as sensor source
        CUSTOM
    };

    struct SensorMetadata {
        const char* typeName;   // e.g. "Inductive Sensor (SPI)"
        const char* unit;       // e.g. "RP_code", "g", "Gauss"
        float       minValue;
        float       maxValue;
        float       resolution;
        uint16_t    sampleRate; // Hz
    };

    // ── Measurement result ────────────────────────────────────────────────────

    struct SensorData {
        float    value1;        // Primary channel  (e.g. RP_raw for LDC1101)
        float    value2;        // Secondary channel (e.g. L_raw  for LDC1101; 0 if unused)
        float    confidence;    // 0.0–1.0 (1.0 = validated reading)
        uint32_t timestamp;     // millis() at the moment of capture
        bool     valid;         // false → discard; hardware not ready or data stale
    };

    // ── Sensor-specific API ───────────────────────────────────────────────────

    virtual SensorType     getType()     const = 0;
    virtual SensorMetadata getMetadata() const = 0;

    /**
     * @brief Returns the latest cached measurement.
     *
     * Does NOT trigger a new conversion — data is refreshed by update().
     * Check SensorData::valid before using the values.
     */
    virtual SensorData read() = 0;

    /**
     * @brief Run a calibration sequence (blocking).
     *
     * For LDC1101: captures baseline RP without a coin present.
     * May take up to several seconds depending on averaging window.
     *
     * @return true on success.
     */
    virtual bool calibrate() = 0;
};
