// NAU7802Plugin.cpp — NAU7802 24-bit Weight Sensor Plugin (I2C)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// NAU7802_ARCHITECTURE.md v1.2.0 — D-12a
//
// Critical implementation notes:
//   - OTP reload MUST be executed on every power-up (ADR-NAU-001, §2.3)
//   - CTRL1/CTRL2 written AFTER OTP reload in _startupSequence() (ADR-NAU-007)
//   - CRS bits at [7:5] in CTRL2 — mask 0xE0, not 0x70 (ADR-NAU-007, B-05)
//   - All I2C in update() — Strategy A, no async FreeRTOS task (ADR-NAU-003)
//   - _mutex protects only _cachedMassG/_cachedMassN/_cachedTs/_cachedValid
//     (written from update()/Core0, read from read()/any task)
//   - wireMutex NOT used here — I2C only from update() = single task (§3.2)

#include "NAU7802Plugin.h"
#include <Preferences.h>
#include <cstring>
#include <cstdarg>
#include <cstdio>

// ============================================================================
// Helpers
// ============================================================================

void NAU7802Plugin::_setError(uint8_t code, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(_lastErrorMsg, sizeof(_lastErrorMsg), fmt, ap);
    va_end(ap);
    _lastError = {code, _lastErrorMsg};
    if (_ctx && _ctx->log) {
        _ctx->log->error("NAU7802", "%s", _lastErrorMsg);
    }
}

// ============================================================================
// I2C primitives
// ============================================================================

bool NAU7802Plugin::_writeReg(uint8_t reg, uint8_t val) {
    _ctx->wire->beginTransmission(_addr);
    _ctx->wire->write(reg);
    _ctx->wire->write(val);
    uint8_t err = _ctx->wire->endTransmission();
    if (err != 0) {
        _ds.failedReads++;
        _setError(1, "I2C write failed: reg=0x%02X err=%d", reg, err);
        return false;
    }
    return true;
}

uint8_t NAU7802Plugin::_readReg(uint8_t reg) {
    _ctx->wire->beginTransmission(_addr);
    _ctx->wire->write(reg);
    if (_ctx->wire->endTransmission(false) != 0) {
        _ds.failedReads++;
        return 0xFF;  // Error sentinel
    }
    uint8_t n = _ctx->wire->requestFrom((uint8_t)_addr, (uint8_t)1);
    if (n < 1) {
        _ds.failedReads++;
        return 0xFF;
    }
    return (uint8_t)_ctx->wire->read();
}

bool NAU7802Plugin::_readAdc24(int32_t& out) {
    // Read 3 bytes starting at REG_ADCO_B2 (0x12) using auto-increment
    _ctx->wire->beginTransmission(_addr);
    _ctx->wire->write(REG_ADCO_B2);
    if (_ctx->wire->endTransmission(false) != 0) {
        _ds.failedReads++;
        return false;
    }
    uint8_t n = _ctx->wire->requestFrom((uint8_t)_addr, (uint8_t)3);
    if (n < 3) {
        _ds.failedReads++;
        return false;
    }
    uint8_t b2 = (uint8_t)_ctx->wire->read();  // MSB
    uint8_t b1 = (uint8_t)_ctx->wire->read();
    uint8_t b0 = (uint8_t)_ctx->wire->read();  // LSB

    // Reconstruct 24-bit two's complement, sign-extend to int32
    int32_t raw = ((int32_t)b2 << 16) | ((int32_t)b1 << 8) | b0;
    if (raw & 0x800000) raw |= 0xFF000000;  // sign extension

    out = raw;
    _ds.totalReads++;
    _ds.lastSuccessMs = millis();
    return true;
}

bool NAU7802Plugin::_isReady() {
    // ADR-NAU-002: poll PU_CTRL.CR bit (bit5) — 1 = conversion result ready
    uint8_t val = _readReg(REG_PU_CTRL);
    return (val != 0xFF) && (val & PU_CTRL_CR);
}

