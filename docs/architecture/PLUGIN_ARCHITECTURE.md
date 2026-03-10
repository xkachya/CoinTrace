# Архітектура плагінів CoinTrace

**Документ:** Професійна архітектура з підтримкою плагінів  
**Версія:** 1.0.0  
**Дата:** 9 березня 2026  
**Статус:** 📝 Проектування (не імплементовано)

---

## 🎯 Мета

Створити архітектуру, яка дозволяє:
- ✅ Додавати нові сенсори **без зміни існуючого коду**
- ✅ Конфігурувати hardware через **JSON файл**
- ✅ Автоматично виявляти та завантажувати плагіни
- ✅ Легко мігрувати на інший hardware (Cardputer → CoreS3 → своя плата)
- ✅ Зовнішнім розробникам достатньо додати свою папку з кодом

---

## 🏗️ Концепція: Configuration-Driven Plugin System

### Основна ідея
```
┌─────────────────────────────────────────────────────────┐
│  main.cpp (НЕ ЗМІНЮЄТЬСЯ!)                              │
│  ├─ Читає config.json                                   │
│  ├─ Автоматично завантажує плагіни з конфігу           │
│  └─ Запускає систему                                    │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│  data/config.json (КОРИСТУВАЧ РЕДАГУЄ!)                 │
│  {                                                       │
│    "hardware": "M5Cardputer-ADV",                       │
│    "plugins": [                                         │
│      {"type": "sensor", "name": "LDC1101", "enabled": true},│
│      {"type": "imu", "name": "BMI270", "enabled": true},│
│      {"type": "storage", "name": "SDCard", "enabled": true},│
│      {"type": "sensor", "name": "QMC5883L", "enabled": false}│
│    ]                                                     │
│  }                                                       │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│  lib/ (ПЛАГІНИ - незалежні папки)                       │
│  ├─ LDC1101Plugin/                                      │
│  │  ├─ plugin.json          ← метадані плагіна         │
│  │  ├─ LDC1101Plugin.h      ← реалізація               │
│  │  └─ LDC1101Plugin.cpp                                │
│  ├─ BMI270Plugin/                                       │
│  ├─ QMC5883LPlugin/          ← НОВИЙ ПЛАГІН (просто додати папку!)│
│  └─ MyCustomSensorPlugin/    ← КОРИСТУВАЦЬКИЙ ПЛАГІН    │
└─────────────────────────────────────────────────────────┘
```

---

## 📁 Структура проекту

```
CoinTrace/
├── platformio.ini                # Build configuration
├── src/
│   └── main.cpp                  # ⚠️ НЕ ТРЕБА РЕДАГУВАТИ!
│
├── include/
│   ├── PluginSystem.h            # Ядро системи плагінів
│   ├── IPlugin.h                 # Базовий інтерфейс плагіна
│   ├── ISensorPlugin.h           # Інтерфейс для сенсорів
│   ├── IIMUPlugin.h              # Інтерфейс для IMU
│   ├── IStoragePlugin.h          # Інтерфейс для сховищ
│   └── ConfigManager.h           # Читання config.json
│
├── lib/                          # 🔌 ПЛАГІНИ (незалежні модулі)
│   ├── LDC1101Plugin/
│   │   ├── plugin.json           # Метадані плагіна
│   │   ├── LDC1101Plugin.h
│   │   └── LDC1101Plugin.cpp
│   │
│   ├── BMI270Plugin/
│   │   ├── plugin.json
│   │   ├── BMI270Plugin.h
│   │   └── BMI270Plugin.cpp
│   │
│   ├── SDCardPlugin/
│   │   ├── plugin.json
│   │   ├── SDCardPlugin.h
│   │   └── SDCardPlugin.cpp
│   │
│   └── QMC5883LPlugin/           # ← НОВИЙ ПЛАГІН (просто додати!)
│       ├── plugin.json
│       ├── QMC5883LPlugin.h
│       └── QMC5883LPlugin.cpp
│
├── data/                         # 📝 КОНФІГУРАЦІЯ (редагує користувач)
│   ├── config.json               # Основна конфігурація системи
│   ├── hardware/                 # Профілі hardware
│   │   ├── m5cardputer-adv.json  # M5Stack Cardputer-ADV
│   │   ├── m5cores3.json         # M5Stack CoreS3
│   │   ├── custom-board.json     # Користувацька плата
│   │   └── esp32-devkit.json     # Generic ESP32
│   └── plugins/                  # Конфігурації плагінів (опціонально)
│       ├── ldc1101.json          # Параметри LDC1101
│       └── bmi270.json           # Параметри BMI270
│
└── docs/
    └── architecture/
        ├── PLUGIN_ARCHITECTURE.md     # Цей документ
        ├── CREATING_PLUGINS.md        # Гайд створення плагінів
        ├── PLUGIN_CONTRACT.md         # Формальний контракт система ↔ плагін
        ├── PLUGIN_DIAGNOSTICS.md      # Система self-diagnostics
        ├── PLUGIN_INTERFACES_EXTENDED.md  # Розширені інтерфейси
        └── COMPARISON.md              # Економічне обґрунтування
```

