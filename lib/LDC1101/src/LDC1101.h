// LDC1101.h - Texas Instruments LDC1101 Inductive Sensor Driver
// Part of CoinTrace - Open Source Inductive Coin Analyzer
// License: GPL v3
// Repository: https://github.com/xkachya/CoinTrace

#ifndef LDC1101_H
#define LDC1101_H

#include <Arduino.h>
#include <Wire.h>

// LDC1101 I2C default address
#define LDC1101_I2C_DEFAULT_ADDR 0x2A

// LDC1101 Register addresses (Texas Instruments datasheet)
#define LDC1101_REG_RP_MAX       0x01
#define LDC1101_REG_RP_MIN       0x02
#define LDC1101_REG_SENSOR_FREQ  0x03
#define LDC1101_REG_LHR_CONFIG   0x04
#define LDC1101_REG_STATUS       0x20
#define LDC1101_REG_RP_DATA_MSB  0x21
#define LDC1101_REG_RP_DATA_LSB  0x22
#define LDC1101_REG_L_DATA_MSB   0x23
#define LDC1101_REG_L_DATA_LSB   0x24

/**
 * @brief LDC1101 Inductive Sensor Driver Class
 * 
 * This driver controls the Texas Instruments LDC1101 inductive-to-digital
 * converter used for coin metal analysis in CoinTrace.
 */
class LDC1101 {
public:
  /**
   * @brief Constructor
   * @param i2c_addr I2C address of LDC1101 (default 0x2A)
   */
  LDC1101(uint8_t i2c_addr = LDC1101_I2C_DEFAULT_ADDR);
  
  /**
   * @brief Initialize LDC1101 sensor
   * @param sda_pin I2C SDA pin
   * @param scl_pin I2C SCL pin
   * @param frequency Operating frequency (Hz, typically 5 MHz)
   * @return true if initialization successful
   */
  bool begin(int sda_pin, int scl_pin, uint32_t frequency);
  
  /**
   * @brief Read RP (proximity) value from sensor
   * @return 16-bit RP value (0-65535)
   */
  uint16_t readRP();
  
  /**
   * @brief Read L (inductance) value from sensor
   * @return 16-bit inductance value (0-65535)
   */
  uint16_t readL();
  
  /**
   * @brief Check if sensor is ready for reading
   * @return true if data ready
   */
  bool dataReady();
  
  /**
   * @brief Get sensor status register
   * @return Status byte
   */
  uint8_t getStatus();
  
  /**
   * @brief Set sensor frequency
   * @param frequency Frequency in Hz (1-10 MHz)
   * @return true if set successfully
   */
  bool setFrequency(uint32_t frequency);

private:
  uint8_t _i2c_addr;
  uint32_t _frequency;
  
  /**
   * @brief Write register via I2C
   * @param reg Register address
   * @param value Value to write
   */
  void writeRegister(uint8_t reg, uint8_t value);
  
  /**
   * @brief Read register via I2C
   * @param reg Register address
   * @return Register value
   */
  uint8_t readRegister(uint8_t reg);
  
  /**
   * @brief Read 16-bit value from two consecutive registers
   * @param reg_msb MSB register address
   * @return 16-bit value
   */
  uint16_t readRegister16(uint8_t reg_msb);
};

#endif // LDC1101_H
