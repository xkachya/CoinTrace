// NAU7802Plugin.h — NAU7802 24-bit Weight Sensor Plugin (I2C)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// NAU7802_ARCHITECTURE.md v1.4.0 — D-12a
//
// Hardware: Nuvoton NAU7802 24-bit sigma-delta ADC + 100 g load cell
//   I2C address: 0x2A (ADDR pin -> GND, default)
//   I2C speed:   400 kHz Fast Mode
//
// Key facts (NAU7802 datasheet Rev 2.6, cross-ref Adafruit + SparkFun libs):
//   - 24-bit sigma-delta ADC, 2 differential channels
//   - PGA gain 1x/2x/4x/8x/16x/32x/64x/128x
//   - Sample rates: 10/20/40/80/320 SPS (CTRL2 CRS[2:0] at bits[6:4], shift <<4)
//   - Internal LDO: 2.4/2.7/3.0/3.3/3.6/3.9/4.2/4.5 V (CTRL1 VLDO[2:0] at bits[5:3])
//   - Analog init: CLK_CHP disable (0x15|=0x30), clear LDOMODE (0x1B&~0x40), PGA_CAP_EN (0x1C|=0x80)
//     Verified against Adafruit_NAU7802 + SparkFun Qwiic Scale libs (audit 2026-04-10)
//   - DRDY pin not connected in v1 hardware — polling CR bit in PU_CTRL bit 5 (ADR-NAU-002)
//
// ADR-NAU-001: [SUPERSEDED 2026-04-10] OTP auto-loads at power-on; no explicit reload needed.
//   Original impl wrote 0x30 to REG_PGA (0x1B) → BYPASS_EN=1 → effective gain=1x not 128x.
//   Evidence: 151 counts/g observed vs 168 (bypass model) vs 21474 (128x model).
//   Fix: CLK_CHP via 0x15|=0x30; LDOMODE via 0x1B&~0x40; PGA_CAP_EN via 0x1C|=0x80.
//   Source: docs/external/2026-04-10.NAU7802_IMPLEMENTATION_AUDIT.md (X-01, X-02, X-03)
// ADR-NAU-002: DRDY pin not connected in v1; use PU_CTRL.CR bit (bit 5 = 0x20) polling
// ADR-NAU-003: Strategy A (no async FreeRTOS task); all I2C in update(), <= 250 us/call
// ADR-NAU-004: mass_n = -1.0f sentinel when NAU unavailable (6D fallback in FingerprintCache)
// ADR-NAU-005: MASS_REF_G = 33.3 (XUSSR10 reference coin, heaviest class)
// ADR-NAU-006: SETTLE_MS = 500 ms (mechanical settling verified; 200 ms insufficient)
// ADR-NAU-007: CTRL1/CTRL2 written after analog init (steps 5-7); B-08 CRS at bits[6:4] still valid

#pragma once

#include "ISensorPlugin.h"
#include "IDiagnosticPlugin.h"
#include "PluginContext.h"
#include <Arduino.h>
#include <Wire.h>            // NAU7802 is I2C-only (addr 0x2A, SDA=GPIO8, SCL=GPIO9)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class NAU7802Plugin : public ISensorPlugin, public IDiagnosticPlugin {
public:

    // ── Acquisition state machine ────────────────────────────────────────────
    enum class AcqState : uint8_t {
        IDLE,       // Waiting for startAcquisition() call
        SETTLING,   // Mechanical settling after coin placement (SETTLE_MS)
        SAMPLING,   // Collecting N_SAMPLES samples at 80 SPS
        COMPLETE,   // All samples collected, result valid
        ERROR       // Hardware fault, requires re-init
    };

