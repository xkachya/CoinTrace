// NAU7802Plugin.cpp — NAU7802 24-bit Weight Sensor Plugin (I2C)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// NAU7802_ARCHITECTURE.md v1.4.0 — D-12a
//
// Critical implementation notes:
//   - OTP reload MUST be executed on every power-up (ADR-NAU-001, §2.3)
//   - CTRL1/CTRL2 written AFTER OTP reload in _startupSequence() (ADR-NAU-007)
//   - CRS bits at [6:4] in CTRL2 — mask 0x70 (B-08 fix; pre-B-08 erroneously used [7:5]/0xE0)
//   - All I2C in update() — Strategy A, no async FreeRTOS task (ADR-NAU-003)
//   - _mutex protects only _cachedMassG/_cachedMassN/_cachedTs/_cachedValid
//     (written from update()/Core0, read from read()/any task)
//   - wireMutex NOT used here — I2C only from update() = single task (§3.2)

#include "NAU7802Plugin.h"
#include "Logger.h"         // full Logger definition (PluginContext.h only forward-declares)
#include "ConfigManager.h"  // full ConfigManager definition (PluginContext.h only forward-declares)
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
    if (val == 0xFF) {
        // P1.1: fast I2C escalation — 5 consecutive 0xFF (~60ms at 80SPS) → ERROR.
        // Prevents 3-second silent SAMPLING timeout when WiFi DMA kills the I2C bus.
        _i2cFailCount++;
        if (_i2cFailCount >= I2C_FAIL_THRESHOLD && _acqState == AcqState::SAMPLING) {
            _acqState  = AcqState::ERROR;
            _ds.status = HealthStatus::SENSOR_FAULT;
            _setError(20, "NAU7802: I2C dead (%u consecutive 0xFF) — fast escalate",
                      _i2cFailCount);
        }
        return false;
    }
    _i2cFailCount = 0;  // reset on any successful I2C read
    return (val & PU_CTRL_CR) != 0;
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
    delay(10);  // NAU7802_ARCHITECTURE §2.3 step 2: 10 ms reset pulse (was 1 ms — insufficient)

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

    // Step 5: Disable ADC chopper clock — per datasheet §9.1 startup sequence.
    // Register 0x15 is the ADC Control register (NOT OTP). Both Adafruit and SparkFun
    // libraries write bits[5:4]=11 here: "Turn off CLK_CHP from 9.1 power on sequencing".
    // Default CLK_CHP=00 injects maximum chopper noise into ADC output.
    uint8_t adc_ctrl = _readReg(REG_ADC_CTRL);
    if (adc_ctrl == 0xFF) {
        _setError(3, "NAU7802: ADC control register (0x15) read failed");
        return false;
    }
    if (!_writeReg(REG_ADC_CTRL, adc_ctrl | ADC_CHP_DIS)) return false;

    // Step 6: Configure REG_PGA (0x1B) — clear LDOMODE (bit6) for low-ESR caps (low-noise mode).
    // CRITICAL: ONLY clear bit6. Both reference libs only clear bit6 of this register.
    // bit4 = BYPASS_EN MUST remain 0 — if set to 1, PGA is bypassed entirely and
    // effective gain = 1x regardless of CTRL1 GAINS setting (audit X-01: was writing 0x30
    // which set BYPASS_EN=1 + OUT_EN=1, causing 151 counts/g instead of 21,474 counts/g).
    uint8_t pga = _readReg(REG_PGA);
    if (!_writeReg(REG_PGA, pga & ~PGA_LDOMODE_BIT)) return false;  // clear bit6 only

    // Step 7: Enable 330pF PGA decoupling capacitor — REG_PGA_PWR bit7 = PGA_CAP_EN = 0x80.
    // Per datasheet §9.14 application note (SparkFun: setBit(PGA_CAP_EN, PGA_PWR_REG)).
    // Previous PGA_PWR_VAL=0x30 was wrong: it set MSTR_BIAS_CURR bits[6:4] instead (audit X-03).
    if (!_writeReg(REG_PGA_PWR, PGA_PWR_VAL)) return false;  // 0x80 = PGA_CAP_EN only

    // Step 8: Configure LDO + PGA gain (CTRL1) — after analog init steps 5-7
    //   VLDO=3.0V (bits[7:5]=101b=0xA0) | GAINS=128x (bits[4:2]=111b=0x1C) = 0xBC
    if (!_writeReg(REG_CTRL1, CTRL1_VAL)) return false;

    // Step 9: Configure sample rate + channel (CTRL2) — after analog init steps 5-7
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
        uint8_t pu     = _readReg(REG_PU_CTRL);
        uint8_t ctrl1  = _readReg(REG_CTRL1);
        uint8_t ctrl2  = _readReg(REG_CTRL2);
        uint8_t pga_rb = _readReg(REG_PGA);
        _ctx->log->info("NAU7802", "Startup OK: PU_CTRL=0x%02X REG_PGA=0x%02X LDO=3.0V PGA=128 80SPS",
                        pu, pga_rb);
        if (pga_rb & PGA_BYPASS_EN) {
            // BYPASS_EN=1 means PGA is bypassed: effective gain=1x NOT 128x
            // sensitivity will be ~151 counts/g instead of ~21474 counts/g
            _ctx->log->error("NAU7802", "BYPASS_EN=1 in REG_PGA=0x%02X — PGA bypassed! gain=1x not 128x",
                             pga_rb);
        }
        // Readback CTRL1/CTRL2 — mismatch indicates I2C write failure during init
        if (ctrl1 != CTRL1_VAL || ctrl2 != CTRL2_VAL) {
            _ctx->log->warning("NAU7802", "Register mismatch: CTRL1=0x%02X(exp 0x%02X) CTRL2=0x%02X(exp 0x%02X)",
                               ctrl1, CTRL1_VAL, ctrl2, CTRL2_VAL);
        } else {
            _ctx->log->debug("NAU7802", "Regs OK: CTRL1=0x%02X CTRL2=0x%02X", ctrl1, ctrl2);
        }
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

    // Optional: load I2C address + bus parameters from config
    if (ctx->config) {
        int addr = ctx->config->getInt("nau7802.i2c_addr", 0x2A);
        if (addr > 0) _addr = (uint8_t)addr;
        _sda   = (int8_t) ctx->config->getInt("nau7802.sda",    8);
        _scl   = (int8_t) ctx->config->getInt("nau7802.scl",    9);
        _i2cHz = (uint32_t)ctx->config->getInt("nau7802.i2c_hz", 400000);
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
        _ctx->log->warning("NAU7802", "No calibration in NVS — run tare() + calibrate()");
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
        // Safety timeout: 3 s overall since acquisition start
        if ((millis() - _acqStartMs) > (SETTLE_MS + 3000UL)) {
            _acqState  = AcqState::ERROR;
            _ds.status = HealthStatus::SENSOR_FAULT;
            _setError(11, "NAU7802: SAMPLING timeout (%u samples)", _sampleIdx);
            break;
        }
        // P1.3 fast-fail: 0 samples after 500 ms in SAMPLING → chip stalled.
        // _isReady() may return false without 0xFF if I2C data is corrupted-but-plausible.
        // At 80 SPS the first sample must appear within 12.5 ms; 500 ms budget is generous.
        if (_sampleIdx == 0 && (millis() - _acqStartMs) > (SETTLE_MS + 500UL)) {
            _acqState  = AcqState::ERROR;
            _ds.status = HealthStatus::SENSOR_FAULT;
            _setError(12, "NAU7802: SAMPLING stalled (0/%u samples, %lums) — fast fail",
                      N_SAMPLES, (unsigned long)(millis() - _acqStartMs));
            break;
        }
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
            float median_raw = _computeMedian(_sampleBuf, N_SAMPLES);  // sorts _sampleBuf in-place
            float mass_g = (median_raw - static_cast<float>(_zeroOffset)) * _scaleFactor;
            if (mass_g < 0.0f) mass_g = 0.0f;

            // Compute stddev of all N_SAMPLES in grams — quality indicator
            // Buffer is sorted after _computeMedian(); variance is order-independent.
            float sum_raw = 0.0f, sum2_raw = 0.0f;
            for (uint8_t i = 0; i < N_SAMPLES; i++) {
                sum_raw  += _sampleBuf[i];
                sum2_raw += _sampleBuf[i] * _sampleBuf[i];
            }
            float mean_raw = sum_raw / N_SAMPLES;
            float var_raw  = (sum2_raw / N_SAMPLES) - (mean_raw * mean_raw);
            float sigma_g  = (var_raw > 0.0f) ? sqrtf(var_raw) * fabsf(_scaleFactor) : 0.0f;

            float mass_n = _calibrated ? (mass_g / MASS_REF_G) : -1.0f;  // ADR-NAU-004

            // Thread-safe update (guarded by mutex — read() may run on other task)
            if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                _cachedMassG  = mass_g;
                _cachedMassN  = mass_n;
                _cachedSigmaG = sigma_g;
                _cachedTs     = millis();
                _cachedValid  = _calibrated;
                xSemaphoreGive(_mutex);
            }
            _acqState  = AcqState::COMPLETE;
            _ds.status = HealthStatus::OK;
            if (_ctx && _ctx->log) {
                // sigma_g > 0.3g is suspicious (coin oscillating or not fully on platform)
                if (sigma_g > 0.3f) {
                    _ctx->log->warning("NAU7802",
                        "acq done: mass=%.2f g  mass_n=%.4f  sigma=%.3f g  n=%d  *** HIGH SIGMA — coin unstable?",
                        mass_g, mass_n, sigma_g, N_SAMPLES);
                } else {
                    _ctx->log->debug("NAU7802", "acq done: mass=%.2f g  mass_n=%.4f  sigma=%.3f g  n=%d",
                                     mass_g, mass_n, sigma_g, N_SAMPLES);
                }
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
    _acqState = AcqState::IDLE;  // always reset stale COMPLETE/ERROR before new attempt
    // B-12: same pre-flight as B-10 in tare()/calibrate() — WiFi-induced CS reset
    // causes CS=0 → CR bit never set → _isReady() always false → SAMPLING hangs silently.
    if (!_ensureConversionsRunning()) {
        _acqState = AcqState::ERROR;  // P1.3: trigger auto-retry in STEP_WEIGHT tick-loop
        _setError(10, "NAU7802: startAcquisition pre-flight failed — acq skipped");
        return;  // isAcquisitionError()=true → tick-loop retries up to kWeightRetryMax
    }
    _acqStartMs   = millis();
    _sampleIdx    = 0;
    _errorCount   = 0;
    _i2cFailCount = 0;  // reset fast-escalation counter for new acquisition
    _acqState     = AcqState::SETTLING;

    if (_ctx && _ctx->log) {
        _ctx->log->debug("NAU7802", "startAcquisition() -> SETTLING (%d ms)", SETTLE_MS);
    }
}