bool NAU7802Plugin::_waitPowerUpReady(uint16_t timeout_ms) {
    uint32_t start = millis();
    while ((millis() - start) < timeout_ms) {
        uint8_t val = _readReg(REG_PU_CTRL);
        if ((val != 0xFF) && (val & PU_CTRL_PUR)) return true;
        delay(1);
    }
    return false;
}

// ============================================================================
// Startup sequence — ADR-NAU-001 (14 steps, OTP reload mandatory)
// Reference: NAU7802 datasheet Rev 2.6, §7.4 / Application Note
// ============================================================================

bool NAU7802Plugin::_startupSequence() {
    // Step 1: Register Reset (RR bit)
    if (!_writeReg(REG_PU_CTRL, PU_CTRL_RR)) return false;
    delay(1);

    // Step 2: Power-up digital (PUD bit, clear RR)
    if (!_writeReg(REG_PU_CTRL, PU_CTRL_PUD)) return false;
    delay(1);

    // Step 3: Wait for power-up ready (PUR bit = 1, timeout 200 ms)
    if (!_waitPowerUpReady(INIT_TIMEOUT_MS)) {
        _setError(2, "NAU7802: power-up timeout (PUR never set)");
        return false;
    }

    // Step 4: Power-up analog + enable internal LDO (AVDDS=1, PUA=1, PUD=1)
    if (!_writeReg(REG_PU_CTRL, PU_CTRL_AVDDS | PU_CTRL_PUA | PU_CTRL_PUD)) return false;
    delay(1);

    // Step 5: Read OTP byte 1 (REG_OTP_B1 = 0x15) — MUST precede CTRL1/CTRL2 (ADR-NAU-007)
    uint8_t otp_b1 = _readReg(REG_OTP_B1);
    if (otp_b1 == 0xFF) {
        _setError(3, "NAU7802: OTP byte 1 read failed");
        return false;
    }

    // Step 6: Reload OTP into PGA — bits[5:3] from OTP_B1, PGA_CAP_EN=1 (ADR-NAU-007)
    if (!_writeReg(REG_PGA, (otp_b1 & 0x38) | 0x30)) return false;

    // Step 7: PGA power config — must follow OTP reload (ADR-NAU-007)
    if (!_writeReg(REG_PGA_PWR, PGA_PWR_VAL)) return false;  // PGA_CAP_EN=1, bypass=0

    // Step 8: Configure LDO + PGA gain (CTRL1) — AFTER OTP reload (ADR-NAU-007, B-05)
    //   VLDO=3.0V (bits[7:5]=101b=0xA0) | GAINS=128x (bits[4:2]=111b=0x1C) = 0xBC
    if (!_writeReg(REG_CTRL1, CTRL1_VAL)) return false;

    // Step 9: Configure sample rate + channel (CTRL2) — AFTER OTP reload (ADR-NAU-007, B-05)
    //   CRS=80SPS (bits[7:5]=011b=0x60), CH1
    if (!_writeReg(REG_CTRL2, CTRL2_VAL)) return false;

    // Step 10: Enable start of conversions (CS bit in PU_CTRL)
    uint8_t pu = _readReg(REG_PU_CTRL);
    if (pu == 0xFF) return false;
    if (!_writeReg(REG_PU_CTRL, pu | PU_CTRL_CS)) return false;

    // Step 11: Wait for at least one conversion to complete (~12.5ms at 80 SPS)
    delay(15);

    // Step 12: Read and discard first sample (data sheet §7.4 — first reading after
    //   power-up is unreliable due to filter settling)
    int32_t dummy;
    _readAdc24(dummy);  // Result discarded; resets internal filter state

    // Step 13: Perform internal zero-scale calibration of ADC (not of load cell)
    //   CTRL2 CALS bit: set, wait, check CAL_ERR
    uint8_t ctrl2 = _readReg(REG_CTRL2);
    if (!_writeReg(REG_CTRL2, ctrl2 | CTRL2_CALS)) return false;
    uint32_t cal_start = millis();
    while ((millis() - cal_start) < 400) {
        uint8_t c2 = _readReg(REG_CTRL2);
        if (!(c2 & CTRL2_CALS)) {  // CALS cleared = calibration done
            if (c2 & CTRL2_CAL_ERROR) {
                _setError(4, "NAU7802: internal ADC calibration failed");
                return false;
            }
            break;
        }
        delay(5);
    }

    // Step 14: Restore CTRL2 to production config (80 SPS, CH1, no cal trigger)
    if (!_writeReg(REG_CTRL2, CTRL2_VAL)) return false;

    if (_ctx && _ctx->log) {
        _ctx->log->info("NAU7802", "Startup OK: PU_CTRL=0x%02X OTP_B1=0x%02X LDO=3.0V PGA=128 80SPS",
                        _readReg(REG_PU_CTRL), otp_b1);
    }
    return true;
}

