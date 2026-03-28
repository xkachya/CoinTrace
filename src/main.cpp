// CoinTrace - Open Source Inductive Coin Analyzer
// License: GPL v3 (firmware) + CERN OHL v2 (hardware)
// Hardware: M5Stack Cardputer-Adv + LDC1101 (SPI)
// Repository: https://github.com/xkachya/CoinTrace

#include <Arduino.h>
#include <M5Cardputer.h>
#include <Wire.h>
#include <SPI.h>
#include <esp_ota_ops.h>    // Wave 8 A-4 — OTA partition ops (rollback)
#include <ArduinoJson.h>    // Wave 7 Phase 2 — plugin config load from LittleFS
#include "Logger.h"
#include "SerialTransport.h"
#include "RingBufferTransport.h"
#include "logger_macros.h"
#include "ConfigManager.h"
#include "PluginSystem.h"
#include "PluginContext.h"
#include "LDC1101Plugin.h"
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
// C-5 calibrated for p3 d≈0.6mm protocol (bare coin, 0.6mm base spacer).
// PENDING HW-QS-6: verify with real hardware after first flash.
// All values are static constexpr → tunable in source, no runtime overhead.
static constexpr float QUICK_NOISE_FLOOR_PCT    =  2.0f;   // dRpPct below → signal in noise
static constexpr float QUICK_L_NOISE_FLOOR_CT   =  2.0f;   // |dL_raw| below → noise
static constexpr float QUICK_SILVER_THRESH_PCT  = 40.0f;   // dRpPct > 40% → SILVER
static constexpr float QUICK_COPPER_THRESH_PCT  = 30.0f;   // dRpPct > 30% → COPPER
static constexpr float QUICK_ALUM_THRESH_PCT    = 15.0f;   // dRpPct > 15% → ALUMINIUM
static constexpr float QUICK_FERRO_THRESH_L_RAW = 100.0f;  // dL_raw > 100 ct → ferro (⚠ verify S-5)

// Reset to true when coin is removed → forces full redraw on next placement.
// File-scope so loop() can reset it outside drawQuickScreen() (QUICK_SCREEN_SPEC §5).
static bool sQuickScreenFresh = true;

// Set by doMeasCompute() after showing the result screen — suppresses Quick Screen
// until the coin is physically removed, keeping the result visible.
static bool sResultPending = false;

// Quick Screen metal classification (Phase 1 — threshold-based).
// Phase 2 will call gMatcher.matchQuick() instead, after quick_centroid hw-data.
struct QuickClass {
    const char* label;    // ASCII label for UART log
    const char* display;  // short string for display (ASCII — Cardputer has no Cyrillic font)
};

static QuickClass classifyQuick(float dRpPct, bool isFerro) {
    if (isFerro)                           return {"STEEL",     "STEEL !"};
    if (dRpPct > QUICK_SILVER_THRESH_PCT)  return {"SILVER",    "SILVER" };
    if (dRpPct > QUICK_COPPER_THRESH_PCT)  return {"COPPER",    "COPPER" };
    if (dRpPct > QUICK_ALUM_THRESH_PCT)    return {"ALUMINIUM", "ALUM"   };
    return                                        {"?",         "?"      };
}

// ── C-2 Display Helpers ──────────────────────────────────────────────────────
// drawMeasIdle()       — full-screen idle state (shown after session ends)
// drawQuickScreen()    — live Quick Screen (IDLE + COIN_PRESENT)
// drawMeasStep_full()  — full-screen redraw on each state transition
// drawMeasResult()     — full-screen result view after COMPUTE step
// doMeasCompute()      — runs vector math, FP match, save; called on STEP_DRIFT capture

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
            isFerro ? RED : (dRpPct > QUICK_ALUM_THRESH_PCT ? GREEN : DARKGREY));
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
    M5Cardputer.Display.fillRect(0, 22, 240, 14, BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(dRpPct > QUICK_NOISE_FLOOR_PCT ? YELLOW : DARKGREY);
    M5Cardputer.Display.setCursor(4, 30);
    M5Cardputer.Display.printf("  dRp: %+.1f%%", dRpPct);

    M5Cardputer.Display.fillRect(0, 38, 240, 14, BLACK);
    M5Cardputer.Display.setTextColor(fabsf(dL_raw) > QUICK_L_NOISE_FLOOR_CT ? YELLOW : DARKGREY);
    M5Cardputer.Display.setCursor(4, 46);
    if (lValid) { M5Cardputer.Display.printf("  dL:  %+.0f ct", dL_raw); }
    else        { M5Cardputer.Display.print("  dL:  -- (no CLKIN)"); }
}

