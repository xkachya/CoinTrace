// LDC1101Plugin.h — LDC1101 Inductive Sensor Plugin (SPI)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// LDC1101_ARCHITECTURE.md §8 (v1.5.0 — D-2 LHR continuous ADR-LHR-001; D-2b StabilityTracker ADR-STAB-001)
//
// Hardware: MIKROE-3240 breakout, ESP32-S3 VSPI
//   SCK = GPIO40, MISO = GPIO39, MOSI = GPIO14, CS = GPIO5 (configurable)
//
// Key facts (LDC1101 datasheet SNOSD01D):
//   - SPI MODE0, MSBFIRST, max 4 MHz
//   - Read REG_RP_DATA_LSB (0x21) FIRST — this latches MSB + L_DATA shadow registers
//   - Auto-increment supported: 0x21→0x22→0x23→0x24 in one CS frame is valid (§3 SPI)
//   - Configure registers only in Sleep mode (REG_START_CONFIG = 0x01)
//   - CHIP_ID (0x3F) must read 0xD4
//
// ADR-COIN-001: dual-threshold hysteresis for coin detect/release
// ADR-CLKIN-002: L_DATA requires CLKIN on mikroBUS Pin16 — without it L=0 or garbage.
//   RP_DATA is independent of CLKIN and works correctly without it (amplitude-based).
//   Current wiring: CLKIN connected (G4 → mikroBUS Pin16, hw-verified S-3). L_DATA valid.
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

    // ── LHR register map (ADR-LHR-001, datasheet §8.6.25–§8.6.31) ──────────────
    static const uint8_t REG_LHR_RCOUNT_LSB  = 0x30; // Reference count LSB
    static const uint8_t REG_LHR_RCOUNT_MSB  = 0x31; // Reference count MSB
    static const uint8_t REG_LHR_OFFSET_LSB  = 0x32; // Offset LSB (0 = max range)
    static const uint8_t REG_LHR_OFFSET_MSB  = 0x33; // Offset MSB
    static const uint8_t REG_LHR_CONFIG      = 0x34; // SENSOR_DIV[1:0] (0x00 = div1, fSENSOR<fCLKIN/4)
    static const uint8_t REG_LHR_DATA_LSB    = 0x38; // ⚠ READ FIRST — latches MID + MSB
    static const uint8_t REG_LHR_DATA_MID    = 0x39;
    static const uint8_t REG_LHR_DATA_MSB    = 0x3A;
    static const uint8_t REG_LHR_STATUS      = 0x3B; // bit0=DRDYB(0=ready,inverted); bits1-4=ERR
    static const uint8_t LHR_STATUS_DRDYB    = 0x01; // bit0: 0=data ready (inverted logic)
    static const uint8_t LHR_STATUS_ERR_MASK = 0x1E; // bits4=ERR_ZC,3=ERR_OR,2=ERR_UR,1=ERR_OF

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
    // MIN_FREQ watchdog nibble — DIG_CONFIG[7:4]
    // Formula (datasheet §8.6.5): fSENSOR_min = 8 MHz / (16 - nibble)
    // If fSENSOR drops below fSENSOR_min, watchdog restarts oscillator → DRDYB=1 permanently.
    //
    // Nibble table for MIKROE-3240 (fSENSOR_baseline = 909 kHz):
    //   0x0 → fSENSOR_min = 500 kHz  (too low — unlikely to catch real halts)
    //   0x6 → fSENSOR_min = 800 kHz  (109 kHz below baseline — conservative, hw-verified 2026-03-24 ✓)
    //   0x7 → fSENSOR_min = 889 kHz  (20 kHz below baseline — optimal, hw-verified 2026-03-24 ✓)
    //   0xD → fSENSOR_min = 2.67 MHz (MikroE SDK legacy default — intended for small coils)
    //   0xF → fSENSOR_min = 8.0 MHz  (DANGEROUS: triggers NO_OSC at 909 kHz)
    //
    // Config key: ldc1101.min_freq_nibble (default matches this field)
    uint8_t  minFreqNibble_       = 0x06;       // 800 kHz threshold — 109 kHz margin, hw-verified
    uint8_t  rpSetValue_          = 0x26;       // MIKROE-3240 default (ADR-LDC-001)
    uint32_t clkinFreqHz_         = 16000000UL;
    int      clkinGpio_           = -1;         // -1 = CLKIN not connected; ≥ 0 = LEDC output (ADR-CLKIN-002)
    float    coinDetectThreshold_ = 0.90f;      // DETECT:  RP < baseline × 0.90
    float    coinReleaseThreshold_= 0.96f;      // RELEASE: RP > baseline × 0.96 (hysteresis gap 6%)
    uint8_t  detectDebounceN_     = 5;          // 5 consecutive → COIN_PRESENT (~100 ms @ 50 Hz)
    uint8_t  releaseDebounceM_    = 3;          // 3 consecutive → COIN_REMOVED  (~60 ms @ 50 Hz)
    // LHR continuous (D-2, ADR-LHR-001)
    bool     lhrContinuous_       = false;      // ldc1101.lhr_continuous: read LHR in every update()
    uint32_t lhrRcount_           = 65535UL;    // ldc1101.lhr_rcount: 0xFFFF = max 24-bit res (~65ms)
    // StabilityTracker threshold (D-2b, ADR-STAB-001)
    float    stabThreshRp_        = 50.0f;      // ldc1101.stab_sigma_thresh_rp: σ(RP) stable threshold

    // ── Measurement cache (protected by dataMutex_) ──────────────────────────
    SemaphoreHandle_t dataMutex_ = nullptr;

    struct MeasurementCache {
        uint16_t rpRaw     = 0;
        uint16_t lRaw      = 0;
        uint32_t lhrRaw    = 0;     // 24-bit LHR_DATA (ADR-LHR-001); valid when lhrValid=true
        uint32_t timestamp = 0;
        bool     valid     = false;
        bool     lhrValid  = false; // true after first successful LHR read at lhr_continuous=true
    } cache_;

    // ── Diagnostics / runtime statistics ─────────────────────────────────────
    struct {
        uint32_t     totalReads     = 0;
        uint32_t     failedReads    = 0;
        uint32_t     staleCount     = 0;     // consecutive DRDYB=1 calls (LA-7)
        uint32_t     lastSuccess    = 0;
        HealthStatus status         = HealthStatus::UNKNOWN;
        bool         lhrErrorLogged  = false; // prevent LHR error log spam (D-2)
        bool         lhrFirstValid    = false; // one-shot INFO when LHR data first arrives
    } ds_;

    // Dynamic error message buffer (prevents dangling pointer from snprintf locals)
    char      lastErrorMsg_[64] = {};
    ErrorCode lastError_        = {0, "No error"};

    // ── Calibration ───────────────────────────────────────────────────────────
    float    calibrationRpBaseline_ = 0.0f;
    float    calibrationLBaseline_  = 0.0f;  // Average L_DATA from calibrate()
    float    calibrationFSensor_    = 0.0f;  // Hz: (fCLKIN × RESP_CYCLES) / (3 × L_avg)
    uint32_t lastCalibrationTime_   = 0;  // Reserved: NVS age-check (M-2)

    // ── Stale detection (M-1: volatile bool = lock-free on Xtensa LX7) ───────
    volatile bool staleFlag_ = false;

    // ── Coin state machine (ADR-COIN-001) ────────────────────────────────────
    struct {
        CoinState state        = CoinState::IDLE_NO_COIN;
        uint8_t   detectCount  = 0;
        uint8_t   releaseCount = 0;
    } coin_;

    // ── Signal Stability Tracking (D-2b, ADR-STAB-001) ───────────────────────
    // Circular buffer N=8 — rolling σ(RP). ~36 bytes BSS, zero public API impact.
    struct StabilityTracker {
        uint16_t buf[8] = {};   // circular buffer of RP samples
        uint8_t  head   = 0;    // next write position
        uint8_t  count  = 0;    // samples accumulated (0–8)
        float    sigma  = 0.0f; // rolling σ(RP) over last N samples
        float    mean   = 0.0f; // rolling mean — copied to stableCache_.rpRaw when stable
        bool     stable = false;// true if count==8 AND sigma < stabThreshRp_

        void reset() { *this = StabilityTracker{}; }

        void feed(uint16_t rpRaw, float thresh) {
            buf[head] = rpRaw;
            head = (head + 1) & 7;      // power-of-2 wrap (N=8 hardcoded)
            if (count < 8) ++count;
            if (count < 8) { stable = false; return; }
            float sum = 0.0f, sumSq = 0.0f;
            for (uint8_t i = 0; i < 8; i++) {
                sum   += buf[i];
                sumSq += static_cast<float>(buf[i]) * buf[i];
            }
            mean   = sum / 8.0f;
            sigma  = sqrtf(sumSq / 8.0f - mean * mean);
            stable = (sigma < thresh);
        }
    } stab_;

    // Frozen stable snapshot — written only from update() (same mutex as live cache).
    // Race-free: getStableRp() returns this snapshot, not a live reading.
    struct StableCache {
        uint16_t rpRaw = 0;     // mean of last N stable samples (truncated to uint16)
        uint16_t lRaw  = 0;     // lRaw at the moment of stable snapshot
        bool     valid = false; // true once stab_.stable triggered first time
    } stableCache_;

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
    float     getLBaseline()  const { return calibrationLBaseline_; }
    float     getFSensor()    const { return calibrationFSensor_; }   // Hz

    // ── Quick Screen API (Wave 8 C-7c) ────────────────────────────────────────
    // QUICK_SCREEN_SPEC.md §8

    // Returns the most recent live RP_DATA reading from the update() cache.
    // Thread-safe: acquires dataMutex_ with 50 ms timeout.
    // Returns 0.0f if not ready, mutex timeout, or cache invalid.
    float getLiveRp() const {
        if (!dataMutex_) return 0.0f;
        if (xSemaphoreTake(dataMutex_, pdMS_TO_TICKS(50)) != pdTRUE) return 0.0f;
        const float rp = cache_.valid ? static_cast<float>(cache_.rpRaw) : 0.0f;
        xSemaphoreGive(dataMutex_);
        return rp;
    }

    // Returns the most recent live L_DATA reading from the update() cache.
    // Thread-safe: acquires dataMutex_ with 50 ms timeout.
    // ⚠️ ADR-CLKIN-002: meaningful only if isLDataValid() == true (CLKIN wired).
    // Returns 0.0f if not ready, mutex timeout, or cache invalid.
    float getLiveL() const {
        if (!dataMutex_) return 0.0f;
        if (xSemaphoreTake(dataMutex_, pdMS_TO_TICKS(50)) != pdTRUE) return 0.0f;
        const float l = cache_.valid ? static_cast<float>(cache_.lRaw) : 0.0f;
        xSemaphoreGive(dataMutex_);
        return l;
    }

    // Returns true if CLKIN is configured (clkin_gpio >= 0 in ldc1101.json).
    // Without CLKIN: getLiveL() and getLBaseline() return 0 or garbage.
    // ADR-CLKIN-002: always guard L_DATA usage with this check.
    bool isLDataValid() const { return clkinGpio_ >= 0; }

    // Returns the most recent 24-bit LHR_DATA as float (ADR-LHR-001, D-2).
    // Convert to fSENSOR [Hz] via: fSENSOR = getLiveLHR() * fCLKIN / 16777216.
    // Returns 0.0f if lhr_continuous=false, CLKIN not wired, or no read yet.
    // Thread-safe: acquires dataMutex_ with 50 ms timeout.
    float getLiveLHR() const {
        if (!dataMutex_) return 0.0f;
        if (xSemaphoreTake(dataMutex_, pdMS_TO_TICKS(50)) != pdTRUE) return 0.0f;
        const float lhr = cache_.lhrValid ? static_cast<float>(cache_.lhrRaw) : 0.0f;
        xSemaphoreGive(dataMutex_);
        return lhr;
    }

    // Re-measure the no-coin baseline using 10 fresh samples (avg of ≥5 valid).
    // Call this when the user presses 'R' key in IDLE state (Quick Screen recal).
    // Guard: returns false immediately if coin is present — caller must ensure no coin.
    // Blocks ~250 ms (10 × convTimeMs + 5 ms each). Call from MainLoop, not update().
    // Does NOT recompute calibrationFSensor_ (retained from last calibrate() call).
    bool recalibrate() {
        if (!ready_) return false;
        if (isCoinPresent()) {
            ctx_->log->warning(getName(), "recalibrate() — coin present, remove coin first");
            return false;
        }
        float    rpSum = 0.0f, lSum = 0.0f;
        uint32_t ok    = 0;
        for (int i = 0; i < 10; i++) {
            delay(convTimeMs_() + 5);
            const uint8_t status = spiRead_(REG_STATUS);
            if (status & STATUS_NO_OSC) continue;   // skip if coil not oscillating
            if (status & STATUS_DRDYB)  continue;   // skip if conversion in progress
            uint16_t rp, l;
            if (readBurst_(rp, l) && rp > 0 && rp < 65535) {
                rpSum += rp;
                lSum  += l;
                ++ok;
            }
        }
        if (ok < 5) {
            ctx_->log->warning(getName(), "recalibrate() failed — only %u/10 valid samples", ok);
            return false;
        }
        const float oldRp = calibrationRpBaseline_;
        const float oldL  = calibrationLBaseline_;
        calibrationRpBaseline_ = rpSum / ok;
        calibrationLBaseline_  = lSum  / ok;
        lastCalibrationTime_   = millis();
        ctx_->log->info(getName(),
            "recalibrate OK: RP %.0f\u2192%.0f  L %.0f\u2192%.0f  (%u/10 samples)",
            oldRp, calibrationRpBaseline_, oldL, calibrationLBaseline_, ok);
        stab_.reset();              // ADR-STAB-001: new baseline — re-establish stability
        stableCache_.valid = false;
        return true;
    }

    // ── Signal Stability API (D-2b, ADR-STAB-001) ────────────────────────────
    // getStableRp()/getStableL(): frozen snapshot — race-free with isSignalStable().
    // isSignalStable(): lock-free bool read (1 byte, atomic on Xtensa LX7).
    // getSignalSigma(): lock-free float read — for Serial diagnostics and Discovery.

    float getStableRp() const {
        if (!dataMutex_) return 0.0f;
        if (xSemaphoreTake(dataMutex_, pdMS_TO_TICKS(50)) != pdTRUE) return 0.0f;
        const float rp = stableCache_.valid ? static_cast<float>(stableCache_.rpRaw) : 0.0f;
        xSemaphoreGive(dataMutex_);
        return rp;
    }

    float getStableL() const {
        if (!dataMutex_) return 0.0f;
        if (xSemaphoreTake(dataMutex_, pdMS_TO_TICKS(50)) != pdTRUE) return 0.0f;
        const float l = stableCache_.valid ? static_cast<float>(stableCache_.lRaw) : 0.0f;
        xSemaphoreGive(dataMutex_);
        return l;
    }

    bool  isSignalStable() const { return stab_.stable; }
    float getSignalSigma() const { return stab_.sigma; }

