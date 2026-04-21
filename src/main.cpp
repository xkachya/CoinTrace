// CoinTrace - Open Source Inductive Coin Analyzer
// License: GPL v3 (firmware) + CERN OHL v2 (hardware)
// Hardware: M5Stack Cardputer-Adv + LDC1101 (SPI)
// Repository: https://github.com/xkachya/CoinTrace

#include <Arduino.h>
#include <M5Cardputer.h>
#include <Wire.h>
#include <SPI.h>
#include <esp_ota_ops.h>    // Wave 8 A-4 — OTA partition ops (rollback)
#include <esp_wifi.h>       // P0: esp_wifi_set_ps(WIFI_PS_NONE) — disable DTIM wake bursts
#include <ArduinoJson.h>    // Wave 7 Phase 2 — plugin config load from LittleFS
#include "Logger.h"
#include "SerialTransport.h"
#include "RingBufferTransport.h"
#include "logger_macros.h"
#include "ConfigManager.h"
#include "PluginSystem.h"
#include "PluginContext.h"
#include "LDC1101Plugin.h"
#include "NAU7802Plugin.h"   // Wave 10 D-12a — weight sensor (I2C, GPIO8/GPIO9)
#include "NVSManager.h"
#include "WiFiManager.h"     // Wave 8 A-1
#include "HttpServer.h"      // Wave 8 A-2
#include "LittleFSManager.h"
#include "LittleFSTransport.h"
#include "MeasurementStore.h"
#include "SDCardManager.h"      // Wave 7 P-4
#include "FingerprintCache.h"   // Wave 7 P-4
#include "StorageManager.h"     // Wave 7 P-5
#include "VectorCompute.h"      // Wave 8 C-2/C-3 — fingerprint vector
#include "MetalMatcher.h"       // Wave 8 C-7a/e — classification layer

// ── Logger globals (ініціалізуються першими в setup()) ────────────
static Logger              gLogger;
// EXT 2.54-14P UART debug: G15=TX (Pin 14), G13=RX (Pin 12), GND (Pin 4).
// FT232RL on COM4 — independent of USB-CDC (COM3), no DTR-reset issue.
// See docs/guides/UART_DEBUG_SETUP.md for wiring details.
static HardwareSerial      gUart1(1);  // UART1 peripheral
static SerialTransport     gSerialTransport(gUart1, SerialTransport::Format::TEXT, 115200);
static RingBufferTransport gRingTransport(20, /*usePsram=*/false);  // ESP32-S3FN8: NO PSRAM — 20 entries (20×220=4.4KB) to preserve heap for WiFi+HTTP (LA-1)

// ── Plugin system globals ────────────────────────────────────────
static ConfigManager gConfig;
static PluginSystem  gPluginSystem;
static PluginContext gCtx;
static NVSManager        gNVS;
static LittleFSManager   gLFS;
static LittleFSTransport gLfsTransport(gLFS, /*maxLogKB=*/200, /*queue=*/64);
static MeasurementStore  gMeasStore(gLFS, gNVS);
static SDCardManager     gSDCard;        // Wave 7 P-4 — optional SD archive tier
static FingerprintCache  gFPCache;        // Wave 7 P-4 — fingerprint index in RAM
static MetalMatcher      gMatcher;        // Wave 8 C-7e — weighted 5D classification (matchFull/matchQuick)
static StorageManager    gStorage(gNVS, gLFS, gSDCard, gFPCache, gMeasStore);  // Wave 7 P-5 — unified Facade
static WiFiManager       gWifi;                    // Wave 8 A-1 — AP/STA management
static AsyncWebServer    gHttpServer(80);           // Wave 8 A-2 — non-blocking HTTP on port 80
static HttpServer        gHttp;                    // Wave 8 A-2 — REST API + static web UI
// LDC1101Plugin is heap-allocated in setup() so PluginSystem::end()
// can safely call delete (ownership contract — PLUGIN_ARCHITECTURE.md §3.1).
// Hold a raw pointer (non-owning) for direct coin-state access in loop().
static LDC1101Plugin*    gLDC = nullptr;
// NAU7802Plugin (Wave 10 D-12a): heap-allocated in setup(), NOT added to
// PluginSystem yet (D-12d). Pointer is kept for C-13 smoke test and future
// D-12c calibration wizard ('K' key). Null if hardware absent.
static NAU7802Plugin*    gNAU = nullptr;
// D-12d: current measurement mass_g; -1.0f = not measured (NAU absent / uncalibrated / STEP_WEIGHT not reached yet)
static float             sMassG = -1.0f;
// P1.3: STEP_WEIGHT acquisition retry counter — reset at each new session start
static uint8_t           sWeightRetryCount = 0;
static constexpr uint8_t kWeightRetryMax   = 5; // After 5 retries, skip weight acquisition and proceed with mass_n=-1 (sentinel)
// D-12e+: true after user presses 1st ENTER (coin is on scale); false until then
static bool              sWeightAcqStarted = false;

// RTC memory survives esp_restart() — used to pass boot reason into the
// normal Logger pipeline (→ LittleFS log) after a recovery restart.
RTC_DATA_ATTR static char gRtcBootReason[32] = "";

// ── OTA window state (Wave 8 A-4) ADR-007 ───────────────────────────────
// sOtaWindowOpen is written by MainLoop ('O' key) and read by the lwIP task
// (HttpServer handlers). volatile ensures visiblity across FreeRTOS tasks on
// the same core (ESP32-S3 single-core lwIP on core 0; MainLoop on core 1).
static volatile bool     sOtaWindowOpen   = false; // true = 30-s upload window active
static volatile uint32_t sOtaWindowOpenMs = 0;     // millis() when window was opened
constexpr uint32_t kOtaWindowMs = 30000;           // 30-second upload window

// Rollback timer: set if firmware booted from a pending (unconfirmed) OTA.
// MainLoop cancels it when user presses 'O' to confirm. If it fires, we
// revert to app0 via esp_ota_set_boot_partition() and restart.
static bool     sOtaRollbackPending = false;
static uint32_t sOtaBootMs         = 0;
constexpr uint32_t kOtaRollbackMs  = 60000;       // 60-second confirm deadline

// ── C-2 Multi-position Measurement State Machine ────────────────────────────
// WAVE8_ROADMAP.md §C-2 — 4-position fingerprint acquisition.
// Positions: base spacer 0.6mm (d≈0.6mm) → +1mm spacer (d≈1.6mm) → +2mm spacer (d≈2.6mm) → drift-check.
// p3 protocol: bare coin (no capsule), 0.6mm 3D-printed base spacer (hw-verified 2026-03-26).
// Trigger: HTTP POST /measure/start or keyboard ENTER at IDLE + COIN_PRESENT.
// Advance:  ENTER key captures current RP/L reading and moves to next step.
// Abort:    Backspace or 120-second step timeout → IDLE.
enum class MeasState : uint8_t {
    IDLE,        // awaiting ENTER or HTTP /measure/start trigger
    STEP_WEIGHT, // D-12e: NAU7802 mass acquisition — skipped to STEP_BASE if !calibrated
    STEP_BASE,   // base spacer 0.6mm  (d≈0.6mm) — press ENTER → rp[0]/l[0]
    STEP_1,      // +1mm spacer        (d≈1.6mm) — press ENTER → rp[1]/l[1]
    STEP_3,      // +2mm spacer        (d≈2.6mm) — press ENTER → rp[2]/l[2]
    STEP_DRIFT,  // remove spacers, back on tray  — press ENTER → rp[3] (drift)
    COMPUTE      // auto: vector → FP query → save → show result → IDLE
};

struct MeasSession {
    MeasState   state     = MeasState::IDLE;
    Measurement m         = {};
    uint32_t    stepMs    = 0;     // millis() when current step was entered
    bool        driftWarn = false; // true if drift ratio > DRIFT_THRESHOLD
};

static MeasSession sMeas;

// C-4: Written by lwIP thread (POST /api/v1/measure/start → measStartFn_ lambda).
// Read + cleared by MainLoop in loop(). volatile ensures MainLoop sees lwIP write
// without memory ordering issues on the ESP32-S3 dual-core architecture.
volatile bool gMeasStartRequested = false;

// ── Quick Screen constants (Wave 8 C-7b) ─────────────────────────────────────
// QUICK_SCREEN_SPEC.md §3 — Phase 1 threshold-based classification.
// HW-QS-4 empirical calibration 2026-03-30: 5 coins × 5 readings, p3 d≈0.6mm, spacer 0.6mm.
// DONE HW-QS-4: thresholds tuned; QUICK_SETTLE_MS=400 confirmed; dL ferro needs real ferro coin.
// All values are static constexpr → tunable in source, no runtime overhead.
static constexpr float QUICK_NOISE_FLOOR_PCT    =  2.0f;   // dRpPct below → signal in noise
static constexpr float QUICK_L_NOISE_FLOOR_CT   =  2.0f;   // |dL_raw| below → noise
// Empirical dRp% ordering (ascending): Ag 33.4% < Fe 36.6% < Al 38.6% < Cu 43.4% ≈ ZnNi 44.3%.
// Original estimate (SILVER=highest bucket) was INVERTED: large Ag coin (38 mm) > coil area
// → lower eddy coupling efficiency than smaller Cu/ZnNi coins. Constants listed high→low to
// match classifyQuick() cascade order. Retained names: COPPER=highest, SILVER=lowest bucket.
static constexpr float QUICK_COPPER_THRESH_PCT  = 41.0f;   // dRpPct > 41%     → COPPER (Cu≈43.4%, ZnNi≈44.3%)
static constexpr float QUICK_ALUM_THRESH_PCT    = 37.3f;   // dRpPct 37.3–41%  → ALUM   (Al≈38.6%)  ⚠ 0.5% gap to Fe
static constexpr float QUICK_SILVER_THRESH_PCT  = 35.0f;   // dRpPct 35–37.3%  → ?      (Fe zone, no dL ferro signal)
                                                             // dRpPct 2–35%     → SILVER (Ag≈33.4%, large-coin coupling)
static constexpr float QUICK_FERRO_THRESH_L_RAW = 100.0f;  // dL_raw > +100 ct → ferro (genuine ferromagnet, positive dL)
                                                             // HW-QS-4: all 5 test coins show NEGATIVE dL (eddy dominant)
                                                             // → ferro flag never triggered; validate with real steel coin (S-5)
// Settling window: RP signal is unstable for ~300-500 ms after COIN_PRESENT because
// the user's hand is still moving. Quick Screen is suppressed until signal settles.
// HW-QS-4 confirmed: QUICK_SETTLE_MS=400 working correctly, no adjustment needed.
static constexpr uint32_t QUICK_SETTLE_MS       = 400;     // ms after COIN_PRESENT before drawing Quick Screen
static constexpr uint32_t MEAS_STEP_SETTLE_MS   = 300;     // ms — fallback settle (legacy, kept for #ifdef-free builds)

// ── Unified multi-sample capture (D-7b, ADR-MEAS-001) ───────────────────────
// CaptureStats: accumulates RP+L+LHR samples over captureMs window.
// Used by both production and discovery paths — same struct, same algorithm.
// 4 × sizeof(CaptureStats) ≈ 4 × 348 B = 1,392 B BSS — within budget.

struct CaptureStats {
    double   rpSum   = 0.0, rpSumSq = 0.0;
    double   lSum    = 0.0, lSumSq  = 0.0;
    uint64_t lhrSum  = 0;
    uint16_t rpMin   = 65535, rpMax = 0;
    uint16_t lMin    = 65535, lMax  = 0;
    uint32_t lhrMin  = 0xFFFFFFUL, lhrMax = 0;
    uint16_t rpReservoir[64] = {}, lReservoir[64] = {};
    uint16_t count = 0, failCount = 0, lhrCount = 0;
    float    rpMedian = 0.0f, rpMean = 0.0f, rpSigma = 0.0f;
    float    lMedian  = 0.0f, lMean  = 0.0f, lSigma  = 0.0f;
    float    lhrMean  = 0.0f, fSensorHz = 0.0f;

    void reset() { *this = CaptureStats{}; }

    void finalize(uint32_t fClkinHz) {
        if (count == 0) return;
        // Means
        rpMean = static_cast<float>(rpSum / count);
        lMean  = static_cast<float>(lSum  / count);
        // Sigma
        const float rpVar = static_cast<float>(rpSumSq / count) - rpMean * rpMean;
        const float lVar  = static_cast<float>(lSumSq  / count) - lMean  * lMean;
        rpSigma = rpVar > 0.0f ? sqrtf(rpVar) : 0.0f;
        lSigma  = lVar  > 0.0f ? sqrtf(lVar)  : 0.0f;
        // Medians — sort reservoir (reservoir has min(count,64) valid entries)
        const uint16_t n = count < 64 ? count : 64;
        uint16_t rpBuf[64], lBuf[64];
        memcpy(rpBuf, rpReservoir, n * sizeof(uint16_t));
        memcpy(lBuf,  lReservoir,  n * sizeof(uint16_t));
        // Simple insertion sort — N ≤ 64, O(N²) is fine on embedded
        for (uint16_t i = 1; i < n; i++) {
            const uint16_t rk = rpBuf[i], lk = lBuf[i];
            int16_t j = i - 1;
            while (j >= 0 && rpBuf[j] > rk) { rpBuf[j + 1] = rpBuf[j]; j--; }
            rpBuf[j + 1] = rk;
            j = i - 1;
            while (j >= 0 && lBuf[j] > lk)  { lBuf[j + 1] = lBuf[j];  j--; }
            lBuf[j + 1] = lk;
        }
        rpMedian = (n % 2 == 1) ? rpBuf[n / 2]
                                 : (rpBuf[n / 2 - 1] + rpBuf[n / 2]) * 0.5f;
        lMedian  = (n % 2 == 1) ? lBuf[n / 2]
                                 : (lBuf[n / 2 - 1]  + lBuf[n / 2])  * 0.5f;
        // LHR — fSENSOR = lhrMean * fCLKIN / 2^24  (ADR-LHR-001 corrected formula)
        if (lhrCount > 0) {
            lhrMean   = static_cast<float>(lhrSum) / lhrCount;
            fSensorHz = lhrMean * static_cast<float>(fClkinHz) / 16777216.0f;
        }
    }
};

// 4-step capture array — always present (production + discovery share same array).
static CaptureStats sSteps[4];

// Capture timing — loaded from config in setup(). Production default: 1500ms per step.
// Discovery overrides captureMs to disc_capture_ms (typically 2000ms).
static uint32_t sCaptureSettleMs = 300;   // ldc1101.capture_settle_ms
static uint32_t sCaptureMs       = 1500;  // ldc1101.prod_capture_ms

#ifdef DISCOVERY_MODE
// Discovery-specific state — session file, measurement index, dump enable flag.
static bool     sDiscoveryActive      = false;
static uint32_t sDiscoveryCaptureMs   = 2000;  // ldc1101.disc_capture_ms (overrides sCaptureMs when active)
static char     sDiscoverySessionFile[52] = {};
static uint16_t sDiscoveryMeasIndex       = 0;
#endif // DISCOVERY_MODE

// Reset to true when coin is removed → forces full redraw on next placement.
// File-scope so loop() can reset it outside drawQuickScreen() (QUICK_SCREEN_SPEC §5).
static bool sQuickScreenFresh = true;

// Timestamp of the last COIN_PRESENT transition — Quick Screen is suppressed
// for QUICK_SETTLE_MS ms to let the RP signal stabilise after hand removal.
static uint32_t sCoinSettleMs = 0;