static void drawMeasStep_full(const MeasSession& s, uint16_t rpLive = 0) {
    M5Cardputer.Display.fillScreen(BLACK);

    // ── Header ─────────────────────────────────────────────────────────────
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(DARKGREY);
    M5Cardputer.Display.setCursor(2, 2);
    M5Cardputer.Display.print("CoinTrace");

    // ── Step indicator ─────────────────────────────────────────────────────
    // MeasState: STEP_BASE=1 .. STEP_DRIFT=4 map directly to display step 1..4
    const uint8_t idx = (uint8_t)s.state;
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

static void drawMeasResult(const MeasSession& s) {
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
    M5Cardputer.Display.printf("k1=%.3f  k2=%.3f  slope=%.4f",
                               VectorCompute::k1(s.m),
                               VectorCompute::k2(s.m),
                               VectorCompute::slope(s.m));

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

    // ── Footer ─────────────────────────────────────────────────────────────
    M5Cardputer.Display.setTextColor(DARKGREY);
    M5Cardputer.Display.setCursor(5, 95);
    M5Cardputer.Display.printf("Saved #%u", gNVS.getMeasCount() - 1);

    if (s.driftWarn) {
        M5Cardputer.Display.setTextColor(ORANGE);
        M5Cardputer.Display.setCursor(5, 108);
        M5Cardputer.Display.printf("Drift: %.1f%%  (>5%%)  recheck coil",
                                   VectorCompute::driftRatio(s.m) * 100.0f);
    } else {
        M5Cardputer.Display.setTextColor(DARKGREY);
        M5Cardputer.Display.setCursor(5, 108);
        M5Cardputer.Display.print("Remove coin for next measurement");
    }
}

static void doMeasCompute() {
    // ── 1. Drift check (rp[3] vs rp[0]) ───────────────────────────────────
    const float drift = VectorCompute::driftRatio(sMeas.m);
    sMeas.driftWarn   = (drift > VectorCompute::DRIFT_THRESHOLD);
    sMeas.m.pos_count = 4;
    if (sMeas.driftWarn) {
        sMeas.m.conf = 0.0f;
        gLogger.warning("Meas", "Drift %.1f%% > 5%% — conf forced=0", drift * 100.0f);
    }

    // ── 2. Fingerprint vector (log raw values for diagnostics) ───────────────
    gLogger.info("Meas", "Vec: dRp1=%.0f  k1=%.3f  k2=%.3f  slope=%.4f  dL1=%.0f",
                 VectorCompute::dRp1(sMeas.m),
                 VectorCompute::k1(sMeas.m),
                 VectorCompute::k2(sMeas.m),
                 VectorCompute::slope(sMeas.m),
                 VectorCompute::dL1(sMeas.m));

    // ── 3. Fingerprint match via MetalMatcher (skip on drift — unreliable vector) ──
    // matchFull() normalises internally via VectorCompute (ADR-M6).
    // logTopCandidates() emits #1..#4 with per-axis dist breakdown to UART.
    if (gMatcher.isReady() && !sMeas.driftWarn) {
        const MatchResult mr = gMatcher.matchFull(sMeas.m);
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
    drawMeasResult(sMeas);
    sResultPending = true;

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
          gLogger.info("Matcher", "Config loaded — sigma=%.2f weights=[%.1f,%.1f,%.1f,%.1f,%.1f]",
                       gMatcher.config().sigma,
                       gMatcher.config().full_weights[0], gMatcher.config().full_weights[1],
                       gMatcher.config().full_weights[2], gMatcher.config().full_weights[3],
                       gMatcher.config().full_weights[4]);
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
  gLDC = new LDC1101Plugin();           // PluginSystem owns (deletes on end()); gLDC is non-owning
  gPluginSystem.addPlugin(gLDC);
  gPluginSystem.begin(&gCtx);  // calls canInitialize() → initialize() for each plugin

  gLogger.info("System", "CoinTrace ready — %d/%d plugins initialised",
               gPluginSystem.readyCount(), gPluginSystem.pluginCount());

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

  // ── 6. WiFiManager (Wave 8 A-1) §17.2 [10] ──────────────────────────────
  // begin() blocks ≤10 s in STA mode, then falls back to AP automatically.
  LOG_DEBUG(&gLogger, "Heap", "before WiFi: %u B free", (uint32_t)ESP.getFreeHeap());
  gWifi.begin(gNVS);
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
    if (sMeas.state >= MeasState::STEP_BASE && sMeas.state <= MeasState::STEP_DRIFT) {
      if (!gLDC || !gLDC->isReady()) {
        gLogger.warning("Meas", "BtnA: sensor not ready");
      } else {
        ISensorPlugin::SensorData d = gLDC->read();
        if (!d.valid || d.value1 < 1.0f) {
          gLogger.warning("Meas", "BtnA: bad read (RP=%.0f) — retry", d.value1);
        } else {
          switch (sMeas.state) {
            case MeasState::STEP_BASE:
              sMeas.m.rp[0] = d.value1;  sMeas.m.l[0] = d.value2;
              gLogger.info("Meas", "BASE : RP=%.0f  L=%.0f", d.value1, d.value2);
              sMeas.state  = MeasState::STEP_1;
              sMeas.stepMs = millis();
              drawMeasStep_full(sMeas, (uint16_t)d.value1);
              break;
            case MeasState::STEP_1:
              sMeas.m.rp[1] = d.value1;  sMeas.m.l[1] = d.value2;
              gLogger.info("Meas", "1mm  : RP=%.0f  L=%.0f", d.value1, d.value2);
              sMeas.state  = MeasState::STEP_3;
              sMeas.stepMs = millis();
              drawMeasStep_full(sMeas, (uint16_t)d.value1);
              break;
            case MeasState::STEP_3:
              sMeas.m.rp[2] = d.value1;  sMeas.m.l[2] = d.value2;
              gLogger.info("Meas", "2mm  : RP=%.0f  L=%.0f", d.value1, d.value2);
              sMeas.state  = MeasState::STEP_DRIFT;
              sMeas.stepMs = millis();
              drawMeasStep_full(sMeas, (uint16_t)d.value1);
              break;
            case MeasState::STEP_DRIFT:
              sMeas.m.rp[3] = d.value1;
              gLogger.info("Meas", "DRIFT: RP=%.0f", d.value1);
              sMeas.state = MeasState::COMPUTE;
              doMeasCompute();
              break;
            default: break;
          }
        }
      }
    } else if (sMeas.state == MeasState::IDLE &&
               gLDC && gLDC->isReady() &&
               gLDC->getCoinState() == LDC1101Plugin::CoinState::COIN_PRESENT &&
               gLFS.isDataMounted()) {
      // BtnA at IDLE + COIN_PRESENT → start session (same as HTTP POST)
      sMeas = {};
      sMeas.state   = MeasState::STEP_BASE;
      sMeas.stepMs  = millis();
      sMeas.m.ts    = millis() / 1000;
      strlcpy(sMeas.m.metal_code,  "UNKN",                    sizeof(sMeas.m.metal_code));
      strlcpy(sMeas.m.coin_name,   "Unclassified",            sizeof(sMeas.m.coin_name));
      strlcpy(sMeas.m.protocol_id, "p3_MIKROE3240_b06_012mm", sizeof(sMeas.m.protocol_id));
      drawMeasStep_full(sMeas);
      gLogger.info("Meas", "BtnA start: session started (STEP_BASE)");
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
      if (!status.word.empty() || status.enter) {
        const char key = status.word.empty() ? '\r' : status.word[0];
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
            sMeas = {};
            sMeas.state   = MeasState::STEP_BASE;
            sMeas.stepMs  = millis();
            sMeas.m.ts    = millis() / 1000;
            strlcpy(sMeas.m.metal_code,  "UNKN",                    sizeof(sMeas.m.metal_code));
            strlcpy(sMeas.m.coin_name,   "Unclassified",            sizeof(sMeas.m.coin_name));
            strlcpy(sMeas.m.protocol_id, "p3_MIKROE3240_b06_012mm", sizeof(sMeas.m.protocol_id));
            drawMeasStep_full(sMeas);
            gLogger.info("Meas", "M/Enter: session started (STEP_BASE)");
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
                    sMeas.m.rp[0] = d.value1;  sMeas.m.l[0] = d.value2;
                    gLogger.info("Meas", "BASE : RP=%.0f  L=%.0f", d.value1, d.value2);
                    sMeas.state  = MeasState::STEP_1;
                    sMeas.stepMs = millis();
                    drawMeasStep_full(sMeas, (uint16_t)d.value1);
                    break;
                  case MeasState::STEP_1:
                    sMeas.m.rp[1] = d.value1;  sMeas.m.l[1] = d.value2;
                    gLogger.info("Meas", "1mm  : RP=%.0f  L=%.0f", d.value1, d.value2);
                    sMeas.state  = MeasState::STEP_3;
                    sMeas.stepMs = millis();
                    drawMeasStep_full(sMeas, (uint16_t)d.value1);
                    break;
                  case MeasState::STEP_3:
                    sMeas.m.rp[2] = d.value1;  sMeas.m.l[2] = d.value2;
                    gLogger.info("Meas", "2mm  : RP=%.0f  L=%.0f", d.value1, d.value2);
                    sMeas.state  = MeasState::STEP_DRIFT;
                    sMeas.stepMs = millis();
                    drawMeasStep_full(sMeas, (uint16_t)d.value1);
                    break;
                  case MeasState::STEP_DRIFT:
                    sMeas.m.rp[3] = d.value1;
                    gLogger.info("Meas", "DRIFT: RP=%.0f", d.value1);
                    sMeas.state = MeasState::COMPUTE;
                    doMeasCompute();
                    break;
                  default: break;
                }
              }
            }
          }
        } else if (key == '\b') {
          // ── BACKSPACE: abort measurement session ──────────────────────────
          if (sMeas.state != MeasState::IDLE) {
            gLogger.info("Meas", "Session aborted (Bksp)");
            sMeas = {};
            drawMeasIdle();
          }
        } else if ((key == 'r' || key == 'R') && sMeas.state == MeasState::IDLE) {
          // ── R: recalibrate no-coin baseline (QUICK_SCREEN_SPEC.md §6) ─────
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
  gPluginSystem.update();

  // ── One-time diagnostic: LFS task stack watermark ────────────────────────────
  // Logged once after 10 s so task has processed all boot-log entries.
  // Stack history: 4096 B (2026-03-18) → 3072 B (watermark 1332 B) → 3584 B (2026-03-24,
  // watermark 308 B was too thin; +512 B → 820 B headroom, hw-verified).
  // If this log shows < 512 B free → increase stack in LittleFSTransport.cpp.
  static bool sDiagLogged = false;
  if (!sDiagLogged && millis() > 10000) {
      sDiagLogged = true;
      LOG_DEBUG(&gLogger, "Stack", "LFS task watermark: %u B free (of 3584 B stack)",
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
        sMeas = {};
        sMeas.state   = MeasState::STEP_BASE;
        sMeas.stepMs  = millis();
        sMeas.m.ts    = millis() / 1000;
        strlcpy(sMeas.m.metal_code,  "UNKN",                sizeof(sMeas.m.metal_code));
        strlcpy(sMeas.m.coin_name,   "Unclassified",        sizeof(sMeas.m.coin_name));
        strlcpy(sMeas.m.protocol_id, "p3_MIKROE3240_b06_012mm", sizeof(sMeas.m.protocol_id));
        drawMeasStep_full(sMeas);
        gLogger.info("Meas", "HTTP start: session started (STEP_BASE)");
      }
    }

    // ── Quick Screen — IDLE + live coin detection (Wave 8 C-7b) ──────────────
    // QUICK_SCREEN_SPEC.md §2: no new MeasState — visual mode inside IDLE.
    // drawQuickScreen() rate-limits to 250 ms internally and does early return
    // when nothing changed — no busy-loop rendering, no flicker.
    if (sMeas.state == MeasState::IDLE) {
      static LDC1101Plugin::CoinState sPrevCoinState = LDC1101Plugin::CoinState::IDLE_NO_COIN;
      if (coinState == LDC1101Plugin::CoinState::COIN_PRESENT) {
        if (!sResultPending) {
          // Normal Quick Screen — no recent full measurement
          drawQuickScreen(gLDC->getLiveRp(), gLDC->getLiveL(),
                          gLDC->getBaseline(), gLDC->getLBaseline());
          if (sPrevCoinState != LDC1101Plugin::CoinState::COIN_PRESENT) {
            const float basRp  = gLDC->getBaseline();
            const float liveRp = gLDC->getLiveRp();
            const float dRpPct = (basRp > 1.0f) ? (basRp - liveRp) / basRp * 100.0f : 0.0f;
            gLogger.info("Meas", "QuickScreen ON: basRp=%.0f liveRp=%.0f dRp=%+.1f%%",
                         basRp, liveRp, dRpPct);
          }
        }
        // else: result screen is showing — do nothing until coin removed
      } else {
        if (sPrevCoinState == LDC1101Plugin::CoinState::COIN_PRESENT) {
          // Coin removed — clear result (if any) and restore idle screen
          if (sResultPending) {
            sResultPending    = false;
            gLogger.info("Meas", "Result dismissed: coin removed");
          }
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

    // ── Per-step 120-second timeout ────────────────────────────────────────
    if (sMeas.state >= MeasState::STEP_BASE && sMeas.state <= MeasState::STEP_DRIFT) {
      if (millis() - sMeas.stepMs > 120000UL) {
        gLogger.warning("Meas", "Step %u timeout — session aborted", (uint8_t)sMeas.state);
        sMeas = {};
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