// ============================================================================
// IPlugin lifecycle
// ============================================================================

bool NAU7802Plugin::canInitialize() {
    // Stateless pre-flight — no I2C access here (ctx not yet valid)
    return true;
}

bool NAU7802Plugin::initialize(PluginContext* ctx) {
    _ctx = ctx;

    // Create internal mutex for cached data (thread safety between update() and read())
    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) {
        _setError(5, "NAU7802: mutex creation failed");
        return false;
    }

    // Optional: load I2C address from config
    if (ctx->config) {
        int addr = ctx->config->getInt("nau7802.i2c_addr", 0x2A);
        if (addr > 0) _addr = (uint8_t)addr;
    }

    // Check hardware presence before startup sequence
    if (!checkHardwarePresence()) {
        _setError(6, "NAU7802: chip not found at I2C 0x%02X", _addr);
        _ds.status = HealthStatus::NOT_FOUND;
        return false;
    }

    // Execute 14-step OTP reload startup sequence (ADR-NAU-001)
    if (!_startupSequence()) {
        _ds.status = HealthStatus::INITIALIZATION_FAILED;
        return false;
    }

    // Attempt to load saved calibration from NVS
    if (loadCalibration()) {
        _ctx->log->info("NAU7802", "Calibration loaded: zero=%ld scale=%.6f mass_ref=%.1fg",
                        (long)_zeroOffset, _scaleFactor, _calMassG);
    } else {
        _ctx->log->warn("NAU7802", "No calibration in NVS — run tare() + calibrate()");
    }

    _initialized = true;
    _enabled     = true;
    _ds.status   = _calibrated ? HealthStatus::OK : HealthStatus::CALIBRATION_NEEDED;

    _ctx->log->info("NAU7802", "Initialized: addr=0x%02X cal=%s",
                    _addr, _calibrated ? "YES" : "NO");
    return true;
}

void NAU7802Plugin::update() {
    if (!_initialized || !_enabled) return;
    _updateAcqStateMachine();
}

void NAU7802Plugin::shutdown() {
    _enabled     = false;
    _initialized = false;
    _acqState    = AcqState::IDLE;

    if (_mutex) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }
    if (_ctx && _ctx->log) {
        _ctx->log->info("NAU7802", "Shutdown");
    }
    _ctx = nullptr;
}

// ============================================================================
// ISensorPlugin::read() — thread-safe, returns cached result
// ============================================================================

ISensorPlugin::SensorData NAU7802Plugin::read() {
    SensorData sd{};
    sd.timestamp = millis();

    if (!_mutex) {
        sd.valid = false;
        return sd;
    }

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        sd.value1    = _cachedMassG;
        sd.value2    = _cachedMassN;
        sd.valid     = _cachedValid;
        sd.timestamp = _cachedTs;
        sd.confidence= _cachedValid ? 1.0f : 0.0f;
        xSemaphoreGive(_mutex);
    } else {
        sd.valid = false;
        _setError(7, "NAU7802: read() mutex timeout");
    }
    return sd;
}

