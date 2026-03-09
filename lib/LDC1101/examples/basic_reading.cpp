// Example 1: Basic LDC1101 Sensor Reading
// This example demonstrates how to initialize LDC1101 and read sensor data

#include <Arduino.h>
#include <M5Cardputer.h>
#include "LDC1101.h"

// Create LDC1101 sensor instance
LDC1101 sensor(0x2A);  // Default I2C address

void setup() {
  // Initialize M5Cardputer
  auto cfg = M5.config();
  M5Cardputer.begin(cfg);
  M5Cardputer.Display.setRotation(1);
  
  Serial.begin(115200);
  Serial.println("CoinTrace - LDC1101 Basic Example");
  
  // Initialize LDC1101 sensor
  // Parameters: SDA pin, SCL pin, frequency (5 MHz)
  if (sensor.begin(2, 1, 5000000)) {
    Serial.println("LDC1101 initialized successfully!");
    
    M5Cardputer.Display.println("LDC1101 Ready");
  } else {
    Serial.println("ERROR: Failed to initialize LDC1101");
    
    M5Cardputer.Display.setTextColor(RED);
    M5Cardputer.Display.println("Sensor ERROR!");
  }
}

void loop() {
  M5Cardputer.update();
  
  // Check if sensor data is ready
  if (sensor.dataReady()) {
    // Read RP (proximity) and L (inductance) values
    uint16_t rp_value = sensor.readRP();
    uint16_t l_value = sensor.readL();
    
    // Display on serial monitor
    Serial.printf("RP: %5d  |  L: %5d\n", rp_value, l_value);
    
    // Display on screen
    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setCursor(10, 10);
    M5Cardputer.Display.setTextColor(GREEN);
    M5Cardputer.Display.println("CoinTrace Sensor");
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.printf("RP: %d\n", rp_value);
    M5Cardputer.Display.printf("L:  %d\n", l_value);
  }
  
  delay(100);  // Read every 100ms
}