bool NAU7802Plugin::isAcquisitionComplete() const {
    return _acqState == AcqState::COMPLETE;
}

bool NAU7802Plugin::isAcquisitionError() const {
    return _acqState == AcqState::ERROR;
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

float NAU7802Plugin::getLastSigmaG() const {
    if (_acqState != AcqState::COMPLETE) return 0.0f;
    if (!_mutex) return 0.0f;
    float val = 0.0f;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        val = _cachedSigmaG;
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
    // ISensorPlugin::calibrate() override — tare only, NOT full 2-point calibration.
    // Returns false always: _calibrated stays false, _scaleFactor stays 1.0f.
    // Caller using ISensorPlugin* must use tare() + calibrate(float, uint16_t) explicitly. (A-03)
    if (_ctx && _ctx->log) {
        _ctx->log->warning("NAU7802",
            "calibrate() via ISensorPlugin performs tare only — use calibrate(float, uint16_t) for full calibration");
    }
    tare(32);
    return false;
}

// Pre-flight: verify chip is alive and conversions are running before any blocking op.
// ESP32 WiFi startup can (a) corrupt the I2C bus (PU_CTRL=0xFF) or (b) cause a brief
// voltage dip that resets NAU7802 registers (CS cleared, conversions stopped).
// Both make _blockingCaptureSamples() spin for the full deadline with 0 collected samples.
bool NAU7802Plugin::_ensureConversionsRunning() {
    uint8_t pu = _readReg(REG_PU_CTRL);
    if (pu == 0xFF) {
        if (_ctx && _ctx->log) {
            _ctx->log->warning("NAU7802",
                "pre-flight: PU_CTRL=0xFF — I2C bus hung; resetting (SDA=%d SCL=%d %lukHz)",
                _sda, _scl, (unsigned long)(_i2cHz / 1000));
        }
        _ctx->wire->end();
        delay(5);
        _ctx->wire->begin(_sda, _scl);
        _ctx->wire->setClock(_i2cHz);
        delay(5);
        pu = _readReg(REG_PU_CTRL);
        if (pu == 0xFF) {
            _setError(9, "NAU7802: I2C unresponsive after bus recovery");
            return false;
        }
        // B-12: After I2C hang, chip state is unknown (CS may read 1 but
        // conversions stalled). Always re-run startup to guarantee CR ticks.
        if (_ctx && _ctx->log) {
            _ctx->log->warning("NAU7802",
                "pre-flight: bus recovered (PU_CTRL=0x%02X) — re-running startup", pu);
        }
        if (!_startupSequence()) {
            _setError(9, "NAU7802: startup recovery failed after bus hang");
            return false;
        }
        // B-12: verify CR bit actually appears — confirms ADC is converting,
        // not just that registers read back correctly. Timeout 150 ms = 12 conversions at 80SPS.
        {
            uint32_t t0 = millis();
            uint8_t  cr_pu = 0;
            bool     cr_ok = false;
            while ((millis() - t0) < 150) {
                cr_pu = _readReg(REG_PU_CTRL);
                if ((cr_pu != 0xFF) && (cr_pu & PU_CTRL_CR)) { cr_ok = true; break; }
                delay(5);
            }
            if (!cr_ok) {
                if (_ctx && _ctx->log) {
                    _ctx->log->error("NAU7802",
                        "pre-flight: CR never set after recovery (PU_CTRL=0x%02X) — acq skipped",
                        cr_pu);
                }
                _setError(9, "NAU7802: CR never set after startup recovery");
                return false;  // caller stays IDLE → 6D fallback
            }
            if (_ctx && _ctx->log) {
                _ctx->log->info("NAU7802",
                    "pre-flight: CR verified OK (PU_CTRL=0x%02X, %lums)", cr_pu,
                    (unsigned long)(millis() - t0));
            }
        }
        return true;  // startup + CR verified — skip CS check below
    }
    if (!(pu & PU_CTRL_CS)) {
        if (_ctx && _ctx->log) {
            _ctx->log->warning("NAU7802",
                "pre-flight: PU_CTRL=0x%02X — CS=0 (chip reset); re-running startup", pu);
        }
        if (!_startupSequence()) {
            _setError(9, "NAU7802: startup recovery failed");
            return false;
        }
        delay(50);  // allow first conversions at 80 SPS (~12.5 ms)
    }
    // Final sanity: verify CR is set (or will be set within 150 ms).
    // Catches the case where CS=1 in PU_CTRL but ADC is internally stalled.
    {
        uint32_t t0 = millis();
        bool     cr_ok = false;
        uint8_t  cr_pu = 0;
        while ((millis() - t0) < 150) {
            cr_pu = _readReg(REG_PU_CTRL);
            if ((cr_pu != 0xFF) && (cr_pu & PU_CTRL_CR)) { cr_ok = true; break; }
            delay(5);
        }
        if (!cr_ok) {
            if (_ctx && _ctx->log) {
                _ctx->log->error("NAU7802",
                    "pre-flight: CR never set (PU_CTRL=0x%02X) — acq skipped", cr_pu);
            }
            _setError(9, "NAU7802: CR never set — ADC stalled");
            return false;
        }
    }
    return true;
}

bool NAU7802Plugin::tare(uint16_t samples) {
    if (!_initialized) return false;
    _acqState = AcqState::IDLE;  // Abort any in-progress acquisition before blocking I2C (A-07)

    if (!_ensureConversionsRunning()) return false;

    // Stay at 80 SPS for tare — empirically confirmed optimal for this noise environment.
    // Noise source is low-frequency mechanical vibration (0.3–3 Hz from fan/surface), NOT 50 Hz mains.
    // Evidence: sigma=14 at 80 SPS (0.06s window) vs sigma=1,460,033 at 10 SPS (3.2s window).
    // 10 SPS measurement window (3.2s) captures multiple full cycles of 0.3–3 Hz noise → catastrophic.
    // 80 SPS measurement window (0.4s) stays below the noise period → sigma<20 achievable.
    // 50 Hz mains not present: if it were, 10 SPS would give BETTER sigma via sinc³ null — opposite observed.

    float mean, sigma;
    bool ok = _blockingCaptureSamples(samples, &mean, &sigma);

    if (!ok) {
        uint8_t diag = _readReg(REG_PU_CTRL);
        _setError(9, "NAU7802: tare() capture failed — PU_CTRL=0x%02X CS=%d CR=%d",
                     diag,
                     (diag != 0xFF) ? (int)!!(diag & PU_CTRL_CS) : -1,
                     (diag != 0xFF) ? (int)!!(diag & PU_CTRL_CR) : -1);
        return false;
    }

    _zeroOffset = static_cast<int32_t>(mean);

    // Persist zero offset to NVS immediately — survives reboot between tare() and calibrate() (A-04)
    // Does NOT set cal_ok; loadCalibration() restores this even without full calibration.
    {
        Preferences prefs;
        if (prefs.begin("nau7802", false)) {
            prefs.putInt("zero", _zeroOffset);
            prefs.end();
        }
    }

    if (_ctx && _ctx->log) {
        // Interpret sigma: < 20 = good; 20-1000 = marginal/noise; > 1000 = floating/bad contact
        const char* quality;
        if      (sigma <    20.0f) quality = "GOOD ✓ (< 20)";
        else if (sigma <   100.0f) quality = "MARGINAL (20-100, check shielding)";
        else if (sigma < 10000.0f) quality = "POOR (100-10K, noise/cable issue)";
        else                       quality = "FAIL (>10K — A+/A- floating or bad contact)";
        _ctx->log->info("NAU7802", "Tare OK: zero_offset=%ld  sigma=%.1f  n=%d  quality=%s",
                        (long)_zeroOffset, sigma, samples, quality);
    }
    return true;
}

bool NAU7802Plugin::calibrate(float known_mass_g, uint16_t samples) {
    if (!_initialized) return false;
    _acqState = AcqState::IDLE;  // Abort any in-progress acquisition before blocking I2C (A-07)
    if (known_mass_g <= 0.0f || known_mass_g > 200.0f) {
        _setError(10, "NAU7802: invalid known_mass_g=%.2f (expected 0..200)", known_mass_g);
        return false;
    }

    if (!_ensureConversionsRunning()) return false;

    // Stay at 80 SPS for calibration — same reason as tare() (see tare() comment).

    float mean, sigma;
    bool ok = _blockingCaptureSamples(samples, &mean, &sigma);

    // Restore 80 SPS (already at 80 SPS — explicit for clarity)
    _writeReg(REG_CTRL2, CTRL2_VAL);

    if (!ok) {
        uint8_t diag = _readReg(REG_PU_CTRL);
        _setError(11, "NAU7802: calibrate() capture failed — PU_CTRL=0x%02X CS=%d CR=%d",
                      diag,
                      (diag != 0xFF) ? (int)!!(diag & PU_CTRL_CS) : -1,
                      (diag != 0xFF) ? (int)!!(diag & PU_CTRL_CR) : -1);
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
    prefs.putFloat("cal_mass_g", _calMassG);  // NVS key per arch §7.1
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

    // Always restore zero offset if available — tare() persists it independently of cal_ok (A-04)
    _zeroOffset = prefs.getInt("zero", 0);

    bool cal_ok = prefs.getBool("cal_ok", false);
    if (!cal_ok) {
        prefs.end();
        return false;  // scale not calibrated; zero_offset still restored above
    }

    _zeroOffset   = prefs.getInt("zero",       0);  // re-read after confirming cal_ok
    _scaleFactor  = prefs.getFloat("scale",    1.0f);
    _calTimestamp = prefs.getUInt("cal_ts",    0);
    _calMassG     = prefs.getFloat("cal_mass_g", 0.0f);  // NVS key per arch §7.1
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

    // 2. Read 5 consecutive ADC samples — log all values for diagnostics
    int32_t samples[5] = {};
    for (int i = 0; i < 5; i++) {
        uint32_t t = millis();
        while (!_isReady() && (millis() - t) < 100) delay(2);
        if (!_readAdc24(samples[i])) {
            _setError(15, "NAU7802 self-test: ADC read failed at sample %d", i);
            return false;
        }
        if (samples[i] == 0) {
            _setError(16, "NAU7802 self-test: ADC output is zero at sample %d", i);
            return false;
        }
    }

    if (_ctx && _ctx->log) {
        // Analyze range and drift direction to distinguish noise sources:
        //   range < 100, |drift| < 50   → STABLE  (good)
        //   range > 100, |drift| < range/2 → NOISY   (EMI or vibration)
        //   |drift| >= range/2            → DRIFTING (mechanical creep — load cell under preload)
        int32_t minVal = samples[0], maxVal = samples[0];
        for (int i = 1; i < 5; i++) {
            if (samples[i] < minVal) minVal = samples[i];
            if (samples[i] > maxVal) maxVal = samples[i];
        }
        int32_t range = maxVal - minVal;
        int32_t drift = samples[4] - samples[0];  // net displacement over 5 samples
        const char* stability;
        if      (range < 100)                      stability = "STABLE ✓";
        else if (labs(drift) >= range / 2)         stability = "DRIFTING (mechanical creep — check load cell mounting/preload)";
        else                                       stability = "NOISY (EMI/vibration — check shielding)";
        _ctx->log->info("NAU7802",
            "Self-test PASSED  raw[0..4]: %ld %ld %ld %ld %ld  range=%ld  %s",
            (long)samples[0], (long)samples[1], (long)samples[2],
            (long)samples[3], (long)samples[4], (long)range, stability);
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