---

## 🔌 Система плагінів: Як це працює

### Крок 1: Базовий інтерфейс плагіна

```cpp
// include/IPlugin.h
// Актуальний інтерфейс — деталі в розділі PluginContext нижче

class IPlugin {
public:
    // Метадані плагіна
    virtual const char* getName() const = 0;
    virtual const char* getVersion() const = 0;
    virtual const char* getAuthor() const = 0;
    
    // Життєвий цикл
    virtual bool canInitialize() = 0;                    // Перевірка попередніх умов (без шин)
    virtual bool initialize(PluginContext* ctx) = 0;     // Ініціалізація з доступом до ресурсів
    virtual void update() = 0;                           // Оновлення (викликається в loop)
    virtual void shutdown() = 0;                         // Вимкнення
    
    // Статус
    virtual bool isEnabled() const = 0;
    virtual bool isReady() const = 0;
    
    virtual ~IPlugin() = default;
};
```

### Крок 2: Спеціалізовані інтерфейси

```cpp
// include/ISensorPlugin.h

class ISensorPlugin : public IPlugin {
public:
    // Тип сенсора (базові для прикладу)
    // Повний список типів дивись: PLUGIN_INTERFACES_EXTENDED.md
    enum class SensorType {
        INDUCTIVE,    // LDC1101, LDC1612 - індуктивність
        WEIGHT,       // HX711, NAU7802 - вага монети
        MAGNETIC,     // QMC5883L, BMM150 - феромагнетизм
        DIAMETER,     // Caliper sensors - діаметр
        CAPACITIVE,   // FDC2214 - ємність
        OPTICAL,      // TCS34725 - колір/відбиття
        ACOUSTIC,     // MAX4466 - звук удару монети
        POSITION,     // Hall effect - позиція
        TEMPERATURE,  // DHT22, BME280 - температура
        PRESSURE,     // BMP280 - тиск
        CUSTOM        // Користувацький тип
    };
    
    virtual SensorType getType() const = 0;
    
    // Універсальний інтерфейс зчитування
    struct SensorData {
        float value1;        // RP для LDC1101, field strength для QMC5883L
        float value2;        // L для LDC1101
        float confidence;    // 0.0-1.0
        uint32_t timestamp;
    };
    
    virtual SensorData read() = 0;
    virtual bool calibrate() = 0;
};

// include/IIMUPlugin.h

class IIMUPlugin : public IPlugin {
public:
    struct IMUData {
        float accelX, accelY, accelZ;
        float gyroX, gyroY, gyroZ;
        float pitch, roll, yaw;
    };
    
    virtual IMUData read() = 0;
    virtual bool isLevel(float tolerance = 0.1) = 0;
    virtual void calibrate() = 0;
};

// include/IStoragePlugin.h

class IStoragePlugin : public IPlugin {
public:
    virtual bool save(const char* key, const void* data, size_t size) = 0;
    virtual bool load(const char* key, void* data, size_t size) = 0;
    virtual bool remove(const char* key) = 0;
    virtual size_t getAvailableSpace() = 0;
};
```

---

## � PluginContext: Доступ до ресурсів системи

### Критична зміна архітектури (за рекомендацією рецензії)

**Проблема старого підходу:** Кожен плагін викликав `Wire.begin()` самостійно → конфлікти I2C шини, перезапис пінів, непередбачувана поведінка.

**Рішення:** Структура `PluginContext` передає вже ініціалізовані ресурси в плагін.

```cpp
// include/PluginContext.h

struct PluginContext {
    // Вже ініціалізовані шини
    TwoWire*          wire;        // I2C шина (ініціалізована системою)
    SemaphoreHandle_t wireMutex;   // Mutex для I2C — обов'язковий для async плагінів
    SPIClass*         spi;         // SPI шина (ініціалізована системою)
    SemaphoreHandle_t spiMutex;    // Mutex для SPI — обов'язковий для async плагінів
                                   // ⚠️ Cardputer-Adv: SD карта і LDC1101 на одній SPI шині!

    // Система конфігурації
    ConfigManager*    config;      // Доступ до config.json та hardware профілів

    // Система логування
    Logger*           log;         // Уніфіковане логування з рівнями (DEBUG/INFO/WARNING/ERROR)
};
```

### Оновлений інтерфейс IPlugin