// Set by doMeasCompute() after showing the result screen — suppresses Quick Screen
// until the coin is physically removed, keeping the result visible.
static bool sResultPending = false;

// Quick Screen metal classification (Phase 1 — threshold-based).
// Phase 2 will call gMatcher.matchQuick() instead, after quick_centroid hw-data.
struct QuickClass {
    const char* label;    // ASCII label for UART log
    const char* display;  // short string for display (ASCII — Cardputer has no Cyrillic font)
};

// Cascade checks HIGH → LOW to match threshold ordering above.
static QuickClass classifyQuick(float dRpPct, bool isFerro) {
    if (isFerro)                           return {"STEEL",     "STEEL !"};
    if (dRpPct > QUICK_COPPER_THRESH_PCT)  return {"COPPER",    "COPPER" };  // > 41%: Cu / ZnNi
    if (dRpPct > QUICK_ALUM_THRESH_PCT)    return {"ALUMINIUM", "ALUM"   };  // 37.3–41%: Al
    if (dRpPct > QUICK_SILVER_THRESH_PCT)  return {"?",         "?"      };  // 35–37.3%: Fe zone (⚠ 0.5% margin)
    if (dRpPct > QUICK_NOISE_FLOOR_PCT)    return {"SILVER",    "SILVER" };  // 2–35%: Ag (large-coin low coupling)
    return                                        {"?",         "?"      };
}

// ── C-2 Display Helpers ──────────────────────────────────────────────────────
// drawMeasIdle()       — full-screen idle state (shown after session ends)
// drawQuickScreen()    — live Quick Screen (IDLE + COIN_PRESENT)
// drawMeasStep_full()  — full-screen redraw on each state transition
// drawMeasResult()     — full-screen result view after COMPUTE step; top-3 ranking
// doMeasCompute()      — runs vector math, FP match, save; called on STEP_DRIFT capture

// drawCaptureProgress() — partial-redraw progress bar during multi-sample capture loop.
// Called every ~100 ms from captureStep(); updates progress bar + live stats.
// Used in both production and discovery paths — always compiled.
// ⚠️ SPI bus: display and LDC1101 share SPI (VSPI). Strategy A (single task) — no
//    mutex needed. fillRect() costs ~2-5 ms; call at most every 100 ms.
static void drawCaptureProgress(uint8_t stepIdx, const CaptureStats& s,
                                uint32_t elapsedMs, uint32_t totalMs,
                                float rpMeanLive, float rpSigLive, float fSLive) {
    static uint8_t sLastStepIdx = 0xFF;  // force full redraw on first call per step
    const bool fullRedraw = (stepIdx != sLastStepIdx);
    if (fullRedraw) {
        sLastStepIdx = stepIdx;
        M5Cardputer.Display.fillScreen(BLACK);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(CYAN);
        M5Cardputer.Display.setCursor(4, 6);
        M5Cardputer.Display.print("MEASURING");
        M5Cardputer.Display.setTextColor(WHITE);
        M5Cardputer.Display.setCursor(4, 18);
        static const char* labels[] = {"BASE (0.6mm)", "ADDON (1.6mm)", "ADDON (2.6mm)", "DRIFT (0.6mm)"};
        M5Cardputer.Display.printf("Step: %s", labels[stepIdx < 4 ? stepIdx : 0]);
    }

    // Progress bar (x=10, y=34, w=180, h=10)
    const int pct    = (int)(elapsedMs * 100UL / (totalMs > 0 ? totalMs : 1));
    const int filled = 180 * pct / 100;
    M5Cardputer.Display.fillRect(10, 34, 180, 10, BLACK);
    if (filled > 0) M5Cardputer.Display.fillRect(10, 34, filled, 10, GREEN);
    M5Cardputer.Display.drawRect(10, 34, 180, 10, DARKGREY);
    M5Cardputer.Display.setCursor(194, 36);
    M5Cardputer.Display.setTextColor(DARKGREY);
    M5Cardputer.Display.printf("%u%%", pct < 100 ? pct : 100);

    // Live stats row: N + RP mean ± sigma
    M5Cardputer.Display.fillRect(0, 48, 240, 14, BLACK);
    M5Cardputer.Display.setTextColor(YELLOW);
    M5Cardputer.Display.setCursor(4, 56);
    M5Cardputer.Display.printf("N=%u  RP:%.0f+/-%.0f", s.count, rpMeanLive, rpSigLive);

    // LHR row
    M5Cardputer.Display.fillRect(0, 64, 240, 14, BLACK);
    if (s.lhrCount > 0) {
        M5Cardputer.Display.setTextColor(WHITE);
        M5Cardputer.Display.setCursor(4, 72);
        M5Cardputer.Display.printf("LHR:%u  fS:%.0fHz", s.lhrCount, fSLive);
    }
}

// captureStep() — blocking multi-sample capture for one measurement step.
// Settles for settleMs, then accumulates RP+L+LHR samples for captureMs.
// Vitter's Algorithm R reservoir (N=64) used for median estimation.
// Returns populated CaptureStats (finalize() called internally).
// ⚠️ Blocks loop() for (settleMs + captureMs) ≈ 1.8–2.3 s per call.
static CaptureStats captureStep(uint8_t stepIdx,
                                uint32_t settleMs,
                                uint32_t captureMs) {
    CaptureStats s;

    // Settle phase — wait for mechanical + electrical transients to subside
    delay(settleMs);

    const uint32_t  startMs      = millis();
    uint32_t        lastDisplayMs = 0;
    const uint32_t  dtMs          = gLDC ? gLDC->convTimeMs() + 2 : 16;
    const uint8_t   LHR_STATUS_DRDYB = 0x01;   // LHR_STATUS bit0 — 0=data ready (inverted)

    while (millis() - startMs < captureMs) {
        // ── RP + L read ──────────────────────────────────────────────────
        uint16_t rp = 0, l = 0;
        if (gLDC && gLDC->readMeasurementBurstPublic(rp, l)) {
            if (rp > 0 && rp < 65535) {
                s.rpSum   += rp;
                s.rpSumSq += (double)rp * rp;
                if (rp < s.rpMin) s.rpMin = rp;
                if (rp > s.rpMax) s.rpMax = rp;
                s.lSum    += l;
                s.lSumSq  += (double)l * l;
                if (l < s.lMin) s.lMin = l;
                if (l > s.lMax) s.lMax = l;

                // Vitter's Algorithm R — uniform reservoir sampling
                if (s.count < 64) {
                    s.rpReservoir[s.count] = rp;
                    s.lReservoir[s.count]  = l;
                } else {
                    const uint32_t j = (uint32_t)(esp_random() % (s.count + 1));
                    if (j < 64) {
                        s.rpReservoir[j] = rp;
                        s.lReservoir[j]  = l;
                    }
                }
                s.count++;
            } else {
                s.failCount++;
            }
        } else {
            s.failCount++;
        }

        // ── LHR read (non-blocking check) ────────────────────────────────
        if (gLDC) {
            const uint8_t lhrStat = gLDC->spiReadPublic(0x3B);  // REG_LHR_STATUS = 0x3B
            if (!(lhrStat & LHR_STATUS_DRDYB)) {                 // 0 = data ready
                const uint32_t lhrRaw = gLDC->readLHRBurstPublic();
                if (lhrRaw > 0 && lhrRaw < 0xFFFFFFUL) {
                    s.lhrSum += lhrRaw;
                    if (lhrRaw < s.lhrMin) s.lhrMin = lhrRaw;
                    if (lhrRaw > s.lhrMax) s.lhrMax = lhrRaw;
                    s.lhrCount++;
                }
            }
        }

        // ── Display update every ~100 ms ─────────────────────────────────
        const uint32_t now = millis();
        if (now - lastDisplayMs >= 100) {
            lastDisplayMs = now;
            float rpMeanLive = 0.0f, rpSigLive = 0.0f, fSLive = 0.0f;
            if (s.count > 0) {
                rpMeanLive = static_cast<float>(s.rpSum / s.count);
                const float rpVar = static_cast<float>(s.rpSumSq / s.count) - rpMeanLive * rpMeanLive;
                rpSigLive = rpVar > 0.0f ? sqrtf(rpVar) : 0.0f;
            }
            if (s.lhrCount > 0 && gLDC) {
                const float lhrMeanLive = static_cast<float>(s.lhrSum) / s.lhrCount;
                fSLive = lhrMeanLive * static_cast<float>(gLDC->getClkinFreqHz()) / 16777216.0f;
            }
            drawCaptureProgress(stepIdx, s, now - startMs, captureMs,
                                rpMeanLive, rpSigLive, fSLive);
        }

        delay(dtMs);
    }

    s.finalize(gLDC ? gLDC->getClkinFreqHz() : 16000000UL);
    return s;
}

static void drawMeasIdle();  // forward declaration — defined below runCalibrationWizard

// ── D-12c: NAU7802 Calibration Wizard ────────────────────────────────────────
// Triggered by key 'K' at IDLE. Blocking — wizard runs to completion.
// Sequence:
//   Screen 1: "Remove weight → ENTER"       → tare(32)
//   Screen 2: "Place 20g weight → ENTER"    → calibrate(20.0g, 32) after 5s settle
//   Screen 3: verify reading on display (3s)
// ENTER key advances each screen; Backspace aborts wizard at any step.
// Returns when wizard exits (success, abort, or hardware error).
static void runCalibrationWizard() {
    if (!gNAU) {
        gLogger.error("Cal", "NAU7802 not present — calibration impossible");
        return;
    }

    auto& disp = M5Cardputer.Display;
    constexpr float  CAL_KNOWN_MASS_G = 20.0f;
    constexpr uint16_t CAL_SAMPLES    = 32;
    constexpr uint32_t CAL_SETTLE_MS  = 5000;  // 5s: conservative for DRIFTING state

    // ── SCREEN 1: Tare ────────────────────────────────────────────────────
    disp.fillScreen(BLACK);
    disp.setTextSize(1);
    disp.setTextColor(CYAN);
    disp.setCursor(4, 6);
    disp.print("SCALE CALIBRATION  1/3");
    disp.setTextColor(WHITE);
    disp.setCursor(4, 24);
    disp.print("Remove ALL weight from");
    disp.setCursor(4, 36);
    disp.print("the platform.");
    disp.setTextColor(DARKGREY);
    disp.setCursor(4, 56);
    disp.print("Press ENTER when ready");
    disp.setCursor(4, 68);
    disp.print("or BACKSPACE to abort");
    gLogger.info("Cal", "Wizard start: waiting for empty platform (ENTER)");

    // Wait for ENTER or Backspace
    for (;;) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            const auto& st  = M5Cardputer.Keyboard.keysState();
            const char  key = st.del ? '\b' : (st.word.empty() ? (st.enter ? '\r' : '\0') : st.word[0]);
            if (key == '\r' || key == '\n') break;
            if (key == '\b') {
                gLogger.info("Cal", "Wizard aborted at step 1");
                drawMeasIdle();
                return;
            }
        }
        delay(20);
    }

    // Run tare
    disp.fillScreen(BLACK);
    disp.setTextSize(1);
    disp.setTextColor(CYAN);
    disp.setCursor(4, 6);
    disp.print("TARE...  (~0.4s)");
    disp.setTextColor(DARKGREY);
    disp.setCursor(4, 24);
    disp.print("Please wait");

    if (!gNAU->tare(CAL_SAMPLES)) {
        gLogger.error("Cal", "Tare failed — aborting wizard");
        disp.fillRect(0, 20, 240, 60, BLACK);
        disp.setTextColor(RED);
        disp.setCursor(4, 36);
        disp.print("TARE FAILED!");
        disp.setTextColor(WHITE);
        disp.setCursor(4, 50);
        disp.print("Check wiring, retry K");
        delay(3000);
        drawMeasIdle();
        return;
    }
    gLogger.info("Cal", "Tare OK");

    // ── SCREEN 2: Place weight ────────────────────────────────────────────
    disp.fillScreen(BLACK);
    disp.setTextSize(1);
    disp.setTextColor(CYAN);
    disp.setCursor(4, 6);
    disp.print("SCALE CALIBRATION  2/3");
    disp.setTextColor(WHITE);
    disp.setCursor(4, 24);
    disp.printf("Place %.0fg weight on", CAL_KNOWN_MASS_G);
    disp.setCursor(4, 36);
    disp.print("the platform.");
    disp.setTextColor(YELLOW);
    disp.setCursor(4, 50);
    disp.printf("Wait 5s for settling,");
    disp.setCursor(4, 62);
    disp.print("then press ENTER");
    gLogger.info("Cal", "Step 2: place %.0fg weight, ENTER when stable", CAL_KNOWN_MASS_G);

    for (;;) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            const auto& st  = M5Cardputer.Keyboard.keysState();
            const char  key = st.del ? '\b' : (st.word.empty() ? (st.enter ? '\r' : '\0') : st.word[0]);
            if (key == '\r' || key == '\n') break;
            if (key == '\b') {
                gLogger.info("Cal", "Wizard aborted at step 2");
                drawMeasIdle();
                return;
            }
        }
        delay(20);
    }

    // Countdown settle (gives load cell time to stop drifting)
    disp.fillScreen(BLACK);
    disp.setTextSize(1);
    disp.setTextColor(CYAN);
    disp.setCursor(4, 6);
    disp.print("Settling...");
    const uint32_t settleStart = millis();
    while (millis() - settleStart < CAL_SETTLE_MS) {
        const uint32_t remaining = (CAL_SETTLE_MS - (millis() - settleStart) + 999) / 1000;
        disp.fillRect(0, 20, 240, 20, BLACK);
        disp.setTextColor(WHITE);
        disp.setCursor(4, 26);
        disp.printf("Stabilizing: %us...", (unsigned)remaining);
        M5Cardputer.update();
        delay(250);
    }

    // Run calibrate
    disp.fillRect(0, 20, 240, 60, BLACK);
    disp.setTextColor(DARKGREY);
    disp.setCursor(4, 26);
    disp.print("Capturing...  (~0.4s)");

    if (!gNAU->calibrate(CAL_KNOWN_MASS_G, CAL_SAMPLES)) {
        gLogger.error("Cal", "calibrate() failed — aborting wizard");
        disp.fillRect(0, 20, 240, 60, BLACK);
        disp.setTextColor(RED);
        disp.setCursor(4, 36);
        disp.print("CAL FAILED!");
        disp.setTextColor(WHITE);
        disp.setCursor(4, 50);
        disp.print("delta<=0? Check weight");
        delay(3000);
        drawMeasIdle();
        return;
    }

    // ── SCREEN 3: Verify ─────────────────────────────────────────────────
    // Take a quick blocking reading to show the user the live mass
    float verifyMass = -1.0f;
    // Use raw tare+scale directly: take one ADC read
    {
        int32_t raw = 0;
        // brief acquire: let the state machine do one sample via blocking delay trick
        // simplest: use tare() internals pattern (readAdc24 is private) — instead,
        // use startAcquisition + poll with tight loop (N=1 via fast mode)
        // We can't call private methods directly, so read from getNAU if available.
        // Cleanest: display the known mass as confirmation — actual verify happens via live read.
        verifyMass = CAL_KNOWN_MASS_G;  // placeholder; live read in next acquisition
    }

    disp.fillScreen(BLACK);
    disp.setTextSize(1);
    disp.setTextColor(CYAN);
    disp.setCursor(4, 6);
    disp.print("SCALE CALIBRATION  3/3");
    disp.setTextColor(GREEN);
    disp.setTextSize(2);
    disp.setCursor(10, 26);
    disp.print("CAL OK!");
    disp.setTextSize(1);
    disp.setTextColor(WHITE);
    disp.setCursor(4, 56);
    disp.printf("Ref: %.1fg  Saved to NVS", CAL_KNOWN_MASS_G);
    disp.setTextColor(DARKGREY);
    disp.setCursor(4, 70);
    disp.print("Verify with K after reboot");
    gLogger.info("Cal", "Wizard complete: cal=YES ref=%.1fg", CAL_KNOWN_MASS_G);
    delay(3000);
    drawMeasIdle();
}