private:

    // ── NAU7802 Register map (datasheet Rev 2.6, Table 1) ───────────────────
    static constexpr uint8_t REG_PU_CTRL   = 0x00;  // Power-up control
    static constexpr uint8_t REG_CTRL1     = 0x01;  // PGA gain, LDO voltage
    static constexpr uint8_t REG_CTRL2     = 0x02;  // Channel select, SPS, cal
    static constexpr uint8_t REG_I2C_CTRL  = 0x11;  // I2C config
    static constexpr uint8_t REG_ADCO_B2   = 0x12;  // ADC output byte 2 (MSB)
    static constexpr uint8_t REG_ADCO_B1   = 0x13;  // ADC output byte 1
    static constexpr uint8_t REG_ADCO_B0   = 0x14;  // ADC output byte 0 (LSB)
    static constexpr uint8_t REG_ADC_CTRL  = 0x15;  // ADC control: CLK_CHP in bits[5:4] (NOT OTP)
    static constexpr uint8_t REG_PGA       = 0x1B;  // PGA config: LDOMODE(6) BYPASS_EN(4)
    static constexpr uint8_t REG_PGA_PWR   = 0x1C;  // PGA power: PGA_CAP_EN(7)
    static constexpr uint8_t REG_DEV_REV   = 0x1F;  // Device revision / I2C_CTRL extended

    // PU_CTRL bits
    static constexpr uint8_t PU_CTRL_RR    = 0x01;  // Register reset
    static constexpr uint8_t PU_CTRL_PUD   = 0x02;  // Power-up digital
    static constexpr uint8_t PU_CTRL_PUA   = 0x04;  // Power-up analog
    static constexpr uint8_t PU_CTRL_PUR   = 0x08;  // Power-up ready (read-only)
    static constexpr uint8_t PU_CTRL_CS    = 0x10;  // Cycle start (start ADC conversion)
    static constexpr uint8_t PU_CTRL_CR    = 0x20;  // Conversion result ready (read-only)
    static constexpr uint8_t PU_CTRL_OSCS  = 0x40;  // System clock source (0=internal)
    static constexpr uint8_t PU_CTRL_AVDDS = 0x80;  // AVDD source (1=internal LDO)

    // CTRL1 bits — LDO voltage [5:3] and PGA gain [2:0] (cross-ref Adafruit_NAU7802 + SparkFun libs)
    static constexpr uint8_t CTRL1_GAINS_128 = 0x07;  // PGA = 128x (GAINS[2:0] at bits[2:0])
    static constexpr uint8_t CTRL1_LDO_30V   = 0x05;  // LDO = 3.0 V (VLDO[2:0] at bits[5:3] = 101b)
    // Full CTRL1 value: VLDO=3.0V (bits[5:3]=101b=0x28) | GAINS=128x (bits[2:0]=111b=0x07)
    // B-08: B-05 had wrong shift (<<5 and <<2); correct is <<3 and <<0 per datasheet
    static constexpr uint8_t CTRL1_VAL       = (0x05 << 3) | (0x07 << 0);  // 0x2F (B-08 fix)

    // CTRL2 bits — conversion rate and channel
    static constexpr uint8_t CTRL2_CRS_80SPS = 0x03;  // 80 SPS: CRS[2:0] = 011
    static constexpr uint8_t CTRL2_CRS_320SPS= 0x07;  // 320 SPS: CRS[2:0] = 111 (fast/debug mode)
    static constexpr uint8_t CTRL2_CH1       = 0x00;  // Channel 1 (CHS bit = 0)
    // Full CTRL2 value: CH1, 80SPS, no calibration — CRS[2:0] at bits[6:4], CHS at bit[7]
    // B-08: B-05 had wrong shift (<<5); correct is <<4 per Adafruit/SparkFun reference libs
    static constexpr uint8_t CTRL2_VAL       = (CTRL2_CRS_80SPS << 4);  // 0x30 (B-08 fix)

    // CTRL2 CAL bits (§7.4.2)
    static constexpr uint8_t CTRL2_CALS      = 0x04;  // Calibration start bit
    static constexpr uint8_t CTRL2_CAL_ERROR = 0x08;  // Calibration error flag

    // REG_PGA (0x1B) / REG_ADC_CTRL (0x15) / REG_PGA_PWR (0x1C) bit constants
    static constexpr uint8_t PGA_PWR_VAL     = 0x80;  // PGA_CAP_EN = bit7 (§9.14, SparkFun ref)
    static constexpr uint8_t ADC_CHP_DIS     = 0x30;  // bits[5:4]=11 → CLK_CHP off (§9.1)
    static constexpr uint8_t PGA_LDOMODE_BIT = 0x40;  // bit6: 0 = low-ESR caps (low-noise mode)
    static constexpr uint8_t PGA_BYPASS_EN   = 0x10;  // bit4: 1 = bypass PGA — MUST remain 0

    // ── Plugin state ─────────────────────────────────────────────────────────
    PluginContext*    _ctx         = nullptr;
    uint8_t           _addr        = 0x2A;
    bool              _initialized = false;
    bool              _enabled     = false;

    // ── Calibration (NVS-persisted, namespace "nau7802") ────────────────────
    int32_t           _zeroOffset  = 0;         // NVS key: "zero"
    float             _scaleFactor = 1.0f;      // NVS key: "scale"
    bool              _calibrated  = false;     // NVS key: "cal_ok" (bool)
    uint32_t          _calTimestamp= 0;         // NVS key: "cal_ts" (unix ts)
    float             _calMassG    = 0.0f;      // NVS key: "cal_mass_g" (reference mass used)

    // ── Acquisition state machine (ADR-NAU-003: Strategy A) ─────────────────
    AcqState          _acqState    = AcqState::IDLE;
    uint8_t           _sampleIdx   = 0;
    uint32_t          _acqStartMs  = 0;
    uint8_t           _errorCount  = 0;
    uint8_t           _i2cFailCount = 0;  // P1.1: consecutive _isReady() 0xFF reads — fast escalation

    static constexpr uint8_t  N_SAMPLES   = 20;   // Samples per acquisition (80SPS -> 250ms)
    static constexpr uint16_t SETTLE_MS   = 500;  // ADR-NAU-006: mechanical settling (hw-verified)

    float             _sampleBuf[N_SAMPLES];       // 80 bytes, static in plugin object

    // ── Cached result (protected by _mutex for thread safety) ───────────────
    float             _cachedMassG  = 0.0f;
    float             _cachedMassN  = 0.0f;
    uint32_t          _cachedTs     = 0;
    bool              _cachedValid  = false;
    SemaphoreHandle_t _mutex        = nullptr;

    // ── Constants ────────────────────────────────────────────────────────────
    static constexpr uint16_t INIT_TIMEOUT_MS= 200;     // Power-up ready timeout
    static constexpr uint8_t  MAX_ERRORS     = 10;      // Error threshold -> ERROR state
    static constexpr uint8_t  I2C_FAIL_THRESHOLD = 5;  // P1.1: consecutive 0xFF in _isReady() -> fast ERROR (~60ms at 80SPS)

    // ── Diagnostics ─────────────────────────────────────────────────────────
    struct {
        uint32_t     totalReads    = 0;
        uint32_t     failedReads   = 0;
        uint32_t     lastSuccessMs = 0;
        HealthStatus status        = HealthStatus::UNKNOWN;
    } _ds;

    char              _lastErrorMsg[64] = {};
    ErrorCode         _lastError        = {0, "No error"};

    // ── I2C bus recovery parameters (used in tare() pre-flight) ─────────────
    // Stored at initialize() time so tare() can reset the bus after WiFi-induced hangs.
    int8_t            _sda          = 8;         // SDA GPIO; overridden by config "nau7802.sda"
    int8_t            _scl          = 9;         // SCL GPIO; overridden by config "nau7802.scl"
    uint32_t          _i2cHz        = 400000;    // I2C clock Hz; overridden by "nau7802.i2c_hz"

    // ── Private hardware methods ─────────────────────────────────────────────
    bool     _writeReg(uint8_t reg, uint8_t val);
    uint8_t  _readReg(uint8_t reg);
    bool     _readAdc24(int32_t& out);           // Reads 3 bytes ADCO_B2..B0
    bool     _isReady();                         // Checks PU_CTRL.CR bit (ADR-NAU-002)
    bool     _startupSequence();                 // OTP reload + config (ADR-NAU-001, 14 steps)
    bool     _waitPowerUpReady(uint16_t timeout_ms); // Waits for PU_CTRL.PUR bit

    // ── Private acquisition methods ──────────────────────────────────────────
    void     _updateAcqStateMachine();           // Called from update()
    float    _computeMedian(float* buf, uint8_t n); // In-place partial sort, n <= 20

    // ── Private calibration helpers ──────────────────────────────────────────
    bool     _blockingCaptureSamples(uint16_t n, float* out_mean, float* out_sigma);
    bool     _ensureConversionsRunning();  // Pre-flight: recover I2C bus / chip reset before blocking ops

    void     _setError(uint8_t code, const char* fmt, ...);

