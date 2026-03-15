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
#include "LittleFSManager.h"
#include "LittleFSTransport.h"
#include "MeasurementStore.h"

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
// LDC1101Plugin is heap-allocated in setup() so PluginSystem::end()
// can safely call delete (ownership contract — PLUGIN_ARCHITECTURE.md §3.1).
// Hold a raw pointer (non-owning) for direct coin-state access in loop().
static LDC1101Plugin*    gLDC = nullptr;

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
  M5Cardputer.Display.println("Press any key...");
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
  gLogger.info("System", "CPU: %d MHz | Heap: %u B | PSRAM: %u MB",
               ESP.getCpuFreqMHz(), ESP.getFreeHeap(),
               (unsigned int)(ESP.getPsramSize() / 1024 / 1024));
  LOG_DEBUG(&gLogger, "System", "Flash: %u MB",
            (unsigned int)(ESP.getFlashChipSize() / 1024 / 1024));
  // ── 2. Display startup screen ────────────────────────────────
  displayStartupInfo();

  // ── 3. Hardware buses ─────────────────────────────────────────
  // VSPI: SCK=GPIO40, MISO=GPIO39, MOSI=GPIO14
  SPI.begin(40, 39, 14);
  // Grove I2C: SDA=GPIO2, SCL=GPIO1 (available for future sensors)
  Wire.begin(2, 1);

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
    gCtx.storage = &gNVS;
    gLogger.info("NVS", "Ready — meas_count=%u slot=%u",
                 gNVS.getMeasCount(), gNVS.getMeasSlot());
  } else {
    gCtx.storage = nullptr;  // plugins guard with null-check
    gLogger.error("NVS", "begin() failed — storage unavailable (flash corruption?)");
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
  } else {
    gLogger.warning("LFS", "data mount failed — measurements will not persist");
  }

  // ── 5. Plugin system ──────────────────────────────────────────
  gLDC = new LDC1101Plugin();           // PluginSystem owns (deletes on end()); gLDC is non-owning
  gPluginSystem.addPlugin(gLDC);
  gPluginSystem.begin(&gCtx);  // calls canInitialize() → initialize() for each plugin

  gLogger.info("System", "CoinTrace ready — %d/%d plugins initialised",
               gPluginSystem.readyCount(), gPluginSystem.pluginCount());
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

        // Display key on screen
        M5Cardputer.Display.fillRect(0, M5Cardputer.Display.height() - 20,
                                       M5Cardputer.Display.width(), 20, BLACK);
        M5Cardputer.Display.setCursor(10, M5Cardputer.Display.height() - 18);
        M5Cardputer.Display.setTextColor(YELLOW);
        M5Cardputer.Display.printf("Key: %c", key);
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