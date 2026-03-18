// CoinTrace - Open Source Inductive Coin Analyzer
// License: GPL v3 (firmware) + CERN OHL v2 (hardware)
// Hardware: M5Stack Cardputer-Adv + LDC1101 (SPI)
// Repository: https://github.com/xkachya/CoinTrace

#include <Arduino.h>
#include <M5Cardputer.h>
#include <Wire.h>
#include <SPI.h>
#include <esp_ota_ops.h>    // Wave 8 A-4 — OTA partition ops (rollback)
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
  // Stack was reduced to 3072 B based on 2026-03-18 measurement (watermark=1332B free).
  // If this log shows < 512 B free → increase stack in LittleFSTransport.cpp.
  static bool sDiagLogged = false;
  if (!sDiagLogged && millis() > 10000) {
      sDiagLogged = true;
      LOG_DEBUG(&gLogger, "Stack", "LFS task watermark: %u B free (of 3072 B stack)",
                gLfsTransport.stackWatermarkBytes());
  }

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