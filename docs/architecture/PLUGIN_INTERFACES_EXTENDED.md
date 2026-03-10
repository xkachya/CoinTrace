# Розширені інтерфейси плагінів CoinTrace

**Документ:** Повний список інтерфейсів для всіх типів залізя  
**Версія:** 1.1.0  
**Дата:** 10 березня 2026  
**Статус:** 📝 Проектування (розширення базової архітектури)

---

## 🎯 Філософія: Plugin System для ВСЬОГО заліза

**Концепція:** Будь-яка апаратна компонента = плагін

```
┌──────────────────────────────────────────┐
│         IPlugin (базовий)                │
│  ✓ Універсальний lifecycle               │
│  ✓ Метадані (ім'я, версія, автор)       │
│  ✓ Статус (ready, enabled)               │
└──────────────────────────────────────────┘
              ↓ extends
    ┌─────────────────────────┐
    │   Спеціалізовані типи   │
    └─────────────────────────┘
              ↓
┌─────────────────────────────────────────────────┐
│ - ISensorPlugin      (датчики)                  │
│ - IDisplayPlugin     (екрани)                   │
│ - IInputPlugin       (введення: клавіатура, тач)│
│ - IAudioPlugin       (аудіо: codec, speaker)    │
│ - IStoragePlugin     (пам'ять: SD, EEPROM)     │
│ - IIMUPlugin         (IMU, акселерометр, гіро) │
│ - IConnectivityPlugin (WiFi, BLE, LoRa)        │
│ - IPowerPlugin       (батарея, живлення)        │
│ - IPeripheralPlugin  (LED, IR, GPIO)           │
└─────────────────────────────────────────────────┘
```

---

## 🔍 1. ISensorPlugin - Розширений

### Повний список типів сенсорів для нумізматики

```cpp
// include/ISensorPlugin.h

class ISensorPlugin : public IPlugin {
public:
    enum class SensorType {
        // === Основні для нумізматики ===
        INDUCTIVE,          // LDC1101, LDC1612 - індуктивність (метал)
        WEIGHT,             // HX711, NAU7802 - вага монети
        MAGNETIC,           // QMC5883L, BMM150 - феромагнетизм
        DIAMETER,           // Caliper sensors - діаметр монети
        THICKNESS,          // Laser/ultrasonic - товщина
        
        // === Додаткові фізичні ===
        CAPACITIVE,         // FDC2214 - ємність
        OPTICAL,            // TCS34725 - колір/відбиття
        ACOUSTIC,           // MAX4466 - звук удару (акустика монети)
        CONDUCTIVITY,       // Електропровідність матеріалу
        POSITION,           // Hall effect, оптичні енкодери - позиція
        
        // === Умови середовища ===
        TEMPERATURE,        // DHT22, BME280 - температура
        HUMIDITY,           // DHT22, BME280 - вологість
        PRESSURE,           // BMP280 - атм. тиск
        LIGHT,              // BH1750 - освітленість
        
        // === Розширення ===
        RFID,               // RC522, PN532 - RFID мітки
        BARCODE,            // Barcode scanner modules
        CUSTOM              // Користувацький тип
    };
    
    virtual SensorType getType() const = 0;
    
    // Метадані типу сенсора
    struct SensorMetadata {
        const char* typeName;       // "Weight Sensor"
        const char* unit;           // "g", "mm", "Gauss", "Ohm"
        float minValue;             // Мінімальне значення
        float maxValue;             // Максимальне значення
        float resolution;           // Роздільна здатність (0.01g)
        uint16_t sampleRate;        // Частота зчитування (Hz)
    };
    
    virtual SensorMetadata getMetadata() const = 0;
    
    // Універсальний інтерфейс зчитування
    struct SensorData {
        float value1;               // Основне значення
        float value2;               // Додаткове (напр. L для LDC1101)
        float confidence;           // 0.0-1.0 впевненість вимірювання
        uint32_t timestamp;         // Час зчитування (ms)
        bool valid;                 // Чи валідне вимірювання
    };
    
    virtual SensorData read() = 0;
    virtual bool calibrate() = 0;
    
    // Розширені можливості
    virtual bool supportsContinuousMode() const { return false; }
    virtual bool startContinuous(uint16_t intervalMs) { return false; }
    virtual bool stopContinuous() { return false; }
};
```

### Приклад: Ваговий сенсор (HX711)