ISensorPlugin::SensorMetadata NAU7802Plugin::getMetadata() const {
    return {
        .typeName   = "Weight Sensor (I2C NAU7802)",
        .unit       = "g",
        .minValue   = 0.0f,
        .maxValue   = 100.0f,
        .resolution = 0.004f,     // ~4 mg at 80SPS/N=20 (datasheet SNR estimate)
        .sampleRate = 80          // Hz
    };
}

// ============================================================================
// Non-blocking acquisition state machine (ADR-NAU-003)
// update() contract: <= 10 ms. Each path below is <= 250 us.
// ============================================================================

void NAU7802Plugin::_updateAcqStateMachine() {
    switch (_acqState) {
    case AcqState::IDLE:
        break;  // 0 us — no work

    case AcqState::SETTLING:
        // Wait SETTLE_MS for mechanical platform stabilization
        if ((millis() - _acqStartMs) >= SETTLE_MS) {
            _sampleIdx  = 0;
            _errorCount = 0;
            _acqState   = AcqState::SAMPLING;
        }
        break;  // ~5 us total (millis() comparison)

    case AcqState::SAMPLING: {
        // One sample per update() call — only if ADC ready
        if (!_isReady()) break;  // ~75 us, not ready -> skip

        int32_t raw;
        if (!_readAdc24(raw)) {  // ~175 us
            _errorCount++;
            if (_errorCount >= MAX_ERRORS) {
                _acqState  = AcqState::ERROR;
                _ds.status = HealthStatus::SENSOR_FAULT;
                _setError(8, "NAU7802: >%d consecutive read failures", MAX_ERRORS);
            }
            break;
        }
        _errorCount = 0;
        _sampleBuf[_sampleIdx++] = static_cast<float>(raw);

        if (_sampleIdx >= N_SAMPLES) {
            // All samples collected — compute median and convert to grams
            float median_raw = _computeMedian(_sampleBuf, N_SAMPLES);
            float mass_g = (median_raw - static_cast<float>(_zeroOffset)) * _scaleFactor;
            if (mass_g < 0.0f) mass_g = 0.0f;

            float mass_n = _calibrated ? (mass_g / MASS_REF_G) : -1.0f;  // ADR-NAU-004

            // Thread-safe update (guarded by mutex — read() may run on other task)
            if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                _cachedMassG  = mass_g;
                _cachedMassN  = mass_n;
                _cachedTs     = millis();
                _cachedValid  = _calibrated;
                xSemaphoreGive(_mutex);
            }
            _acqState  = AcqState::COMPLETE;
            _ds.status = HealthStatus::OK;
            if (_ctx && _ctx->log) {
                _ctx->log->debug("NAU7802", "acq done: mass=%.2f g  mass_n=%.4f  n=%d",
                                 mass_g, mass_n, N_SAMPLES);
            }
        }
        break;  // TOTAL per ready update(): ~250 us  [OK]
    }

    case AcqState::COMPLETE:
        break;  // 0 us — waiting for next startAcquisition()

    case AcqState::ERROR:
        break;  // 0 us — waiting for shutdown/re-init
    }
}

// ============================================================================
// Acquisition control (called from Measurement Workflow)
// ============================================================================

void NAU7802Plugin::startAcquisition() {
    if (!_initialized) return;
    _acqStartMs = millis();
    _sampleIdx  = 0;
    _errorCount = 0;
    _acqState   = AcqState::SETTLING;

    if (_ctx && _ctx->log) {
        _ctx->log->debug("NAU7802", "startAcquisition() -> SETTLING (%d ms)", SETTLE_MS);
    }
}

bool NAU7802Plugin::isAcquisitionComplete() const {
    return _acqState == AcqState::COMPLETE;
}

