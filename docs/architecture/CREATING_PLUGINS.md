# Гайд: Створення плагінів для CoinTrace

**Для розробників:** Як додати свій датчик/модуль без зміни основного коду

---

## 🚀 Quick Start: Ваш перший плагін за 15 хвилин

### Приклад: Додати датчик освітлення BH1750

**1. Створити структуру плагіна (2 хвилини)**

```bash
lib/BH1750Plugin/
├── plugin.json
├── BH1750Plugin.h
└── BH1750Plugin.cpp
```

**2. Описати метадані `plugin.json` (3 хвилини)**

```json
{
  "name": "BH1750",
  "version": "1.0.0",
  "author": "Your Name",
  "description": "Digital light intensity sensor (lux meter)",
  "type": "sensor",
  "i2c_address": "0x23",
  "contract_version": "1.0.0",
  "dependencies": ["Wire"],
  "enabled_by_default": false
}
```

**3. Створити клас `BH1750Plugin.h` (5 хвилин)**

```cpp
#ifndef BH1750_PLUGIN_H
#define BH1750_PLUGIN_H

#include "ISensorPlugin.h"
#include "PluginContext.h"
#include <Wire.h>

class BH1750Plugin : public ISensorPlugin {
private:
    static const uint8_t I2C_ADDR = 0x23;
    PluginContext* ctx = nullptr;  // ✅ Контекст системи
    bool ready = false;
    
public:
    // Метадані
    const char* getName() const override { return "BH1750"; }
    const char* getVersion() const override { return "1.0.0"; }
    const char* getAuthor() const override { return "Your Name"; }
    
    // Перевірка доступності hardware
    bool canInitialize() override {
        // На цьому етапі context ще немає
        // Повертаємо true — перевіримо в initialize()
        return true;
    }
    
    // Ініціалізація
    bool initialize(PluginContext* context) override {
        ctx = context;  // ✅ Зберігаємо контекст
        
        // ✅ Перевірка hardware через вже ініціалізований Wire
        ctx->wire->beginTransmission(I2C_ADDR);
        if (ctx->wire->endTransmission() != 0) {
            ctx->log->error(getName(), "I2C NACK at 0x23 - check wiring");
            return false;
        }
        
        // BH1750 Power On + Continuous High Resolution Mode
        ctx->wire->beginTransmission(I2C_ADDR);
        ctx->wire->write(0x10);
        ctx->wire->endTransmission();
        
        ready = true;
        ctx->log->info(getName(), "Initialized successfully");
        return true;
    }
    
    // Оновлення (якщо потрібно щось робити в loop)
    void update() override {
        // Для BH1750 не потрібно
    }
    
    // Вимкнення
    void shutdown() override {
        if (!ctx) return;  // Захист якщо initialize() не викликався
        
        ctx->wire->beginTransmission(I2C_ADDR);
        ctx->wire->write(0x00); // Power Down
        ctx->wire->endTransmission();
        ready = false;
        
        ctx->log->info(getName(), "Shutdown complete");
    }
    
    // Статус
    bool isEnabled() const override { return ready; }
    bool isReady() const override { return ready; }
    
    // Тип сенсора (PA3-9 fix: CUSTOM→LIGHT — BH1750FVI є optical ambient light sensor)
    SensorType getType() const override { return SensorType::LIGHT; }

    // N-1 fix: поля struct SensorMetadata визначені в INTERFACES_EXTENDED §1:
    // {typeName, unit, minValue, maxValue, resolution, sampleRate}
    SensorMetadata getMetadata() const override {
        return {
            .typeName   = "Light Sensor",
            .unit       = "lux",
            .minValue   = 0.0f,
            .maxValue   = 65535.0f,
            .resolution = 1.0f,
            .sampleRate = 10
        };
    }
    
    // Зчитування даних
    SensorData read() override {
        if (!ready) return {0, 0, 0.0f, millis(), false};
        
        // PA2-17 fix: wireMutex обов'язковий — read() може викликатись з будь-якого task (CONTRACT §2.2)
        if (xSemaphoreTake(ctx->wireMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
            return {0, 0, 0.0f, millis(), false};
        }
        ctx->wire->requestFrom(I2C_ADDR, (uint8_t)2);
        uint16_t raw = 0;
        if (ctx->wire->available() == 2) {
            raw = (ctx->wire->read() << 8) | ctx->wire->read();
        }
        xSemaphoreGive(ctx->wireMutex);
        if (raw == 0) return {0, 0, 0.0f, millis(), false};
        
        float lux = raw / 1.2f; // Convert to lux
        
        return {
            .value1 = lux,
            .value2 = 0,
            .confidence = (lux > 0) ? 0.95f : 0.0f,
            .timestamp = millis(),
            .valid = (lux > 0)
        };
    }
    
    // Калібрування (опціонально)
    bool calibrate() override {
        return true;
    }
};

#endif
```

