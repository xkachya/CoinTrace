// CoinTrace - Open Source Inductive Coin Analyzer
// License: GPL v3 (firmware) + CERN OHL v2 (hardware)
// Hardware: M5Stack Cardputer-Adv + LDC1101
// Repository: https://github.com/xkachya/CoinTrace

#include <Arduino.h>
#include <M5Cardputer.h>

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
  
  // Initialize serial for debugging
  Serial.begin(115200);
  delay(100);
  
  #ifdef DEBUG_MODE
  Serial.println("\n=== CoinTrace Development Build ===");
  Serial.printf("Version: %s\n", COINTRACE_VERSION);
  Serial.printf("CPU Frequency: %d MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("Flash Size: %d MB\n", ESP.getFlashChipSize() / 1024 / 1024);
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
  #ifdef BOARD_HAS_PSRAM
  Serial.printf("PSRAM Size: %d MB\n", ESP.getPsramSize() / 1024 / 1024);
  #endif
  #endif
  
  // Display startup screen
  displayStartupInfo();
  
  #ifdef LDC1101_DEBUG
  Serial.println("\n=== LDC1101 Configuration ===");
  Serial.printf("I2C SDA: GPIO %d\n", LDC1101_I2C_SDA);
  Serial.printf("I2C SCL: GPIO %d\n", LDC1101_I2C_SCL);
  Serial.printf("I2C Address: 0x%02X\n", LDC1101_I2C_ADDR);
  Serial.printf("Operating Frequency: %d Hz\n", SINGLE_FREQUENCY);
  Serial.printf("Distance Steps: %d\n", DISTANCE_STEPS);
  #endif
  
  // TODO: Initialize LDC1101 driver
  // Wire.begin(LDC1101_I2C_SDA, LDC1101_I2C_SCL);
  // ldc1101.begin(LDC1101_I2C_ADDR);
  
  Serial.println("\nCoinTrace initialized. Ready!");
}

void loop() {
  M5Cardputer.update();
  
  // Keyboard event handling
  if (M5Cardputer.Keyboard.isChange()) {
    if (M5Cardputer.Keyboard.isPressed()) {
      Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
      
      #ifdef SERIAL_DEBUG
      Serial.printf("Key pressed: %c (0x%02X)\n", status.word[0], status.word[0]);
      #endif
      
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