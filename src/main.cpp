// CoinTrace - Open Source Inductive Coin Analyzer
// License: GPL v3 (firmware) + CERN OHL v2 (hardware)
// Hardware: M5Stack Cardputer-Adv + LDC1101 (SPI)
// Repository: https://github.com/xkachya/CoinTrace

#include <Arduino.h>
#include <M5Cardputer.h>
#include <Wire.h>
#include <SPI.h>
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
#include "LittleFSManager.h"
#include "LittleFSTransport.h"
#include "MeasurementStore.h"
#include "SDCardManager.h"      // Wave 7 P-4
#include "FingerprintCache.h"   // Wave 7 P-4
#include "StorageManager.h"     // Wave 7 P-5

// ── Logger globals (ініціалізуються першими в setup()) ────────────
static Logger              gLogger;
static SerialTransport     gSerialTransport(Serial, SerialTransport::Format::TEXT, 115200);
static RingBufferTransport gRingTransport(100, /*usePsram=*/false);  // ESP32-S3FN8: NO PSRAM (LA-1)

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
static StorageManager    gStorage(gNVS, gLFS, gSDCard, gFPCache, gMeasStore);  // Wave 7 P-5 — unified Facade
static WiFiManager       gWifi;                    // Wave 8 A-1 — AP/STA management
// LDC1101Plugin is heap-allocated in setup() so PluginSystem::end()
// can safely call delete (ownership contract — PLUGIN_ARCHITECTURE.md §3.1).
// Hold a raw pointer (non-owning) for direct coin-state access in loop().
static LDC1101Plugin*    gLDC = nullptr;

// RTC memory survives esp_restart() — used to pass boot reason into the
// normal Logger pipeline (→ LittleFS log) after a recovery restart.
RTC_DATA_ATTR static char gRtcBootReason[32] = "";

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
  } else {
    gLogger.warning("LFS", "data mount failed — measurements will not persist");
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

  // ── 5. Plugin system ──────────────────────────────────────────
  gLDC = new LDC1101Plugin();           // PluginSystem owns (deletes on end()); gLDC is non-owning
  gPluginSystem.addPlugin(gLDC);
  gPluginSystem.begin(&gCtx);  // calls canInitialize() → initialize() for each plugin

  gLogger.info("System", "CoinTrace ready — %d/%d plugins initialised",
               gPluginSystem.readyCount(), gPluginSystem.pluginCount());

  // ── 6. WiFiManager (Wave 8 A-1) §17.2 [10] ──────────────────────────────
  // begin() blocks ≤10 s in STA mode, then falls back to AP automatically.
  gWifi.begin(gNVS);
  gCtx.wifi = &gWifi;
  gLogger.info("WiFi", "%s — SSID: %s  IP: %s",
               gWifi.isAP() ? "AP mode" : "STA mode",
               gWifi.getSSID(), gWifi.getIP());
  // Show WiFi status in the bottom section of the splash screen.
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(gWifi.isAP() ? CYAN : GREEN);
  M5Cardputer.Display.setCursor(5, 107);
  M5Cardputer.Display.printf("%s  %s", gWifi.getSSID(), gWifi.getIP());
}

void loop() {
  M5Cardputer.update();
  
  // Keyboard event handling
  if (M5Cardputer.Keyboard.isChange()) {
    if (M5Cardputer.Keyboard.isPressed()) {
      // Use reference — keysState() returns KeysState&.
      // Copying by value crashes: the vector copy-ctor reads data()==nullptr
      // when hid_keys/modifier_keys are non-empty but not yet allocated
      // (race with keyboard scanner, or physical-button-only press).
      const Keyboard_Class::KeysState& status = M5Cardputer.Keyboard.keysState();
      
      if (!status.word.empty()) {
        const char key = status.word[0];
        LOG_DEBUG(&gLogger, "Input", "Key: %c (0x%02X)", key, (uint8_t)key);

        if (key == 'w' || key == 'W') {
          // Wave 8 A-1: interactive STA provisioning via keyboard ('W' key).
          // promptSTA() is blocking — reads SSID + password from keyboard,
          // saves to NVS on success, then reconnects.
          gWifi.promptSTA(gNVS);
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

  // ── Wave 7 P-3: Save measurement on COIN_REMOVED ──────────────────
  // COIN_REMOVED is a transient state (1 update() cycle) set after coin lifts.
  // Must be checked immediately after update() to avoid missing the transition.
  if (gLDC && gLDC->isReady() && gLFS.isDataMounted()) {
    if (gLDC->getCoinState() == LDC1101Plugin::CoinState::COIN_REMOVED) {
      ISensorPlugin::SensorData data = gLDC->read();
      if (data.valid && data.value1 > 1.0f) {
        Measurement m = {};
        m.ts        = millis() / 1000;
        m.rp[0]     = data.value1;   // LDC1101 Rp raw code
        m.l[0]      = data.value2;   // LDC1101 L  raw code
        m.pos_count = 1;             // P-3: single position only
        strlcpy(m.metal_code, "UNKN",          sizeof(m.metal_code));
        strlcpy(m.coin_name,  "Unclassified",  sizeof(m.coin_name));
        m.conf = 0.0f;               // classification deferred to P-5+
        if (gMeasStore.save(m)) {
          gLogger.info("Meas", "Saved #%u — RP=%.0f L=%.0f ts=%us",
                       gNVS.getMeasCount() - 1, m.rp[0], m.l[0], m.ts);
        } else {
          gLogger.warning("Meas", "save() failed (RP=%.0f)", m.rp[0]);
        }
      }
    }
  }

  delay(10);
}