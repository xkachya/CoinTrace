// LDC1101Plugin.h — LDC1101 Inductive Sensor Plugin (SPI)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// LDC1101_ARCHITECTURE.md §8 (v1.2.0)
//
// Hardware: MIKROE-3240 breakout, ESP32-S3 VSPI
//   SCK = GPIO40, MISO = GPIO39, MOSI = GPIO14, CS = GPIO5 (configurable)
//
// Key facts (LDC1101 datasheet SNOSD01D):
//   - SPI MODE0, MSBFIRST, max 4 MHz
//   - Read REG_RP_DATA_LSB (0x21) FIRST — this latches MSB + L_DATA registers
//   - Configure registers only in Sleep mode (REG_START_CONFIG = 0x01)
//   - CHIP_ID (0x3F) must read 0xD4
//
// ADR-COIN-001: dual-threshold hysteresis for coin detect/release
// SYS-1 fix: read LSB first (MikroE SDK reads MSB first — BUG)
// M-1: staleFlag_ is a volatile bool — lock-free read from getHealthStatus()

#pragma once

#include "ISensorPlugin.h"
#include "IDiagnosticPlugin.h"
#include "PluginContext.h"
#include <Arduino.h>
#include <SPI.h>

class LDC1101Plugin : public ISensorPlugin, public IDiagnosticPlugin {
public:

    // ── Coin detection state (ADR-COIN-001) ──────────────────────────────────
    enum class CoinState : uint8_t {
        IDLE_NO_COIN,   // RP > release threshold — no coin on coil
        COIN_PRESENT,   // RP < detect threshold (debounced) — coin present
        COIN_REMOVED    // Transient: exactly 1 update() cycle, then → IDLE_NO_COIN (M-3)
    };

private:

    // ── Register map (datasheet SNOSD01D, Table 3) ───────────────────────────
    static const uint8_t REG_RP_SET        = 0x01; // RP dynamic range
    static const uint8_t REG_TC1           = 0x02; // Internal time constant 1
    static const uint8_t REG_TC2           = 0x03; // Internal time constant 2
    static const uint8_t REG_DIG_CONFIG    = 0x04; // RESP_TIME[2:0] + MIN_FREQ[7:4]
    static const uint8_t REG_ALT_CONFIG    = 0x05; // LOPTIMAL, SHUTDOWN_EN
    static const uint8_t REG_INTB_MODE     = 0x0A; // (unused — polling mode)
    static const uint8_t REG_START_CONFIG  = 0x0B; // FUNC_MODE
    static const uint8_t REG_D_CONFIG      = 0x0C; // DOK_REPORT
    static const uint8_t REG_STATUS        = 0x20; // DRDYB, NO_SENSOR_OSC, POR_READ
    static const uint8_t REG_RP_DATA_LSB   = 0x21; // ⚠ READ FIRST — latches MSB+L_DATA
    static const uint8_t REG_RP_DATA_MSB   = 0x22;
    static const uint8_t REG_L_DATA_LSB    = 0x23;
    static const uint8_t REG_L_DATA_MSB    = 0x24;
    static const uint8_t REG_CHIP_ID       = 0x3F; // Expected: 0xD4

    static const uint8_t FUNC_MODE_ACTIVE  = 0x00; // Continuous conversion
    static const uint8_t FUNC_MODE_SLEEP   = 0x01; // Low-power, config retained

    static const uint8_t STATUS_NO_OSC     = 0x80; // bit7: coil not oscillating
    static const uint8_t STATUS_DRDYB      = 0x40; // bit6: 0=ready, 1=in-progress (inverted)
    static const uint8_t STATUS_POR_READ   = 0x01; // bit0: POR occurred (cleared by reading)

    // ── Plugin state ─────────────────────────────────────────────────────────
    PluginContext* ctx_     = nullptr;
    bool           ready_   = false;
    bool           enabled_ = false;
    int            csPin_   = -1;