```cpp
// lib/HX711Plugin/HX711Plugin.h

class HX711Plugin : public ISensorPlugin {
private:
    PluginContext* ctx = nullptr;
    const uint8_t DOUT_PIN = 5;
    const uint8_t SCK_PIN = 6;
    HX711 scale;
    float calibrationFactor = 2280.0; // Калібрування
    
public:
    const char* getName() const override { return "HX711"; }
    
    SensorType getType() const override { return SensorType::WEIGHT; }
    
    SensorMetadata getMetadata() const override {
        return {
            .typeName = "Weight Sensor",
            .unit = "g",
            .minValue = 0.0,
            .maxValue = 200.0,      // 200g максимум
            .resolution = 0.01,     // 0.01g точність
            .sampleRate = 10        // 10 Hz
        };
    }
    
    bool canInitialize() override {
        return true;  // GPIO завжди доступні
    }
    
    bool initialize(PluginContext* context) override {
        ctx = context;
        scale.begin(DOUT_PIN, SCK_PIN);  // Налаштування GPIO пінів
        scale.set_scale(calibrationFactor);
        scale.tare();  // Скинути вагу
        if (!scale.is_ready()) {
            ctx->log->error(getName(), "HX711 not responding — check wiring");
            return false;
        }
        ctx->log->info(getName(), "Initialized successfully");
        return true;
    }
    
    SensorData read() override {
        if (!scale.is_ready()) {
            return {0, 0, 0, millis(), false};
        }
        
        float weight = scale.get_units(5);  // Середнє з 5 зчитувань
        
        return {
            .value1 = weight,
            .value2 = 0,
            .confidence = (weight > 0.1) ? 0.95f : 0.1f,
            .timestamp = millis(),
            .valid = true
        };
    }
    
    bool calibrate() override {
        ctx->log->info(getName(), "Place 100g calibration weight...");
        delay(5000);
        
        float rawValue = scale.get_units(10);
        calibrationFactor = rawValue / 100.0;
        scale.set_scale(calibrationFactor);
        
        return true;
    }
};
```

---

## 🖥️ 2. IDisplayPlugin - Новий інтерфейс

### Підтримка різних екранів (ST7789V2, ILI9341, OLED)

```cpp
// include/IDisplayPlugin.h

class IDisplayPlugin : public IPlugin {
public:
    enum class DisplayType {
        TFT_COLOR,          // ST7789V2, ILI9341
        OLED_MONO,          // SSD1306, SSD1322
        OLED_COLOR,         // SSD1351
        EINK,               // E-Paper displays
        LED_MATRIX,         // MAX7219
        CUSTOM
    };
    
    struct DisplayInfo {
        DisplayType type;
        uint16_t width;
        uint16_t height;
        uint8_t colorDepth;     // 1=mono, 16=RGB565, 24=RGB888
        bool touchscreen;
        uint16_t maxFPS;
    };
    
    virtual DisplayType getType() const = 0;
    virtual DisplayInfo getInfo() const = 0;
    
    // Базові операції
    virtual void clear() = 0;
    virtual void setBrightness(uint8_t level) = 0;  // 0-255
    virtual void sleep() = 0;
    virtual void wake() = 0;
    
    // Графіка (базові примітиви)
    virtual void drawPixel(int16_t x, int16_t y, uint32_t color) = 0;
    virtual void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint32_t color) = 0;
    virtual void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color) = 0;
    virtual void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color) = 0;
    virtual void drawText(int16_t x, int16_t y, const char* text, uint32_t color) = 0;
    
    // High-level API (опціонально)
    virtual void* getGraphicsLibrary() { return nullptr; }  // M5GFX, TFT_eSPI, Adafruit_GFX
};
```

### Приклад: ST7789V2 Plugin (M5Cardputer)

```cpp
// lib/ST7789V2Plugin/ST7789V2Plugin.h

class ST7789V2Plugin : public IDisplayPlugin {
private:
    PluginContext* ctx = nullptr;
    M5GFX* display;  // Використовуємо M5GFX
    
public:
    const char* getName() const override { return "ST7789V2"; }
    
    DisplayType getType() const override { return DisplayType::TFT_COLOR; }
    
    DisplayInfo getInfo() const override {
        return {
            .type = DisplayType::TFT_COLOR,
            .width = 240,
            .height = 135,
            .colorDepth = 16,       // RGB565
            .touchscreen = false,
            .maxFPS = 60
        };
    }
    
    bool canInitialize() override {
        return true;  // Дисплей завжди присутній на M5Cardputer
    }
    
    bool initialize(PluginContext* context) override {
        ctx = context;
        display = &M5Cardputer.Display;
        // ✅ M5Cardputer.begin() вже ініціалізував дисплей
        display->setRotation(1);  // Landscape
        display->setBrightness(128);
        ctx->log->info(getName(), "ST7789V2 initialized");
        return true;
    }
    
    void clear() override {
        display->fillScreen(TFT_BLACK);
    }
    
    void setBrightness(uint8_t level) override {
        display->setBrightness(level);
    }
    
    void drawText(int16_t x, int16_t y, const char* text, uint32_t color) override {
        display->setCursor(x, y);
        display->setTextColor(color);
        display->print(text);
    }
    
    void* getGraphicsLibrary() override {
        return (void*)display;  // Повертаємо вказівник на M5GFX для advanced API
    }
};
```

