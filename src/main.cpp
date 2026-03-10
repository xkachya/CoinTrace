// CoinTrace - Open Source Inductive Coin Analyzer
// License: GPL v3 (firmware) + CERN OHL v2 (hardware)
// Hardware: M5Stack Cardputer-Adv + LDC1101 (SPI)
// Repository: https://github.com/xkachya/CoinTrace

#include <Arduino.h>
#include <M5Cardputer.h>
#include "Logger.h"
#include "SerialTransport.h"
#include "RingBufferTransport.h"
#include "logger_macros.h"

// ── Logger globals (ініціалізуються першими в setup()) ────────────
static Logger              gLogger;
static SerialTransport     gSerialTransport(Serial, SerialTransport::Format::TEXT, 115200);
static RingBufferTransport gRingTransport(100, /*usePsram=*/true);

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
  M5Cardputer.Display.println("- LDC1101 sensor");
  M5Cardputer.Display.print("  Frequency: ");
  M5Cardputer.Display.print(SINGLE_FREQUENCY / 1000000);
  M5Cardputer.Display.println(" MHz");
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
  LOG_DEBUG(&gLogger, "LDC1101", "I2C SDA=%d SCL=%d addr=0x%02X freq=%dHz steps=%d",
            LDC1101_I2C_SDA, LDC1101_I2C_SCL, LDC1101_I2C_ADDR,
            SINGLE_FREQUENCY, DISTANCE_STEPS);

  // ── 2. Display startup screen ────────────────────────────────
  displayStartupInfo();

  // ── 3. TODO: Hardware init (SPI/I2C + plugin context) ────────
  // SPI.begin(...);
  // Wire.begin(LDC1101_I2C_SDA, LDC1101_I2C_SCL);
  // ctx.log = &gLogger; ctx.spiMutex = xSemaphoreCreateMutex();

  gLogger.info("System", "CoinTrace ready");
}

void loop() {
  M5Cardputer.update();
  
  // Keyboard event handling
  if (M5Cardputer.Keyboard.isChange()) {
    if (M5Cardputer.Keyboard.isPressed()) {
      Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
      
      LOG_DEBUG(&gLogger, "Input", "Key: %c (0x%02X)", status.word[0], status.word[0]);
      
      // Display key on screen
      M5Cardputer.Display.fillRect(0, M5Cardputer.Display.height() - 20, 
                                     M5Cardputer.Display.width(), 20, BLACK);
      M5Cardputer.Display.setCursor(10, M5Cardputer.Display.height() - 18);
      M5Cardputer.Display.setTextColor(YELLOW);
      M5Cardputer.Display.printf("Key: %c", status.word[0]);
    }
  }
  
  // TODO: Main CoinTrace measurement loop
  // - Read LDC1101 sensor data
  // - Process k1/k2 algorithm
  // - Display results on screen
  
  delay(10);
}