    // ── Configuration (loaded from ctx->config in initialize()) ──────────────
    uint8_t  respTimeBits_        = 0x07;       // RESP_TIME = 6144 cycles (max quality)
    uint8_t  rpSetValue_          = 0x26;       // MIKROE-3240 default (ADR-LDC-001)
    uint32_t clkinFreqHz_         = 16000000UL;
    float    coinDetectThreshold_ = 0.85f;      // DETECT:  RP < baseline × 0.85
    float    coinReleaseThreshold_= 0.92f;      // RELEASE: RP > baseline × 0.92 (hysteresis gap 7%)
    uint8_t  detectDebounceN_     = 5;          // 5 consecutive → COIN_PRESENT (~100 ms @ 50 Hz)
    uint8_t  releaseDebounceM_    = 3;          // 3 consecutive → COIN_REMOVED  (~60 ms @ 50 Hz)

    // ── Measurement cache (protected by dataMutex_) ──────────────────────────
    SemaphoreHandle_t dataMutex_ = nullptr;

    struct MeasurementCache {
        uint16_t rpRaw     = 0;
        uint16_t lRaw      = 0;
        uint32_t timestamp = 0;
        bool     valid     = false;
    } cache_;

    // ── Diagnostics / runtime statistics ─────────────────────────────────────
    struct {
        uint32_t     totalReads  = 0;
        uint32_t     failedReads = 0;
        uint32_t     staleCount  = 0;  // consecutive DRDYB=1 calls (LA-7)
        uint32_t     lastSuccess = 0;
        HealthStatus status      = HealthStatus::UNKNOWN;
    } ds_;

    // Dynamic error message buffer (prevents dangling pointer from snprintf locals)
    char      lastErrorMsg_[64] = {};
    ErrorCode lastError_        = {0, "No error"};

    // ── Calibration ───────────────────────────────────────────────────────────
    float    calibrationRpBaseline_ = 0.0f;
    uint32_t lastCalibrationTime_   = 0;  // Reserved: NVS age-check (M-2)

    // ── Stale detection (M-1: volatile bool = lock-free on Xtensa LX7) ───────
    volatile bool staleFlag_ = false;

    // ── Coin state machine (ADR-COIN-001) ────────────────────────────────────
    struct {
        CoinState state        = CoinState::IDLE_NO_COIN;
        uint8_t   detectCount  = 0;
        uint8_t   releaseCount = 0;
    } coin_;

public:

    // ── IPlugin metadata ──────────────────────────────────────────────────────
    const char* getName()    const override { return "LDC1101"; }
    const char* getVersion() const override { return "1.0.0"; }
    const char* getAuthor()  const override { return "CoinTrace Team"; }

    // ── ISensorPlugin type / metadata ─────────────────────────────────────────
    SensorType     getType()     const override { return SensorType::INDUCTIVE; }
    SensorMetadata getMetadata() const override {
        return {
            .typeName   = "Inductive Sensor (SPI)",
            .unit       = "RP_code",
            // value1 = raw RP code  (0–65535): reflects metal conductivity
            // value2 = raw L_DATA code: reflects magnetic permeability
            .minValue   = 0.0f,
            .maxValue   = 65535.0f,
            .resolution = 1.0f,
            .sampleRate = 50
        };
    }

    // ── Coin detection helpers (public read-only) ─────────────────────────────
    CoinState getCoinState()  const { return coin_.state; }
    bool      isCoinPresent() const { return coin_.state == CoinState::COIN_PRESENT; }
    float     getBaseline()   const { return calibrationRpBaseline_; }

    // ── IPlugin status ────────────────────────────────────────────────────────
    bool isEnabled() const override { return enabled_; }
    bool isReady()   const override { return ready_; }

    // ── IPlugin lifecycle ─────────────────────────────────────────────────────

    bool canInitialize() override {
        // cs pin and SPI validation deferred to initialize() when ctx is available
        return true;
    }