---

## ⌨️ 3. IInputPlugin - Клавіатури, тачскрін

```cpp
// include/IInputPlugin.h

class IInputPlugin : public IPlugin {
public:
    enum class InputType {
        KEYBOARD,           // QWERTY, numeric
        TOUCHSCREEN,        // Resistive, capacitive
        BUTTON,             // Physical buttons
        ROTARY_ENCODER,     // Encoder з кнопкою
        JOYSTICK,           // Analog joystick
        CUSTOM
    };
    
    struct InputEvent {
        enum Type { PRESS, RELEASE, HOLD, MOVE } type;
        uint32_t timestamp;
        union {
            struct { char key; } keyboard;
            struct { int16_t x, y; } touch;
            struct { uint8_t button; } button;
            struct { int16_t delta; } encoder;
        };
    };
    
    virtual InputType getType() const = 0;
    virtual bool hasEvent() = 0;
    virtual InputEvent getEvent() = 0;
    virtual void clearEvents() = 0;
};
```

---

## 🔊 4. IAudioPlugin - Аудіо кодеки

```cpp
// include/IAudioPlugin.h

class IAudioPlugin : public IPlugin {
public:
    enum class AudioType {
        CODEC,              // ES8311, WM8960
        SPEAKER_ONLY,       // NS4150B amp
        MICROPHONE_ONLY,    // MEMS mic
        I2S_PERIPHERAL,     // External I2S
        CUSTOM
    };
    
    virtual AudioType getType() const = 0;
    
    // Playback
    virtual bool beep(uint16_t frequencyHz, uint16_t durationMs) = 0;
    virtual bool playTone(uint16_t frequency) = 0;
    virtual bool stopTone() = 0;
    
    // Recording (для acoustic sensor)
    virtual bool startRecording(uint32_t sampleRate) = 0;
    virtual bool stopRecording() = 0;
    virtual int16_t* getAudioBuffer(size_t* length) = 0;
    
    // Volume
    virtual void setVolume(uint8_t level) = 0;  // 0-100
};
```

---

## 🔌 5. Повний список інтерфейсів

### Базова екосистема

| Інтерфейс | Призначення | Приклади заліза |
|-----------|-------------|-----------------|
| **ISensorPlugin** | Датчики вимірювання | LDC1101, HX711, QMC5883L, BH1750 |
| **IDisplayPlugin** | Екрани виводу | ST7789V2, SSD1306, ILI9341 |
| **IInputPlugin** | Пристрої введення | TCA8418 keyboard, FT6336 touch |
| **IAudioPlugin** | Аудіо пристрої | ES8311 codec, buzzer |
| **IStoragePlugin** | Сховища даних | SD card, EEPROM, LittleFS |
| **IIMUPlugin** | Інерційні датчики | BMI270, MPU6050, LSM6DS3 |

### Розширена екосистема

| Інтерфейс | Призначення | Приклади заліза |
|-----------|-------------|-----------------|
| **IConnectivityPlugin** | Бездротові модулі | WiFi, BLE, LoRa, NFC |
| **IPowerPlugin** | Живлення та батарея | AXP192, IP5306, battery monitor |
| **IPeripheralPlugin** | Периферія | LED, IR, servo, relay |
| **ICameraPlugin** | Камери | OV2640, OV5640 для ML розпізнавання |

---

## 🎯 Використання в CoinTrace

### Приклад: Комплексна система ідентифікації монет