**4. Додати в конфігурацію `data/config.json` (1 хвилина)**

```json
{
  "plugins": [
    {"type": "sensor", "name": "LDC1101", "enabled": true},
    {"type": "sensor", "name": "BH1750", "enabled": true}  ← ДОДАЛИ!
  ]
}
```

**5. Скомпілювати і запустити (2 хвилини)**

```bash
pio run -e cointrace-dev
pio run -t upload
```

**6. Перевірити в Serial Monitor (2 хвилини)**

```
Loading plugins...
✅ Loaded plugin: LDC1101
✅ Loaded plugin: BH1750         ← Ваш плагін!

Initializing plugins...
Init LDC1101... ✅
Init BH1750... ✅               ← Працює!

=== Plugin Status ===
LDC1101              ✅ 1.0.0
BH1750               ✅ 1.0.0  ← Готово!
====================
```

---

## 📋 Контрольний список (Checklist)

Перед публікацією плагіна переконайтесь:

### Обов'язкові компоненти
- [ ] `plugin.json` - метадані плагіна
- [ ] `*.h` файл - оголошення класу
- [ ] `*.cpp` файл - реалізація (якщо потрібно)
- [ ] Наслідування від правильного інтерфейсу:
  - `ISensorPlugin` для датчиків
  - `IIMUPlugin` для IMU/гіроскопів
  - `IStoragePlugin` для сховищ
  - `IPlugin` для інших модулів

### Реалізовані методи
- [ ] `getName()`, `getVersion()`, `getAuthor()`
- [ ] `canInitialize()` - перевірка hardware
- [ ] `initialize()` - налаштування датчика
- [ ] `update()` - оновлення в loop (може бути порожнім)
- [ ] `shutdown()` - коректне вимкнення
- [ ] `isEnabled()`, `isReady()` - статус

### Тестування
- [ ] Плагін компілюється без помилок
- [ ] `canInitialize()` повертає `true` (перевірка hardware в `initialize()`)
- [ ] `initialize(ctx)` коректно налаштовує датчик
- [ ] `read()` повертає реальні дані
- [ ] `shutdown()` коректно вимикає датчик
- [ ] Плагін не падає якщо hardware відключити під час роботи

### Документація
- [ ] `README.md` в папці плагіна
- [ ] Опис підключення (wiring diagram)
- [ ] Приклад використання
- [ ] Troubleshooting

---

## 🎯 Шаблони плагінів

### Шаблон сенсора I2C