    bool initialize(PluginContext* ctx) override {
        ctx_ = ctx;

        // Load configuration — all keys have safe defaults
        csPin_                 = ctx_->config->getInt   ("ldc1101.spi_cs_pin",            5);
        respTimeBits_          = ctx_->config->getUInt8 ("ldc1101.resp_time_bits",      0x07);
        rpSetValue_            = ctx_->config->getUInt8 ("ldc1101.rp_set",              0x26);
        clkinFreqHz_           = ctx_->config->getUInt32("ldc1101.clkin_freq_hz",  16000000UL);
        coinDetectThreshold_   = ctx_->config->getFloat ("ldc1101.coin_detect_threshold",  0.85f);
        coinReleaseThreshold_  = ctx_->config->getFloat ("ldc1101.coin_release_threshold", 0.92f);
        detectDebounceN_       = ctx_->config->getUInt8 ("ldc1101.detect_debounce_n",      5);
        releaseDebounceM_      = ctx_->config->getUInt8 ("ldc1101.release_debounce_m",     3);

        if (csPin_ < 0 || !ctx_->spi) {
            return fail_(1, "CS pin or SPI bus not available");
        }

        pinMode(csPin_, OUTPUT);
        digitalWrite(csPin_, HIGH);  // CS inactive
        delay(5);

        // Wait for Power-On Reset to complete
        uint32_t t = millis();
        while (spiRead_(REG_STATUS) & STATUS_POR_READ) {
            if (millis() - t > 100) {
                ds_.status = HealthStatus::TIMEOUT;
                return fail_(3, "POR timeout: chip not ready after 100ms");
            }
            delay(1);
        }

        // Verify CHIP_ID (must be read after POR clears)
        uint8_t chipId = spiRead_(REG_CHIP_ID);
        if (chipId != 0xD4) {
            ds_.status = HealthStatus::SENSOR_FAULT;
            snprintf(lastErrorMsg_, sizeof(lastErrorMsg_),
                     "CHIP_ID mismatch: expected 0xD4, got 0x%02X", chipId);
            lastError_ = {2, lastErrorMsg_};
            ctx_->log->error(getName(), lastError_.message);
            return false;
        }

        dataMutex_ = xSemaphoreCreateMutex();
        if (!dataMutex_) {
            return fail_(4, "Failed to create data mutex");
        }

        if (!configure_()) {
            ds_.status = HealthStatus::INITIALIZATION_FAILED;
            return fail_(5, "Sensor configuration failed");
        }

        // Switch to Active mode — starts continuous conversions
        spiWrite_(REG_START_CONFIG, FUNC_MODE_ACTIVE);

        ready_     = true;
        enabled_   = true;
        ds_.status = HealthStatus::OK;
        lastError_ = {0, "No error"};
        ctx_->log->info(getName(), "Ready. CS=%d, RESP_TIME=0x%02X, RP_SET=0x%02X",
                        csPin_, respTimeBits_, rpSetValue_);
        return true;
    }