```cpp
// ✅ Отримуємо плагіни через PluginSystem (не глобальні змінні)
class CoinAnalyzer {
    PluginSystem& plugins;
    CoinDatabase& db;

public:
    explicit CoinAnalyzer(PluginSystem& ps, CoinDatabase& coinDb)
        : plugins(ps), db(coinDb) {}

    void analyzeCoin() {
        // 1. Отримати всі активні сенсори
        auto weightSensor   = plugins.getPlugin<ISensorPlugin>("HX711");
        auto inductiveSensor = plugins.getPlugin<ISensorPlugin>("LDC1101");
        auto magneticSensor  = plugins.getPlugin<ISensorPlugin>("QMC5883L");
        auto diameterSensor  = plugins.getPlugin<ISensorPlugin>("Caliper");
        auto display         = plugins.getPlugin<IDisplayPlugin>("ST7789V2");
        auto audio           = plugins.getPlugin<IAudioPlugin>("Buzzer");
        auto storage         = plugins.getPlugin<IStoragePlugin>("SPIFFS");

        // 2. Перевірити рівень (IMU)
        auto imu = plugins.getPlugin<IIMUPlugin>("BMI270");
        if (!imu->isLevel(0.05)) {
            display->drawText(10, 60, "⚠️ Вирівняйте пристрій!", TFT_YELLOW);
            return;
        }
        
        // 3. Зчитати всі параметри монети
        CoinParameters coin;
        coin.weight    = weightSensor->read().value1;      // Вага (g)
        coin.inductance = inductiveSensor->read().value1;  // RP (Ohm)
        coin.magnetic  = magneticSensor->read().value1;    // Gauss
        coin.diameter  = diameterSensor->read().value1;    // mm
        
        // 4. Ідентифікувати монету
        CoinIdentity result = db.identify(coin);
        
        // 5. Показати результат
        display->clear();
        display->drawText(10, 10, result.name, TFT_WHITE);
        display->drawText(10, 30, result.year, TFT_CYAN);
        display->drawText(10, 50, result.metal, TFT_GREEN);
        
        // 6. Звуковий сигнал
        if (result.confidence > 0.9) {
            audio->beep(1000, 200);  // 1kHz, 200ms
        }
        
        // 7. Зберегти вимірювання
        storage->save("last_measurement", &coin, sizeof(coin));
    }
};
```

---

## 🔄 Міграція між платформами

### Cardputer → CoreS3 (приклад зміни заліза)

**M5Cardputer-ADV:**
```json
{
  "hardware": "M5Cardputer-ADV",
  "plugins": [
    {"type": "display", "name": "ST7789V2", "enabled": true},
    {"type": "input", "name": "TCA8418Keyboard", "enabled": true},
    {"type": "audio", "name": "ES8311", "enabled": true},
    {"type": "imu", "name": "BMI270", "enabled": true}
  ]
}
```

**M5Stack CoreS3:**
```json
{
  "hardware": "M5CoreS3",
  "plugins": [
    {"type": "display", "name": "ILI9342C", "enabled": true},
    {"type": "input", "name": "FT6336Touch", "enabled": true},
    {"type": "audio", "name": "AW88298", "enabled": true},
    {"type": "imu", "name": "BMI270", "enabled": true}
  ]
}
```

**Результат:** Код залишається незмінним! Система автоматично завантажує правильні плагіни.

---

## 📊 Статистика типів сенсорів

### Для професійної нумізматики

| Параметр | Тип сенсора | Точність | Важливість |
|----------|-------------|----------|------------|
| **Вага** | HX711 | ±0.01g | ⭐⭐⭐⭐⭐ Критично |
| **Індуктивність** | LDC1101 | ±0.1% | ⭐⭐⭐⭐⭐ Критично |
| **Діаметр** | Caliper | ±0.01mm | ⭐⭐⭐⭐ Дуже важливо |
| **Товщина** | Laser | ±0.01mm | ⭐⭐⭐⭐ Дуже важливо |
| **Магнетизм** | QMC5883L | ±2mG | ⭐⭐⭐ Важливо (феро) |
| **Звук** | MAX4466 | - | ⭐⭐⭐ Додатково |
| **Колір** | TCS34725 | - | ⭐⭐ Опціонально |

**Мінімальна система:** Вага + Індуктивність = 70% точності ідентифікації  
**Професійна система:** Вага + Індуктивність + Діаметр + Товщина = 95%+ точності

---

## 🎓 Висновки

### ✅ Розширена архітектура дозволяє:

1. **Універсальність** - Будь-яке залізо = плагін
2. **Гнучкість** - 15+ типів сенсорів (вага, магнетизм, діаметр, звук, колір...)
3. **Масштабованість** - Легко додати новий тип (ambient light, radiation, vibration)
4. **Міграція** - CoreS3 має інший дисплей? Не проблема!
5. **Композиція** - Комбінуй різні сенсори для точності

### 🔮 Майбутні розширення:

- **INetworkPlugin** - WiFi sync, cloud backup
- **IMLPlugin** - TensorFlow Lite для ML розпізнавання
- **IProtocolPlugin** - UART, I2C, SPI абстракції
- **IUIPlugin** - UI themes, layouts

---

**Статус:** 📝 Розширення базової архітектури  
**Дата:** 10 березня 2026  
**Наступний крок:** Імплементація базових інтерфейсів (IPlugin.h, ISensorPlugin.h, IDisplayPlugin.h)