```cpp
class MyI2CSensorPlugin : public ISensorPlugin {
private:
    static const uint8_t I2C_ADDR = 0x??;
    PluginContext* ctx = nullptr;  // ✅ Контекст системи
    bool ready = false;
    
public:
    const char* getName() const override { return "MySensor"; }
    const char* getVersion() const override { return "1.0.0"; }
    const char* getAuthor() const override { return "Your Name"; }
    
    bool canInitialize() override {
        // На цьому етапі context ще немає
        return true;
    }
    
    bool initialize(PluginContext* context) override {
        ctx = context;  // ✅ Зберігаємо контекст
        
        // ✅ Перевірка hardware через ctx->wire (вже ініціалізований!)
        ctx->wire->beginTransmission(I2C_ADDR);
        if (ctx->wire->endTransmission() != 0) {
            ctx->log->error(getName(), "Hardware not found");
            return false;
        }
        
        // TODO: Налаштувати датчик
        ready = true;
        ctx->log->info(getName(), "Initialized");
        return true;
    }
    
    void update() override {}
    void shutdown() override { ready = false; }
    
    bool isEnabled() const override { return ready; }
    bool isReady() const override { return ready; }
    
    SensorType getType() const override { return SensorType::CUSTOM; }  // TODO: замінити на потрібний тип

    // N-1 fix: поля struct SensorMetadata визначені в INTERFACES_EXTENDED §1:
    // {typeName, unit, minValue, maxValue, resolution, sampleRate}
    SensorMetadata getMetadata() const override {
        // TODO: заповнити як відповідає ваш датчик
        return {.typeName = "Custom Sensor", .unit = "units",
                .minValue = 0.0f, .maxValue = 1000.0f, .resolution = 1.0f, .sampleRate = 10};
    }

    SensorData read() override {
        if (!ready) return {0, 0, 0.0f, millis(), false};
        // TODO: Прочитати дані з датчика
        // PA3-13 fix: valid=false до реальної реалізації (було true — false success!)
        return {0, 0, 0.0f, millis(), false};
    }
    
    bool calibrate() override { return true; }
};
```

### Шаблон сенсора SPI

```cpp
class MySPISensorPlugin : public ISensorPlugin {
private:
    PluginContext* ctx = nullptr;
    SPIClass* spi = nullptr;
    uint8_t csPin;
    bool ready = false;
    
public:
    MySPISensorPlugin(uint8_t cs = 5) : csPin(cs) {}
    
    bool canInitialize() override {
        // ✅ Без апаратних шин — ctx ще недоступний
        return true;
    }
    
    bool initialize(PluginContext* context) override {
        ctx = context;
        spi = ctx->spi;  // ✅ SPI вже ініціалізований системою
        // ❌ НЕ викликати spi->begin()!
        // TODO: Налаштувати SPI датчик
        ready = true;
        return true;
    }
    
    SensorData read() override {
        if (!ready) return {0, 0, 0.0f, millis(), false};
        
        // ✅ ADR-ST-008: spiMutex обов'язковий для всіх SPI-транзакцій (SD карта + цей сенсор на одній VSPI)
        if (xSemaphoreTake(ctx->spiMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
            return {0, 0, 0.0f, millis(), false};  // Очікування на mutex істекло
        }
        
        digitalWrite(csPin, LOW);
        // TODO: SPI транзакція
        uint16_t data = spi->transfer16(0x0000);
        digitalWrite(csPin, HIGH);
        
        xSemaphoreGive(ctx->spiMutex);
        
        return {(float)data, 0, 1.0f, millis(), true};
    }
    
    // ... решта методів
};
```

### Шаблон IMU плагіна

```cpp
class MyIMUPlugin : public IIMUPlugin {
private:
    bool ready = false;
    
public:
    IMUData read() override {
        // TODO: Прочитати акселерометр + гіроскоп
        IMUData data;
        data.accelX = 0;
        data.accelY = 0;
        data.accelZ = 1.0; // 1G вниз
        // ... решта
        return data;
    }
    
    bool isLevel(float tolerance) override {
        auto data = read();
        return (abs(data.accelX) < tolerance && 
                abs(data.accelY) < tolerance);
    }
    
    bool calibrate() override {
        // TODO: Калібрування IMU
        // Повернути false якщо hardware не відповів або baseline невалідний
        return true;
    }
    
    // ... базові методи IPlugin
};
```