static void drawMeasIdle() {
    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(GREEN);
    M5Cardputer.Display.setCursor(10, 10);
    M5Cardputer.Display.print("CoinTrace");
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.setCursor(5, 40);
    M5Cardputer.Display.print("Place coin on coil to start");
    M5Cardputer.Display.setTextColor(DARKGREY);
    M5Cardputer.Display.setCursor(5, 56);
    M5Cardputer.Display.printf("Meas: %u   %s", gNVS.getMeasCount(), gWifi.getIP());
}

// Wave 8 C-7b — live Quick Screen while a coin is present in IDLE state.
// Redraw strategy:
//   needFullRedraw  → fillScreen(BLACK) + all static elements (header/ferro/class/footer)
//   needPartialUpdate → ΔRp% and ΔL rows only, rate-limited to 250 ms to prevent flicker
//   early return if nothing needs updating this tick
static void drawQuickScreen(float liveRp, float liveL, float basRp, float basL) {
    const float dRpPct  = (basRp > 1.0f) ? (basRp - liveRp) / basRp * 100.0f : 0.0f;
    const bool  lValid  = gLDC ? gLDC->isLDataValid() : false;
    const float dL_raw  = lValid ? (liveL - basL) : 0.0f;
    const bool  isFerro = lValid && (dL_raw > QUICK_FERRO_THRESH_L_RAW);
    const QuickClass qc = classifyQuick(dRpPct, isFerro);

    static bool     sLastFerro    = false;
    static uint32_t sLastUpdateMs = 0;

    const bool needFullRedraw    = sQuickScreenFresh || (isFerro != sLastFerro);
    const bool needPartialUpdate = needFullRedraw || (millis() - sLastUpdateMs >= 250);

    if (!needPartialUpdate) return;
    sLastUpdateMs = millis();

    // ── Full redraw: clear screen + static elements ───────────────────────
    // fillScreen(BLACK) eliminates any leftover pixels from drawMeasIdle().
    if (needFullRedraw) {
        M5Cardputer.Display.fillScreen(BLACK);

        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(WHITE);
        M5Cardputer.Display.setCursor(4, 6);
        M5Cardputer.Display.print("QUICK SCREEN");
        M5Cardputer.Display.setTextColor(DARKGREY);
        M5Cardputer.Display.setCursor(138, 6);
        M5Cardputer.Display.print("[ENTER=Full]");

        M5Cardputer.Display.setTextColor(isFerro ? RED : GREEN);
        M5Cardputer.Display.setCursor(4, 66);
        M5Cardputer.Display.printf("  Ferro: %s", isFerro ? "YES !" : "NO  v");

        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setTextColor(
            isFerro ? RED : (qc.label[0] != '?' ? GREEN : DARKGREY));  // GREEN = classified, DARKGREY = uncertain
        M5Cardputer.Display.setCursor(4, 84);
        M5Cardputer.Display.print(qc.display);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(DARKGREY);
        M5Cardputer.Display.setCursor(4, 100);
        M5Cardputer.Display.print("  (quick estimate)");

        M5Cardputer.Display.setTextColor(DARKGREY);
        M5Cardputer.Display.setCursor(4, 122);
        M5Cardputer.Display.print("[ENTER] Full meas  [R] Recal");

        sLastFerro        = isFerro;
        sQuickScreenFresh = false;
    }

    // ── Partial update: ΔRp% and ΔL rows (rate-limited to 250 ms) ────────
    // Left column (x=4):  delta values — primary info for classification
    // Right column (x=145): absolute live values — secondary / tech reference
    M5Cardputer.Display.fillRect(0, 22, 240, 14, BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(dRpPct > QUICK_NOISE_FLOOR_PCT ? YELLOW : DARKGREY);
    M5Cardputer.Display.setCursor(4, 30);
    M5Cardputer.Display.printf("  dRp: %+.1f%%", dRpPct);
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.setCursor(145, 30);
    M5Cardputer.Display.printf("Rp:%5.0f", liveRp);

    M5Cardputer.Display.fillRect(0, 38, 240, 14, BLACK);
    M5Cardputer.Display.setTextColor(fabsf(dL_raw) > QUICK_L_NOISE_FLOOR_CT ? YELLOW : DARKGREY);
    M5Cardputer.Display.setCursor(4, 46);
    if (lValid) {
        M5Cardputer.Display.printf("  dL:  %+.0f ct", dL_raw);
        M5Cardputer.Display.setTextColor(WHITE);
        M5Cardputer.Display.setCursor(145, 46);
        M5Cardputer.Display.printf(" L:%5.0f", liveL);
    } else {
        M5Cardputer.Display.print("  dL:  -- (no CLKIN)");
    }
}

static void drawMeasStep_full(const MeasSession& s, uint16_t rpLive = 0) {
    // D-12e: STEP_WEIGHT — dedicated weight acquisition screen (pre-step 0)
    if (s.state == MeasState::STEP_WEIGHT) {
        M5Cardputer.Display.fillScreen(BLACK);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(DARKGREY);
        M5Cardputer.Display.setCursor(2, 2);
        M5Cardputer.Display.print("CoinTrace");
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setTextColor(CYAN);
        M5Cardputer.Display.setCursor(5, 12);
        M5Cardputer.Display.print("Weigh Coin");
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(WHITE);
        M5Cardputer.Display.setCursor(5, 36);
        M5Cardputer.Display.print("Place coin on scale,");
        M5Cardputer.Display.setCursor(5, 48);
        M5Cardputer.Display.print("then press ENTER.");
        M5Cardputer.Display.fillRect(0, 61, 240, 14, BLACK);  // periodic update fills on next tick
        M5Cardputer.Display.setTextColor(DARKGREY);
        M5Cardputer.Display.setCursor(5, 84);
        M5Cardputer.Display.print("Bksp = skip (6D mode)");
        const uint32_t elapsed_w  = millis() - s.stepMs;
        const uint32_t secsLeft_w = elapsed_w < 120000UL ? (120000UL - elapsed_w) / 1000 : 0;
        M5Cardputer.Display.setTextColor(secsLeft_w < 30 ? ORANGE : DARKGREY);
        M5Cardputer.Display.setCursor(5, 108);
        M5Cardputer.Display.printf("Timeout: %3us  Bksp=Skip", secsLeft_w);
        return;
    }

    M5Cardputer.Display.fillScreen(BLACK);

    // ── Header ─────────────────────────────────────────────────────────────
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(DARKGREY);
    M5Cardputer.Display.setCursor(2, 2);
    M5Cardputer.Display.print("CoinTrace");

    // ── Step indicator ─────────────────────────────────────────────────────
    // D-12e: STEP_BASE=(enum 2)..STEP_DRIFT=(enum 5) → display step idx 1..4
    const uint8_t idx = (uint8_t)s.state - (uint8_t)MeasState::STEP_BASE + 1;
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(CYAN);
    M5Cardputer.Display.setCursor(5, 12);
    M5Cardputer.Display.printf("Step %u / 4", idx);

    // ── Instruction ────────────────────────────────────────────────────────
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.setCursor(5, 36);
    switch (s.state) {
        case MeasState::STEP_BASE:  M5Cardputer.Display.print("Coin on base   (d~0.6 mm)");    break;
        case MeasState::STEP_1:     M5Cardputer.Display.print("Add 1mm spacer (d~1.6 mm)");    break;
        case MeasState::STEP_3:     M5Cardputer.Display.print("Add 2mm spacer (d~2.6 mm)");    break;
        case MeasState::STEP_DRIFT: M5Cardputer.Display.print("Remove spacers (back to tray)");  break;
        default: break;
    }

    // ── Action prompt ──────────────────────────────────────────────────────
    M5Cardputer.Display.setTextColor(GREEN);
    M5Cardputer.Display.setCursor(5, 50);
    M5Cardputer.Display.print("Press ENTER to capture");

    // ── Live RP reading ────────────────────────────────────────────────────
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(YELLOW);
    M5Cardputer.Display.setCursor(5, 66);
    if (rpLive > 0) {
        M5Cardputer.Display.printf("RP: %5u", rpLive);
    } else {
        M5Cardputer.Display.print("RP: -----");
    }

    // ── Progress boxes [1][2][3][4] ────────────────────────────────────────
    for (uint8_t i = 1; i <= 4; i++) {
        const int16_t bx = 3 + (i - 1) * 28;
        if      (i < idx)  { M5Cardputer.Display.fillRect(bx, 84, 22, 12, GREEN);    } // done
        else if (i == idx) { M5Cardputer.Display.drawRect(bx, 84, 22, 12, CYAN);     } // active
        else               { M5Cardputer.Display.drawRect(bx, 84, 22, 12, DARKGREY); } // future
    }

    // ── Timeout countdown ──────────────────────────────────────────────────
    const uint32_t elapsed  = millis() - s.stepMs;
    const uint32_t secsLeft = elapsed < 120000UL ? (120000UL - elapsed) / 1000 : 0;
    M5Cardputer.Display.setTextColor(secsLeft < 30 ? ORANGE : DARKGREY);
    M5Cardputer.Display.setCursor(5, 108);
    M5Cardputer.Display.printf("Timeout: %3us  Bksp=Abort", secsLeft);
}

static void drawMeasResult(const MeasSession& s, const MatchResult& mr) {
    M5Cardputer.Display.fillScreen(BLACK);

    // ── Title ──────────────────────────────────────────────────────────────
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(s.driftWarn ? ORANGE : GREEN);
    M5Cardputer.Display.setCursor(5, 3);
    M5Cardputer.Display.print(s.driftWarn ? "DRIFT WARN" : "MEASURED");

    // ── Raw RP vector ──────────────────────────────────────────────────────
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(YELLOW);
    M5Cardputer.Display.setCursor(5, 28);
    M5Cardputer.Display.printf("RP: %.0f  %.0f  %.0f  %.0f",
                               s.m.rp[0], s.m.rp[1], s.m.rp[2], s.m.rp[3]);

    // ── Computed fingerprint components ───────────────────────────────────
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.setCursor(5, 40);
    const float baseFS_disp = gLDC ? gLDC->getFSensor() : 0.0f;
    const float dfn_disp    = (sSteps[0].fSensorHz > 0.0f && baseFS_disp > 0.0f)
                              ? (sSteps[0].fSensorHz - baseFS_disp) / baseFS_disp : 0.0f;
    M5Cardputer.Display.printf("k1=%.3f  k2=%.3f  df_n=%.4f",
                               VectorCompute::k1(s.m),
                               VectorCompute::k2(s.m),
                               dfn_disp);

    // ── Match result ───────────────────────────────────────────────────────
    M5Cardputer.Display.setCursor(5, 57);
    if (s.m.conf > 0.01f) {
        M5Cardputer.Display.setTextColor(GREEN);
        M5Cardputer.Display.printf("%.6s  conf=%.0f%%", s.m.metal_code, s.m.conf * 100.0f);
        M5Cardputer.Display.setTextColor(WHITE);
        M5Cardputer.Display.setCursor(5, 69);
        M5Cardputer.Display.printf("%.38s", s.m.coin_name);
    } else {
        M5Cardputer.Display.setTextColor(DARKGREY);
        M5Cardputer.Display.print("No match in DB  (metal = UNKN)");
    }

    // ── Alternatives (top-2 / top-3) ────────────────────────────────────────
    for (uint8_t i = 0; i < mr.alt_count && i < 2; ++i) {
        M5Cardputer.Display.setTextColor(DARKGREY);
        M5Cardputer.Display.setCursor(5, 80 + i * 11);
        M5Cardputer.Display.printf("#%u %-7s %3.0f%%",
                                   i + 2,
                                   mr.alternatives[i].metal_code,
                                   mr.alternatives[i].confidence * 100.0f);
    }

    // ── Footer ─────────────────────────────────────────────────────────────
    M5Cardputer.Display.setTextColor(DARKGREY);
    M5Cardputer.Display.setCursor(5, 103);
    M5Cardputer.Display.printf("Saved #%u", gNVS.getMeasCount() - 1);

    if (s.driftWarn) {
        M5Cardputer.Display.setTextColor(ORANGE);
        M5Cardputer.Display.setCursor(5, 114);
        M5Cardputer.Display.printf("Drift: %.1f%%  (>5%%)  recheck coil",
                                   VectorCompute::driftRatio(s.m) * 100.0f);
    } else {
        M5Cardputer.Display.setTextColor(DARKGREY);
        M5Cardputer.Display.setCursor(5, 114);
        M5Cardputer.Display.print("Remove coin for next measurement");
    }
}

// ── D-3: Raw dump to SD ───────────────────────────────────────────────────────
// Serialises the full 4-step CaptureStats + production vector + discovery_derived
// to a per-session JSON file on the SD card.
// Called from doMeasCompute() before sMeas is reset, only when sDiscoveryActive.
// Heap: DynamicJsonDocument(3072) allocated + freed within this function (~50 ms).
// SD path: /CoinTrace/discovery/session_<millis/1000>.json (one file per boot).
// Thread safety: acquires gCtx.spiMutex for the full mkdir + open + write + close.
#ifdef DISCOVERY_MODE
static void saveDiscoveryDump(const MatchResult& mr) {
    if (!sDiscoveryActive) return;
    if (!gSDCard.isAvailable()) {
        gLogger.warning("Discovery", "SD not available — dump skipped");
        return;
    }

    // Heap guard: large JsonDocument allocation (steps×15 fields) requires ~3-4 KB
    // contiguous block. After many measurements the heap fragments; if we attempt
    // allocation when too little is free, operator new() panics (no-exceptions build).
    const uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 20000) {
        gLogger.warning("Discovery", "Heap low (%u B) — dump #%u skipped to prevent OOM",
                        freeHeap, sDiscoveryMeasIndex);
        ++sDiscoveryMeasIndex;
        return;
    }

    // ArduinoJson v7: dynamic allocation, no fixed capacity.
    JsonDocument doc;
    doc["index"]      = sDiscoveryMeasIndex;
    doc["coin_name"]  = sMeas.m.coin_name;
    doc["metal_code"] = sMeas.m.metal_code;
    doc["mass_g"]     = (sMassG > 0.0f) ? roundf(sMassG * 100.0f) / 100.0f : -1.0f;  // P0-B: top-level raw mass for build_gen8_db.py

    const char* stepNames[] = {"base_0.6mm", "addon_1.6mm", "addon_2.6mm", "drift_0.6mm"};
    JsonArray stepsArr = doc["steps"].to<JsonArray>();
    for (int i = 0; i < 4; i++) {
        JsonObject s = stepsArr.add<JsonObject>();
        s["step"]      = stepNames[i];
        s["rp_median"] = sSteps[i].rpMedian;
        s["rp_mean"]   = roundf(sSteps[i].rpMean   * 10.0f) / 10.0f;
        s["rp_sigma"]  = roundf(sSteps[i].rpSigma  * 10.0f) / 10.0f;
        s["rp_min"]    = sSteps[i].rpMin;
        s["rp_max"]    = sSteps[i].rpMax;
        s["rp_n"]      = sSteps[i].count;
        s["rp_fail"]   = sSteps[i].failCount;
        s["l_median"]  = sSteps[i].lMedian;
        s["l_mean"]    = roundf(sSteps[i].lMean   * 10.0f) / 10.0f;
        s["l_sigma"]   = roundf(sSteps[i].lSigma  * 10.0f) / 10.0f;
        s["l_min"]     = sSteps[i].lMin;
        s["l_max"]     = sSteps[i].lMax;
        if (sSteps[i].lhrCount > 0) {
            s["lhr_mean"]   = roundf(sSteps[i].lhrMean);
            s["lhr_min"]    = sSteps[i].lhrMin;
            s["lhr_max"]    = sSteps[i].lhrMax;
            s["lhr_n"]      = sSteps[i].lhrCount;
            s["fSensor_hz"] = roundf(sSteps[i].fSensorHz * 10.0f) / 10.0f;
        }
    }

    // Production vector v2 (ADR-VEC-001, 2026-04-02):
    //   Removed: slope (= (k2−1)/2 for p3 uniform spacing — zero information, weight=0)
    //   Added:   df_n = (fSensor_coin − fSensor_empty) / fSensor_empty — LHR-derived
    const float baseFS = gLDC ? gLDC->getFSensor() : 0.0f;  // fSENSOR empty baseline

    JsonObject pv = doc["production_vector"].to<JsonObject>();
    pv["dRp1_n"] = roundf(VectorCompute::dRp1_n(sMeas.m) * 1000.0f) / 1000.0f;
    pv["k1"]     = roundf(VectorCompute::k1(sMeas.m)     * 10000.0f) / 10000.0f;
    pv["k2"]     = roundf(VectorCompute::k2(sMeas.m)     * 10000.0f) / 10000.0f;
    pv["dL1_n"]  = roundf(VectorCompute::dL1_n(sMeas.m)  * 10000.0f) / 10000.0f;
    if (sSteps[0].fSensorHz > 0.0f && baseFS > 0.0f)
        pv["df_n"] = roundf((sSteps[0].fSensorHz - baseFS) / baseFS * 10000.0f) / 10000.0f;
    if (sSteps[1].fSensorHz > 0.0f && baseFS > 0.0f)  // ADR-VEC-002
        pv["df1_n"] = roundf((sSteps[1].fSensorHz - baseFS) / baseFS * 10000.0f) / 10000.0f;
    {   // D-12d: include mass_n when NAU calibrated; omit otherwise (backward compat gen-6 DB)
        const float pv_mass_n = (sMassG > 0.0f) ? (sMassG / NAU7802Plugin::MASS_REF_G) : -1.0f;
        if (pv_mass_n >= 0.0f)
            pv["mass_n"] = roundf(pv_mass_n * 10000.0f) / 10000.0f;
    }

    // Discovery-specific derived parameters
    JsonObject dd = doc["discovery_derived"].to<JsonObject>();
    if (sSteps[0].fSensorHz > 0.0f && baseFS > 0.0f)
        dd["delta_f_base_hz"] = roundf((sSteps[0].fSensorHz - baseFS) * 10.0f) / 10.0f;
    dd["rp_sigma_base"]       = roundf(sSteps[0].rpSigma * 10.0f) / 10.0f;
    dd["l_sigma_base"]        = roundf(sSteps[0].lSigma  * 10.0f) / 10.0f;
    const float bRp = gLDC ? gLDC->getBaseline() : 0.0f;
    dd["dRpPct_baseline"]     = (bRp > 1.0f)
        ? roundf((bRp - sSteps[0].rpMedian) / bRp * 1000.0f) / 10.0f
        : 0.0f;
    dd["baseline_rp_session"] = roundf(bRp);

    // Match result (may be empty if matcher not ready or drift warn active)
    JsonObject mrJ = doc["match_result"].to<JsonObject>();
    mrJ["metal_code"] = mr.metal_code;
    mrJ["confidence"] = roundf(mr.confidence * 1000.0f) / 1000.0f;
    mrJ["distance"]   = roundf(mr.distance   * 10000.0f) / 10000.0f;
    mrJ["algo"]       = (mr.algo == 0) ? "FULL" : "QUICK";

    // Check for heap allocation failure (doc.overflowed() in v7 = malloc failed)
    if (doc.overflowed()) {
        gLogger.warning("Discovery", "JSON alloc failed (heap OOM) — dump skipped");
        return;
    }

    // Write to SD under spiMutex
    if (xSemaphoreTake(gCtx.spiMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        SD.mkdir("/CoinTrace/discovery");  // idempotent — no-op if dir exists
        File f = SD.open(sDiscoverySessionFile, FILE_APPEND);
        if (f) {
            serializeJson(doc, f);
            f.print('\n');  // NDJSON: one JSON object per line, no commas
            f.close();
            gLogger.info("Discovery", "Dump #%u → %s (%u B JSON)",
                         sDiscoveryMeasIndex, sDiscoverySessionFile,
                         (unsigned)measureJson(doc));
        } else {
            gLogger.warning("Discovery", "SD open failed: %s", sDiscoverySessionFile);
        }
        xSemaphoreGive(gCtx.spiMutex);
    } else {
        gLogger.warning("Discovery", "spiMutex timeout — dump skipped");
    }
    ++sDiscoveryMeasIndex;
}
#endif // DISCOVERY_MODE

static void doMeasCompute() {
    // ── 0. Heap diagnostic (helps diagnose OOM panics after many measurements) ──
    gLogger.debug("Heap", "doMeasCompute: %u B free  max-block: %u B",
                  (uint32_t)ESP.getFreeHeap(), (uint32_t)ESP.getMaxAllocHeap());

    // ── 1. Drift check (rp[3] vs rp[0]) ───────────────────────────────────
    const float drift = VectorCompute::driftRatio(sMeas.m);
    sMeas.driftWarn   = (drift > VectorCompute::DRIFT_THRESHOLD);
    sMeas.m.pos_count = 4;
    if (sMeas.driftWarn) {
        sMeas.m.conf = 0.0f;
        gLogger.warning("Meas", "Drift %.1f%% > 5%% — conf forced=0", drift * 100.0f);
    }

    // ── 2. Fingerprint vector (compute df_n/df1_n from sSteps, log all for diagnostics) ──
    const float baseFS_rt  = gLDC ? gLDC->getFSensor() : 0.0f;
    const float meas_df_n  = (sSteps[0].fSensorHz > 0.0f && baseFS_rt > 0.0f)
                             ? (sSteps[0].fSensorHz - baseFS_rt) / baseFS_rt : 0.0f;
    const float meas_df1_n = (sSteps[1].fSensorHz > 0.0f && baseFS_rt > 0.0f)
                             ? (sSteps[1].fSensorHz - baseFS_rt) / baseFS_rt : 0.0f;
    gLogger.info("Meas", "Vec: dRp1=%.0f  k1=%.3f  k2=%.3f  df_n=%.4f  dL1=%.0f",
                 VectorCompute::dRp1(sMeas.m),
                 VectorCompute::k1(sMeas.m),
                 VectorCompute::k2(sMeas.m),
                 meas_df_n,
                 VectorCompute::dL1(sMeas.m));

    // ── 3. Fingerprint match via MetalMatcher (skip on drift — unreliable vector) ──
    // matchFull() normalises internally via VectorCompute (ADR-M6).
    // df_n = (fSensor_coin - fSensor_empty) / fSensor_empty is passed explicitly (ADR-VEC-001).
    // mass_n = sMassG / MASS_REF_G; sentinel -1.0f when NAU absent/uncalibrated (D-12d).
    // logTopCandidates() emits #1..#4 with per-axis dist breakdown to UART.
    MatchResult mr = {};  // always declared — used by D-3 saveDiscoveryDump()
    const float mass_n = (sMassG > 0.0f) ? (sMassG / NAU7802Plugin::MASS_REF_G) : -1.0f;
    if (gMatcher.isReady() && !sMeas.driftWarn) {
        mr = gMatcher.matchFull(sMeas.m, meas_df_n, meas_df1_n, mass_n);
        gMatcher.logTopCandidates(mr);
        if (mr.valid) {
            strlcpy(sMeas.m.metal_code, mr.metal_code, sizeof(sMeas.m.metal_code));
            strlcpy(sMeas.m.coin_name,  mr.coin_name,  sizeof(sMeas.m.coin_name));
            sMeas.m.conf = mr.confidence;
        }
    } else if (!gMatcher.isReady()) {
        gLogger.info("Meas", "Matcher not ready — skipping match");
    }

    // ── 4. Save measurement ────────────────────────────────────────────────
    if (gMeasStore.save(sMeas.m)) {
        gLogger.info("Meas", "Saved #%u — 4pos [%.0f,%.0f,%.0f,%.0f]  conf=%.2f%s",
                     gNVS.getMeasCount() - 1,
                     sMeas.m.rp[0], sMeas.m.rp[1], sMeas.m.rp[2], sMeas.m.rp[3],
                     sMeas.m.conf, sMeas.driftWarn ? " [DRIFT]" : "");
    } else {
        gLogger.warning("Meas", "save() failed (RP=%.0f)", sMeas.m.rp[0]);
    }

    // ── 5. Show result screen ──────────────────────────────────────────────
    // sResultPending suppresses Quick Screen until coin is removed — result
    // stays visible as long as the coin remains on the coil.
    drawMeasResult(sMeas, mr);
    sResultPending = true;

#ifdef DISCOVERY_MODE
    // ── 5a. D-3 raw dump to SD (before sMeas is cleared) ──────────────────
    saveDiscoveryDump(mr);
#endif

    // ── 6. Reset session — next trigger requires coin removal + re-placement
    sMeas = {};
}

// Display CoinTrace version and configuration on startup
void displayStartupInfo() {
  M5Cardputer.Display.fillScreen(BLACK);
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextColor(GREEN);
  M5Cardputer.Display.setCursor(10, 10);
  
  // Project name
  M5Cardputer.Display.println("CoinTrace");
  
  // Version from build flags
  #ifdef COINTRACE_VERSION
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(YELLOW);
  M5Cardputer.Display.print("v");
  M5Cardputer.Display.println(COINTRACE_VERSION);
  #endif
  
  M5Cardputer.Display.setTextColor(WHITE);
  M5Cardputer.Display.println();
  
  // Hardware configuration
  M5Cardputer.Display.println("Hardware:");
  M5Cardputer.Display.print("- ESP32-S3 @ ");
  M5Cardputer.Display.print(ESP.getCpuFreqMHz());
  M5Cardputer.Display.println(" MHz");
  
  #ifdef BOARD_HAS_PSRAM
  M5Cardputer.Display.print("- PSRAM: ");
  M5Cardputer.Display.print(ESP.getPsramSize() / 1024 / 1024);
  M5Cardputer.Display.println(" MB");
  #endif
  
  #ifdef LDC1101_ENABLED
  M5Cardputer.Display.println("- LDC1101 (SPI)");
  #endif
  
  M5Cardputer.Display.println();
  M5Cardputer.Display.setTextColor(CYAN);
  M5Cardputer.Display.println("Hold G0 to factory reset");
}

void setup() {
  // Initialize M5Cardputer hardware
  auto cfg = M5.config();
  M5Cardputer.begin(cfg);
  M5Cardputer.Display.setRotation(1);
  
  // Serial.begin() до Logger — SerialTransport використовує вже ініціалізований Serial
  Serial.begin(115200);
  delay(100);

  // ── 0. EXT UART init — до Logger, щоб перший лог вже йшов через FT232RL ──
  // UART1 mapped to EXT 2.54-14P: TX=G15 (Pin 14), RX=G13 (Pin 12).
  // COM4 (FT232RL) is open independently of USB-CDC COM3 — no DTR reset.
  gUart1.begin(115200, SERIAL_8N1, /*rx=*/13, /*tx=*/15);

  // ── 1. Logger ПЕРШИМ — до будь-якого іншого коду ─────────────
  // begin() створює FreeRTOS mutex (не можна в конструкторі: він виконується
  // як статичний глобал до старту FreeRTOS scheduler).
  gLogger.begin();
  gLogger.addTransport(&gSerialTransport);
  gLogger.addTransport(&gRingTransport);

  gLogger.info("System", "CoinTrace %s starting", COINTRACE_VERSION);
  if (gRtcBootReason[0] != '\0') {
    gLogger.info("System", "BOOT_REASON: %s", gRtcBootReason);
    gRtcBootReason[0] = '\0';  // consume once
  }
  // Log hardware reset reason so unexpected reboots (watchdog, brownout, panic)
  // are visible in Serial and LittleFS log. esp_reset_reason() is valid immediately
  // after power-on and survives all reset types including watchdog and brownout.
  {
    const char* hwReason = "unknown";
    switch (esp_reset_reason()) {
      case ESP_RST_POWERON:   hwReason = "power_on";       break;
      case ESP_RST_EXT:       hwReason = "ext_pin";        break;
      case ESP_RST_SW:        hwReason = "sw_restart";     break;
      case ESP_RST_PANIC:     hwReason = "panic";          break;
      case ESP_RST_INT_WDT:   hwReason = "int_watchdog";   break;
      case ESP_RST_TASK_WDT:  hwReason = "task_watchdog";  break;
      case ESP_RST_WDT:       hwReason = "other_watchdog"; break;
      case ESP_RST_DEEPSLEEP: hwReason = "deep_sleep";     break;
      case ESP_RST_BROWNOUT:  hwReason = "brownout";       break;
      case ESP_RST_SDIO:      hwReason = "sdio";           break;
      default:                hwReason = "unknown";        break;
    }
    gLogger.info("System", "HW_RESET: %s", hwReason);
  }
  gLogger.info("System", "CPU: %d MHz | Heap: %u B | PSRAM: %u MB",
               ESP.getCpuFreqMHz(), ESP.getFreeHeap(),
               (unsigned int)(ESP.getPsramSize() / 1024 / 1024));
  LOG_DEBUG(&gLogger, "System", "Flash: %u MB",
            (unsigned int)(ESP.getFlashChipSize() / 1024 / 1024));
  // ── 2. Display startup screen ────────────────────────────────
  displayStartupInfo();

  // ── 2a. GPIO0 boot recovery window (Wave 8 B-3) ──────────────
  // STORAGE_ARCHITECTURE.md §17.2 [1.5]: hold G0 during 3s splash window
  // → format LittleFS_data (factory data clear) + restart.
  // NOTE: GPIO0 CANNOT be checked at power-on — ROM bootloader intercepts
  // GPIO0=LOW before firmware starts and enters Download Mode. We check
  // it here, after display init, with a visual countdown.
  // NOTE: GPIO0 behavior is USB-CDC specific (Cardputer-Adv, no UART bridge).
  // On boards with external CH343/CP2102 bridge, GPIO0 may reach setup()
  // even when held at power-on. See B-3 Architecture Delta D-01.
  // Skip if waking from deep sleep (Soft Shutdown Fn+Q — §14.3).
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED) {
    pinMode(0, INPUT_PULLUP);
    constexpr uint32_t kWindowMs  = 3000;
    constexpr uint32_t kPollMs    = 50;
    constexpr uint32_t kHoldMs    = 500;  // must be held ≥500 ms to trigger
    constexpr uint32_t kQuickMs   = 200;  // early-exit: skip window if G0 idle
    // Quick check: if G0 not active within 200ms, skip the full 3s window.
    // Normal-boot penalty: +200ms (imperceptible vs ~850ms total boot).
    // See B-3 Architecture Delta Fix 1 (audit: boot penalty 3000→200ms).
    bool g0Active = false;
    for (uint32_t t = 0; t < kQuickMs; t += kPollMs) {
      if (digitalRead(0) == LOW) { g0Active = true; break; }
      delay(kPollMs);
    }
    if (g0Active) {
      uint32_t heldMs = 0;
      uint32_t elapsed = 0;
      uint32_t lastCountdown = 0;
      while (elapsed < kWindowMs) {
        // Update countdown on display every second
        uint32_t remaining = (kWindowMs - elapsed + 999) / 1000;
        if (remaining != lastCountdown) {
          lastCountdown = remaining;
          M5Cardputer.Display.fillRect(0, 110, 240, 30, BLACK);
          M5Cardputer.Display.setTextSize(2);
          M5Cardputer.Display.setTextColor(CYAN);
          M5Cardputer.Display.setCursor(10, 110);
          M5Cardputer.Display.print("G0 reset: ");
          M5Cardputer.Display.print(remaining);
          M5Cardputer.Display.print("s");
        }
        if (digitalRead(0) == LOW) {
          heldMs += kPollMs;
          // Show progress bar while held
          M5Cardputer.Display.fillRect(10, 130, (heldMs * 200) / kHoldMs, 10, RED);
          if (heldMs >= kHoldMs) {
            gLogger.info("System", "GPIO0 held — formatting LittleFS_data");
            Serial.println("[BOOT] GPIO0 held — formatting LittleFS_data...");
            LittleFSManager tmpLfs;
            if (tmpLfs.mountData()) {
              tmpLfs.formatData();
              strlcpy(gRtcBootReason, "gpio0_format_ok", sizeof(gRtcBootReason));
              Serial.println("[BOOT] LittleFS_data formatted — restarting");
            } else {
              strlcpy(gRtcBootReason, "gpio0_mount_failed", sizeof(gRtcBootReason));
              Serial.println("[BOOT] LittleFS_data mount failed during GPIO0 recovery");
            }
            esp_restart();
          }
        } else {
          if (heldMs > 0) {
            // Clear progress bar on release
            M5Cardputer.Display.fillRect(10, 130, 200, 10, BLACK);
          }
          heldMs = 0;
        }
        delay(kPollMs);
        elapsed += kPollMs;
      }
      // Clear the countdown area before continuing normal boot
      M5Cardputer.Display.fillRect(0, 105, 240, 40, BLACK);
    }
  }

  // ── 3. Hardware buses ─────────────────────────────────────────
  // VSPI: SCK=GPIO40, MISO=GPIO39, MOSI=GPIO14
  SPI.begin(40, 39, 14);
  // Internal I2C bus: SDA=GPIO8, SCL=GPIO9 (per schematic §17.2 [2.3]).
  // Used by TCA8418 keyboard controller (Wave 8, §17.2 [2.5]).
  // Grove port (GPIO2/GPIO1) does not require Wire.begin() — it is
  // on the same hardware I2C peripheral; sensors that need it call begin()
  // via ctx->wire which is pre-configured to the correct pins.
  Wire.begin(8, 9);
  // TODO(Wave 8 A-2): TCA8418::begin(0x34, /*INT=*/GPIO_NUM_11) — §17.2 [2.5]

  // ── 4. Plugin context ─────────────────────────────────────────
  gCtx.spi       = &SPI;
  gCtx.spiMutex  = xSemaphoreCreateMutex();
  gCtx.wire      = &Wire;
  gCtx.wireMutex = xSemaphoreCreateMutex();
  // PLUGIN_CONTRACT.md §1.3: mutexes must be non-null before begin()
  if (!gCtx.spiMutex || !gCtx.wireMutex) {
    gLogger.error("System", "FATAL: mutex creation failed — insufficient heap");
    while (true) { delay(1000); }  // halt: no safe recovery without synchronisation
  }
  gCtx.config    = &gConfig;
  gCtx.log       = &gLogger;

  // ── 4b. NVS (Wave 7) ──────────────────────────────────────────
  // begin() opens all NVS namespaces. Fatal if NVS is unavailable
  // (indicates flash corruption — device needs reflash).
  if (gNVS.begin()) {
    gLogger.info("NVS", "Ready — meas_count=%u slot=%u",
                 gNVS.getMeasCount(), gNVS.getMeasSlot());
  } else {
    gLogger.error("NVS", "begin() failed — NVS tier unavailable (flash corruption?)");
  }

  // A-4: Check for unconfirmed OTA on this boot.
  // If the previous boot applied an OTA but user never confirmed ('O' key),
  // start the 60-second rollback timer now.
  {
    NVSManager::OtaMeta otaMeta;
    if (gNVS.loadOtaMeta(otaMeta) && otaMeta.pending && !otaMeta.confirmed) {
      sOtaRollbackPending = true;
      sOtaBootMs = millis();
      gLogger.warning("OTA", "Unconfirmed OTA — press 'O' within %us to keep, then auto-rollback",
                      kOtaRollbackMs / 1000);
    }
  }

  // ── 4c. LittleFS (Wave 7 P-2) ────────────────────────────────────────────
  // mountSys():  read-only partition, no auto-format. Requires uploadfs-sys
  //              to be run once before /sys/config/device.json is available.
  // mountData(): formats on first boot; creates measurements/, cache/, logs/.
  // Both gracefully degrade: failure → warn + continue (no crash).
  if (gLFS.mountSys()) {
    gLogger.info("LFS", "sys mounted — free: %u KB", gLFS.sysFreeBytes() / 1024);
    if (gLFS.sys().exists("/config/device.json")) {
      gLogger.info("LFS", "/sys/config/device.json found");
    } else {
      gLogger.warning("LFS", "/sys/config/device.json missing — run: pio run -e uploadfs-sys -t uploadfs");
    }
  } else {
    gLogger.warning("LFS", "sys mount failed — no web UI or plugin config (uploadfs-sys required)");
  }
  if (gLFS.mountData()) {
    gLogger.info("LFS", "data mounted — free: %u KB", gLFS.dataFreeBytes() / 1024);

    // ── 4d. LittleFS log transport (Wave 7 P-3) ─────────────────────────
    // startTask() BEFORE addTransport() — write() needs the queue to exist.
    gLfsTransport.startTask(/*core=*/0, /*prio=*/2);
    gLogger.addTransport(&gLfsTransport);
    gLogger.info("LFS", "LittleFSTransport started");

    // ── 4e. MeasurementStore (Wave 7 P-3) ───────────────────────────
    gMeasStore.begin();

    // ── 4f. SD Card (Wave 7 P-4) ─────────────────────────────────────────────
    // tryMount() acquires spiMutex for SD.begin() — safe here (SPI init done at step 3).
    // Non-fatal: if SD absent, device continues in LittleFS-only mode.
    gSDCard.tryMount(gCtx.spi, gCtx.spiMutex);
    gMeasStore.setSDCardManager(&gSDCard);
    gLfsTransport.setSDCardManager(&gSDCard);
    gLogger.info("SD", "SD card %s",
                 gSDCard.isAvailable() ? "available (archive tier active)"
                                       : "not available (LittleFS-only mode)");

    // ── 4g. FingerprintCache (Wave 7 P-4) — boot step [7] ────────────────────
    // Loads index.json from SD into RAM with CRC32 + generation validation.
    // Non-fatal: matching unavailable if both SD and LittleFS cache absent.
    const bool cacheReady = gFPCache.init(gLFS, &gSDCard, gCtx.spiMutex);
    if (cacheReady) {
        gLogger.info("Cache", "FingerprintCache ready — %u entries",
                     (unsigned)gFPCache.entryCount());
    } else {
        gLogger.warning("Cache", "FingerprintCache unavailable — matching disabled");
    }
    // Struct size audit: verify against FINGERPRINT_DB_ARCHITECTURE.md §6.3 RAM budget.
    // sizeof(CacheEntry) × MAX_ENTRIES = peak RAM if fully populated.
    LOG_DEBUG(&gLogger, "Mem", "sizeof(LogEntry)=%u sizeof(CacheEntry)=%u (MAX=%u entries)",
              (unsigned)sizeof(LogEntry), (unsigned)sizeof(CacheEntry),
              (unsigned)FingerprintCache::MAX_ENTRIES);
  } else {
    gLogger.warning("LFS", "data mount failed — measurements will not persist");
  }

  // ── 4g-b. MetalMatcher (Wave 8 C-7e) ──────────────────────────────────────
  // init() takes a const-ref to gFPCache — must follow 4g so entries are loaded.
  // loadConfig() is best-effort: falls back to compiled defaults if SD absent.
  gMatcher.init(gFPCache);
  if (gSDCard.isAvailable()) {
      if (gMatcher.loadConfig(&gSDCard, gCtx.spiMutex)) {
          gLogger.info("Matcher", "Config loaded — sigma=%.2f weights=[%.1f,%.1f,%.1f,%.1f,%.1f,%.1f]",
                       gMatcher.config().sigma,
                       gMatcher.config().full_weights[0], gMatcher.config().full_weights[1],
                       gMatcher.config().full_weights[2], gMatcher.config().full_weights[3],
                       gMatcher.config().full_weights[4], gMatcher.config().full_weights[5]);
      } else {
          gLogger.info("Matcher", "Using default config (sigma=%.2f)", gMatcher.config().sigma);
      }
  } else {
      gLogger.info("Matcher", "SD not available — default config");
  }

  // ── 4h. StorageManager Facade (Wave 7 P-5) ───────────────────────────────────
  // Unified entry point for all storage tiers injected into PluginContext.
  // Graceful degradation is encapsulated here — plugins call ctx->storage
  // unconditionally; each method handles unavailable tiers internally.
  gCtx.storage = &gStorage;
  gLogger.info("Storage", "StorageManager ready — NVS:%s LFS:%s SD:%s FP:%u entries",
               gNVS.isReady()        ? "ok" : "fail",
               gLFS.isDataMounted()  ? "ok" : "fail",
               gSDCard.isAvailable() ? "ok" : "n/a",
               (unsigned)gFPCache.entryCount());

  // ── 4i. Load plugin configs from LittleFS sys → ConfigManager ────────────
  // Parses /plugins/ldc1101.json and populates gConfig with "ldc1101.*" keys.
  // MUST run AFTER mountSys() and BEFORE gPluginSystem.begin() — plugins read
  // gConfig inside initialize(). Without this the JSON is ignored and all
  // parameters fall back to their compiled defaults (ADR-CLKIN-002: clkin_gpio
  // defaults to -1 → LEDC never started → L_DATA invalid).
  if (gLFS.isSysMounted()) {
    const char* pluginJson = "/plugins/ldc1101.json";
    if (gLFS.sys().exists(pluginJson)) {
      fs::File f = gLFS.sys().open(pluginJson, FILE_READ);
      if (f) {
        JsonDocument doc;
        DeserializationError jsonErr = deserializeJson(doc, f);
        f.close();
        if (!jsonErr) {
          uint8_t loaded = 0;
          for (JsonPair kv : doc.as<JsonObject>()) {
            char cfgKey[48];
            snprintf(cfgKey, sizeof(cfgKey), "ldc1101.%s", kv.key().c_str());
            JsonVariant v = kv.value();
            // ArduinoJson 7: bool and int are distinct stored types; check bool first.
            if      (v.is<bool>())        { gConfig.setBool  (cfgKey, v.as<bool>());         ++loaded; }
            else if (v.is<int>())         { gConfig.setInt   (cfgKey, v.as<int32_t>());      ++loaded; }
            else if (v.is<double>())      { gConfig.setFloat (cfgKey, (float)v.as<double>()); ++loaded; }
            else if (v.is<const char*>()) { gConfig.setString(cfgKey, v.as<const char*>());  ++loaded; }
          }
          gLogger.info("Config", "ldc1101.json: %u keys loaded into ConfigManager", loaded);
        } else {
          gLogger.warning("Config", "ldc1101.json parse error: %s", jsonErr.c_str());
        }
      } else {
        gLogger.warning("Config", "ldc1101.json open failed");
      }
    } else {
      gLogger.warning("Config", "ldc1101.json not found — run: pio run -e uploadfs-sys -t uploadfs");
    }
  } else {
    gLogger.warning("Config", "LittleFS_sys not mounted — plugin config unavailable, using compiled defaults");
  }

  // ── 5. Plugin system ──────────────────────────────────────────
  // PluginSystem takes ownership of all plugins (deletes them in end()).
  // Non-owning pointers (gLDC, gNAU) kept for direct access in measurement workflow.
  gLDC = new LDC1101Plugin();
  gNAU = new NAU7802Plugin();
  gPluginSystem.addPlugin(gLDC);
  gPluginSystem.addPlugin(gNAU);  // D-12d: lifecycle + update() managed by PluginSystem
  gPluginSystem.begin(&gCtx);  // calls canInitialize() → initialize() for each plugin

  gLogger.info("System", "CoinTrace ready — %d/%d plugins initialised",
               gPluginSystem.readyCount(), gPluginSystem.pluginCount());

  // ── 5a. Unified capture timing — always compiled ──────────────────────────
  // Production: ldc1101.prod_capture_ms (default 1500ms per step → 4 steps ≈ 7.2s total).
  // Discovery:  sCaptureMs overridden below to ldc1101.discovery_capture_ms (typically 2000ms).
  sCaptureSettleMs = gConfig.getUInt32("ldc1101.discovery_settle_ms", 300UL);
  sCaptureMs       = gConfig.getUInt32("ldc1101.prod_capture_ms",    1500UL);

#ifdef DISCOVERY_MODE
  // ── 5b. Discovery mode config (D-1) ──────────────────────────────────────
  // discovery_enabled in ldc1101.json AND SD card mounted = discovery active.
  // Config keys are already loaded into gConfig by step 4i above.
  {
      const bool disc = gConfig.getBool("ldc1101.discovery_enabled", false);
      sDiscoveryActive    = disc && gSDCard.isAvailable();
      sDiscoveryCaptureMs = gConfig.getUInt32("ldc1101.discovery_capture_ms", 2000UL);
      if (sDiscoveryActive) {
          sCaptureMs = sDiscoveryCaptureMs;  // discovery overrides production capture window
          // esp_random() gives 32-bit hardware RNG — no collision risk across boots.
          snprintf(sDiscoverySessionFile, sizeof(sDiscoverySessionFile),
                   "/CoinTrace/discovery/session_%08lx.ndjson",
                   (unsigned long)esp_random());
          sDiscoveryMeasIndex = 0;
      }
      gLogger.info("Discovery", "mode %s (enabled=%d sd=%d settle=%ums cap=%ums)",
                   sDiscoveryActive ? "ACTIVE" : "inactive",
                   disc, gSDCard.isAvailable(),
                   (unsigned)sCaptureSettleMs, (unsigned)sCaptureMs);
      if (sDiscoveryActive)
          gLogger.info("Discovery", "Session file: %s", sDiscoverySessionFile);
  }
#else
  gLogger.info("Capture", "Unified pipeline: settle=%ums capture=%ums",
               (unsigned)sCaptureSettleMs, (unsigned)sCaptureMs);
#endif // DISCOVERY_MODE

  // ── STEP-0 HW VERIFICATION: initial calibrate() baseline measurement ─────
  // Temporary probe to measure real RP baseline and fSENSOR on actual hardware.
  // Results to be recorded in docs/hardware/HW_VERIFICATION_JOURNAL.md §Step-0.
  // REMOVE after C-1 baseline RP is documented (protocol_id determined).
  // NOTE: calibrate() uses delay() — safe here (setup context, not lwIP thread).
  if (gLDC && gLDC->isReady()) {
    gLogger.info("Step0", "=== HW VERIFICATION STEP-0 START ===");
    gLogger.info("Step0", "Remove ALL coins from sensor, then wait 4s...");
    delay(4000);  // extra window for the user to clear the coil
    const bool calOk = gLDC->calibrate();  // logs RP, L, fSENSOR (or "n/a" if no CLKIN)
    if (calOk) {
      gLogger.info("Step0", "RP=%.0f  L=%.0f  → §S-3 (fSENSOR needs CLKIN: ADR-CLKIN-002)",
                   gLDC->getBaseline(), gLDC->getLBaseline());
    } else {
      gLogger.error("Step0", "Calibration FAILED — check wiring, RP_SET, coil oscillation");
    }
    gLogger.info("Step0", "=== HW VERIFICATION STEP-0 END ===");
  }
  // ── END STEP-0 ──────────────────────────────────────────────────────────────

  // ── C-13: NAU7802 post-init diagnostics ──────────────────────────────────
  // NAU7802 was initialized by gPluginSystem.begin() above — this block only
  // runs deeper diagnostics: I2C bus check, two self-tests with mechanical
  // settle delay, tare baseline, and full diagnostic snapshots.
  // Hardware: Adafruit NAU7802 #4538, SDA=GPIO8, SCL=GPIO9 (shared Wire bus),
  //           load cell connected per §3.1 (GREEN=A+, WHITE=A−).
  {
    // HealthStatus → human-readable string for log output
    auto healthStr = [](IDiagnosticPlugin::HealthStatus s) -> const char* {
      switch (s) {
        case IDiagnosticPlugin::HealthStatus::OK:                    return "OK";
        case IDiagnosticPlugin::HealthStatus::DEGRADED:              return "DEGRADED";
        case IDiagnosticPlugin::HealthStatus::SENSOR_FAULT:          return "SENSOR_FAULT";
        case IDiagnosticPlugin::HealthStatus::INITIALIZATION_FAILED: return "INIT_FAILED";
        case IDiagnosticPlugin::HealthStatus::NOT_FOUND:             return "NOT_FOUND";
        case IDiagnosticPlugin::HealthStatus::CALIBRATION_NEEDED:    return "CAL_NEEDED";
        case IDiagnosticPlugin::HealthStatus::OK_WITH_WARNINGS:      return "OK_WARNINGS";
        default:                                                      return "UNKNOWN";
      }
    };

    gLogger.info("C13", "=== NAU7802 DIAGNOSTICS START ===");

    if (!gNAU || !gNAU->isReady()) {
      gLogger.error("C13", "NAU7802 not initialized — diagnostics skipped");
      gLogger.error("C13", "  Check: SDA=GPIO8 SCL=GPIO9, VIN=3.3V, GND");
      gLogger.error("C13", "  Check: I2C addr 0x2A (ADDR pin tied to GND)");
      if (gNAU) {
        auto err = gNAU->getLastError();
        gLogger.error("C13", "  Last error [%d]: %s", err.code, err.message);
      }
    } else {
      // ── I2C communication check — 3 consecutive PU_CTRL reads ────────────
      const bool commOk = gNAU->checkCommunication();
      gLogger.info("C13", "I2C comm (3x PU_CTRL):  %s", commOk ? "PASS ✓" : "FAIL ✗");

      // ── Diagnostic snapshot (health, counters) ────────────────────────────
      {
        auto diag = gNAU->runDiagnostics();
        gLogger.info("C13", "Diagnostics: health=%s  reads=%lu  fail=%lu  rate=%u%%",
                     healthStr(diag.status),
                     (unsigned long)diag.stats.totalReads,
                     (unsigned long)diag.stats.failedReads,
                     (unsigned)diag.stats.successRate);
        if (diag.error.code != 0)
          gLogger.warning("C13", "  Last error [%d]: %s", diag.error.code, diag.error.message);
      }

      // ── Self-test 1 — wait for empty platform to settle ──────────────────
      gLogger.info("C13", "Ensure load cell platform is EMPTY, waiting 3s...");
      delay(3000);
      gLogger.info("C13", "--- Self-test [1/2] ---");
      const bool st1Ok = gNAU->runSelfTest();
      gLogger.info("C13", "Self-test 1:  %s", st1Ok ? "PASS ✓" : "FAIL ✗");

      // ── Tare (only if self-test 1 passed) ────────────────────────────────
      // B-07: sigma < 20 counts → good; 20–100: marginal; >100: OTP reload failed.
      bool tareOk = false;
      if (st1Ok) {
        gLogger.info("C13", "Running tare(32) at 80 SPS — ~0.4s...");
        tareOk = gNAU->tare(32);
        gLogger.info("C13", "Tare:         %s", tareOk ? "OK ✓  (B-07: sigma<20 counts ideal)" : "FAIL ✗");
      }

      // ── Self-test 2 — after mechanical settle ─────────────────────────────
      // Compare stability label to self-test 1:
      //   DRIFTING → STABLE   = creep finished (temporary preload on cell)
      //   DRIFTING → DRIFTING = persistent mechanical preload (bad mount / wire pull)
      //   NOISY    → STABLE   = platform was still moving during self-test 1
      //   NOISY    → NOISY    = EMI / cable — check shielding
      gLogger.info("C13", "--- Self-test [2/2] — waiting 2s for mechanical settle...");
      delay(2000);
      const bool st2Ok = gNAU->runSelfTest();
      gLogger.info("C13", "Self-test 2:  %s", st2Ok ? "PASS ✓" : "FAIL ✗");

      // ── Post-settle diagnostic snapshot ──────────────────────────────────
      {
        auto diag2 = gNAU->runDiagnostics();
        gLogger.info("C13", "Post-settle: health=%s  reads=%lu  fail=%lu  rate=%u%%",
                     healthStr(diag2.status),
                     (unsigned long)diag2.stats.totalReads,
                     (unsigned long)diag2.stats.failedReads,
                     (unsigned)diag2.stats.successRate);
      }

      // ── Summary ───────────────────────────────────────────────────────────
      gLogger.info("C13", "--- DIAGNOSTICS SUMMARY ---");
      gLogger.info("C13", "  Init:        OK ✓  (PluginSystem)");
      gLogger.info("C13", "  I2C comm:    %s", commOk ? "PASS" : "FAIL");
      gLogger.info("C13", "  Self-test 1: %s", st1Ok  ? "PASS" : "FAIL");
      gLogger.info("C13", "  Tare:        %s", st1Ok  ? (tareOk ? "PASS" : "FAIL") : "SKIP (st1 failed)");
      gLogger.info("C13", "  Self-test 2: %s", st2Ok  ? "PASS" : "FAIL");
      if (gNAU->isCalibrated()) {
        gLogger.info("C13", "  Calibration: OK ✓  (scale=%.8f  zero=%ld)",
                     gNAU->getScaleFactor(), (long)gNAU->getZeroOffset());
      } else {
        gLogger.info("C13", "  Calibration: NOT YET — put known weight, press 'K' (D-12c)");
      }
    }

    gLogger.info("C13", "=== NAU7802 DIAGNOSTICS END ===");
  }
  // ── END C-13 ─────────────────────────────────────────────────────────────
  // begin() blocks ≤10 s in STA mode, then falls back to AP automatically.
  LOG_DEBUG(&gLogger, "Heap", "before WiFi: %u B free", (uint32_t)ESP.getFreeHeap());
  gWifi.begin(gNVS);
  // P0: disable WiFi power-save DTIM wake bursts — they cause I2C DMA collisions
  // on NAU7802 during SAMPLING (WiFi wake burst every 100 ms ≈ 5 ms DMA = 5% duty cycle).
  // WIFI_PS_NONE keeps radio always-on; at USB power the +30 mA overhead is negligible.
  // Reduces WiFi-induced I2C collision probability from ~86% to ~11% (Monte-Carlo, §2.2).
  esp_wifi_set_ps(WIFI_PS_NONE);
  gCtx.wifi = &gWifi;
  LOG_DEBUG(&gLogger, "Heap", "after WiFi:  %u B free", (uint32_t)ESP.getFreeHeap());
  gLogger.info("WiFi", "%s — SSID: %s  IP: %s",
               gWifi.isAP() ? "AP mode" : "STA mode",
               gWifi.getSSID(), gWifi.getIP());
  // Show WiFi status in the bottom section of the splash screen.
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(gWifi.isAP() ? CYAN : GREEN);
  M5Cardputer.Display.setCursor(5, 107);
  M5Cardputer.Display.printf("%s  %s", gWifi.getSSID(), gWifi.getIP());

  // ── 7. HttpServer (Wave 8 A-2) §17.2 [11] ───────────────────────────────
  // begin() registers all /api/v1/ routes, serves /sys/web/ static files,
  // and calls AsyncWebServer::begin() internally.
  // WiFiManager must be initialised before this step (step [10] above).
  gHttp.begin(gHttpServer, gStorage, gNVS, gWifi, gLFS, gRingTransport,
              gMeasStore, gFPCache);
  gHttp.setOtaWindow(&sOtaWindowOpen, &sOtaWindowOpenMs);  // A-4: inject window flags
  // C-4: Inject sensor state accessor and measurement start callback.
  // sensorStateFn reads sMeas.state (uint8_t, atomic on ESP32) + gLDC coin state —
  // safe to call from lwIP thread without a mutex.
  // measStartFn sets gMeasStartRequested (volatile bool); MainLoop consumes next tick.
  gHttp.setSensorState(
    []() -> const char* {
      switch (sMeas.state) {
        case MeasState::STEP_WEIGHT: return "MEASURING_STEP_WEIGHT";
        case MeasState::STEP_BASE:  return "MEASURING_STEP_BASE";
        case MeasState::STEP_1:     return "MEASURING_STEP_1";
        case MeasState::STEP_3:     return "MEASURING_STEP_3";
        case MeasState::STEP_DRIFT: return "MEASURING_STEP_DRIFT";
        case MeasState::COMPUTE:    return "MEASURING_COMPUTE";
        default: break;
      }
      if (gLDC && gLDC->getCoinState() == LDC1101Plugin::CoinState::COIN_PRESENT)
        return "IDLE_COIN_PRESENT";
      return "IDLE_NO_COIN";
    },
    []() -> bool {
      if (sMeas.state != MeasState::IDLE) return false;
      gMeasStartRequested = true;
      return true;
    }
  );
  gCtx.http = &gHttp;
  LOG_DEBUG(&gLogger, "Heap", "after HTTP:  %u B free", (uint32_t)ESP.getFreeHeap());
  gLogger.info("HTTP", "REST API ready — http://%s/api/v1/status", gWifi.getIP());

  // A-4: Show rollback banner if an unconfirmed OTA is pending.
  if (sOtaRollbackPending) {
    NVSManager::OtaMeta _bannerMeta;
    gNVS.loadOtaMeta(_bannerMeta);
    const uint32_t sketchKB = ESP.getSketchSize() / 1024;

    M5Cardputer.Display.fillRect(0, 46, 240, 58, BLACK);
    M5Cardputer.Display.setTextSize(1);
    // Line 1: new version + size
    M5Cardputer.Display.setTextColor(CYAN);
    M5Cardputer.Display.setCursor(5, 49);
    M5Cardputer.Display.printf("New: v%s  (%u KB)", COINTRACE_VERSION, sketchKB);
    // Line 2: previous version
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.setCursor(5, 59);
    if (_bannerMeta.pre_version[0] != '\0') {
      M5Cardputer.Display.printf("Prev: v%s", _bannerMeta.pre_version);
    } else {
      M5Cardputer.Display.print("Prev: unknown");
    }
    // Line 3: action prompt + countdown
    M5Cardputer.Display.setTextColor(YELLOW);
    M5Cardputer.Display.setCursor(5, 69);
    M5Cardputer.Display.printf("Press O to keep  (rollback %us)", kOtaRollbackMs / 1000);
    // Line 4: chip id / partition slot for quick sanity check
    M5Cardputer.Display.setTextColor(DARKGREY);
    M5Cardputer.Display.setCursor(5, 79);
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running) {
      M5Cardputer.Display.printf("Slot: %s  @ 0x%06X", running->label, running->address);
    }
  }
}