#ifdef DISCOVERY_MODE
    // ── ADR-D6: Discovery capture API (compile-time guarded) ─────────────────
    // Exposes private SPI methods for the blocking capture loop in discoveryCaptureStep().
    // NEVER call from update() or ISR — not mutex-protected by design.
    uint8_t  spiReadPublic(uint8_t reg)                             { return spiRead_(reg); }
    bool     readMeasurementBurstPublic(uint16_t& rp, uint16_t& l) { return readBurst_(rp, l); }
    uint32_t readLHRBurstPublic()                                   { return readLHRBurst_(); }
    uint32_t getClkinFreqHz() const                                 { return clkinFreqHz_; }
    uint32_t convTimeMs()     const                                 { return convTimeMs_(); }
#endif

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
        minFreqNibble_         = ctx_->config->getUInt8 ("ldc1101.min_freq_nibble",     0x06);
        rpSetValue_            = ctx_->config->getUInt8 ("ldc1101.rp_set",              0x26);
        clkinFreqHz_           = ctx_->config->getUInt32("ldc1101.clkin_freq_hz",  16000000UL);
        clkinGpio_             = ctx_->config->getInt   ("ldc1101.clkin_gpio",             -1);
        coinDetectThreshold_   = ctx_->config->getFloat ("ldc1101.coin_detect_threshold",  0.85f);
        coinReleaseThreshold_  = ctx_->config->getFloat ("ldc1101.coin_release_threshold", 0.92f);
        detectDebounceN_       = ctx_->config->getUInt8 ("ldc1101.detect_debounce_n",      5);
        releaseDebounceM_      = ctx_->config->getUInt8 ("ldc1101.release_debounce_m",     3);
        lhrContinuous_         = ctx_->config->getBool  ("ldc1101.lhr_continuous",      false);
        lhrRcount_             = ctx_->config->getUInt32("ldc1101.lhr_rcount",       65535UL);
        stabThreshRp_          = ctx_->config->getFloat ("ldc1101.stab_sigma_thresh_rp", 50.0f);

        if (csPin_ < 0 || !ctx_->spi) {
            return fail_(1, "CS pin or SPI bus not available");
        }

        // ADR-CLKIN-002: start LEDC 16 MHz on clkin_gpio if configured.
        // Without CLKIN: RP_DATA valid, L_DATA=0/garbage. With CLKIN: both valid.
        // Arduino-ESP32 2.x API: ledcSetup/ledcAttachPin/ledcWrite(channel, duty).
        // Channel 0 is reserved for CLKIN; no other LEDC use in this project.
        if (clkinGpio_ >= 0) {
            ledcSetup(0, clkinFreqHz_, 1);   // channel=0, 1-bit resolution (2 levels → 50% duty)
            ledcAttachPin(clkinGpio_, 0);    // bind GPIO to channel 0
            ledcWrite(0, 1);                 // duty=1/2^1=50%
            ctx_->log->info(getName(), "CLKIN on GPIO%d at %lu Hz (LEDC ch0)",
                            clkinGpio_, (unsigned long)clkinFreqHz_);
            delay(1);  // allow the clock to stabilize before SPI config
        } else {
            ctx_->log->info(getName(), "CLKIN not configured (L_DATA will be invalid — ADR-CLKIN-002)");
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
                    "DRDYB=1 for %lu consecutive calls — STATUS=0x%02X (check MIN_FREQ/RESP_TIME)",
                    (unsigned long)ds_.staleCount, status);
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

        // ── LHR continuous (D-2, ADR-LHR-001) ──────────────────────────────
        // Non-blocking: status check every update(); 3-byte burst only when DRDYB=0.
        // Overhead: ~5 μs (status) + ~15 μs (burst, ~every 3-4 cycles) = 0.1% budget.
        if (lhrContinuous_) {
            const uint8_t lhrStat = spiRead_(REG_LHR_STATUS);
            if (lhrStat & LHR_STATUS_ERR_MASK) {
                if (!ds_.lhrErrorLogged) {
                    ctx_->log->warning(getName(),
                        "LHR error flags: 0x%02X (ZC=%d OR=%d UR=%d OF=%d)",
                        lhrStat,
                        (lhrStat >> 4) & 1, (lhrStat >> 3) & 1,
                        (lhrStat >> 2) & 1, (lhrStat >> 1) & 1);
                    ds_.lhrErrorLogged = true;
                }
            }
            if (!(lhrStat & LHR_STATUS_DRDYB)) {  // 0 = data ready (inverted)
                const uint32_t lhrRaw = readLHRBurst_();
                if (lhrRaw > 0 && lhrRaw < 0xFFFFFFUL) {
                    if (xSemaphoreTake(dataMutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
                        cache_.lhrRaw   = lhrRaw;
                        cache_.lhrValid = true;
                        xSemaphoreGive(dataMutex_);
                    }
                    ds_.lhrErrorLogged = false;
                    if (!ds_.lhrFirstValid) {
                        // fSENSOR [Hz] = lhrRaw * fCLKIN / 2^24  (LDC1101 datasheet §7.3.5)
                        const float fSensor = static_cast<float>(lhrRaw)
                                              * static_cast<float>(clkinFreqHz_)
                                              / 16777216.0f;
                        ctx_->log->info(getName(),
                            "LHR first valid: raw=%lu  fSENSOR=%.1f kHz",
                            (unsigned long)lhrRaw, fSensor / 1000.0f);
                        ds_.lhrFirstValid = true;
                    }
                }
            }
        }

        // ── StabilityTracker (D-2b, ADR-STAB-001) ───────────────────────────
        // ~5 μs worst case (N=8 multiply/add + 1 sqrtf). 0.05% of 10 ms budget.
        stab_.feed(rpRaw, stabThreshRp_);
        if (stab_.stable) {
            if (xSemaphoreTake(dataMutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
                stableCache_.rpRaw = static_cast<uint16_t>(stab_.mean);
                stableCache_.lRaw  = lRaw;
                stableCache_.valid = true;
                xSemaphoreGive(dataMutex_);
            }
        }

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

        float    rpSum = 0.0f, lSum = 0.0f;
        uint32_t ok    = 0;
        for (int i = 0; i < 20; i++) {
            delay(convTimeMs_() + 5);
            uint16_t rp, l;
            if (readBurst_(rp, l) && rp > 0 && rp < 65535) {
                rpSum += rp;
                lSum  += l;
                ++ok;
            }
        }

        if (ok < 10) {
            ctx_->log->error(getName(), "Calibration failed: only %u valid readings", ok);
            return false;
        }

        calibrationRpBaseline_ = rpSum / ok;
        calibrationLBaseline_  = lSum  / ok;
        lastCalibrationTime_   = millis();
        stab_.reset();              // ADR-STAB-001: new baseline — re-establish stability
        stableCache_.valid = false;

        // fSENSOR = (fCLKIN × RESP_TIME_cycles) / (3 × L_avg)  — Eq.6, datasheet §8.6.21
        // ⚠ Without CLKIN the chip has no reference for L counting — L_DATA = 0 or garbage.
        //   calibrationFSensor_ will be nonsensical until CLKIN is wired (ADR-CLKIN-002).
        //   With CLKIN=16 MHz (LEDC on clkinGpio_) this formula gives the true fSENSOR.
        static const uint32_t kRespCycles[] = {192, 192, 192, 384, 768, 1536, 3072, 6144};
        const uint32_t respCycles = kRespCycles[respTimeBits_ & 0x07];
        calibrationFSensor_ = (static_cast<float>(clkinFreqHz_) * respCycles) /
                               (3.0f * calibrationLBaseline_);

        // fSENSOR validity check: > 5 MHz means L_DATA is garbage (no CLKIN in RP+L mode).
        // Valid range for MIKROE-3240: 200–500 kHz (LDC1101_ARCHITECTURE.md §Eq.6).
        // For precise fSENSOR wire CLKIN → mikroBUS Pin 16 and use LHR mode (ADR-LHR-001).
        const bool fSensorValid = calibrationFSensor_ < 5000000.0f;  // < 5 MHz
        if (fSensorValid) {
            ctx_->log->info(getName(),
                "Calibration OK. RP=%.0f  L=%.0f  fSENSOR=%.1f kHz (%u samples)",
                calibrationRpBaseline_, calibrationLBaseline_,
                calibrationFSensor_ / 1000.0f, ok);
        } else {
            ctx_->log->info(getName(),
                "Calibration OK. RP=%.0f  L=%.0f  fSENSOR=n/a (RP+L mode, no CLKIN — ADR-LHR-001) (%u samples)",
                calibrationRpBaseline_, calibrationLBaseline_, ok);
        }
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
                        stab_.reset();              // ADR-STAB-001: coin arrived — re-establish stability
                        stableCache_.valid = false;
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

        // DIG_CONFIG: MIN_FREQ nibble[7:4] | RESP_TIME bits[2:0]
        // 0x0=500kHz, 0xD=118kHz, 0xF≈1kHz (effectively disabled).
        // Ferromagnetic coins at d≈0 can drop fSENSOR below 118kHz — use lower nibble.
        uint8_t digCfg = ((minFreqNibble_ & 0x0F) << 4) | (respTimeBits_ & 0x07);
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

        // ── LHR subsystem (D-2, ADR-LHR-001) ──────────────────────────────────
        // RCOUNT: 0xFFFF = max 24-bit resolution, conv time ~65ms.
        // OFFSET: 0x0000 = maximum dynamic range.
        // LHR_CONFIG: 0x00 = SENSOR_DIV=0 (fSENSOR < fCLKIN/4 = 4 MHz for MIKROE-3240).
        // Note: LHR requires CLKIN — without it LHR_DATA=0. Guarded by isLDataValid().
        {
            const uint8_t lhrL = static_cast<uint8_t>(lhrRcount_ & 0xFF);
            const uint8_t lhrM = static_cast<uint8_t>((lhrRcount_ >> 8) & 0xFF);
            spiWrite_(REG_LHR_RCOUNT_LSB, lhrL);
            spiWrite_(REG_LHR_RCOUNT_MSB, lhrM);
            spiWrite_(REG_LHR_OFFSET_LSB, 0x00);
            spiWrite_(REG_LHR_OFFSET_MSB, 0x00);
            spiWrite_(REG_LHR_CONFIG,     0x00);
            if (spiRead_(REG_LHR_RCOUNT_LSB) != lhrL || spiRead_(REG_LHR_RCOUNT_MSB) != lhrM) {
                ctx_->log->warning(getName(), "LHR_RCOUNT verify failed — LHR output may be unreliable");
            } else {
                ctx_->log->info(getName(), "LHR configured: RCOUNT=0x%04lX, continuous=%s",
                                (unsigned long)lhrRcount_, lhrContinuous_ ? "true" : "false");
            }
        }

        ctx_->log->info(getName(), "configure_: DIG_CONFIG=0x%02X, RP_SET=0x%02X",
                        digCfg, rpSetValue_);
        return true;
        // Caller (initialize) switches to FUNC_MODE_ACTIVE after this returns true
    }

    // ── SPI helpers ───────────────────────────────────────────────────────────

    // Read RP_DATA (0x21-0x22) + L_DATA (0x23-0x24) in one 4-byte SPI transaction.
    // SYS-1: REG_RP_DATA_LSB (0x21) MUST be read FIRST — reading LSB triggers the
    //        shadow latch that freezes MSB and L_DATA for consistent readout.
    // Auto-increment (§3): LDC1101 increments address after each byte within one CS frame
    //   → 0x21 → 0x22 → 0x23 → 0x24 in a single transaction is valid per datasheet.
    // ADR-CLKIN-002: L_DATA is only valid when CLKIN is connected (mikroBUS Pin 16).
    //   Without CLKIN: lRaw = 0 (CLKIN→GND) or garbage (CLKIN floating).
    //   RP_DATA is unaffected by CLKIN state.
    bool readBurst_(uint16_t& rpRaw, uint16_t& lRaw) {
        if (!ctx_ || !ctx_->spi || csPin_ < 0) return false;
        ctx_->spi->beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
        digitalWrite(csPin_, LOW);
        ctx_->spi->transfer(REG_RP_DATA_LSB | 0x80);  // Read bit | 0x21 — FIRST!
        uint8_t rpLsb = ctx_->spi->transfer(0x00);    // 0x21: RP_DATA_LSB (latches shadow)
        uint8_t rpMsb = ctx_->spi->transfer(0x00);    // 0x22: RP_DATA_MSB
        uint8_t lLsb  = ctx_->spi->transfer(0x00);    // 0x23: L_DATA_LSB
        uint8_t lMsb  = ctx_->spi->transfer(0x00);    // 0x24: L_DATA_MSB
        digitalWrite(csPin_, HIGH);
        ctx_->spi->endTransaction();
        rpRaw = (static_cast<uint16_t>(rpMsb) << 8) | rpLsb;
        lRaw  = (static_cast<uint16_t>(lMsb)  << 8) | lLsb;
        return true;
    }

    // Read 24-bit LHR_DATA in one SPI burst (D-2, ADR-LHR-001).
    // REG_LHR_DATA_LSB (0x38) MUST be read FIRST — unlocks MID and MSB (datasheet §9.2.2.2).
    // Call only when LHR_STATUS.DRDYB=0 (data ready). Returns 0 on SPI failure.
    uint32_t readLHRBurst_() {
        if (!ctx_ || !ctx_->spi || csPin_ < 0) return 0;
        ctx_->spi->beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
        digitalWrite(csPin_, LOW);
        ctx_->spi->transfer(REG_LHR_DATA_LSB | 0x80);   // Read bit | 0x38 — FIRST
        const uint8_t lsb = ctx_->spi->transfer(0x00);  // 0x38: LHR_DATA_LSB (unlocks MID+MSB)
        const uint8_t mid = ctx_->spi->transfer(0x00);  // 0x39: LHR_DATA_MID
        const uint8_t msb = ctx_->spi->transfer(0x00);  // 0x3A: LHR_DATA_MSB
        digitalWrite(csPin_, HIGH);
        ctx_->spi->endTransaction();
        return (static_cast<uint32_t>(msb) << 16) |
               (static_cast<uint32_t>(mid) << 8)  |
               lsb;
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
