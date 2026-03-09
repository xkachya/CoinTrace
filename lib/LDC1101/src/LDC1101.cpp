// LDC1101.cpp - Texas Instruments LDC1101 Inductive Sensor Driver Implementation
// Part of CoinTrace - Open Source Inductive Coin Analyzer
// License: GPL v3
// Repository: https://github.com/xkachya/CoinTrace

#include "LDC1101.h"

LDC1101::LDC1101(uint8_t i2c_addr) : _i2c_addr(i2c_addr), _frequency(0) {
  // Constructor
}

bool LDC1101::begin(int sda_pin, int scl_pin, uint32_t frequency) {
  // Initialize I2C
  Wire.begin(sda_pin, scl_pin);
  Wire.setClock(400000); // 400 kHz I2C speed
  
  _frequency = frequency;
  
  // TODO: Implement LDC1101 initialization sequence
  // 1. Check sensor presence via I2C
  // 2. Reset sensor
  // 3. Configure frequency
  // 4. Configure measurement range
  // 5. Enable sensor
  
  #ifdef LDC1101_DEBUG
  Serial.println("[LDC1101] Initialization started");
  Serial.printf("[LDC1101] I2C Address: 0x%02X\n", _i2c_addr);
  Serial.printf("[LDC1101] Target Frequency: %d Hz\n", frequency);
  #endif
  
  // Test I2C connection
  Wire.beginTransmission(_i2c_addr);
  uint8_t error = Wire.endTransmission();
  
  if (error == 0) {
    #ifdef LDC1101_DEBUG
    Serial.println("[LDC1101] Sensor found on I2C bus");
    #endif
    
    // Set frequency
    setFrequency(frequency);
    
    return true;
  } else {
    #ifdef LDC1101_DEBUG
    Serial.printf("[LDC1101] ERROR: Sensor not found (I2C error %d)\n", error);
    #endif
    return false;
  }
}

uint16_t LDC1101::readRP() {
  // Read RP (proximity) data from registers 0x21 (MSB) and 0x22 (LSB)
  return readRegister16(LDC1101_REG_RP_DATA_MSB);
}

uint16_t LDC1101::readL() {
  // Read L (inductance) data from registers 0x23 (MSB) and 0x24 (LSB)
  return readRegister16(LDC1101_REG_L_DATA_MSB);
}

bool LDC1101::dataReady() {
  // Check status register bit for data ready
  uint8_t status = getStatus();
  return (status & 0x01); // Bit 0 indicates data ready
}

uint8_t LDC1101::getStatus() {
  return readRegister(LDC1101_REG_STATUS);
}

bool LDC1101::setFrequency(uint32_t frequency) {
  _frequency = frequency;
  
  // TODO: Calculate frequency register value from Hz
  // According to LDC1101 datasheet, frequency is set via dividers
  
  #ifdef LDC1101_DEBUG
  Serial.printf("[LDC1101] Setting frequency to %d Hz\n", frequency);
  #endif
  
  // For now, use default 5 MHz configuration
  // This requires calculating proper divider values from datasheet
  
  return true;
}

// ═══════════════════════════════════════════════════════════
// Private methods - I2C register access
// ═══════════════════════════════════════════════════════════

void LDC1101::writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(_i2c_addr);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
  
  #ifdef LDC1101_DEBUG
  Serial.printf("[LDC1101] Write register 0x%02X = 0x%02X\n", reg, value);
  #endif
}

uint8_t LDC1101::readRegister(uint8_t reg) {
  Wire.beginTransmission(_i2c_addr);
  Wire.write(reg);
  Wire.endTransmission(false); // Keep bus active
  
  Wire.requestFrom(_i2c_addr, (uint8_t)1);
  if (Wire.available()) {
    uint8_t value = Wire.read();
    
    #ifdef LDC1101_DEBUG
    Serial.printf("[LDC1101] Read register 0x%02X = 0x%02X\n", reg, value);
    #endif
    
    return value;
  }
  
  return 0;
}

uint16_t LDC1101::readRegister16(uint8_t reg_msb) {
  Wire.beginTransmission(_i2c_addr);
  Wire.write(reg_msb);
  Wire.endTransmission(false);
  
  Wire.requestFrom(_i2c_addr, (uint8_t)2);
  if (Wire.available() >= 2) {
    uint8_t msb = Wire.read();
    uint8_t lsb = Wire.read();
    uint16_t value = (msb << 8) | lsb;
    
    #ifdef LDC1101_DEBUG
    Serial.printf("[LDC1101] Read register16 0x%02X = 0x%04X\n", reg_msb, value);
    #endif
    
    return value;
  }
  
  return 0;
}