void loop() {
  M5Cardputer.update();
  
  // BtnA (physical OK button) — treat as ENTER
  if (M5Cardputer.BtnA.wasClicked()) {
    LOG_DEBUG(&gLogger, "Input", "BtnA click -> ENTER");
    // Inject synthetic '\r' by reusing the same handler block via goto is
    // messy — duplicate the ENTER logic inline via a lambda-like flag.
    // Simplest: set a flag and fall through to the key handler below.
    // We use a local bool so the identical ENTER block runs once.
    const char syntheticEnter = '\r';
    // ── ENTER (BtnA): advance measurement step ────────────────────────────
    if (sMeas.state == MeasState::STEP_WEIGHT) {
      // D-12e+: two-phase — 1st press starts acquisition (coin on scale), 2nd press confirms
      if (!sWeightAcqStarted) {
        sWeightAcqStarted = true;
        sWeightRetryCount = 0;
        if (gNAU) gNAU->startAcquisition();
        gLogger.info("Meas", "BtnA STEP_WEIGHT: acquisition started");
        M5Cardputer.Display.fillRect(0, 61, 240, 14, BLACK);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(YELLOW);
        M5Cardputer.Display.setCursor(5, 66);
        M5Cardputer.Display.print("Acquiring...");
      } else if (gNAU && gNAU->isAcquisitionComplete()) {
        sMassG = gNAU->getLastMassG();
        gLogger.info("Meas", "BtnA STEP_WEIGHT: mass=%.2f g  mass_n=%.4f",
                     sMassG, sMassG / NAU7802Plugin::MASS_REF_G);
        sMeas.state  = MeasState::STEP_BASE;
        sMeas.stepMs = millis();
        drawMeasStep_full(sMeas);
      } else {
        sMassG = -1.0f;
        gLogger.warning("Meas", "BtnA STEP_WEIGHT: acq not ready/error — 6D fallback");
        sMeas.state  = MeasState::STEP_BASE;
        sMeas.stepMs = millis();
        drawMeasStep_full(sMeas);
      }
    } else if (sMeas.state >= MeasState::STEP_BASE && sMeas.state <= MeasState::STEP_DRIFT) {
      if (!gLDC || !gLDC->isReady()) {
        gLogger.warning("Meas", "BtnA: sensor not ready");
      } else {
        ISensorPlugin::SensorData d = gLDC->read();
        if (!d.valid || d.value1 < 1.0f) {
          gLogger.warning("Meas", "BtnA: bad read (RP=%.0f) — retry", d.value1);
        } else {
          switch (sMeas.state) {
            case MeasState::STEP_BASE:
              sSteps[0]     = captureStep(0, sCaptureSettleMs, sCaptureMs);
              sMeas.m.rp[0] = sSteps[0].rpMedian;
              sMeas.m.l[0]  = sSteps[0].lMedian;
              gLogger.info("Meas", "Step 1/4 BASE: N=%u  RP=%.0f+/-%.1f  L=%.0f  fS=%.0fHz",
                           sSteps[0].count, sSteps[0].rpMedian,
                           sSteps[0].rpSigma, sSteps[0].lMedian, sSteps[0].fSensorHz);
              sMeas.state  = MeasState::STEP_1;
              sMeas.stepMs = millis();
              drawMeasStep_full(sMeas, (uint16_t)sMeas.m.rp[0]);
              break;
            case MeasState::STEP_1: {
              sSteps[1]     = captureStep(1, sCaptureSettleMs, sCaptureMs);
              sMeas.m.rp[1] = sSteps[1].rpMedian;
              sMeas.m.l[1]  = sSteps[1].lMedian;
              gLogger.info("Meas", "Step 2/4 ADDON: N=%u  RP=%.0f+/-%.1f  L=%.0f",
                           sSteps[1].count, sSteps[1].rpMedian,
                           sSteps[1].rpSigma, sSteps[1].lMedian);
              sMeas.state  = MeasState::STEP_3;
              sMeas.stepMs = millis();
              drawMeasStep_full(sMeas, (uint16_t)sMeas.m.rp[1]);
              break;
            }
            case MeasState::STEP_3: {
              sSteps[2]     = captureStep(2, sCaptureSettleMs, sCaptureMs);
              sMeas.m.rp[2] = sSteps[2].rpMedian;
              sMeas.m.l[2]  = sSteps[2].lMedian;
              gLogger.info("Meas", "Step 3/4 ADDON: N=%u  RP=%.0f+/-%.1f  L=%.0f",
                           sSteps[2].count, sSteps[2].rpMedian,
                           sSteps[2].rpSigma, sSteps[2].lMedian);
              sMeas.state  = MeasState::STEP_DRIFT;
              sMeas.stepMs = millis();
              drawMeasStep_full(sMeas, (uint16_t)sMeas.m.rp[2]);
              break;
            }
            case MeasState::STEP_DRIFT: {
              sSteps[3]     = captureStep(3, sCaptureSettleMs, sCaptureMs);
              sMeas.m.rp[3] = sSteps[3].rpMedian;
              sMeas.m.l[3]  = sSteps[3].lMedian;
              gLogger.info("Meas", "Step 4/4 DRIFT: N=%u  RP=%.0f+/-%.1f  fS=%.0fHz",
                           sSteps[3].count, sSteps[3].rpMedian,
                           sSteps[3].rpSigma, sSteps[3].fSensorHz);
              sMeas.state = MeasState::COMPUTE;
              doMeasCompute();
              break;
            }
            default: break;
          }
        }
      }
    } else if (sMeas.state == MeasState::IDLE &&
               gLDC && gLDC->isReady() &&
               gLDC->getCoinState() == LDC1101Plugin::CoinState::COIN_PRESENT &&
               gLFS.isDataMounted()) {
      // BtnA at IDLE + COIN_PRESENT → start session (same as HTTP POST)
      sMassG = -1.0f;  // D-12e: reset mass before each new session
      sMeas  = {};
      sMeas.stepMs = millis();
      sMeas.m.ts   = millis() / 1000;
      strlcpy(sMeas.m.metal_code,  "UNKN",                    sizeof(sMeas.m.metal_code));
      strlcpy(sMeas.m.coin_name,   "Unclassified",            sizeof(sMeas.m.coin_name));
      strlcpy(sMeas.m.protocol_id, "p4_MIKROE3240_b06_mass",  sizeof(sMeas.m.protocol_id));
      // D-12e: route to STEP_WEIGHT if NAU calibrated; else 6D fallback
      if (gNAU && gNAU->isCalibrated()) {
        sMeas.state = MeasState::STEP_WEIGHT;
        sWeightRetryCount = 0;
        sWeightAcqStarted = false;  // D-12e+: acquisition starts on 1st ENTER (coin on scale)
        gLogger.info("Meas", "BtnA start: session started (STEP_WEIGHT — place coin & ENTER)");
      } else {
        sMeas.state = MeasState::STEP_BASE;
        gLogger.info("Meas", "BtnA start: session started (STEP_BASE — 6D, NAU absent/uncal)");
      }
      drawMeasStep_full(sMeas);
    }
  }

  // Keyboard event handling
  if (M5Cardputer.Keyboard.isChange()) {
    if (M5Cardputer.Keyboard.isPressed()) {
      // Use reference — keysState() returns KeysState&.
      // Copying by value crashes: the vector copy-ctor reads data()==nullptr
      // when hid_keys/modifier_keys are non-empty but not yet allocated
      // (race with keyboard scanner, or physical-button-only press).
      const Keyboard_Class::KeysState& status = M5Cardputer.Keyboard.keysState();
      
      // status.enter is set by the library for the ↵ key (KEY_ENTER never lands in word)
      // status.del is set for Backspace — it also never lands in word (Keyboard.cpp:175)
      if (!status.word.empty() || status.enter || status.del) {
        const char key = status.del ? '\b' : (status.word.empty() ? '\r' : status.word[0]);
        LOG_DEBUG(&gLogger, "Input", "Key: %c (0x%02X)", key, (uint8_t)key);

        if (key == 'w' || key == 'W') {
          // Wave 8 A-1: interactive STA provisioning via keyboard ('W' key).
          // promptSTA() is blocking — reads SSID + password from keyboard,
          // saves to NVS on success, then reconnects.
          gWifi.promptSTA(gNVS);
        } else if (key == 'o' || key == 'O') {
          // Wave 8 A-4 (ADR-007): physical OTA key.
          //   Case 1 — unconfirmed OTA pending: confirm it ('O' pressed post-reboot).
          //   Case 2 — normal state: open 30-second upload window.
          if (sOtaRollbackPending) {
            // Confirm the OTA that just booted — cancel rollback timer.
            gNVS.setOtaConfirmed();
            sOtaRollbackPending = false;
            gLogger.info("OTA", "OTA confirmed — new firmware v%s (%u KB) accepted",
                         COINTRACE_VERSION, ESP.getSketchSize() / 1024);
            M5Cardputer.Display.fillRect(0, 46, 240, 58, BLACK);
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setTextColor(GREEN);
            M5Cardputer.Display.setCursor(5, 56);
            M5Cardputer.Display.printf("OTA confirmed \x84  v%s", COINTRACE_VERSION);
            M5Cardputer.Display.setTextColor(WHITE);
            M5Cardputer.Display.setCursor(5, 66);
            M5Cardputer.Display.printf("%u KB  — rollback cancelled", ESP.getSketchSize() / 1024);
          } else {
            // Open upload window.
            sOtaWindowOpen   = true;
            sOtaWindowOpenMs = millis();
            gLogger.info("OTA", "OTA window opened — 30 seconds");
          }
        } else if (key == '\r' || key == '\n' || key == 'm' || key == 'M') {
          // ── ENTER / M: start session at IDLE or advance measurement step ──
          if (sMeas.state == MeasState::IDLE &&
              gLDC && gLDC->isReady() &&
              gLDC->getCoinState() == LDC1101Plugin::CoinState::COIN_PRESENT &&
              gLFS.isDataMounted()) {
            sMassG = -1.0f;  // D-12e: reset mass before each new session
            sMeas  = {};
            sMeas.stepMs = millis();
            sMeas.m.ts   = millis() / 1000;
            strlcpy(sMeas.m.metal_code,  "UNKN",                    sizeof(sMeas.m.metal_code));
            strlcpy(sMeas.m.coin_name,   "Unclassified",            sizeof(sMeas.m.coin_name));
            strlcpy(sMeas.m.protocol_id, "p4_MIKROE3240_b06_mass",  sizeof(sMeas.m.protocol_id));
            // D-12e: route to STEP_WEIGHT if NAU calibrated; else 6D fallback
            if (gNAU && gNAU->isCalibrated()) {
              sMeas.state = MeasState::STEP_WEIGHT;
              sWeightRetryCount = 0;
              sWeightAcqStarted = false;  // D-12e+: acquisition starts on 1st ENTER (coin on scale)
              gLogger.info("Meas", "M/Enter: session started (STEP_WEIGHT — place coin & ENTER)");
            } else {
              sMeas.state = MeasState::STEP_BASE;
              gLogger.info("Meas", "M/Enter: session started (STEP_BASE — 6D, NAU absent/uncal)");
            }
            drawMeasStep_full(sMeas);
          } else if (sMeas.state == MeasState::STEP_WEIGHT) {
            // D-12e+: two-phase — 1st ENTER starts acquisition (coin on scale), 2nd ENTER confirms
            if (!sWeightAcqStarted) {
              sWeightAcqStarted = true;
              sWeightRetryCount = 0;
              if (gNAU) gNAU->startAcquisition();
              gLogger.info("Meas", "STEP_WEIGHT: acquisition started");
              M5Cardputer.Display.fillRect(0, 61, 240, 14, BLACK);
              M5Cardputer.Display.setTextSize(1);
              M5Cardputer.Display.setTextColor(YELLOW);
              M5Cardputer.Display.setCursor(5, 66);
              M5Cardputer.Display.print("Acquiring...");
            } else if (gNAU && gNAU->isAcquisitionComplete()) {
              sMassG = gNAU->getLastMassG();
              gLogger.info("Meas", "STEP_WEIGHT: mass=%.2f g  mass_n=%.4f",
                           sMassG, sMassG / NAU7802Plugin::MASS_REF_G);
              sMeas.state  = MeasState::STEP_BASE;
              sMeas.stepMs = millis();
              drawMeasStep_full(sMeas);
            } else {
              sMassG = -1.0f;
              gLogger.warning("Meas", "STEP_WEIGHT: acq not ready/error — 6D fallback");
              sMeas.state  = MeasState::STEP_BASE;
              sMeas.stepMs = millis();
              drawMeasStep_full(sMeas);
            }
          } else if (sMeas.state >= MeasState::STEP_BASE && sMeas.state <= MeasState::STEP_DRIFT) {
            if (!gLDC || !gLDC->isReady()) {
              gLogger.warning("Meas", "ENTER: sensor not ready");
            } else {
              ISensorPlugin::SensorData d = gLDC->read();
              if (!d.valid || d.value1 < 1.0f) {
                gLogger.warning("Meas", "ENTER: bad read (RP=%.0f) — retry", d.value1);
              } else {
                switch (sMeas.state) {
                  case MeasState::STEP_BASE:
                    sSteps[0]     = captureStep(0, sCaptureSettleMs, sCaptureMs);
                    sMeas.m.rp[0] = sSteps[0].rpMedian;
                    sMeas.m.l[0]  = sSteps[0].lMedian;
                    gLogger.info("Meas", "Step 1/4 BASE: N=%u  RP=%.0f+/-%.1f  L=%.0f  fS=%.0fHz",
                                 sSteps[0].count, sSteps[0].rpMedian,
                                 sSteps[0].rpSigma, sSteps[0].lMedian, sSteps[0].fSensorHz);
                    sMeas.state  = MeasState::STEP_1;
                    sMeas.stepMs = millis();
                    drawMeasStep_full(sMeas, (uint16_t)sMeas.m.rp[0]);
                    break;
                  case MeasState::STEP_1: {
                    sSteps[1]     = captureStep(1, sCaptureSettleMs, sCaptureMs);
                    sMeas.m.rp[1] = sSteps[1].rpMedian;
                    sMeas.m.l[1]  = sSteps[1].lMedian;
                    gLogger.info("Meas", "Step 2/4 ADDON: N=%u  RP=%.0f+/-%.1f  L=%.0f",
                                 sSteps[1].count, sSteps[1].rpMedian,
                                 sSteps[1].rpSigma, sSteps[1].lMedian);
                    sMeas.state  = MeasState::STEP_3;
                    sMeas.stepMs = millis();
                    drawMeasStep_full(sMeas, (uint16_t)sMeas.m.rp[1]);
                    break;
                  }
                  case MeasState::STEP_3: {
                    sSteps[2]     = captureStep(2, sCaptureSettleMs, sCaptureMs);
                    sMeas.m.rp[2] = sSteps[2].rpMedian;
                    sMeas.m.l[2]  = sSteps[2].lMedian;
                    gLogger.info("Meas", "Step 3/4 ADDON: N=%u  RP=%.0f+/-%.1f  L=%.0f",
                                 sSteps[2].count, sSteps[2].rpMedian,
                                 sSteps[2].rpSigma, sSteps[2].lMedian);
                    sMeas.state  = MeasState::STEP_DRIFT;
                    sMeas.stepMs = millis();
                    drawMeasStep_full(sMeas, (uint16_t)sMeas.m.rp[2]);
                    break;
                  }
                  case MeasState::STEP_DRIFT: {
                    sSteps[3]     = captureStep(3, sCaptureSettleMs, sCaptureMs);
                    sMeas.m.rp[3] = sSteps[3].rpMedian;
                    sMeas.m.l[3]  = sSteps[3].lMedian;
                    gLogger.info("Meas", "Step 4/4 DRIFT: N=%u  RP=%.0f+/-%.1f  fS=%.0fHz",
                                 sSteps[3].count, sSteps[3].rpMedian,
                                 sSteps[3].rpSigma, sSteps[3].fSensorHz);
                    sMeas.state = MeasState::COMPUTE;
                    doMeasCompute();
                    break;
                  }
                  default: break;
                }
              }
            }
          }
        } else if (key == '\b') {
          // ── BACKSPACE: at STEP_WEIGHT = skip (6D fallback); elsewhere = abort session ──
          if (sMeas.state == MeasState::STEP_WEIGHT) {
            // D-12e: skip NAU weighing — continue session in 6D fallback mode
            sMassG            = -1.0f;
            sWeightAcqStarted = false;
            sMeas.state       = MeasState::STEP_BASE;
            sMeas.stepMs      = millis();
            drawMeasStep_full(sMeas);
            gLogger.info("Meas", "STEP_WEIGHT skipped (Bksp) — 6D fallback");
          } else if (sMeas.state != MeasState::IDLE) {
            gLogger.info("Meas", "Session aborted (Bksp)");
            sMeas  = {};
            sMassG = -1.0f;  // D-12e: reset mass on abort
            drawMeasIdle();
          }
        } else if ((key == 'r' || key == 'R') && sMeas.state == MeasState::IDLE && !sResultPending) {
          // ── R: recalibrate no-coin baseline (QUICK_SCREEN_SPEC.md §6) ─────
          // Guard: !sResultPending prevents R from destroying the result screen
          // while a coin is still on the coil after a full measurement cycle.
          // recalibrate() is ~250 ms blocking — show feedback before calling.
          // It internally guards against coin-present (logs warning, returns false).
          M5Cardputer.Display.fillRect(0, 54, 240, 20, BLACK);
          M5Cardputer.Display.setTextSize(1);
          M5Cardputer.Display.setTextColor(CYAN);
          M5Cardputer.Display.setCursor(4, 66);
          M5Cardputer.Display.print("  Recalibrating...");
          if (gLDC && gLDC->isReady()) {
            gLDC->recalibrate();   // logs result + updated baselines internally
          }
          sQuickScreenFresh = true;  // force full redraw on next coin placement
          drawMeasIdle();            // clear "Recalibrating..." — restore idle screen
          gLogger.info("Meas", "Recalibrate requested (R key)");
        } else if (key == 'k' || key == 'K') {
          // ── K: NAU7802 calibration wizard (D-12c) ────────────────────────
          if (gNAU) {
            gLogger.info("Cal", "Calibration wizard started (K key)");
            runCalibrationWizard();
          } else {
            gLogger.warning("Cal", "K pressed but gNAU==nullptr — NAU7802 absent");
          }
        } else {
          // Display key on screen
          M5Cardputer.Display.fillRect(0, M5Cardputer.Display.height() - 20,
                                         M5Cardputer.Display.width(), 20, BLACK);
          M5Cardputer.Display.setCursor(10, M5Cardputer.Display.height() - 18);
          M5Cardputer.Display.setTextColor(YELLOW);
          M5Cardputer.Display.printf("Key: %c", key);
        }
      }
    }
  }
  
  // Plugin update loop (runs all enabled plugins, ≤ 10 ms each)
  // NAU7802 included: gPluginSystem.update() calls gNAU->update() via PluginSystem (D-12d, ADR-NAU-003)
  gPluginSystem.update();

  // ── One-time diagnostic: LFS task stack watermark ────────────────────────────
  // Logged once after 10 s so task has processed all boot-log entries.
  // Stack history: 4096 B (2026-03-18) → 3072 B (watermark 1332 B) → 3584 B (2026-03-24,
  // watermark 308 B was too thin; +512 B → 820 B headroom, hw-verified).
  // If this log shows < 512 B free → increase stack in LittleFSTransport.cpp.
  static bool sDiagLogged = false;
  if (!sDiagLogged && millis() > 10000) {
      sDiagLogged = true;
      LOG_DEBUG(&gLogger, "Stack", "LFS task watermark: %u B free (of 4608 B stack)",
                gLfsTransport.stackWatermarkBytes());
  }

  // ── C-2 Multi-position Measurement State Machine ─────────────────────────
  // Trigger: edge IDLE_NO_COIN/COIN_REMOVED → COIN_PRESENT (fresh placement).
  // Advance: ENTER key (captured in keyboard handler above).
  // Abort:   Backspace key or 120-second per-step timeout.
  if (gLDC && gLDC->isReady()) {
    const LDC1101Plugin::CoinState coinState = gLDC->getCoinState();

    // ── HTTP-triggered session start (POST /api/v1/measure/start) ─────────
    // lwIP thread sets gMeasStartRequested via measStartFn_; MainLoop consumes here.
    // Coin must be present + LFS mounted; otherwise flag is silently discarded
    // (202 was already sent — client re-polls GET /sensor/state to confirm start).
    if (gMeasStartRequested) {
      gMeasStartRequested = false;
      if (sMeas.state == MeasState::IDLE &&
          coinState    == LDC1101Plugin::CoinState::COIN_PRESENT &&
          gLFS.isDataMounted()) {
        sMassG = -1.0f;  // D-12e: reset mass before each new session
        sMeas  = {};
        sMeas.stepMs = millis();
        sMeas.m.ts   = millis() / 1000;
        strlcpy(sMeas.m.metal_code,  "UNKN",                sizeof(sMeas.m.metal_code));
        strlcpy(sMeas.m.coin_name,   "Unclassified",        sizeof(sMeas.m.coin_name));
        strlcpy(sMeas.m.protocol_id, "p4_MIKROE3240_b06_mass",  sizeof(sMeas.m.protocol_id));
        // D-12e: route to STEP_WEIGHT if NAU calibrated; else 6D fallback
        if (gNAU && gNAU->isCalibrated()) {
          sMeas.state = MeasState::STEP_WEIGHT;
          sWeightRetryCount = 0;
          sWeightAcqStarted = false;  // D-12e+: acquisition starts on 1st ENTER (coin on scale)
          gLogger.info("Meas", "HTTP start: session started (STEP_WEIGHT — place coin & ENTER)");
        } else {
          sMeas.state = MeasState::STEP_BASE;
          gLogger.info("Meas", "HTTP start: session started (STEP_BASE — 6D, NAU absent/uncal)");
        }
        drawMeasStep_full(sMeas);
      }
    }

    // ── Quick Screen — IDLE + live coin detection (Wave 8 C-7b) ──────────────
    // QUICK_SCREEN_SPEC.md §2: no new MeasState — visual mode inside IDLE.
    // drawQuickScreen() rate-limits to 250 ms internally and does early return
    // when nothing changed — no busy-loop rendering, no flicker.
    if (sMeas.state == MeasState::IDLE) {
      static LDC1101Plugin::CoinState sPrevCoinState = LDC1101Plugin::CoinState::IDLE_NO_COIN;
      static bool sQuickLoggedOn = false;
      if (coinState == LDC1101Plugin::CoinState::COIN_PRESENT) {
        if (!sResultPending) {
          const bool isFirstTick = (sPrevCoinState != LDC1101Plugin::CoinState::COIN_PRESENT);
          if (isFirstTick) {
            // Coin just detected — record settle start, show brief indicator.
            // Quick Screen is suppressed for QUICK_SETTLE_MS to let the user’s hand
            // leave and RP signal stabilise (otherwise first reading is ~50% low).
            sCoinSettleMs  = millis();
            sQuickLoggedOn = false;
            M5Cardputer.Display.fillScreen(BLACK);
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setTextColor(DARKGREY);
            M5Cardputer.Display.setCursor(4, 46);
            M5Cardputer.Display.print("  Stabilizing...");
            gLogger.info("Meas", "Coin detected — settling %u ms", QUICK_SETTLE_MS);
          } else if (millis() - sCoinSettleMs >= QUICK_SETTLE_MS) {
            // Settled — draw Quick Screen (rate-limited to 250 ms internally)
            drawQuickScreen(gLDC->getLiveRp(), gLDC->getLiveL(),
                            gLDC->getBaseline(), gLDC->getLBaseline());
            if (!sQuickLoggedOn) {
              sQuickLoggedOn = true;
              const float basRp  = gLDC->getBaseline();
              const float liveRp = gLDC->getLiveRp();
              const float dRpPct = (basRp > 1.0f) ? (basRp - liveRp) / basRp * 100.0f : 0.0f;
              gLogger.info("Meas", "QuickScreen ON: basRp=%.0f liveRp=%.0f dRp=%+.1f%%",
                           basRp, liveRp, dRpPct);
              if (gLDC->isLDataValid()) {
                const float basL   = gLDC->getLBaseline();
                const float liveL  = gLDC->getLiveL();
                const float dL_raw = liveL - basL;
                gLogger.info("Meas", "QuickScreen ON:  basL=%.0f  liveL=%.0f  dL=%+.0f ct  ferro=%s",
                             basL, liveL, dL_raw, dL_raw > QUICK_FERRO_THRESH_L_RAW ? "YES" : "NO");
              }
            }
          }
          // else: still settling — keep "Stabilizing..." visible, do nothing
        }
        // else: result screen is showing — do nothing until coin removed
      } else {
        if (sPrevCoinState == LDC1101Plugin::CoinState::COIN_PRESENT) {
          // Coin removed — clear result (if any) and restore idle screen
          if (sResultPending) {
            sResultPending    = false;
            gLogger.info("Meas", "Result dismissed: coin removed");
          }
          sQuickLoggedOn    = false;
          sQuickScreenFresh = true;   // force full redraw on next placement
          drawMeasIdle();
          gLogger.info("Meas", "QuickScreen OFF: coin removed");
        }
      }
      sPrevCoinState = coinState;
    }

    // ── Periodic live RP + countdown update (every 1 s, no flicker) ───────
    if (sMeas.state >= MeasState::STEP_BASE && sMeas.state <= MeasState::STEP_DRIFT) {
      static uint32_t sLiveUpdateMs = 0;
      if (millis() - sLiveUpdateMs >= 1000) {
        sLiveUpdateMs = millis();
        ISensorPlugin::SensorData live = gLDC->read();
        const uint16_t rpLive = (live.valid && live.value1 > 1.0f)
                                ? (uint16_t)live.value1 : 0u;
        // Partial redraw — RP line only (avoids full-screen flicker)
        M5Cardputer.Display.fillRect(0, 61, 155, 14, BLACK);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(YELLOW);
        M5Cardputer.Display.setCursor(5, 66);
        if (rpLive > 0) {
          M5Cardputer.Display.printf("RP: %5u", rpLive);
        } else {
          M5Cardputer.Display.print("RP: -----");
        }
        // Partial redraw — countdown line only
        const uint32_t el = millis() - sMeas.stepMs;
        const uint32_t sl = el < 120000UL ? (120000UL - el) / 1000 : 0;
        M5Cardputer.Display.fillRect(0, 103, 240, 14, BLACK);
        M5Cardputer.Display.setTextColor(sl < 30 ? ORANGE : DARKGREY);
        M5Cardputer.Display.setCursor(5, 108);
        M5Cardputer.Display.printf("Timeout: %3us  Bksp=Abort", sl);
      }
    }

    // D-12e: STEP_WEIGHT periodic update — acquisition status + countdown (every 500 ms)
    if (sMeas.state == MeasState::STEP_WEIGHT) {
      static uint32_t sWeightUpdateMs = 0;
      if (millis() - sWeightUpdateMs >= 500) {
        sWeightUpdateMs = millis();

        // P1.3: auto-retry on acquisition ERROR (fast-fail detected by state machine)
        if (gNAU && gNAU->isAcquisitionError()) {
          if (sWeightRetryCount < kWeightRetryMax) {
            sWeightRetryCount++;
            gLogger.warning("Meas", "STEP_WEIGHT: acq error — retry %u/%u",
                            sWeightRetryCount, (uint8_t)kWeightRetryMax);
            // Show retry feedback BEFORE startAcquisition() — bus recovery may block
            // ~1s inside _ensureConversionsRunning(), so display must update first
            // to ensure "Retry N/3..." is visible during the blocking recovery period.
            M5Cardputer.Display.fillRect(0, 61, 240, 14, BLACK);
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setTextColor(ORANGE);
            M5Cardputer.Display.setCursor(5, 66);
            M5Cardputer.Display.printf("Retry %u/%u...", sWeightRetryCount, (uint8_t)kWeightRetryMax);
            gNAU->startAcquisition();  // _ensureConversionsRunning() called internally
          } else {
            // All retries exhausted — show error, user can ENTER (6D) or Bksp (skip)
            gLogger.warning("Meas", "STEP_WEIGHT: scale failed after %u retries — 6D fallback",
                            (uint8_t)kWeightRetryMax);
            M5Cardputer.Display.fillRect(0, 61, 240, 14, BLACK);
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setTextColor(RED);
            M5Cardputer.Display.setCursor(5, 66);
            M5Cardputer.Display.print("Scale error. ENTER=6D");
          }
        } else {
          // Normal status update
          M5Cardputer.Display.fillRect(0, 61, 240, 14, BLACK);
          M5Cardputer.Display.setTextSize(1);
          M5Cardputer.Display.setCursor(5, 66);
          if (!sWeightAcqStarted) {
            M5Cardputer.Display.setTextColor(WHITE);
            M5Cardputer.Display.print("ENTER = start weighing");
          } else if (gNAU && gNAU->isAcquisitionComplete()) {
            const float _m = gNAU->getLastMassG();
            const float _s = gNAU->getLastSigmaG();
            // sigma > 0.3g: coin likely unstable — show ORANGE warning
            // sigma > 0.8g: reading is suspect    — show RED  + "CHECK"
            if (_s > 0.8f) {
              M5Cardputer.Display.setTextColor(RED);
              M5Cardputer.Display.printf("%.2fg s=%.2f CHECK!", _m, _s);
            } else if (_s > 0.3f) {
              M5Cardputer.Display.setTextColor(ORANGE);
              M5Cardputer.Display.printf("%.2f g  s=%.2f ?", _m, _s);
            } else {
              M5Cardputer.Display.setTextColor(GREEN);
              M5Cardputer.Display.printf("Ready: %.2f g  s=%.2f", _m, _s);
            }
          } else {
            M5Cardputer.Display.setTextColor(YELLOW);
            M5Cardputer.Display.print("Acquiring...");
          }
        }
        // Partial redraw — countdown line
        const uint32_t el_w = millis() - sMeas.stepMs;
        const uint32_t sl_w = el_w < 120000UL ? (120000UL - el_w) / 1000 : 0;
        M5Cardputer.Display.fillRect(0, 103, 240, 14, BLACK);
        M5Cardputer.Display.setTextColor(sl_w < 30 ? ORANGE : DARKGREY);
        M5Cardputer.Display.setCursor(5, 108);
        M5Cardputer.Display.printf("Timeout: %3us  Bksp=Skip", sl_w);
      }
    }

    // ── Per-step 120-second timeout (including STEP_WEIGHT) ───────────────
    if (sMeas.state >= MeasState::STEP_WEIGHT && sMeas.state <= MeasState::STEP_DRIFT) {
      if (millis() - sMeas.stepMs > 120000UL) {
        gLogger.warning("Meas", "Step %u timeout — session aborted", (uint8_t)sMeas.state);
        sMeas             = {};
        sMassG            = -1.0f;  // D-12e: reset mass on timeout
        sWeightAcqStarted = false;
        drawMeasIdle();
      }
    }
  }

  // ── A-4: OTA window timeout ─────────────────────────────────────────────────
  // Close the upload window 30 s after 'O' was pressed.
  if (sOtaWindowOpen && (millis() - sOtaWindowOpenMs > kOtaWindowMs)) {
    sOtaWindowOpen = false;
    gLogger.info("OTA", "OTA window expired");
  }

  // ── A-4: OTA countdown display ──────────────────────────────────────────────
  // Update display banner once per second while window is open or just closed.
  static uint32_t sOtaDisplayLastSec = UINT32_MAX;
  if (sOtaWindowOpen) {
    const uint32_t elapsed   = millis() - sOtaWindowOpenMs;
    const uint32_t secsLeft  = (elapsed < kOtaWindowMs) ? (kOtaWindowMs - elapsed) / 1000 : 0;
    if (secsLeft != sOtaDisplayLastSec) {
      sOtaDisplayLastSec = secsLeft;
      M5Cardputer.Display.fillRect(0, 50, 240, 55, BLACK);
      M5Cardputer.Display.setTextSize(2);
      M5Cardputer.Display.setTextColor(ORANGE);
      M5Cardputer.Display.setCursor(5, 53);
      M5Cardputer.Display.printf("OTA Ready  %2us", secsLeft);
      M5Cardputer.Display.setTextSize(1);
      M5Cardputer.Display.setTextColor(WHITE);
      M5Cardputer.Display.setCursor(5, 75);
      M5Cardputer.Display.print("POST /api/v1/ota/update");
      M5Cardputer.Display.setCursor(5, 87);
      M5Cardputer.Display.print(gWifi.getIP());
    }
  } else if (sOtaDisplayLastSec != UINT32_MAX) {
    sOtaDisplayLastSec = UINT32_MAX;   // clear banner on close
    M5Cardputer.Display.fillRect(0, 50, 240, 55, BLACK);
  }

  // ── A-4: OTA rollback timer ─────────────────────────────────────────────────
  // If OTA was applied but user never pressed 'O' to confirm within 60 s,
  // revert to the previous firmware partition and restart.
  if (sOtaRollbackPending && (millis() - sOtaBootMs > kOtaRollbackMs)) {
    gLogger.warning("OTA", "Rollback timeout \u2014 reverting to previous firmware");
    gNVS.clearOtaMeta();
    strlcpy(gRtcBootReason, "ota_rollback", sizeof(gRtcBootReason));
    // Revert boot partition to OTA_0 (the known-good slot before our upload).
    // We always upload to OTA_1 (app1), so rolling back means forcing OTA_0.
    const esp_partition_t* app0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    if (app0) {
      esp_ota_set_boot_partition(app0);
    }
    esp_restart();
  }

  delay(10);
}