---

## 🔧 Складні випадки

### 1. Плагін з конфігурацією

**📁 Три рівні конфігурації плагіна (канонічна модель):**

| Файл | Роль | Хто читає |
|------|------|-----------|
| `lib/<Plugin>/plugin.json` | Build-time метадані для IDE/CI — **не у flash** | PlatformIO / IDE |
| `data/plugins/<name>.json` | ✅ Runtime параметри: cs_pin, threshold, calibration | `ctx->config->getInt("name.key", default)` |
| `data/config.json` | ✅ Список активних плагінів + hardware profile | `PluginSystem::loadFromConfig()` |

> `ctx->config->getInt("mysensor.sampleRate", 100)` — читає ключ `mysensor.sampleRate`  
> з файлу **`data/plugins/mysensor.json`** (монтується в LittleFS, не `lib/`).

```cpp
class ConfigurableSensorPlugin : public ISensorPlugin {
private:
    PluginContext* ctx = nullptr;
    struct Config {
        uint8_t sampleRate;
        float threshold;
        bool autoCalibrate;
    } config;
    
    void loadConfig() {
        // ✅ Читання конфігурації через ctx->config
        config.sampleRate   = ctx->config->getInt("mysensor.sampleRate", 100);
        config.threshold    = ctx->config->getFloat("mysensor.threshold", 0.5);
        config.autoCalibrate = ctx->config->getBool("mysensor.autoCalibrate", false);
    }
    
public:
    bool canInitialize() override {
        return true;
    }
    
    bool initialize(PluginContext* context) override {
        ctx = context;
        loadConfig();  // ✅ Тепер ctx доступний
        // Використовувати config.sampleRate, etc.
        return true;
    }
};
```

### 2. Плагін з асинхронним зчитуванням

```cpp
class AsyncSensorPlugin : public ISensorPlugin {
private:
    PluginContext* ctx = nullptr;
    SemaphoreHandle_t dataMutex = nullptr;  // N-8/N-10 fix: явна ініціалізація — nullptr захищає від vTaskDelete(garbage)
    TaskHandle_t readTask = nullptr;        // N-10 fix: без nullptr → shutdown() crash якщо xTaskCreate() fails
    SensorData latestData;
    
    static void readTaskFunc(void* param) {
        auto* self = (AsyncSensorPlugin*)param;
        while (true) {
            // ⚠️ Будь-який доступ до I2C/SPI з async task — брати відповідний mutex!
            // (SPI приклад — для I2C аналогічно використовуй wireMutex)
            if (xSemaphoreTake(self->ctx->spiMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                auto data = self->readFromHardware();
                xSemaphoreGive(self->ctx->spiMutex);
                
                if (xSemaphoreTake(self->dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    self->latestData = data;  // ADR-ST-008: pdMS_TO_TICKS(50), не portMAX_DELAY
                    xSemaphoreGive(self->dataMutex);
                }
            }
            
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    
public:
    bool canInitialize() override {
        return true;
    }
    
    bool initialize(PluginContext* context) override {
        ctx = context;

        // N-8 fix: NULL при нестачі heap → xSemaphoreTake(nullptr,...) в read() → crash
        dataMutex = xSemaphoreCreateMutex();
        if (!dataMutex) {
            ctx->log->error(getName(), "Failed to create data mutex (heap exhausted?)");
            return false;
        }

        // N-7 fix: pdFAIL якщо немає FreeRTOS task slots або heap → initialize() не повинен брехати
        BaseType_t taskResult = xTaskCreate(readTaskFunc, "SensorRead", 4096, this,
                    tskIDLE_PRIORITY + 2,  // ≤ main loop priority; priority 5 може starve loop
                    &readTask);
        if (taskResult != pdPASS) {
            ctx->log->error(getName(), "Failed to create FreeRTOS task (heap full?)");
            vSemaphoreDelete(dataMutex);
            dataMutex = nullptr;
            return false;
        }

        ctx->log->info(getName(), "Async task started");
        return true;
    }
    
    void shutdown() override {
        if (readTask) {
            vTaskDelete(readTask);  // ✅ уникнути task leak + use-after-free при перезавантаженні
            readTask = nullptr;
        }
        if (dataMutex) {
            vSemaphoreDelete(dataMutex);
            dataMutex = nullptr;
        }
    }
    
    SensorData read() override {
        SensorData data;
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            data = latestData;
            xSemaphoreGive(dataMutex);
        } else {
            data = {0, 0, 0.0f, millis(), false};  // ADR-ST-008: timeout
        }
        return data;
    }
};
```