float NAU7802Plugin::getLastMassG() const {
    if (_acqState != AcqState::COMPLETE) return 0.0f;
    if (!_mutex) return 0.0f;

    float val = 0.0f;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        val = _cachedMassG;
        xSemaphoreGive(_mutex);
    }
    return val;
}

float NAU7802Plugin::getLastMassN() const {
    if (!_calibrated) return -1.0f;  // ADR-NAU-004: sentinel for 6D fallback
    if (_acqState != AcqState::COMPLETE) return -1.0f;
    if (!_mutex) return -1.0f;

    float val = -1.0f;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        val = _cachedMassN;
        xSemaphoreGive(_mutex);
    }
    return val;
}

// ============================================================================
// Median computation (partial sort, in-place, n <= 20)
// ============================================================================

float NAU7802Plugin::_computeMedian(float* buf, uint8_t n) {
    // Simple selection sort for small n (n <= 20 -> max ~190 comparisons)
    for (uint8_t i = 0; i < n; i++) {
        uint8_t min_idx = i;
        for (uint8_t j = i + 1; j < n; j++) {
            if (buf[j] < buf[min_idx]) min_idx = j;
        }
        if (min_idx != i) {
            float tmp = buf[i];
            buf[i]    = buf[min_idx];
            buf[min_idx] = tmp;
        }
    }
    if (n & 1) return buf[n / 2];
    return (buf[n / 2 - 1] + buf[n / 2]) * 0.5f;
}

// ============================================================================
// Blocking sample capture (used by tare() and calibrate())
// ============================================================================

bool NAU7802Plugin::_blockingCaptureSamples(uint16_t n, float* out_mean, float* out_sigma) {
    // Blocking read of n ADC samples at current SPS (10 SPS for calibration)
    // Caller sets SPS before calling; this function just reads.
    float sum  = 0.0f;
    float sum2 = 0.0f;
    uint16_t collected = 0;

    uint32_t deadline = millis() + (uint32_t)n * 200;  // 200 ms per sample (10 SPS worst case)
    while (collected < n && millis() < deadline) {
        if (!_isReady()) { delay(2); continue; }
        int32_t raw;
        if (!_readAdc24(raw)) { delay(2); continue; }
        float f = static_cast<float>(raw);
        sum  += f;
        sum2 += f * f;
        collected++;
    }
    if (collected < n / 2) return false;  // Less than 50% success rate

    *out_mean = sum / collected;
    float variance = (sum2 / collected) - (*out_mean) * (*out_mean);
    *out_sigma = (variance > 0.0f) ? sqrtf(variance) : 0.0f;
    return true;
}

// ============================================================================
// Calibration — blocking (tare + 2-point calibrate)
// ============================================================================

bool NAU7802Plugin::calibrate() {
    return tare(32);
}

bool NAU7802Plugin::tare(uint16_t samples) {
    if (!_initialized) return false;

    // Switch to 10 SPS for low-noise tare measurement (CRS[2:0] at bits[7:5], mask ~0xE0, B-05)
    _writeReg(REG_CTRL2, (CTRL2_VAL & ~0xE0) | (0x00 << 5));  // CRS = 000 = 10 SPS
    delay(120);  // Filter settling at 10 SPS (~100 ms)

    float mean, sigma;
    bool ok = _blockingCaptureSamples(samples, &mean, &sigma);

    // Restore 80 SPS
    _writeReg(REG_CTRL2, CTRL2_VAL);

    if (!ok) {
        _setError(9, "NAU7802: tare() capture failed");
        return false;
    }

    _zeroOffset = static_cast<int32_t>(mean);

    if (_ctx && _ctx->log) {
        _ctx->log->info("NAU7802", "Tare OK: zero_offset=%ld (sigma=%.1f raw_counts, n=%d)",
                        (long)_zeroOffset, sigma, samples);
    }
    return true;
}

