# Контракт Plugin System

**Версія:** 1.0.0  
**Дата:** 10 березня 2026  
**Статус:** Обов'язковий до виконання

---

## Призначення документа

Цей документ визначає формальний контракт між **Plugin System** (система) та **Plugin** (плагін). Це не "як написати плагін" (дивіться [`CREATING_PLUGINS.md`](CREATING_PLUGINS.md)), а **що система гарантує плагіну і що плагін зобов'язаний гарантувати системі**.

Порушення контракту призводить до undefined behavior, крашів або непередбачуваної роботи системи.

---

## Зміст

1. [Що система гарантує плагіну](#частина-1-що-система-гарантує-плагіну)
2. [Що плагін зобов'язаний гарантувати системі](#частина-2-що-плагін-зобовязаний-гарантувати-системі)
3. [Контракт для конкретних типів плагінів](#частина-3-контракт-для-конкретних-типів-плагінів)
4. [Санкції за порушення контракту](#частина-4-санкції-за-порушення-контракту)
5. [Перевірка дотримання контракту](#частина-5-перевірка-дотримання-контракту)
6. [Приклади контракту в коді](#частина-6-приклади-контракту-в-коді)
7. [Зміни контракту (Versioning)](#частина-7-зміни-контракту-versioning)

---

## Частина 1: Що система гарантує плагіну

### 1.1 Lifecycle гарантії

#### ✅ Порядок викликів методів

```
canInitialize() → initialize() → update() → ... → update() → shutdown()
```

**Гарантії:**
- `canInitialize()` **завжди** викликається першим
- `initialize()` викликається **тільки якщо** `canInitialize()` повернув `true`
- `update()` викликається **тільки якщо** `initialize()` повернув `true`
- `shutdown()` **завжди** викликається перед деструктором плагіна
- `shutdown()` викликається навіть якщо `initialize()` повернув `false`

**Що це означає для плагіна:**
- Не потрібно перевіряти `canInitialize()` всередині `initialize()` — система це вже зробила
- `update()` може безпечно припускати що hardware ініціалізовано
- `shutdown()` повинен коректно працювати навіть якщо `initialize()` не виконувався

---

#### ⚠️ Архітектурне правило: `canInitialize()` не звертається до апаратних шин

**Контракт:** `canInitialize()` викликається **до** `initialize()` і не має доступу до `PluginContext`. Тому в `canInitialize()` **заборонено** звертатись до `Wire`, `SPI`, `ctx` або будь-якого апаратного ресурсу.

```cpp
// ✅ Правильно — без апаратних шин
bool canInitialize() override {
    return true;  // Або перевірка попередніх умов без шин (напр. наявність конфігу)
}

// ❌ НЕПРАВИЛЬНО — ctx тут nullptr, краш при старті!
bool canInitialize() override {
    ctx->wire->beginTransmission(I2C_ADDR);  // UNDEFINED BEHAVIOR!
    return (ctx->wire->endTransmission() == 0);
}
```

**Реальна перевірка доступності hardware** — на початку `initialize()`:

```cpp
bool initialize(PluginContext* context) override {
    ctx = context;
    // ✅ Тут ctx доступний, перевіряємо I2C:
    ctx->wire->beginTransmission(I2C_ADDR);
    if (ctx->wire->endTransmission() != 0) {
        lastError = {1, "I2C NACK - hardware not found"};
        return false;
    }
    // ... подальша ініціалізація
}
```

---

#### ✅ Частота викликів `update()`

**Гарантія:** `update()` викликається з частотою не менше **10 Hz** (кожні 100 ms).

**Що це означає:**
- Плагін може покладатись на регулярні виклики для watchdog, health monitoring
- Між викликами пройде максимум 100 ms (у нормальних умовах 10-50 ms)

---

### 1.2 Ініціалізація периферії

#### ✅ I2C шина

**Гарантія:** `Wire` вже ініціалізований до першого виклику будь-якого методу плагіна.

```cpp
// PluginSystem викликає один раз при старті:
Wire.begin(config.i2c_sda, config.i2c_scl, config.i2c_frequency);
```

**Що це означає:**
- ❌ **ЗАБОРОНЕНО** викликати `Wire.begin()` в плагіні
- ✅ Можна одразу використовувати `Wire.beginTransmission()`, `Wire.write()` тощо

---

#### ✅ SPI шина

**Гарантія:** `SPI` вже ініціалізований до першого виклику будь-якого методу плагіна.

```cpp
// PluginSystem викликає один раз при старті:
SPI.begin(config.spi_sck, config.spi_miso, config.spi_mosi);
```

**Що це означає:**
- ❌ **ЗАБОРОНЕНО** викликати `SPI.begin()` в плагіні
- ✅ Можна одразу використовувати `SPI.beginTransaction()`, `SPI.transfer()` тощо

---

### 1.3 Доступ до ресурсів через PluginContext

**Гарантія:** Кожен плагін отримує валідний `PluginContext*` при виклику `initialize()`.

```cpp
struct PluginContext {
    TwoWire*          wire;        // Ініціалізований I2C (не nullptr)
    SemaphoreHandle_t wireMutex;   // FreeRTOS mutex для I2C шини (не nullptr)
    SPIClass*         spi;         // Ініціалізований SPI (не nullptr)
    SemaphoreHandle_t spiMutex;    // FreeRTOS mutex для SPI шини (не nullptr)
    ConfigManager*    config;      // Доступ до конфігурації (не nullptr)
    Logger*           log;         // Система логування (не nullptr)
    IStorageManager*  storage      = nullptr;  // Доступ до MeasurementStore, NVS, LittleFS
};
```

**Що це означає:**
- Плагін може безпечно використовувати `ctx->wire`, `ctx->spi` без перевірки на `nullptr`
- `ctx->log` завжди доступний для логування
- `ctx->wireMutex` і `ctx->spiMutex` завжди валідні — використовуються async плагінами

---

### 1.3б ConfigManager — API доступних методів

**Гарантія:** `ctx->config` ніколи не `nullptr`. Ключі читаються з `data/plugins/<plugin_name>.json`.

```cpp
class ConfigManager {
public:
    int32_t     getInt    (const char* key, int32_t defaultVal)       const;
    uint8_t     getUInt8  (const char* key, uint8_t defaultVal)       const;
    uint32_t    getUInt32 (const char* key, uint32_t defaultVal)      const;
    float       getFloat  (const char* key, float defaultVal)         const;
    bool        getBool   (const char* key, bool defaultVal)          const;
    const char* getString (const char* key, const char* defaultVal)   const;
    // формат ключа: "plugin_name.param"
    // приклад: ctx->config->getInt("ldc1101.spi_cs_pin", 5)
};
```

---

### 1.4 Thread Safety з боку системи

**Гарантія:** `update()` завжди викликається з одного і того ж task (Core 0, main task).

**Що це означає:**
- `update()` ніколи не буде викликано одночасно з іншого thread
- `initialize()`, `shutdown()`, `update()` — single-threaded з боку системи

**Але увага:** `read()` може викликатись з будь-якого task! (див. Частину 2)

---

### 1.5 Правило: async плагіни і спільні шини

**Гарантія:** `ctx->wireMutex` і `ctx->spiMutex` ініціалізовані системою до першого виклику `initialize()`.

**Контракт:** Якщо плагін запускає власний FreeRTOS task з доступом до шини — він **зобов'язаний** брати відповідний mutex перед кожною транзакцією.

```cpp
// ✅ Правильно: async плагін з доступом до SPI
static void readTaskFunc(void* param) {
    auto* self = (MyPlugin*)param;
    while (true) {
        // Взяти spiMutex перед будь-яким SPI зверненням
        if (xSemaphoreTake(self->ctx->spiMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            self->ctx->spi->beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
            // ... SPI транзакція ...
            self->ctx->spi->endTransaction();
            xSemaphoreGive(self->ctx->spiMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ✅ Правильно: async плагін з доступом до I2C
static void readTaskFunc(void* param) {
    auto* self = (MyPlugin*)param;
    while (true) {
        if (xSemaphoreTake(self->ctx->wireMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            // ... I2C транзакція ...
            xSemaphoreGive(self->ctx->wireMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ✅ Плагіни БЕЗ async task (Strategy A) — mutex НЕ потрібен
void update() override {
    // Без task — race condition неможливий, mutex не беремо
    // Strategy A: всі update() викликаються послідовно з одного task
    ctx->spi->transfer(...);
}
```

**⚠️ Cardputer-Adv специфіка:** SD карта і LDC1101 ділять одну SPI шину (G40/G14/G39).
`SDTransport` (Logger) також використовує `ctx->spiMutex` при записі на SD.
Будь-який плагін або сервіс що звертається до SPI з async task — зобов'язаний брати `ctx->spiMutex`.

---

### 1.6 Memory Management

**Гарантія:** Система виділяє пам'ять для плагіна один раз при завантаженні і звільняє при shutdown.

```cpp
// Система викликає:
IPlugin* plugin = new MyPlugin();  // One-time allocation
// ...робота...
delete plugin;  // One-time deallocation
```

**Що це означає:**
- Плагін не мусить самостійно керувати своїм lifecycle
- Деструктор `~MyPlugin()` буде викликаний гарантовано
- Плагін відповідає тільки за свою внутрішню пам'ять (буфери, динамічні структури)

---

## Частина 2: Що плагін зобов'язаний гарантувати системі

### 2.1 Performance обмеження

#### ⚠️ `update()` не блокує довше 10 ms

**Контракт:** Метод `update()` повинен повертати управління протягом **максимум 10 ms**.

**Що дозволено:**
```cpp
void update() override {
    // ✅ Швидкі I2C операції (1-5 ms)
    Wire.beginTransmission(addr);
    Wire.write(data);
    Wire.endTransmission();
    
    // ✅ Короткі обчислення
    float result = processData();
}
```

**Що заборонено:**
```cpp
void update() override {
    // ❌ Блокуючий delay
    delay(100);  // ПОРУШЕННЯ КОНТРАКТУ!
    
    // ❌ Довгі цикли
    for (int i = 0; i < 1000000; i++) { ... }  // ПОРУШЕННЯ!
    
    // ❌ Очікування події без timeout
    while (!dataReady()) {}  // ПОРУШЕННЯ!
}
```

**Для довгих операцій:** Використовуйте state machine або окремий FreeRTOS task з mutex (див. приклад в [`CREATING_PLUGINS.md`](CREATING_PLUGINS.md)).

---

#### ⚠️ `read()` не блокує довше 5 ms

**Контракт:** Метод `read()` повинен повертати управління протягом **максимум 5 ms**.

**Рекомендація:** Якщо дані не готові — поверніть `SensorData` з `valid = false`, а не чекайте.

```cpp
SensorData read() override {
    if (!dataReady()) {
        return {0, 0, 0.0f, millis(), false};  // ✅ Швидке повернення
    }
    return {value, 0, 1.0f, millis(), true};
}
```

---

### 2.2 Thread Safety з боку плагіна

#### ⚠️ `read()` повинен бути thread-safe

**Контракт:** Метод `read()` може бути викликаний з **будь-якого task** одночасно з `update()`.

**ESP32-S3 має 2 ядра:**
- Core 0: `update()` викликається з main task
- Core 1: `read()` може викликатись з background task

**Приклад гонки даних (неправильно):**
```cpp
class BrokenPlugin : public ISensorPlugin {
    float cachedValue;  // ❌ shared state без захисту
    
    void update() override {
        cachedValue = readFromSensor();  // Core 0
    }
    
    SensorData read() override {
        return {cachedValue, 0, 1.0f, millis(), true};  // Core 1 — race condition!
    }
};
```

**Правильно з mutex:**
```cpp
class SafePlugin : public ISensorPlugin {
    float cachedValue;
    SemaphoreHandle_t mutex;
    
    bool initialize(PluginContext* ctx) override {
        mutex = xSemaphoreCreateMutex();  // ✅ Create mutex
        return (mutex != nullptr);
    }
    
    void update() override {
        float newValue = readFromSensor();
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            cachedValue = newValue;  // ✅ Захищено (ADR-ST-008)
            xSemaphoreGive(mutex);
        }
    }
    
    SensorData read() override {
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
            return {0, 0, 0.0f, millis(), false};  // ADR-ST-008: timeout
        }
        float value = cachedValue;  // ✅ Захищено
        xSemaphoreGive(mutex);
        return {value, 0, 1.0f, millis(), true};
    }
    
    void shutdown() override {
        if (mutex) vSemaphoreDelete(mutex);
    }
};
```

---

### 2.3 Error Handling

#### ⚠️ Не кидати exceptions

**Контракт:** Плагін **не повинен кидати C++ exceptions** з жодного методу.

**ESP32 не має повноцінної exception support** (навіть з `-fexceptions` це ненадійно). Використовуйте коди повернення або `HealthStatus`.

**Заборонено:**
```cpp
SensorData read() override {
    if (error) {
        throw std::runtime_error("Sensor fault");  // ❌ ПОРУШЕННЯ!
    }
}
```

**Правильно:**
```cpp
SensorData read() override {
    if (error) {
        lastError = {ERR_SENSOR_FAULT, "I2C NACK at 0x2A"};
        return {0, 0, 0.0f, millis(), false};  // ✅ Повернути invalid data
    }
}

HealthStatus getHealthStatus() override {
    return error ? HealthStatus::SENSOR_FAULT : HealthStatus::OK;
}
```

---

#### ⚠️ Коректна робота `shutdown()` завжди

**Контракт:** `shutdown()` повинен коректно спрацювати **навіть якщо `initialize()` не викликався або повернув `false`**.

**Приклад (правильно):**
```cpp
class RobustPlugin : public ISensorPlugin {
    bool initialized = false;
    
    bool initialize(PluginContext* ctx) override {
        // Спроба ініціалізації
        if (!setupHardware()) {
            return false;  // Ініціалізація провалилась
        }
        initialized = true;
        return true;
    }
    
    void shutdown() override {
        // ✅ Захист від подвійного shutdown або shutdown без initialize
        if (!initialized) return;
        
        cleanupHardware();
        initialized = false;
    }
};
```

---

### 2.4 Ресурси та обмеження

#### ⚠️ Не викликати `Wire.begin()` або `SPI.begin()`

**Контракт:** Плагін **НЕ повинен** викликати:
- `Wire.begin()`
- `Wire.begin(sda, scl)`
- `SPI.begin()`
- `SPI.begin(sck, miso, mosi, ss)`

Система вже ініціалізувала ці шини. Повторний виклик може перезаписати налаштування і зламати інші плагіни.

**Дозволено:**
- `Wire.beginTransmission()`, `Wire.write()`, `Wire.read()`, `Wire.endTransmission()`
- `SPI.beginTransaction()`, `SPI.transfer()`, `SPI.endTransaction()`
- Зміна CS піна для SPI пристроїв

---

#### ⚠️ Обмеження пам'яті

**Контракт:** Плагін не повинен виділяти більше **8 KB RAM** для своїх потреб (буфери, структури даних).

**ESP32-S3FN8 має ~337 KB вільного heap** (PSRAM відсутній). Система підтримує до 20 плагінів одночасно. 8 KB на плагін = 160 KB загалом, залишається ~177 KB для системи.

**Для великих буферів:** Використовуйте SD карту. `ps_malloc()` НЕДОСТУПНИЙ на M5Stack Cardputer-ADV.

---

### 2.5 Діагностика та моніторинг

#### ⚠️ Валідний HealthStatus завжди

**Контракт:** `getHealthStatus()` може бути викликаний **в будь-який момент** і повинен повертати актуальний стан плагіна.

```cpp
HealthStatus getHealthStatus() override {
    // ✅ Швидка перевірка без I2C операцій
    if (!initialized) return HealthStatus::INITIALIZATION_FAILED;
    if (errorCount > 10) return HealthStatus::DEGRADED;
    if (lastReadFailed) return HealthStatus::COMMUNICATION_ERROR;
    return HealthStatus::OK;
}
```

**Заборонено:** Виконувати I2C/SPI операції в `getHealthStatus()` (це має бути швидкий getter).

---

#### ⚠️ Інформативні ErrorCode

**Контракт:** Якщо `getHealthStatus()` повертає стан помилки, `getLastError()` повинен надати деталі.

```cpp
ErrorCode getLastError() const override {
    return lastError;  // {code, "I2C NACK at 0x2A - check wiring"}
}
```

**Формат повідомлення:**
- ✅ "I2C NACK at 0x2A - check wiring"
- ✅ "Timeout waiting for data (>500ms)"
- ✅ "Sensor fault: CHIP_ID mismatch (expected 0x14, got 0x00)"
- ❌ "Error" (занадто загально)
- ❌ "Failed" (не інформативно)

---

## Частина 3: Контракт для конкретних типів плагінів

### 3.1 ISensorPlugin

**Додаткові гарантії:**

```cpp
class ISensorPlugin : public IPlugin {
    // read() може викликатись незалежно від update()
    // Навіть якщо update() не викликався жодного разу
    virtual SensorData read() = 0;
    
    // getMetadata() може викликатись до initialize()
    virtual SensorMetadata getMetadata() const = 0;
};
```

**Контракт:**
- `read()` повинен працювати навіть якщо `update()` ще не викликався
- `getMetadata()` має бути константним методом без side effects

---

### 3.2 IDisplayPlugin

**Додаткові гарантії:**

```cpp
class IDisplayPlugin : public IPlugin {
    // clear(), drawPixel(), drawText() можуть викликатись
    // з частотою до 60 Hz (16 ms між викликами)
    virtual void clear() = 0;
    virtual void drawPixel(int16_t x, int16_t y, uint32_t color) = 0;
    virtual void drawText(int16_t x, int16_t y, const char* text) = 0;
};
```

**Контракт:**
- Методи малювання не повинні блокувати довше **2 ms**
- `clear()` + повне перемалювання екрану повинно займати максимум **16 ms** (60 FPS)

---

### 3.3 IInputPlugin

**Додаткові гарантії:**

```cpp
class IInputPlugin : public IPlugin {
    // pollEvent() викликається з main loop (100+ Hz)
    virtual bool pollEvent(InputEvent& event) = 0;
};
```

**Контракт:**
- `pollEvent()` має повертатись протягом **1 ms**
- Якщо немає подій — повернути `false` одразу (не чекати)

---

### 3.4 ISensorPlugin — контракт calibrate()

**Контракт:**
- `calibrate()` — блокуюча операція, `delay()` дозволено
- **Не викликати з `update()`!** — обов'язково з коду застосунку, поза основного циклу
- **Повертає `bool`**: `true` = калібрування успішне, `false` = помилка (hardware не відповідає
  або неможливо визначити baseline). Помилку зосереджувати через `getLastError()`.
- Тривалість калібрування має бути задокументована в коментарі імплементації
- При перерванні не повинна залишати сенсор в невизначеному стані

> **Імплементація:** `IIMUPlugin::calibrate()` також повертає `bool` аналогічно. Усі `calibrate()`-методи в системі мають єдиний тип поверненого значення.

---

## Частина 4: Санкції за порушення контракту

### Що станеться якщо плагін порушить контракт?

| Порушення | Наслідок |
|---|---|
| `update()` блокує >10ms | Інші плагіни не отримають `update()` вчасно, watchdog може перезавантажити систему |
| `read()` не thread-safe | Race condition, corrupted data, краш системи |
| Кинув exception | Undefined behavior, краш ESP32 |
| Викликав `Wire.begin()` | Перезапис пінів I2C, інші I2C плагіни перестануть працювати |
| Виділив >8KB RAM | Out of memory для інших плагінів, система може не запуститись |
| `shutdown()` не працює без `initialize()` | Memory leak, незвільнені ресурси |

---

## Частина 5: Перевірка дотримання контракту

### Як розробник плагіна може перевірити контракт?

**Checklist перед публікацією плагіна:**

- [ ] `update()` виміряно осцилографом — не більше 10 ms
- [ ] `read()` виміряно — не більше 5 ms
- [ ] Немає викликів `Wire.begin()` або `SPI.begin()` в коді плагіна
- [ ] Використовується `xSemaphoreCreateMutex()` для shared state між `update()` та `read()`
- [ ] Всі методи не кидають exceptions (немає `throw` в коді)
- [ ] `shutdown()` протестовано окремо без `initialize()`
- [ ] `getHealthStatus()` не виконує I2C/SPI операцій
- [ ] RAM usage виміряно через `ESP.getFreeHeap()` — не більше 8 KB
- [ ] `getLastError()` повертає інформативні повідомлення з деталями помилки

**Unit тести мають покривати:**
- Виклик `shutdown()` без попереднього `initialize()`
- Одночасний виклик `read()` з двох tasks (race condition test)
- Вимірювання часу виконання `update()` та `read()`

---

## Частина 6: Приклади контракту в коді

### Приклад 1: Мінімальний плагін що дотримується контракту

```cpp
class MinimalPlugin : public ISensorPlugin {
private:
    PluginContext* ctx;
    bool initialized = false;
    float cachedValue = 0.0f;
    SemaphoreHandle_t mutex;
    ErrorCode lastError = {0, ""};

public:
    // === LIFECYCLE ===
    
    bool canInitialize() override {
        // ✅ Не звертається до апаратних шин — ctx ще недоступний
        // Реальна перевірка I2C відбувається на початку initialize()
        return true;
    }
    
    bool initialize(PluginContext* context) override {
        ctx = context;
        
        // ✅ Перевіряємо доступність hardware через I2C (перенесено з canInitialize)
        ctx->wire->beginTransmission(0x48);
        if (ctx->wire->endTransmission() != 0) {
            lastError = {2, "I2C NACK at 0x48 - check wiring"};
            return false;
        }
        
        // ✅ Створюємо mutex для thread safety
        mutex = xSemaphoreCreateMutex();
        if (!mutex) {
            lastError = {1, "Failed to create mutex"};
            return false;
        }
        
        // Налаштування регістрів hardware
        if (!setupSensor()) {
            lastError = {3, "Sensor configuration failed"};
            return false;
        }
        
        initialized = true;
        ctx->log->info(getName(), "Initialized successfully");
        return true;
    }
    
    void update() override {
        // ✅ Швидка операція (<10ms)
        float newValue = readFromSensor();
        
        // ✅ Thread-safe запис (ADR-ST-008: portMAX_DELAY заборонено)
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            cachedValue = newValue;
            xSemaphoreGive(mutex);
        }
    }
    
    void shutdown() override {
        // ✅ Захист від shutdown без initialize
        if (!initialized) return;
        
        if (mutex) {
            vSemaphoreDelete(mutex);
            mutex = nullptr;
        }
        
        initialized = false;
        ctx->log->info(getName(), "Shutdown complete");
    }
    
    // === SENSOR INTERFACE ===
    
    SensorData read() override {
        // ✅ Thread-safe читання (ADR-ST-008: portMAX_DELAY заборонено)
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
            return {0, 0, 0.0f, millis(), false};  // Timeout — дані недоступні
        }
        float value = cachedValue;
        xSemaphoreGive(mutex);
        
        return {value, 0, 1.0f, millis(), true};
    }
    
    // === DIAGNOSTICS ===
    
    HealthStatus getHealthStatus() override {
        // ✅ Швидка перевірка без I2C
        if (!initialized) return HealthStatus::INITIALIZATION_FAILED;
        return HealthStatus::OK;
    }
    
    ErrorCode getLastError() const override {
        return lastError;
    }
    
    // === METADATA ===
    
    const char* getName() const override { return "MinimalPlugin"; }
    const char* getVersion() const override { return "1.0.0"; }
    SensorType getType() const override { return SensorType::CUSTOM; }
};
```

---

## Частина 7: Зміни контракту (Versioning)

### Як змінюється контракт?

**Контракт має версію (наприклад 1.0.0).** Зміни контракту:

- **Patch (1.0.0 → 1.0.1):** Виправлення формулювань, додаткові приклади. Плагіни не потребують змін.
- **Minor (1.0.0 → 1.1.0):** Додані нові **опціональні** методи (з default implementation). Плагіни продовжують працювати.
- **Major (1.0.0 → 2.0.0):** Змінено контракт несумісно (нові обов'язкові методи, змінено signature). Плагіни вимагають оновлення.

**Плагін вказує версію контракту в `plugin.json`:**
```json
{
  "contract_version": "1.0.0"
}
```

Система перевіряє сумісність при завантаженні плагіна.

---

## Висновок

Цей контракт визначає межу між "системою" та "плагіном". Дотримання контракту гарантує:
- ✅ Стабільність системи (no crashes)
- ✅ Передбачувану поведінку
- ✅ Можливість розробляти плагіни незалежно
- ✅ Production-ready якість

**Golden Rule:** Якщо не впевнений чи дозволяє контракт щось робити — краще запитай або подивись приклад.

---

**Автор:** CoinTrace Architecture Team  
**Версія контракту:** 1.0.0  
**Дата останнього оновлення:** 10 березня 2026