### 3. Плагін з dependency на інший плагін

```cpp
class FusionSensorPlugin : public ISensorPlugin {
private:
    ISensorPlugin* sensor1;
    ISensorPlugin* sensor2;
    
public:
    void setDependencies(ISensorPlugin* s1, ISensorPlugin* s2) {
        sensor1 = s1;
        sensor2 = s2;
    }
    
    SensorData read() override {
        auto data1 = sensor1->read();
        auto data2 = sensor2->read();
        
        // ✅ Перевірити .valid обох джерел перед злиттям.
        // Invalid сенсор повертає value1=0.0f — виглядає як валідний вимір, але є artifacts.
        if (!data1.valid || !data2.valid) {
            return {0, 0, 0.0f, millis(), false};
        }
        
        return {
            .value1     = (data1.value1 + data2.value1) / 2.0f,
            .value2     = 0,
            .confidence = min(data1.confidence, data2.confidence),
            .timestamp  = millis(),
            .valid      = true
        };
    }
};
```

---

## 🐛 Debugging плагінів

### Увімкнути debug logging

```cpp
class MyPlugin : public ISensorPlugin {
private:
    PluginContext* ctx = nullptr;
    
public:
    bool canInitialize() override {
        return true;
    }
    
    bool initialize(PluginContext* context) override {
        ctx = context;
        ctx->log->debug(getName(), "Starting initialization...");
        
        // Перевірка hardware
        ctx->wire->beginTransmission(I2C_ADDR);
        if (ctx->wire->endTransmission() != 0) {
            ctx->log->error(getName(), "ERROR: Hardware not found!");
            return false;
        }
        
        ctx->log->debug(getName(), "Hardware detected");
        // ... налаштування
        ctx->log->info(getName(), "Initialization complete");
        return true;
    }
};
```

### Перевірка через Serial Monitor

```cpp
void testPlugin() {
    auto* plugin = new MyPlugin();
    
    // Мок контексту для тесту
    PluginContext mockCtx = {
        .wire      = &Wire,
        .wireMutex = xSemaphoreCreateMutex(),
        .spi       = &SPI,
        .spiMutex  = xSemaphoreCreateMutex(),
        .config    = nullptr,  // TODO: підключити ConfigManager
        .log       = &testLogger
    };
    
    Serial.println("=== Plugin Test ===");
    Serial.printf("Name: %s\n", plugin->getName());
    Serial.printf("Version: %s\n", plugin->getVersion());
    
    Serial.print("Pre-check... ");
    if (plugin->canInitialize()) {
        Serial.println("✅ OK");
    } else {
        Serial.println("❌ Failed");
        return;
    }
    
    Serial.print("Initialization... ");
    if (plugin->initialize(&mockCtx)) {  // ✅ передаємо PluginContext
        Serial.println("✅ OK");
    } else {
        Serial.println("❌ Failed");
        return;
    }
    
    Serial.println("Reading data:");
    for (int i = 0; i < 5; i++) {
        auto data = plugin->read();
        Serial.printf("  [%d] Value: %.2f, Confidence: %.2f\n", 
            i, data.value1, data.confidence);
        delay(100);
    }
    
    Serial.println("=== Test Complete ===");
}
```