```cpp
class IPlugin {
public:
    // Метадані плагіна
    virtual const char* getName() const = 0;
    virtual const char* getVersion() const = 0;
    virtual const char* getAuthor() const = 0;
    
    // Життєвий цикл
    virtual bool canInitialize() = 0;  // Чи доступний hardware? (без PluginContext)
    
    // ⚠️ КРИТИЧНА ЗМІНА: initialize() тепер отримує PluginContext
    virtual bool initialize(PluginContext* ctx) = 0;  // Ініціалізація з доступом до ресурсів
    
    virtual void update() = 0;          // Оновлення (викликається в loop)
    virtual void shutdown() = 0;        // Вимкнення
    
    // Статус
    virtual bool isEnabled() const = 0;
    virtual bool isReady() const = 0;
    
    virtual ~IPlugin() = default;
};
```

### Приклад використання PluginContext

```cpp
class MyPlugin : public ISensorPlugin {
private:
    PluginContext* ctx;  // Зберігаємо контекст
    
public:
    bool canInitialize() override {
        // На цьому етапі context ще немає
        // Просто перевіряємо базову доступність (якщо можливо)
        return true;
    }
    
    bool initialize(PluginContext* context) override {
        ctx = context;  // Зберігаємо контекст
        
        // ✅ Використовуємо вже ініціалізований I2C
        ctx->wire->beginTransmission(I2C_ADDR);
        if (ctx->wire->endTransmission() != 0) {
            ctx->log->error(getName(), "I2C NACK - hardware not found");
            return false;
        }
        
        // ✅ Логуємо через уніфіковану систему
        ctx->log->info(getName(), "Initialized successfully");
        return true;
    }
    
    void update() override {
        // ✅ I2C операції без Wire.begin()
        ctx->wire->beginTransmission(I2C_ADDR);
        ctx->wire->write(REGISTER_DATA);
        ctx->wire->endTransmission();
        
        // Читання даних...
    }
};
```

**Критично:** Плагін **НЕ повинен** викликати `Wire.begin()` або `SPI.begin()` — це робить PluginSystem один раз при старті.

---

## ⚡ Thread Safety та Performance контракт

**ESP32-S3 має 2 ядра (Core 0 та Core 1).** FreeRTOS може викликати методи плагіна з різних tasks одночасно, що створює ризики race conditions.

### Контракт система ↔ плагін

Детальний контракт описано в окремому документі:

### 📖 [PLUGIN_CONTRACT.md](./PLUGIN_CONTRACT.md) ← **ОБОВ'ЯЗКОВО ПРОЧИТАТИ!**

**Основні правила:**

#### Що гарантує система:
- ✅ `update()` викликається тільки з main task (Core 0) — single-threaded
- ✅ `Wire` та `SPI` вже ініціалізовані до першого виклику методів плагіна
- ✅ `canInitialize()` → `initialize()` → `update()` — порядок гарантований
- ✅ `shutdown()` завжди викликається перед деструктором

#### Що зобов'язаний гарантувати плагін:
- ⚠️ `update()` не блокує довше **10 ms** (використовуйте state machine або окремий task)
- ⚠️ `read()` не блокує довше **5 ms**
- ⚠️ `read()` **thread-safe** — може викликатись з будь-якого task одночасно з `update()`
- ⚠️ Не кидати C++ exceptions (ESP32 не має повноцінної підтримки)
- ⚠️ `shutdown()` працює навіть якщо `initialize()` провалився

### Приклад thread-safe плагіна з mutex

```cpp
class ThreadSafePlugin : public ISensorPlugin {
private:
    PluginContext* ctx;
    float cachedValue = 0.0f;
    SemaphoreHandle_t mutex;  // FreeRTOS mutex для захисту shared state
    
public:
    bool initialize(PluginContext* context) override {
        ctx = context;
        
        // Створюємо mutex для thread safety
        mutex = xSemaphoreCreateMutex();
        if (!mutex) {
            ctx->log->error(getName(), "Failed to create mutex");
            return false;
        }
        
        // Ініціалізація hardware...
        return true;
    }
    
    void update() override {
        // Core 0: Читаємо з sensor
        float newValue = readFromSensor();
        
        // Thread-safe запис
        xSemaphoreTake(mutex, portMAX_DELAY);
        cachedValue = newValue;
        xSemaphoreGive(mutex);
    }
    
    SensorData read() override {
        // Core 1: Thread-safe читання (може викликатись одночасно з update())
        xSemaphoreTake(mutex, portMAX_DELAY);
        float value = cachedValue;
        xSemaphoreGive(mutex);
        
        return {value, 0, 1.0f, millis(), true};
    }
    
    void shutdown() override {
        if (mutex) {
            vSemaphoreDelete(mutex);
            mutex = nullptr;
        }
    }
};
```

**Детальніше:** [PLUGIN_CONTRACT.md](./PLUGIN_CONTRACT.md) містить повний checklis, приклади порушень та санкції.

---

## 💾 Memory Management

### Обмеження пам'яті для embedded систем