bool NAU7802Plugin::calibrate(float known_mass_g, uint16_t samples) {
    if (!_initialized) return false;
    if (known_mass_g <= 0.0f || known_mass_g > 200.0f) {
        _setError(10, "NAU7802: invalid known_mass_g=%.2f (expected 0..200)", known_mass_g);
        return false;
    }

    // Switch to 10 SPS for low-noise calibration (CRS[2:0] at bits[7:5], mask ~0xE0, B-05)
    _writeReg(REG_CTRL2, (CTRL2_VAL & ~0xE0) | (0x00 << 5));  // CRS = 10 SPS
    delay(120);

    float mean, sigma;
    bool ok = _blockingCaptureSamples(samples, &mean, &sigma);

    // Restore 80 SPS
    _writeReg(REG_CTRL2, CTRL2_VAL);

    if (!ok) {
        _setError(11, "NAU7802: calibrate() capture failed");
        return false;
    }

    float net_raw = mean - static_cast<float>(_zeroOffset);
    if (fabsf(net_raw) < 1.0f) {
        _setError(12, "NAU7802: net_raw near zero (%.1f) — tare first", net_raw);
        return false;
    }

    _scaleFactor  = known_mass_g / net_raw;
    _calMassG     = known_mass_g;
    _calibrated   = true;
    _calTimestamp = (uint32_t)(millis() / 1000);  // crude Unix-ish timestamp
    _ds.status    = HealthStatus::OK;

    if (_ctx && _ctx->log) {
        _ctx->log->info("NAU7802", "Calibrate OK: scale=%.8f raw/g (ref=%.2fg net_raw=%.1f sigma=%.1f)",
                        _scaleFactor, known_mass_g, net_raw, sigma);
    }
    return saveCalibration();
}

// ============================================================================
// NVS calibration persistence (namespace "nau7802")
// ============================================================================

bool NAU7802Plugin::saveCalibration() {
    Preferences prefs;
    if (!prefs.begin("nau7802", false)) {
        _setError(13, "NAU7802: NVS open failed (save)");
        return false;
    }
    prefs.putInt("zero",       _zeroOffset);
    prefs.putFloat("scale",    _scaleFactor);
    prefs.putBool("cal_ok",    _calibrated);
    prefs.putUInt("cal_ts",    _calTimestamp);
    prefs.putFloat("cal_mass", _calMassG);
    prefs.end();

    if (_ctx && _ctx->log) {
        _ctx->log->info("NAU7802", "Calibration saved to NVS");
    }
    return true;
}

bool NAU7802Plugin::loadCalibration() {
    Preferences prefs;
    if (!prefs.begin("nau7802", true)) {  // read-only
        return false;
    }
    bool cal_ok = prefs.getBool("cal_ok", false);
    if (!cal_ok) {
        prefs.end();
        return false;
    }

    _zeroOffset   = prefs.getInt("zero",       0);
    _scaleFactor  = prefs.getFloat("scale",    1.0f);
    _calTimestamp = prefs.getUInt("cal_ts",    0);
    _calMassG     = prefs.getFloat("cal_mass", 0.0f);
    _calibrated   = true;
    prefs.end();
    return true;
}

void NAU7802Plugin::clearCalibration() {
    Preferences prefs;
    if (prefs.begin("nau7802", false)) {
        prefs.clear();
        prefs.end();
    }
    _zeroOffset  = 0;
    _scaleFactor = 1.0f;
    _calibrated  = false;
    _calMassG    = 0.0f;
    _calTimestamp= 0;
    _ds.status   = HealthStatus::CALIBRATION_NEEDED;
}

// ============================================================================
// IDiagnosticPlugin
// ============================================================================

IDiagnosticPlugin::HealthStatus NAU7802Plugin::getHealthStatus() const {
    return _ds.status;
}