public:

    // ── Public constants ──────────────────────────────────────────────────────
    // Reference mass for mass_n normalization (ADR-NAU-005).
    // Used in doMeasCompute() to compute mass_n = mass_g / MASS_REF_G for 7D vector (D-12d).
    static constexpr float MASS_REF_G = 33.3f;   // XUSSR10, heaviest class

    // ── IPlugin metadata ──────────────────────────────────────────────────────
    const char* getName()    const override { return "NAU7802Plugin"; }
    const char* getVersion() const override { return "1.0.0"; }
    const char* getAuthor()  const override { return "CoinTrace Community"; }

    // ── IPlugin lifecycle ─────────────────────────────────────────────────────
    bool canInitialize() override;
    bool initialize(PluginContext* ctx) override;
    void update() override;      // Non-blocking, <= 250 us/call (ADR-NAU-003)
    void shutdown() override;

    // ── IPlugin status ────────────────────────────────────────────────────────
    bool isReady()   const override { return _initialized; }
    bool isEnabled() const override { return _enabled; }

    // ── ISensorPlugin ─────────────────────────────────────────────────────────
    // read() returns: value1 = mass_g, value2 = mass_n, valid = _cachedValid
    SensorData         read()             override;
    SensorMetadata     getMetadata() const override;
    SensorType         getType()     const override { return SensorType::WEIGHT; }

    // ── Calibration (blocking — call outside update() loop) ──────────────────
    // ISensorPlugin::calibrate() — runs tare() with 32 samples (blocking)
    bool calibrate() override;

    // 2-point calibration with known reference mass (blocking, ~3.5 s at 10 SPS)
    bool calibrate(float known_mass_g, uint16_t samples = 32);

    // Tare (zero with no load), blocking ~3.5 s at 10 SPS for low noise
    bool tare(uint16_t samples = 32);

    // isCalibrated() — true if both tare and scale are set and loaded from NVS
    bool isCalibrated() const { return _calibrated; }

    // Calibration constants (valid only when isCalibrated(); used by smoke test + diagnostics)
    float   getScaleFactor() const { return _scaleFactor; }
    int32_t getZeroOffset()  const { return _zeroOffset; }

    // ── Non-blocking acquisition control ─────────────────────────────────────
    // Called from Measurement Workflow at STEP_BASE entry
    void startAcquisition();

    // True once COMPLETE state reached (result valid in _cachedMassG)
    bool isAcquisitionComplete() const;

    // True if acquisition ended in ERROR (I2C/chip failure) — caller may retry
    bool isAcquisitionError() const;

    // Current acquisition state (for debug/logging)
    AcqState getAcqState() const { return _acqState; }

    // Returns last measured mass (g). Valid only when isAcquisitionComplete().
    float getLastMassG() const;

    // Returns last normalized mass. mass_n = mass_g / MASS_REF_G.
    // Returns -1.0f (ADR-NAU-004) if not calibrated or acquisition not complete.
    float getLastMassN() const;

    // ── NVS calibration persistence ───────────────────────────────────────────
    bool saveCalibration();
    bool loadCalibration();
    void clearCalibration();

    // ── IDiagnosticPlugin ─────────────────────────────────────────────────────
    HealthStatus     getHealthStatus() const override;
    ErrorCode        getLastError()    const override { return _lastError; }
    DiagnosticResult runDiagnostics()        override;
    bool             runSelfTest()           override;
    DiagnosticResult getStatistics()   const override;
    bool             checkHardwarePresence() override;
    bool             checkCommunication()    override;
    bool             checkCalibration()      override;
};
