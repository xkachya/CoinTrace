// IDiagnosticPlugin.h — Diagnostic Mixin Interface
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// PLUGIN_DIAGNOSTICS.md / LDC1101_ARCHITECTURE.md §8

#pragma once

#include <stdint.h>

/**
 * @brief Optional mixin for plugins that expose self-diagnostics.
 *
 * Plugins may inherit from both ISensorPlugin (or IPlugin) AND IDiagnosticPlugin.
 * PluginSystem checks `dynamic_cast<IDiagnosticPlugin*>` before calling diagnostics.
 *
 * Example:
 *   class LDC1101Plugin : public ISensorPlugin, public IDiagnosticPlugin { ... };
 */
class IDiagnosticPlugin {
public:
    virtual ~IDiagnosticPlugin() = default;

    enum class HealthStatus : uint8_t {
        UNKNOWN               = 0,
        OK                    = 1,
        DEGRADED              = 2,   // Working but with reduced quality (high fail rate)
        STALE_DATA            = 3,   // update() not producing fresh data (generic stale)
        TIMEOUT               = 4,   // Data not refreshed within expected window (5 s)
        SENSOR_FAULT          = 5,   // Hardware present but returning invalid readings
        INITIALIZATION_FAILED = 6,   // initialize() succeeded partially; ongoing issue
        PLUGIN_DISABLED        = 7,   // Plugin disabled itself after unrecoverable fault
        NOT_FOUND             = 8,   // Chip not detected (wrong CHIP_ID or no SPI response)
        COMMUNICATION_ERROR   = 9,   // SPI/I2C transaction failed
        CALIBRATION_NEEDED    = 10,  // No calibration baseline established yet
        OK_WITH_WARNINGS      = 11   // Functional but elevated fail rate (>10%)
    };

    struct ErrorCode {
        uint8_t     code;       // 0 = no error
        const char* message;    // Static literal or pointer to a persistent member buffer
    };

    /**
     * @brief Runtime statistics and health snapshot returned by runDiagnostics()
     * and getStatistics().
     */
    struct DiagnosticResult {
        HealthStatus status;
        ErrorCode    error;
        uint32_t     timestamp;   // millis() at check time
        struct {
            uint32_t totalReads;
            uint32_t failedReads;
            uint16_t successRate; // 0–100 %
            uint32_t lastSuccess; // millis() of last successful read
        } stats;
    };

    // ── Mandatory (minimum viable diagnostic) ────────────────────────────────

    /**
     * @brief Returns current health classification.
     *
     * Must be lock-free: implementations use an atomic flag, not a full mutex.
     * Safe to call from any FreeRTOS task.
     */
    virtual HealthStatus getHealthStatus() const = 0;

    /**
     * @brief Returns the most recent error code.
     *
     * code=0 means no error.  message must outlive the ErrorCode object.
     */
    virtual ErrorCode getLastError() const = 0;

    // ── Extended diagnostics ──────────────────────────────────────────────────

    /**
     * @brief Full active diagnostics — communicates with hardware.
     *
     * Checks hardware presence, calibration validity, and fail-rate.
     * Returns a DiagnosticResult suitable for logging or display.
     */
    virtual DiagnosticResult runDiagnostics() = 0;

    /**
     * @brief Quick hardware self-test (blocking, use outside update() loop).
     *
     * Verifies chip ID, STATUS register, and reading stability over 5 samples.
     * @return true if all checks pass.
     */
    virtual bool runSelfTest() = 0;

    /** @brief Snapshot of runtime read/fail statistics. */
    virtual DiagnosticResult getStatistics() const = 0;

    /** @brief True if CHIP_ID matches expected value via one SPI read. */
    virtual bool checkHardwarePresence() = 0;

    /** @brief True if 3 consecutive CHIP_ID reads all return expected value. */
    virtual bool checkCommunication() = 0;

    /** @brief True if calibration baseline is set and within plausible range. */
    virtual bool checkCalibration() = 0;
};