    // update() CONTRACT: returns in ≤ 10 ms (PLUGIN_CONTRACT.md §1.2)
    // Actual cost: ~2 SPI transactions = < 0.1 ms at 4 MHz
    void update() override {
        if (!ready_) return;

        // Stale detection (M-1): set atomically here — read in getHealthStatus() without mutex
        if (cache_.valid && (millis() - cache_.timestamp > 5000)) {
            staleFlag_ = true;
        }

        uint8_t status = spiRead_(REG_STATUS);

        if (status & STATUS_NO_OSC) {
            ++ds_.failedReads;
            ds_.status = HealthStatus::NOT_FOUND;
            lastError_ = {6, "Coil not oscillating — check wiring or RP_SET"};
            ctx_->log->warning(getName(), lastError_.message);
            return;
        }

        if (status & STATUS_DRDYB) {
            // Conversion in progress — normal for Strategy A @ 50 Hz with fast coils
            if (++ds_.staleCount > 10) {
                ctx_->log->warning(getName(),
                    "DRDYB=1 for %lu consecutive update() calls — conversion frozen?",
                    (unsigned long)ds_.staleCount);
            }
            return;
        }

        uint16_t rpRaw, lRaw;
        if (!readBurst_(rpRaw, lRaw)) {
            ++ds_.failedReads;
            ds_.status = HealthStatus::COMMUNICATION_ERROR;
            lastError_ = {7, "SPI burst read failed"};
            return;
        }

        if (rpRaw == 0 || rpRaw == 0xFFFF) {
            ++ds_.failedReads;
            ds_.status = HealthStatus::SENSOR_FAULT;
            lastError_ = {8, "RP_DATA out of valid range (0x0000 or 0xFFFF)"};
            return;
        }

        // Update cache — use timeout, never block indefinitely (PLUGIN_CONTRACT.md §1.3)
        if (xSemaphoreTake(dataMutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
            cache_.rpRaw     = rpRaw;
            cache_.lRaw      = lRaw;
            cache_.timestamp = millis();
            cache_.valid     = true;
            xSemaphoreGive(dataMutex_);
        } else {
            ++ds_.failedReads;  // Mutex timeout — skip this cycle
            return;
        }

        staleFlag_      = false;  // M-1: fresh data received — clear stale flag
        ds_.staleCount  = 0;
        ++ds_.totalReads;
        ds_.lastSuccess = millis();
        ds_.status      = HealthStatus::OK;

        // Coin detection: dual-threshold hysteresis (ADR-COIN-001)
        // Guard: skip until calibrate() has established a valid baseline
        if (calibrationRpBaseline_ > 100.0f) {
            updateCoinState_(rpRaw);
        }
    }

    // read() CONTRACT: returns in ≤ 5 ms (PLUGIN_CONTRACT.md §1.2)
    // Actual cost: mutex acquire + struct copy = < 0.1 ms
    SensorData read() override {
        if (!ready_ || !dataMutex_) {
            return {0.0f, 0.0f, 0.0f, millis(), false};
        }
        if (xSemaphoreTake(dataMutex_, pdMS_TO_TICKS(3)) != pdTRUE) {
            return {0.0f, 0.0f, 0.0f, millis(), false};
        }
        SensorData result = {
            .value1     = static_cast<float>(cache_.rpRaw),
            .value2     = static_cast<float>(cache_.lRaw),
            .confidence = cache_.valid ? 0.95f : 0.0f,
            .timestamp  = cache_.timestamp,
            .valid      = cache_.valid
        };
        xSemaphoreGive(dataMutex_);
        return result;
    }

    // calibrate() NOTE: uses delay() — call outside update() loop
    bool calibrate() override {
        if (!ready_) return false;
        ctx_->log->info(getName(), "Calibration start — remove coin from sensor");
        delay(2000);

        float    sum = 0.0f;
        uint32_t ok  = 0;
        for (int i = 0; i < 20; i++) {
            delay(convTimeMs_() + 5);
            uint16_t rp, l;
            if (readBurst_(rp, l) && rp > 0 && rp < 65535) {
                sum += rp;
                ++ok;
            }
        }

        if (ok < 10) {
            ctx_->log->error(getName(), "Calibration failed: only %u valid readings", ok);
            return false;
        }

        calibrationRpBaseline_ = sum / ok;
        lastCalibrationTime_   = millis();
        ctx_->log->info(getName(), "Calibration OK. Baseline RP=%.0f (%u samples)",
                        calibrationRpBaseline_, ok);
        return true;
    }

    void shutdown() override {
        if (ctx_ && ctx_->spi && csPin_ >= 0) {
            spiWrite_(REG_START_CONFIG, FUNC_MODE_SLEEP);  // Sleep retains config
        }
        if (dataMutex_) {
            vSemaphoreDelete(dataMutex_);
            dataMutex_ = nullptr;
        }
        ready_   = false;
        enabled_ = false;
        if (ctx_) ctx_->log->info(getName(), "Shutdown complete");
    }

    // ── IDiagnosticPlugin ─────────────────────────────────────────────────────

    // getHealthStatus() is lock-free: staleFlag_ is volatile bool (1 byte, atomic on LX7)
    HealthStatus getHealthStatus() const override {
        if (!enabled_) return HealthStatus::PLUGIN_DISABLED;
        if (!ready_)   return HealthStatus::INITIALIZATION_FAILED;
        if (staleFlag_) return HealthStatus::TIMEOUT;
        if (ds_.totalReads > 10) {
            float failRate = static_cast<float>(ds_.failedReads) / ds_.totalReads;
            if (failRate > 0.5f) return HealthStatus::DEGRADED;
            if (failRate > 0.1f) return HealthStatus::OK_WITH_WARNINGS;
        }
        return ds_.status;
    }

    ErrorCode getLastError() const override { return lastError_; }

    DiagnosticResult runDiagnostics() override {
        DiagnosticResult r = {};
        r.timestamp = millis();
        if (!checkHardwarePresence()) {
            r.status = HealthStatus::NOT_FOUND;
            r.error  = {1, "CHIP_ID mismatch — SPI or CS wiring issue"};
            return r;
        }
        if (!checkCalibration()) {
            r.status = HealthStatus::CALIBRATION_NEEDED;
            r.error  = {5, "Calibration baseline not set or out of range"};
            return r;
        }
        r.status = HealthStatus::OK;
        r.error  = {0, "All checks passed"};
        fillStats_(r);
        return r;
    }

    // runSelfTest() NOTE: uses delay() — call outside update() loop
    bool runSelfTest() override {
        if (!ctx_) return false;
        ctx_->log->info(getName(), "Self-test start");

        uint8_t chipId = spiRead_(REG_CHIP_ID);
        if (chipId != 0xD4) {
            snprintf(lastErrorMsg_, sizeof(lastErrorMsg_),
                     "CHIP_ID: expected 0xD4, got 0x%02X", chipId);
            ctx_->log->error(getName(), lastErrorMsg_);
            return false;
        }
        ctx_->log->info(getName(), "  CHIP_ID OK (0xD4)");

        uint8_t st = spiRead_(REG_STATUS);
        if (st & STATUS_NO_OSC) {
            ctx_->log->error(getName(), "  FAIL: NO_SENSOR_OSC — coil not oscillating");
            return false;
        }
        if (st & STATUS_POR_READ) {
            ctx_->log->error(getName(), "  FAIL: POR_READ — chip still in reset");
            return false;
        }
        ctx_->log->info(getName(), "  STATUS OK");

        // Stability test: 5 consecutive reads
        float readings[5] = {};
        for (int i = 0; i < 5; i++) {
            delay(convTimeMs_() + 2);
            uint16_t rp, l;
            if (!readBurst_(rp, l)) {
                ctx_->log->error(getName(), "  FAIL: SPI read error on sample %d", i);
                return false;
            }
            readings[i] = static_cast<float>(rp);
        }
        float mean = 0.0f;
        for (float r : readings) mean += r;
        mean /= 5.0f;
        float maxDev = 0.0f;
        for (float r : readings) { float d = fabsf(r - mean); if (d > maxDev) maxDev = d; }
        if (mean > 0.0f && maxDev > mean * 0.1f) {
            ctx_->log->warning(getName(), "  Stability WARNING: maxDev=%.1f (%.1f%% of mean)",
                               maxDev, 100.0f * maxDev / mean);
        } else {
            ctx_->log->info(getName(), "  Stability OK (maxDev=%.1f)", maxDev);
        }
        ctx_->log->info(getName(), "Self-test PASSED");
        return true;
    }

    DiagnosticResult getStatistics() const override {
        DiagnosticResult r = {};
        r.status    = ds_.status;
        r.error     = lastError_;
        r.timestamp = millis();
        fillStats_(r);
        return r;
    }

    /** One SPI read — check if CHIP_ID returns 0xD4. Strategy A: no spiMutex. */
    bool checkHardwarePresence() override {
        return spiRead_(REG_CHIP_ID) == 0xD4;
    }

    /** Three consecutive CHIP_ID reads — all must return 0xD4. */
    bool checkCommunication() override {
        for (int i = 0; i < 3; i++) {
            if (spiRead_(REG_CHIP_ID) != 0xD4) return false;
        }
        return true;
    }

    /** True if calibration baseline is set and within plausible range (100–60000). */
    bool checkCalibration() override {
        return calibrationRpBaseline_ > 100.0f && calibrationRpBaseline_ < 60000.0f;
    }

private:

    // ── Coin state machine (ADR-COIN-001) ─────────────────────────────────────
    // Dual-threshold hysteresis prevents boundary oscillation when RP ≈ threshold.
    // Called from update() after every successful measurement.
    void updateCoinState_(uint16_t rpRaw) {
        const float rp      = static_cast<float>(rpRaw);
        const float detect  = calibrationRpBaseline_ * coinDetectThreshold_;
        const float release = calibrationRpBaseline_ * coinReleaseThreshold_;

        switch (coin_.state) {
            case CoinState::IDLE_NO_COIN:
                if (rp < detect) {
                    if (++coin_.detectCount >= detectDebounceN_) {
                        coin_.detectCount  = 0;
                        coin_.releaseCount = 0;
                        coin_.state        = CoinState::COIN_PRESENT;
                        ctx_->log->info(getName(),
                            "Coin PRESENT (RP=%.0f, baseline=%.0f, ratio=%.2f)",
                            rp, calibrationRpBaseline_, rp / calibrationRpBaseline_);
                    }
                } else {
                    coin_.detectCount = 0;  // non-consecutive — reset
                }
                break;

            case CoinState::COIN_PRESENT:
                if (rp > release) {
                    if (++coin_.releaseCount >= releaseDebounceM_) {
                        coin_.releaseCount = 0;
                        coin_.detectCount  = 0;
                        coin_.state        = CoinState::COIN_REMOVED;
                        ctx_->log->info(getName(),
                            "Coin REMOVED (RP=%.0f, baseline=%.0f, ratio=%.2f)",
                            rp, calibrationRpBaseline_, rp / calibrationRpBaseline_);
                    }
                } else {
                    coin_.releaseCount = 0;
                }
                break;

            case CoinState::COIN_REMOVED:
                // Transient state: exactly 1 update() cycle — then back to IDLE (M-3)
                coin_.state = CoinState::IDLE_NO_COIN;
                break;
        }
    }

    // ── Sensor configuration (must be called only in Sleep mode) ─────────────
    bool configure_() {
        spiWrite_(REG_START_CONFIG, FUNC_MODE_SLEEP);
        delay(2);

        // RP_SET: ADR-LDC-001 — default 0x26 (RP_MAX=24kΩ / RP_MIN=1.5kΩ, MIKROE-3240)
        spiWrite_(REG_RP_SET, rpSetValue_);

        // TC1/TC2: MikroE SDK values for MIKROE-3240 PCB compensation
        //   TC1=0x1F → C1=0.75pF, R1=21.1kΩ (τ₁=15.8 ns)
        //   TC2=0x3F → C2=3pF,   R2=30.5kΩ (τ₂=91.5 ns)
        spiWrite_(REG_TC1, 0x1F);
        spiWrite_(REG_TC2, 0x3F);

        // Readback verification (LA-5: consistent policy for TC1, TC2, RP_SET, DIG_CONFIG)
        if (spiRead_(REG_TC1) != 0x1F) {
            ctx_->log->error(getName(), "TC1 verify failed");
            return false;
        }
        if (spiRead_(REG_TC2) != 0x3F) {
            ctx_->log->error(getName(), "TC2 verify failed");
            return false;
        }
        uint8_t rpRead = spiRead_(REG_RP_SET);
        if (rpRead != rpSetValue_) {
            snprintf(lastErrorMsg_, sizeof(lastErrorMsg_),
                     "RP_SET verify failed: wrote 0x%02X, read 0x%02X",
                     rpSetValue_, rpRead);
            ctx_->log->error(getName(), lastErrorMsg_);
            return false;
        }

        // DIG_CONFIG: MIN_FREQ=0xD0 (118 kHz threshold) | RESP_TIME bits[2:0]
        // 0x00 (500 kHz threshold) causes false halt for fSENSOR < 500 kHz — dangerous
        uint8_t digCfg = 0xD0 | (respTimeBits_ & 0x07);
        spiWrite_(REG_DIG_CONFIG, digCfg);

        // Fix LA-1: compare full byte, NOT only 3-bit mask (digCfg ∈ [0xD0,0xD7])
        uint8_t digRead = spiRead_(REG_DIG_CONFIG);
        if (digRead != digCfg) {
            snprintf(lastErrorMsg_, sizeof(lastErrorMsg_),
                     "DIG_CONFIG verify: wrote 0x%02X, read 0x%02X",
                     digCfg, digRead);
            ctx_->log->error(getName(), lastErrorMsg_);
            return false;
        }

        spiWrite_(REG_D_CONFIG,   0x00);  // DOK_REPORT = 0: require amplitude regulation
        spiWrite_(REG_ALT_CONFIG, 0x00);  // LOPTIMAL = 0: RP + L both active

        ctx_->log->info(getName(), "configure_: DIG_CONFIG=0x%02X, RP_SET=0x%02X",
                        digCfg, rpSetValue_);
        return true;
        // Caller (initialize) switches to FUNC_MODE_ACTIVE after this returns true
    }

    // ── SPI helpers ───────────────────────────────────────────────────────────

    // Burst read RP_DATA + L_DATA in one transaction (4 bytes, auto-increment).
    // SYS-1 fix: read LSB (0x21) FIRST — this latches MSB (0x22) and L_DATA (0x23-0x24).
    bool readBurst_(uint16_t& rpRaw, uint16_t& lRaw) {
        if (!ctx_ || !ctx_->spi || csPin_ < 0) return false;
        ctx_->spi->beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
        digitalWrite(csPin_, LOW);
        ctx_->spi->transfer(REG_RP_DATA_LSB | 0x80);   // Read bit | 0x21 — FIRST!
        uint8_t rpLsb = ctx_->spi->transfer(0x00);      // 0x21: RP_DATA_LSB
        uint8_t rpMsb = ctx_->spi->transfer(0x00);      // 0x22: RP_DATA_MSB
        uint8_t lLsb  = ctx_->spi->transfer(0x00);      // 0x23: L_DATA_LSB
        uint8_t lMsb  = ctx_->spi->transfer(0x00);      // 0x24: L_DATA_MSB
        digitalWrite(csPin_, HIGH);
        ctx_->spi->endTransaction();
        rpRaw = (static_cast<uint16_t>(rpMsb) << 8) | rpLsb;
        lRaw  = (static_cast<uint16_t>(lMsb)  << 8) | lLsb;
        return true;
    }

    uint8_t spiRead_(uint8_t reg) {
        if (!ctx_ || !ctx_->spi || csPin_ < 0) return 0xFF;
        ctx_->spi->beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
        digitalWrite(csPin_, LOW);
        ctx_->spi->transfer(reg | 0x80);          // Read bit (bit7 = 1)
        uint8_t val = ctx_->spi->transfer(0x00);
        digitalWrite(csPin_, HIGH);
        ctx_->spi->endTransaction();
        return val;
    }

    void spiWrite_(uint8_t reg, uint8_t value) {
        if (!ctx_ || !ctx_->spi || csPin_ < 0) return;
        ctx_->spi->beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
        digitalWrite(csPin_, LOW);
        ctx_->spi->transfer(reg & 0x7F);          // Write bit (bit7 = 0)
        ctx_->spi->transfer(value);
        digitalWrite(csPin_, HIGH);
        ctx_->spi->endTransaction();
    }

    // Conversion time in ms — ceiling(cycles / 500kHz) + 2ms margin (LA-9, L-1)
    // Reserved indices 0,1 fall back to 192-cycle value (safe minimum).
    uint32_t convTimeMs_() const {
        static const uint32_t cycles[] = {192, 192, 192, 384, 768, 1536, 3072, 6144};
        return (cycles[respTimeBits_ & 0x07] + 499) / 500 + 2;
    }

    // Sets static error string and returns false (for simple failure paths)
    bool fail_(uint8_t code, const char* msg) {
        lastError_ = {code, msg};
        if (ctx_) ctx_->log->error(getName(), msg);
        return false;
    }

    void fillStats_(DiagnosticResult& r) const {
        r.stats.totalReads  = ds_.totalReads;
        r.stats.failedReads = ds_.failedReads;
        r.stats.successRate = ds_.totalReads > 0
            ? static_cast<uint16_t>(100u - ds_.failedReads * 100u / ds_.totalReads)
            : 100u;
        r.stats.lastSuccess = ds_.lastSuccess;
    }
};