**ESP32-S3 має:**
- 512 KB SRAM (основна пам'ять)
- 8 MB PSRAM (опційно, повільніша)
- 8 MB Flash (для коду)

**Стратегія розподілу RAM:**
- 350 KB — система (PluginSystem, ConfigManager, Logger, FreeRTOS)
- 160 KB — плагіни (20 плагінів × 8 KB кожен)
- 2 KB — резерв для stack overflow захисту

### Memory контракт

#### Обмеження для одного плагіна: **максимум 8 KB RAM**

```cpp
class MyPlugin : public ISensorPlugin {
private:
    // ✅ Малі буфери допустимі
    float buffer[100];        // 400 bytes
    char logBuffer[256];      // 256 bytes
    
    // ❌ Великі буфери НЕ допустимі
    // float bigBuffer[10000];   // 40 KB — ПОРУШЕННЯ!
    
    // ✅ Для великих даних використовувати PSRAM або SD карту
    float* psramBuffer;       // Виділяти через ps_malloc()
};
```

### Memory lifecycle

**Система керує lifecycle плагіна:**

```cpp
// PluginSystem робить:
IPlugin* plugin = new MyPlugin();   // Один раз при старті
// ...робота...
delete plugin;                       // При shutdown або Hot-Reload
```

**Плагін відповідає за свою internal пам'ять:**

```cpp
class MyPlugin : public ISensorPlugin {
private:
    float* dynamicBuffer;
    
public:
    bool initialize(PluginContext* ctx) override {
        // Виділяємо internal пам'ять
        dynamicBuffer = (float*)malloc(2048);  // 2 KB
        if (!dynamicBuffer) {
            ctx->log->error(getName(), "Out of memory");
            return false;
        }
        return true;
    }
    
    void shutdown() override {
        // ✅ Звільняємо internal пам'ять
        if (dynamicBuffer) {
            free(dynamicBuffer);
            dynamicBuffer = nullptr;
        }
    }
};
```

### Діагностика використання пам'яті

```cpp
void MyPlugin::update() {
    // Періодично логуємо free heap для моніторингу
    static uint32_t lastCheck = 0;
    if (millis() - lastCheck > 30000) {  // Кожні 30 сек
        uint32_t freeHeap = ESP.getFreeHeap();
        ctx->log->info(getName(), "Free heap: " + String(freeHeap) + " bytes");
        lastCheck = millis();
    }
}
```

**Правило:** Якщо `ESP.getFreeHeap() < 50 KB` — система в небезпеці. Переглянути використання RAM плагінами.

### Для великих даних: PSRAM

```cpp
bool initialize(PluginContext* ctx) override {
    // Виділяємо з PSRAM (якщо доступно)
    bigBuffer = (float*)ps_malloc(100 * 1024);  // 100 KB в PSRAM
    if (!bigBuffer) {
        ctx->log->warning(getName(), "PSRAM not available, using SD card");
        // Fallback на SD карту
    }
    return true;
}
```

---

## �📝 Приклад: Створення нового плагіна (QMC5883L)

### 1. Створити папку плагіна

```
lib/QMC5883LPlugin/
├── plugin.json              ← Метадані
├── QMC5883LPlugin.h         ← Оголошення
└── QMC5883LPlugin.cpp       ← Реалізація
```

### 2. Файл метаданих `plugin.json`

```json
{
  "name": "QMC5883L",
  "version": "1.0.0",
  "author": "Community Contributor",
  "description": "3-axis magnetometer for ferromagnetic metal detection",
  "type": "sensor",
  "i2c_address": "0x0D",
  "dependencies": ["Wire"],
  "hardware_requirements": {
    "i2c": true,
    "spi": false,
    "min_ram": 2048
  },
  "configuration": {
    "sample_rate": 200,
    "range": "±8 Gauss",
    "oversampling": 512
  },
  "enabled_by_default": false
}
```

### 3. Реалізація плагіна `QMC5883LPlugin.h`

```cpp
// lib/QMC5883LPlugin/QMC5883LPlugin.h

#ifndef QMC5883L_PLUGIN_H
#define QMC5883L_PLUGIN_H

#include "ISensorPlugin.h"
#include "PluginContext.h"
#include <Wire.h>

class QMC5883LPlugin : public ISensorPlugin {
private:
    static const uint8_t I2C_ADDR = 0x0D;
    PluginContext* ctx = nullptr;  // ✅ Зберігаємо контекст
    bool enabled = false;
    bool ready = false;
    
public:
    // Метадані
    const char* getName() const override { return "QMC5883L"; }
    const char* getVersion() const override { return "1.0.0"; }
    const char* getAuthor() const override { return "Community"; }
    
    // Життєвий цикл
    bool canInitialize() override {
        // На цьому етапі context ще немає
        // Повертаємо true — перевіримо hardware в initialize()
        return true;
    }
    
    bool initialize(PluginContext* context) override {
        ctx = context;  // ✅ Зберігаємо контекст
        
        // ✅ Перевірка hardware через вже ініціалізований Wire
        ctx->wire->beginTransmission(I2C_ADDR);
        if (ctx->wire->endTransmission() != 0) {
            ctx->log->error(getName(), "I2C NACK at 0x0D - hardware not found");
            return false;
        }
        
        // Налаштування QMC5883L
        writeRegister(0x0B, 0x01); // Reset
        delay(10);
        writeRegister(0x09, 0x1D); // Mode: Continuous, 200Hz, 512 OSR, ±8G
        
        ready = true;
        enabled = true;
        
        // ✅ Логування успішної ініціалізації
        ctx->log->info(getName(), "Initialized successfully (200Hz, ±8G)");
        return true;
    }
    
    void update() override {
        // Нічого не треба робити в loop для цього сенсора
    }
    
    void shutdown() override {
        if (!ctx) return;  // Захист якщо initialize() не викликався
        
        writeRegister(0x09, 0x00); // Standby mode
        enabled = false;
        ready = false;
        
        ctx->log->info(getName(), "Shutdown complete");
    }
    
    // Статус
    bool isEnabled() const override { return enabled; }
    bool isReady() const override { return ready; }
    
    // Специфічні методи сенсора
    SensorType getType() const override { return SensorType::MAGNETIC; }
    
    SensorData read() override {
        if (!ready) return {0, 0, 0, 0};
        
        int16_t x = readRegister16(0x00);
        int16_t y = readRegister16(0x02);
        int16_t z = readRegister16(0x04);
        
        // Обчислити напруженість магнітного поля
        float fieldStrength = sqrt(x*x + y*y + z*z) / 3000.0; // Normalize
        
        return {
            .value1 = fieldStrength,
            .value2 = 0,  // Не використовується
            .confidence = (fieldStrength > 0.01) ? 0.9f : 0.0f,
            .timestamp = millis()
        };
    }
    
    bool calibrate() override {
        // Калібрування магнітометра (складніше, тут спрощено)
        ctx->log->warning(getName(), "Calibration not implemented yet");
        return true;
    }
    
private:
    void writeRegister(uint8_t reg, uint8_t value) {
        // ✅ Використовуємо ctx->wire замість Wire
        ctx->wire->beginTransmission(I2C_ADDR);
        ctx->wire->write(reg);
        ctx->wire->write(value);
        ctx->wire->endTransmission();
    }
    
    int16_t readRegister16(uint8_t reg) {
        // ✅ Використовуємо ctx->wire замість Wire
        ctx->wire->beginTransmission(I2C_ADDR);
        ctx->wire->write(reg);
        ctx->wire->endTransmission();
        
        ctx->wire->requestFrom(I2C_ADDR, (uint8_t)2);
        int16_t low = ctx->wire->read();
        int16_t high = ctx->wire->read();
        return (high << 8) | low;
    }
};

#endif
```

### 4. Додати в конфігурацію `data/config.json`

```json
{
  "hardware": "M5Cardputer-ADV",
  "plugins": [
    {"type": "sensor", "name": "LDC1101", "enabled": true},
    {"type": "imu", "name": "BMI270", "enabled": true},
    {"type": "sensor", "name": "QMC5883L", "enabled": true},  ← ДОДАЛИ!
    {"type": "storage", "name": "SDCard", "enabled": true}
  ]
}
```

### 5. **ГОТОВО!** ✅

**Жодного редагування `main.cpp`!**  
Система автоматично:
- Знайде плагін в `lib/QMC5883LPlugin/`
- Прочитає метадані з `plugin.json`
- Побачить `enabled: true` в конфігурації
- Завантажить та ініціалізує плагін

---

## 🚀 Головний файл `main.cpp` (НЕ ЗМІНЮЄТЬСЯ!)

```cpp
// src/main.cpp

#include <M5Cardputer.h>
#include "PluginSystem.h"
#include "ConfigManager.h"

PluginSystem* pluginSystem;
ConfigManager* config;

void setup() {
    Serial.begin(115200);
    M5Cardputer.begin();
    
    Serial.println("\n═══════════════════════════════");
    Serial.println("  CoinTrace v1.0.0");
    Serial.println("  Plugin System");
    Serial.println("═══════════════════════════════\n");
    
    // ============================================
    // 1. Завантажити конфігурацію
    // ============================================
    config = new ConfigManager();
    if (!config->load("/data/config.json")) {
        Serial.println("❌ Failed to load config, using defaults");
        config->loadDefaults();
    }
    
    Serial.printf("Hardware: %s\n", config->getHardware());
    Serial.printf("Plugins configured: %d\n\n", config->getPluginCount());
    
    // ============================================
    // 2. Ініціалізувати систему плагінів
    // ============================================
    pluginSystem = new PluginSystem();
    
    // ============================================
    // 3. Автоматично завантажити плагіни з конфігу
    // ============================================
    Serial.println("Loading plugins...\n");
    pluginSystem->loadFromConfig(config);
    
    // ============================================
    // 4. Ініціалізувати всі плагіни
    // ============================================
    pluginSystem->initializeAll();
    
    // ============================================
    // 5. Показати статус системи
    // ============================================
    pluginSystem->printStatus();
    
    Serial.println("\n✅ CoinTrace Ready!\n");
}

void loop() {
    M5.update();
    
    // Оновити всі плагіни
    pluginSystem->updateAll();
    
    // Ваша логіка програми
    if (M5Cardputer.Keyboard.isPressed()) {
        handleKeyPress();
    }
    
    delay(10);
}

void handleKeyPress() {
    char key = M5Cardputer.Keyboard.keysState().word[0];
    
    if (key == 'M' || key == 'm') {
        performMeasurement();
    }
}

void performMeasurement() {
    // Отримати активні сенсори через систему плагінів
    auto sensors = pluginSystem->getPluginsByType<ISensorPlugin>();
    
    if (sensors.empty()) {
        Serial.println("❌ No sensors available");
        return;
    }
    
    // Перевірити рівень (якщо є IMU)
    auto imus = pluginSystem->getPluginsByType<IIMUPlugin>();
    if (!imus.empty() && !imus[0]->isLevel()) {
        Serial.println("⚠️ Device not level!");
        return;
    }
    
    // Зчитати всі доступні сенсори
    Serial.println("\n=== Measurement ===");
    for (auto* sensor : sensors) {
        auto data = sensor->read();
        Serial.printf("%s: %.2f (conf: %.2f)\n", 
            sensor->getName(), data.value1, data.confidence);
    }
    Serial.println("==================\n");
}
```

---

## 🎨 Система плагінів: Внутрішня структура

```cpp
// include/PluginSystem.h

class PluginSystem {
private:
    std::vector<IPlugin*> plugins;
    std::map<std::string, IPlugin*> pluginMap;
    
public:
    // Завантажити плагіни з конфігурації
    void loadFromConfig(ConfigManager* config) {
        auto pluginConfigs = config->getPlugins();
        
        for (auto& cfg : pluginConfigs) {
            if (!cfg.enabled) continue;
            
            // Динамічно створити плагін (Factory Pattern)
            IPlugin* plugin = createPlugin(cfg.type, cfg.name);
            
            if (plugin) {
                registerPlugin(plugin);
                Serial.printf("✅ Loaded plugin: %s\n", cfg.name.c_str());
            } else {
                Serial.printf("⚠️ Plugin not found: %s\n", cfg.name.c_str());
            }
        }
    }
    
    // Зареєструвати плагін
    void registerPlugin(IPlugin* plugin) {
        plugins.push_back(plugin);
        pluginMap[plugin->getName()] = plugin;
    }
    
    // Ініціалізувати всі плагіни
    void initializeAll() {
        Serial.println("\nInitializing plugins...\n");
        
        for (auto* plugin : plugins) {
            Serial.printf("Init %s... ", plugin->getName());
            
            if (plugin->canInitialize()) {
                if (plugin->initialize()) {
                    Serial.println("✅");
                } else {
                    Serial.println("❌ Init failed");
                }
            } else {
                Serial.println("⚠️ Hardware not available");
            }
        }
    }
    
    // Оновити всі плагіни (викликається в loop)
    void updateAll() {
        for (auto* plugin : plugins) {
            if (plugin->isReady()) {
                plugin->update();
            }
        }
    }
    
    // Отримати плагін за ім'ям
    IPlugin* getPlugin(const char* name) {
        auto it = pluginMap.find(name);
        return (it != pluginMap.end()) ? it->second : nullptr;
    }
    
    // Отримати всі плагіни певного типу (template!)
    template<typename T>
    std::vector<T*> getPluginsByType() {
        std::vector<T*> result;
        for (auto* plugin : plugins) {
            T* typed = dynamic_cast<T*>(plugin);
            if (typed && typed->isReady()) {
                result.push_back(typed);
            }
        }
        return result;
    }
    
    // Показати статус системи
    void printStatus() {
        Serial.println("\n=== Plugin Status ===");
        for (auto* plugin : plugins) {
            Serial.printf("%-20s %s %s\n",
                plugin->getName(),
                plugin->isReady() ? "✅" : "❌",
                plugin->getVersion()
            );
        }
        Serial.println("====================");
    }
    
private:
    // Plugin Factory (автоматична реєстрація)
    IPlugin* createPlugin(const std::string& type, const std::string& name) {
        // Тут можна використати макроси для автоматичної реєстрації
        // Або читати з plugin.json і динамічно створювати
        
        if (name == "LDC1101") return new LDC1101Plugin();
        if (name == "BMI270") return new BMI270Plugin();
        if (name == "QMC5883L") return new QMC5883LPlugin();
        if (name == "SDCard") return new SDCardPlugin();
        
        return nullptr;
    }
};
```

---

## 📋 Приклади конфігураційних файлів

### `data/config.json` - Основна конфігурація

```json
{
  "version": "1.0.0",
  "hardware": "M5Cardputer-ADV",
  "debug": true,
  
  "plugins": [
    {
      "type": "sensor",
      "name": "LDC1101",
      "enabled": true,
      "config": "plugins/ldc1101.json"
    },
    {
      "type": "imu",
      "name": "BMI270",
      "enabled": true
    },
    {
      "type": "sensor",
      "name": "QMC5883L",
      "enabled": false,
      "note": "Magnetometer - enable if installed"
    },
    {
      "type": "storage",
      "name": "SDCard",
      "enabled": true,
      "fallback": "RAMStorage"
    },
    {
      "type": "audio",
      "name": "ES8311",
      "enabled": true
    }
  ],
  
  "features": {
    "acoustic_detection": false,
    "wifi_sync": false,
    "ota_updates": true
  }
}
```

### `data/hardware/m5cardputer-adv.json` - Профіль hardware

```json
{
  "name": "M5Cardputer-ADV",
  "manufacturer": "M5Stack",
  "mcu": "ESP32-S3FN8",
  "flash": "8MB",
  "psram": "8MB",
  
  "peripherals": {
    "display": {
      "type": "ST7789V2",
      "width": 240,
      "height": 135,
      "interface": "SPI"
    },
    "keyboard": {
      "type": "TCA8418",
      "keys": 56,
      "interface": "I2C"
    },
    "imu": {
      "type": "BMI270",
      "address": "0x68",
      "interface": "I2C"
    },
    "audio": {
      "codec": "ES8311",
      "address": "0x18",
      "microphone": "MEMS",
      "speaker": "1W"
    }
  },
  
  "pins": {
    "i2c": {
      "sda": 2,
      "scl": 1
    },
    "grove": {
      "sda": 2,
      "scl": 1
    },
    "ext_bus": {
      "spi_sck": 40,
      "spi_mosi": 14,
      "spi_miso": 39,
      "spi_cs": 5,
      "i2c_sda": 8,
      "i2c_scl": 9
    }
  },
  
  "compatible_plugins": [
    "LDC1101",
    "BMI270",
    "ES8311",
    "SDCard",
    "QMC5883L"
  ]
}
```

### `data/hardware/custom-board.json` - Користувацька плата

```json
{
  "name": "Custom ESP32 Board",
  "manufacturer": "DIY",
  "mcu": "ESP32-WROOM-32",
  "flash": "4MB",
  "psram": "0MB",
  
  "peripherals": {
    "display": {
      "type": "SSD1306",
      "width": 128,
      "height": 64,
      "interface": "I2C"
    }
  },
  
  "pins": {
    "i2c": {
      "sda": 21,
      "scl": 22
    }
  },
  
  "compatible_plugins": [
    "LDC1101",
    "SSD1306Display"
  ]
}
```

---

## 🔄 Міграція на інший hardware

### Сценарій: Перейти з Cardputer-ADV на CoreS3

**1. Створити профіль CoreS3:**
```bash
data/hardware/m5cores3.json
```

**2. Змінити одну строку в config.json:**
```json
{
  "hardware": "M5CoreS3",  ← Змінити тут!
  "plugins": [
    // Залишається без змін
  ]
}
```

**3. Готово!** ✅

Система автоматично:
- Завантажить профіль CoreS3
- Налаштує піни згідно профілю
- Ініціалізує плагіни з правильними параметрами

---

## 🎓 Гайд для зовнішніх розробників

### "Як додати свій сенсор в CoinTrace?"

**Крок 1: Створити папку плагіна**
```bash
lib/MyCoolSensorPlugin/
```

**Крок 2: Додати 3 файли**
```
plugin.json           # Метадані
MyCoolSensorPlugin.h  # Оголошення класу
MyCoolSensorPlugin.cpp # Реалізація
```

**Крок 3: Реалізувати інтерфейс**
```cpp
class MyCoolSensorPlugin : public ISensorPlugin {
    // Імплементувати всі методи з ISensorPlugin
};
```

**Крок 4: Додати в config.json**
```json
{"type": "sensor", "name": "MyCoolSensor", "enabled": true}
```

**Крок 5: Скомпілювати і запустити**
```bash
pio run -e cointrace-dev
pio run -t upload
```

**Готово!** Ваш сенсор працює!

---

## ✅ Переваги такого підходу

| Аспект | Традиційний підхід | Plugin System |
|--------|-------------------|---------------|
| **Додавання сенсора** | Редагувати 5+ файлів | +1 папка, 1 рядок у конфігу |
| **Тестування** | Перетестувати весь код | Тільки новий плагін |
| **Ризик поламати існуючий код** | 🔴 Високий | 🟢 Нульовий |
| **Міграція на інший hardware** | Переписувати код | Змінити 1 строку |
| **Співпраця команди** | Конфлікти в Git | Кожен працює у своїй папці |
| **Версіонування** | Складно | Кожен плагін має версію |
| **Documentація** | Розкидана | plugin.json = документація |

---

## 🚀 Наступні кроки

### Етап 1: Ядро системи (тиждень 1-2)
- [ ] `IPlugin.h` - базовий інтерфейс
- [ ] `ISensorPlugin.h`, `IIMUPlugin.h`, `IStoragePlugin.h`
- [ ] `PluginSystem.h` - менеджер плагінів
- [ ] `ConfigManager.h` - читання JSON

### Етап 2: Базові плагіни (тиждень 2-3)
- [ ] `LDC1101Plugin` - індуктивний сенсор
- [ ] `BMI270Plugin` - IMU з детекцією рівня
- [ ] `SDCardPlugin` - сховище на SD картці
- [ ] `MockSensorPlugin` - demo mode

### Етап 3: Конфігурація (тиждень 3-4)
- [ ] `data/config.json` - основна конфігурація
- [ ] `data/hardware/*.json` - профілі hardware
- [ ] Автоматична детекція hardware (за Config)

### Етап 4: Документація (тиждень 4-5)
- [ ] `CREATING_PLUGINS.md` - гайд для розробників
- [ ] `HARDWARE_PROFILES.md` - гайд профілів
- [ ] Приклади плагінів
- [ ] Video tutorial

---

## 💡 Креативні фічі (бонус)

### 1. Auto-Discovery плагінів
```cpp
// Система сканує lib/ і автоматично знаходить plugin.json
pluginSystem->scanForPlugins("lib/");
```

### 2. Hot-Reload плагінів

> ⚠️ **Застереження:** ESP32-S3 не підтримує динамічне завантаження `.so`/`.dylib` в runtime як на десктопних ОС. `reloadPlugin()` означає переініціалізацію `shutdown()` → `initialize()` в рамках поточної сесії. Для оновлення **коду** плагіна потрібне повне перезавантаження пристрою (OTA прошивка).

```cpp
// Переініціалізація плагіна без перезавантаження (конфігураційні зміни)
pluginSystem->reloadPlugin("QMC5883L");
```

### 3. OTA оновлення плагінів

> ⚠️ **Потребує перезавантаження:** OTA завантажує нову прошивку в flash, після якої потрібний reboot.

```cpp
// Завантажити новий плагін через WiFi
pluginSystem->installPluginOTA("https://github.com/xkachya/CoinTrace/releases/download/plugins/qmc5883l-v2.0.0.zip");
// Після завантаження — виконати ESP.restart() для активації
```

### 4. Plugin Marketplace
```
GitHub Releases - Community Plugins
- 📦 LDC1101 Enhanced v2.1
- 📦 QMC5883L v1.0
- 📦 Acoustic Analyzer v1.5
- 📦 ML Classifier v3.0
```

### 5. Dependency Resolution
```json
// plugin.json
{
  "dependencies": [
    {"name": "Wire", "version": ">=1.0"},
    {"name": "ArduinoJson", "version": "^6.0"}
  ]
}
```

---

## 🔬 Розширення архітектури

**Цей документ описує базову концепцію Plugin System.**  
Для повного списку інтерфейсів та типів заліза дивіться:

### 📖 [PLUGIN_INTERFACES_EXTENDED.md](./PLUGIN_INTERFACES_EXTENDED.md)

**Що там знайдете:**
- **ISensorPlugin** з 15+ типами сенсорів (вага, діаметр, товщина, звук, магнетизм...)
- **IDisplayPlugin** - підтримка різних екранів (ST7789V2 → ILI9341 → OLED)
- **IInputPlugin** - клавіатури, тачскріни, енкодери, джойстики
- **IAudioPlugin** - аудіо кодеки (ES8311), buzzer, мікрофони
- **IConnectivityPlugin** - WiFi, BLE, LoRa, NFC
- **IPowerPlugin** - батарея, живлення, AXP192/IP5306
- **IPeripheralPlugin** - LED, IR, servo, relay

**Приклади реальних плагінів:**
- HX711Plugin (ваговий сенсор ±0.01g)
- ST7789V2Plugin (TFT дисплей M5Cardputer)
- TCA8418Plugin (QWERTY клавіатура)
- ES8311Plugin (аудіо codec з мікрофоном)

**Комплексна система ідентифікації монет:**
Вага + Індуктивність + Магнетизм + Діаметр + Товщина = 95%+ точності

---

## 🎆 Висновок

**З такою архітектурою:**

✅ **Розробник може додати новий сенсор за 30 хвилин**
- Створити 3 файли
- Додати 1 рядок в config.json
- Готово!

✅ **Жодних змін в `main.cpp` або інших core файлах**
- Нульовий ризик поламати існуючий код
- Не треба перетестовувати всю систему

✅ **Легка міграція на інший hardware**
- Змінити 1 строку в конфігурації
- Створити новий hardware profile JSON
- Система адаптується автоматично

✅ **Майбутнє-proof архітектура**
- Готова до розширення
- Модульна
- Тестуємість
- Документуємість

---

**Статус:** 📝 Концепція готова до імплементації  
**Дата:** 9 березня 2026