IDiagnosticPlugin::DiagnosticResult NAU7802Plugin::runDiagnostics() {
    DiagnosticResult r{};
    r.timestamp = millis();

    if (!_initialized) {
        r.status = HealthStatus::INITIALIZATION_FAILED;
        r.error  = _lastError;
        return r;
    }

    r.stats.totalReads   = _ds.totalReads;
    r.stats.failedReads  = _ds.failedReads;
    r.stats.lastSuccess  = _ds.lastSuccessMs;
    r.stats.successRate  = (_ds.totalReads > 0)
        ? (uint16_t)(100 - (_ds.failedReads * 100 / _ds.totalReads))
        : 0;

    if (!checkHardwarePresence()) {
        r.status = HealthStatus::NOT_FOUND;
    } else if (!_calibrated) {
        r.status = HealthStatus::CALIBRATION_NEEDED;
    } else if (r.stats.successRate < 90) {
        r.status = HealthStatus::DEGRADED;
    } else {
        r.status = HealthStatus::OK;
    }

    _ds.status = r.status;
    r.error    = _lastError;
    return r;
}

bool NAU7802Plugin::runSelfTest() {
    if (!_initialized) return false;

    // 1. Check PU_CTRL — PUR bit should be set (powered up)
    uint8_t pu_ctrl = _readReg(REG_PU_CTRL);
    if (!(pu_ctrl & PU_CTRL_PUR)) {
        _setError(14, "NAU7802 self-test: PUR not set (PU_CTRL=0x%02X)", pu_ctrl);
        return false;
    }

    // 2. Read 5 consecutive ADC samples — check all are non-zero
    for (int i = 0; i < 5; i++) {
        uint32_t t = millis();
        while (!_isReady() && (millis() - t) < 100) delay(2);
        int32_t raw;
        if (!_readAdc24(raw)) {
            _setError(15, "NAU7802 self-test: ADC read failed at sample %d", i);
            return false;
        }
        if (raw == 0) {
            _setError(16, "NAU7802 self-test: ADC output is zero at sample %d", i);
            return false;
        }
    }

    if (_ctx && _ctx->log) {
        _ctx->log->info("NAU7802", "Self-test PASSED");
    }
    return true;
}

IDiagnosticPlugin::DiagnosticResult NAU7802Plugin::getStatistics() const {
    DiagnosticResult r{};
    r.timestamp          = millis();
    r.status             = _ds.status;
    r.error              = _lastError;
    r.stats.totalReads   = _ds.totalReads;
    r.stats.failedReads  = _ds.failedReads;
    r.stats.lastSuccess  = _ds.lastSuccessMs;
    r.stats.successRate  = (_ds.totalReads > 0)
        ? (uint16_t)(100 - (_ds.failedReads * 100 / _ds.totalReads))
        : 0;
    return r;
}

bool NAU7802Plugin::checkHardwarePresence() {
    // NAU7802 has no fixed CHIP_ID register — instead, verify:
    // 1. I2C ACK at address 0x2A
    // 2. PU_CTRL register reads a plausible value (not 0xFF)
    _ctx->wire->beginTransmission(_addr);
    if (_ctx->wire->endTransmission() != 0) return false;

    uint8_t pu = _readReg(REG_PU_CTRL);
    return (pu != 0xFF);
}

bool NAU7802Plugin::checkCommunication() {
    // 3 consecutive PU_CTRL reads — all must return non-0xFF
    for (int i = 0; i < 3; i++) {
        if (_readReg(REG_PU_CTRL) == 0xFF) return false;
        delay(1);
    }
    return true;
}

bool NAU7802Plugin::checkCalibration() {
    // scale_factor = known_mass_g / net_raw_counts
    // With PGA=128, 100g load cell: net_raw ~ 50000-500000 counts for 100g
    // -> scale_factor ~ 2e-4 to 2e-3  (well within [1e-6, 10.0] sanity range)
    return _calibrated && (_scaleFactor > 1e-6f) && (_scaleFactor < 10.0f);
}