---

## 📦 Публікація плагіна

### 1. Створити README.md

```markdown
# BH1750 Light Sensor Plugin

Digital light intensity sensor (0-65535 lux)

## Wiring

| BH1750 | M5Cardputer Grove |
|--------|-------------------|
| VCC    | 5V                |
| GND    | GND               |
| SDA    | G2 (SDA)          |
| SCL    | G1 (SCL)          |

## Configuration

\`\`\`json
{"type": "sensor", "name": "BH1750", "enabled": true}
\`\`\`

## Usage

\`\`\`cpp
auto* sensor = pluginSystem->getPlugin("BH1750");
auto data = sensor->read();
float lux = data.value1;
\`\`\`

## Troubleshooting

- **Plugin not found**: Check I2C wiring
- **Always returns 0**: Sensor in wrong mode
```

### 2. Створити release

```bash
# Створити архів плагіна
cd lib/BH1750Plugin/
zip -r BH1750Plugin-v1.0.0.zip .
```

### 3. Додати в Plugin Registry

```json
{
  "name": "BH1750",
  "version": "1.0.0",
  "download": "https://github.com/user/repo/releases/BH1750Plugin-v1.0.0.zip",
  "checksum": "sha256:...",
  "compatible_hardware": ["M5Cardputer-ADV", "M5CoreS3"],
  "category": "environmental"
}
```

---

## 🎓 Поради досвідчених розробників

### 1. canInitialize() — без апаратних шин

```cpp
bool canInitialize() override {
    // ✅ НЕ використовуємо Wire/SPI тут — ctx ще недоступний
    // Реальна перевірка hardware відбувається на початку initialize()
    return true;
}

bool initialize(PluginContext* context) override {
    ctx = context;
    // ✅ Тут перевіряємо hardware:
    ctx->wire->beginTransmission(I2C_ADDR);
    if (ctx->wire->endTransmission() != 0) {
        return false;  // Hardware не знайдений — initialize() повернув false
    }
    // ... ініціалізація
}
```

### 2. Graceful degradation

```cpp
SensorData read() override {
    if (!ready) {
        // Поверни нульові дані, не crash!
        return {0, 0, 0.0f, millis(), false};
    }
    // ... читання
}
```

### 3. Timeout для I2C/SPI

```cpp
uint16_t readWithTimeout(uint32_t timeoutMs = 100) {
    uint32_t start = millis();
    while (!dataReady()) {
        if (millis() - start > timeoutMs) {
            ctx->log->error(getName(), "Read timeout");  // ✅ не Serial.println
            return 0;
        }
        delay(1);
    }
    return readData();
}
```

### 4. Використовуй версії

```cpp
// plugin.json
{
  "version": "1.2.3",  // Semantic Versioning
  "api_version": "1.0"  // Версія Plugin API
}
```

### 5. Документуй всі public методи

```cpp
/**
 * @brief Зчитує освітленість від датчика
 * @return SensorData з value1 = lux (0-65535)
 * @note Блокуючий виклик, ~120ms
 */
SensorData read() override;
```

---

## 🚀 Приклади плагінів

### В репозиторії є готові приклади:

- `lib/LDC1101Plugin/` - Індуктивний сенсор (I2C)
- `lib/BMI270Plugin/` - IMU 6DOF (I2C)
- `lib/SDCardPlugin/` - Сховище (SPI)
- `lib/MockSensorPlugin/` - Demo sensor (no hardware)

---

**Готові питання? Приєднуйтесь до спільноти!**
- 💬 [GitHub Discussions](https://github.com/xkachya/CoinTrace/discussions) - обговорення та питання
- 📝 [GitHub Issues](https://github.com/xkachya/CoinTrace/issues) - знайдені баги
- 📖 [Documentation](../../README.md) - загальна документація
