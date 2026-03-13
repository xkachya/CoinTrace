# Система діагностики плагінів CoinTrace

**Документ:** Професійна self-diagnostics для hardware плагінів  
**Версія:** 1.0.0  
**Дата:** 10 березня 2026  
**Статус:** 📝 Проектування (розширення базової архітектури)

---

## 🎯 Проблема: Неповна діагностика

### ❌ Що не так з поточною архітектурою:

```cpp
// Поточна реалізація (НЕДОСТАТНЯ):
class IPlugin {
    virtual bool canInitialize() = 0;  // Тільки TRUE/FALSE
    virtual bool isReady() const = 0;  // Тільки TRUE/FALSE
};
```

**Проблеми:**
1. **Не зрозуміло ЩО не так** - просто "не працює"
2. **Немає детальної інформації** - чому не працює?
3. **Не можна діагностувати** після ініціалізації
4. **Немає health monitoring** - поступова деградація непомітна
5. **Не можна розрізнити:**
   - Hardware не підключено (I2C NACK)
   - Hardware підключено але не відповідає (timeout)
   - Hardware працює але дає невірні дані (sensor fault)
   - Hardware працює але потребує калібрування

---

## 🏗️ Розширена система діагностики

### 1. Enum HealthStatus - Детальний статус

```cpp
// include/IPlugin.h

class IPlugin {
public:
    // Статус здоров'я плагіна
    enum class HealthStatus {
        // === Нормальні стани ===
        OK,                     // ✅ Все працює ідеально
        OK_WITH_WARNINGS,       // ⚠️ Працює, але є попередження
        
        // === Тимчасові проблеми ===
        DEGRADED,               // 🟡 Працює, але з погіршеною якістю
        TIMEOUT,                // ⏱️ Hardware не відповідає вчасно
        BUSY,                   // 🔄 Hardware зайнятий (спробуй пізніше)
        
        // === Критичні проблеми ===
        NOT_FOUND,              // ❌ Hardware не знайдено (I2C NACK, SPI no response)
        INITIALIZATION_FAILED,  // ❌ Не вдалося ініціалізувати
        SENSOR_FAULT,           // ❌ Датчик повідомляє про внутрішню помилку
        CALIBRATION_NEEDED,     // 🔧 Потрібне калібрування
        OUT_OF_RANGE,           // 📊 Значення поза діапазоном (hardware issue)
        COMMUNICATION_ERROR,    // 🔌 Помилка зв'язку (CRC, checksum)
        POWER_ISSUE,            // 🔋 Проблема з живленням
        OVERHEATING,            // 🌡️ Перегрів (якщо є thermal sensor)
        
        // === Інші ===
        DISABLED,               // ⏸️ Плагін вимкнений користувачем
        UNKNOWN                 // ❓ Невідомий стан
    };
    
    // Код помилки (опціонально, для debugging)
    struct ErrorCode {
        uint8_t code;           // Код помилки (залежить від плагіна)
        const char* message;    // Текстовий опис
    };
    
    // Результат діагностики
    struct DiagnosticResult {
        HealthStatus status;
        ErrorCode error;
        uint32_t timestamp;     // Коли проводилась діагностика
        
        // Додаткова інформація
        struct {
            uint16_t successRate;   // % успішних операцій (0-100)
            uint32_t totalReads;    // Кількість зчитувань
            uint32_t failedReads;   // Кількість помилок
            uint32_t lastSuccess;   // Час останнього успіху (ms)
        } stats;
    };
    
    // === Базові методи (lifecycle) ===
    virtual bool canInitialize() = 0;
    virtual bool initialize(PluginContext* ctx) = 0;
    virtual void update() = 0;
    virtual void shutdown() = 0;
    
    virtual bool isEnabled() const = 0;
    virtual bool isReady() const = 0;
    
    // === Базова діагностика — default impl для плагінів без повної діагностики ===
    // Плагіни що мають full diagnostics — перевизначають їх (через IDiagnosticPlugin mixin).
    // Default = "невідомий стан" (не помилка, просто немає інформації).
    
    // Швидка перевірка статусу (викликається часто, має бути швидкою — без I2C/SPI)
    virtual HealthStatus getHealthStatus() const { return HealthStatus::UNKNOWN; }
    
    // Отримати останню помилку
    virtual ErrorCode getLastError() const { return {0, "No error"}; }
    
    virtual ~IPlugin() = default;
};

// include/IDiagnosticPlugin.h
//
// Опціональний mixin для плагінів із повною діагностикою.
// Використання: class LDC1101Plugin : public ISensorPlugin, public IDiagnosticPlugin {}
//
class IDiagnosticPlugin {
public:
    // ⚠️ BLOCKING ~57 ms (checkStability: 10×5 ms delay). НЕ викликати з loop() або update()!
    // Запускати лише з окремого FreeRTOS task або по явному user request (кнопка/команда).
    // PA3-6 fix: попередження на публічний API (раніше було тільки на checkStability()).
    virtual IPlugin::DiagnosticResult runDiagnostics() = 0;
    
    // Self-test (викликається при запуску або за вимогою)
    virtual bool runSelfTest() = 0;
    
    // Статистика роботи
    virtual IPlugin::DiagnosticResult getStatistics() const = 0;
    
    // Hardware capabilities check
    virtual bool checkHardwarePresence() = 0;  // Чи hardware підключено?
    virtual bool checkCommunication() = 0;     // Чи працює зв'язок?
    virtual bool checkCalibration() = 0;       // Чи актуальне калібрування?
    
    virtual ~IDiagnosticPlugin() = default;
};
```

---

## 🔍 2. Приклад: LDC1101 з повною діагностикою

```cpp
// lib/LDC1101Plugin/LDC1101Plugin.h

class LDC1101Plugin : public ISensorPlugin, public IDiagnosticPlugin {
private:
    PluginContext* ctx = nullptr;  // ✅ Контекст системи
    int csPin = -1;                // SPI CS пін (з конфіг ldc1101.spi_cs_pin)

    // Регістри LDC1101 (SPI, per datasheet SNOSD01D)
    static const uint8_t LDC1101_DEVICE_ID = 0x3F;  // CHIP_ID
    static const uint8_t LDC1101_CONFIG    = 0x0B;  // START_CONFIG
    static const uint8_t LDC1101_STATUS    = 0x20;  // STATUS
    static const uint8_t LDC1101_RP_MSB    = 0x22;  // RP_DATA_MSB
    static const uint8_t LDC1101_RP_LSB    = 0x21;  // RP_DATA_LSB
    // Примітка: LDC1101 не має записуваного TEST_REG; в реальній реалізації
    // використовуйте DIG_CONFIG (0x04) для R/W перевірки
    static const uint8_t LDC1101_TEST_REG  = 0x04;  // DIG_CONFIG (для R/W тесту)
    
    // Статистика
    struct {
        uint32_t totalReads = 0;
        uint32_t failedReads = 0;
        uint32_t lastSuccess = 0;
        HealthStatus currentStatus = HealthStatus::UNKNOWN;
        ErrorCode lastError = {0, "No error"};
    } diagnostics;
    
    bool enabled = false;
    bool ready = false;
    
public:
    // === Базова ініціалізація ===
    
    bool canInitialize() override {
        // На цьому етапі context ще немає
        // Повертаємо true — перевіримо hardware в initialize()
        return true;
    }
    
    bool initialize(PluginContext* context) override {
        ctx = context;  // ✅ Зберігаємо контекст
        
        // ✅ SPI CS пін — з конфігурації
        csPin = ctx->config ? ctx->config->getInt("ldc1101.spi_cs_pin", 5) : 5;
        pinMode(csPin, OUTPUT);
        digitalWrite(csPin, HIGH);
        delay(5);  // POR stabilization
        
        // Перевірка SPI зв'язку: читаємо CHIP_ID
        uint8_t chipId = readRegister(LDC1101_DEVICE_ID);
        if (chipId != 0xD4) {
            diagnostics.currentStatus = HealthStatus::NOT_FOUND;
            diagnostics.lastError = {2, "CHIP_ID mismatch — SPI/CS wiring issue"};
            ctx->log->error(getName(), "CHIP_ID: expected 0xD4, got 0x%02X", chipId);
            return false;
        }
        
        // Конфігурація
        writeRegister(LDC1101_CONFIG, 0x01);  // FUNC_MODE_SLEEP (0x01) — конфігурація лише в sleep режимі
        delay(10);
        
        // Перевірка чи конфігурація застосувалась
        uint8_t readBack = readRegister(LDC1101_CONFIG);
        if (readBack != 0x01) {
            diagnostics.currentStatus = HealthStatus::INITIALIZATION_FAILED;
            diagnostics.lastError = {4, "Configuration write failed"};
            ctx->log->error(getName(), "Config write failed");
            return false;
        }
        
        ready = true;
        enabled = true;
        diagnostics.currentStatus = HealthStatus::OK;
        diagnostics.lastError = {0, "No error"};
        
        ctx->log->info(getName(), "Initialized successfully");
        return true;
    }
    
    // === ДІАГНОСТИКА ===
    
    HealthStatus getHealthStatus() const override {
        // Швидка перевірка (без SPI операцій — тільки stale check)
        if (!enabled) return HealthStatus::DISABLED;
        if (!ready) return HealthStatus::INITIALIZATION_FAILED;
        
        // Перевірка статистики
        if (diagnostics.totalReads > 100) {
            float failRate = (float)diagnostics.failedReads / diagnostics.totalReads;
            if (failRate > 0.5) {
                return HealthStatus::DEGRADED;  // >50% помилок
            } else if (failRate > 0.1) {
                return HealthStatus::OK_WITH_WARNINGS;  // 10-50% помилок
            }
        }
        
        // Перевірка чи застаріли дані
        if (millis() - diagnostics.lastSuccess > 5000) {
            return HealthStatus::TIMEOUT;  // >5 сек без успішного зчитування
        }
        
        return diagnostics.currentStatus;
    }
    
    DiagnosticResult runDiagnostics() override {
        DiagnosticResult result;
        result.timestamp = millis();
        
        // 1. Перевірка присутності
        if (!checkHardwarePresence()) {
            result.status = HealthStatus::NOT_FOUND;
            result.error = {1, "Hardware not responding"};
            return result;
        }
        
        // 2. Перевірка зв'язку
        if (!checkCommunication()) {
            result.status = HealthStatus::COMMUNICATION_ERROR;
            result.error = {2, "Communication errors detected"};
            return result;
        }
        
        // 3. Перевірка калібрування
        if (!checkCalibration()) {
            result.status = HealthStatus::CALIBRATION_NEEDED;
            result.error = {5, "Calibration expired or invalid"};
            return result;
        }
        
        // 4. Перевірка стабільності
        if (!checkStability()) {
            result.status = HealthStatus::DEGRADED;
            result.error = {6, "Readings are unstable (check connection)"};
            return result;
        }
        
        // 5. Статистика
        uint16_t successRate = 100;
        if (diagnostics.totalReads > 0) {
            successRate = 100 - (diagnostics.failedReads * 100 / diagnostics.totalReads);
        }
        
        result.status = HealthStatus::OK;
        result.error = {0, "All checks passed"};
        result.stats.successRate = successRate;
        result.stats.totalReads = diagnostics.totalReads;
        result.stats.failedReads = diagnostics.failedReads;
        result.stats.lastSuccess = diagnostics.lastSuccess;
        
        return result;
    }
    
    ErrorCode getLastError() const override {
        return diagnostics.lastError;
    }
    
    bool runSelfTest() override {
        if (!ctx) return false;
        ctx->log->info(getName(), "Self-test start");
        
        // Test 1: Device ID
        uint8_t id = readRegister(LDC1101_DEVICE_ID);
        if (id != 0xD4) {
            ctx->log->error(getName(), "CHIP_ID mismatch: expected 0xD4, got 0x%02X", id);
            return false;
        }
        ctx->log->info(getName(), "CHIP_ID OK (0xD4)");
        
        // Test 2: Read/Write register (DIG_CONFIG 0x04 — R/W доступний)
        uint8_t testValue = 0xAA;
        writeRegister(LDC1101_TEST_REG, testValue);
        uint8_t readBack = readRegister(LDC1101_TEST_REG);
        if (readBack != testValue) {
            ctx->log->error(getName(), "R/W test failed: wrote 0x%02X, read 0x%02X", testValue, readBack);
            return false;
        }
        ctx->log->info(getName(), "R/W test passed");
        
        // Test 3: Status register
        uint8_t status = readRegister(LDC1101_STATUS);
        if (status & 0x80) {  // Error bit
            ctx->log->warning(getName(), "Status register error: 0x%02X", status);
            return false;
        }
        ctx->log->info(getName(), "Status register OK");
        
        // Test 4: Multiple reads consistency
        float readings[5];
        for (int i = 0; i < 5; i++) {
            readings[i] = readRP();
            delay(10);
        }
        
        // Перевірка розкиду (стандартне відхилення)
        float mean = 0;
        for (int i = 0; i < 5; i++) mean += readings[i];
        mean /= 5;
        
        float variance = 0;
        for (int i = 0; i < 5; i++) {
            float diff = readings[i] - mean;
            variance += diff * diff;
        }
        float stddev = sqrt(variance / 5);
        
        if (stddev > mean * 0.1) {  // >10% розкид
            ctx->log->warning(getName(), "Readings unstable: stddev=%.2f, mean=%.2f", stddev, mean);
            return false;
        }
        ctx->log->info(getName(), "Stability test passed (stddev=%.2f)", stddev);
        
        ctx->log->info(getName(), "Self-test PASSED");
        diagnostics.currentStatus = HealthStatus::OK;
        return true;
    }
    
    DiagnosticResult getStatistics() const override {
        DiagnosticResult result;
        result.status = diagnostics.currentStatus;
        result.error = diagnostics.lastError;
        result.timestamp = millis();
        
        uint16_t successRate = 100;
        if (diagnostics.totalReads > 0) {
            successRate = 100 - (diagnostics.failedReads * 100 / diagnostics.totalReads);
        }
        
        result.stats.successRate = successRate;
        result.stats.totalReads = diagnostics.totalReads;
        result.stats.failedReads = diagnostics.failedReads;
        result.stats.lastSuccess = diagnostics.lastSuccess;
        
        return result;
    }
    
    bool checkHardwarePresence() override {
        return (readRegister(LDC1101_DEVICE_ID) == 0xD4);  // SPI CHIP_ID check
    }
    
    bool checkCommunication() override {
        // Читаємо Device ID 3 рази
        for (int i = 0; i < 3; i++) {
            uint8_t id = readRegister(LDC1101_DEVICE_ID);
            if (id != 0xD4) return false;
        }
        return true;
    }
    
    bool checkCalibration() override {
        // ⚠️ millis() скидається після перезавантаження — не зберігається між сеансами.
        // Цей приклад підходить лише для перевірки в межах поточного сеансу.
        // Для постійного зберігання терміну калібрування — зберігай в NVS/SD
        // (epoch timestamp або лічильник millis після повернення з NVS при старті).
        uint32_t calibrationAge = millis() - lastCalibrationTime;
        if (calibrationAge > 30UL * 24 * 60 * 60 * 1000) {  // 30 днів
            return false;
        }
        
        // Перевірка чи базове значення в розумних межах
        if (calibrationBaseline < 100 || calibrationBaseline > 10000) {
            return false;
        }
        
        return true;
    }
    
    bool checkStability() {
        // ⚠️ BLOCKING: 10 × readRP() + 10 × delay(5ms) ≈ 50 ms блокування в поточному task.
        // НЕ викликати з loop() або update() — порушує CONTRACT §1.1 (гарантія updateAll ≥ 10 Hz).
        // Дозволено тільки з: setup() при старті, або окремого background diagnostic task
        // з низьким пріоритетом (наприклад, tskIDLE_PRIORITY + 1).
        const int N = 10;
        float readings[N];
        for (int i = 0; i < N; i++) {
            readings[i] = readRP();
            delay(5);
        }
        
        float mean = 0;
        for (int i = 0; i < N; i++) mean += readings[i];
        mean /= N;
        
        float maxDev = 0;
        for (int i = 0; i < N; i++) {
            float dev = abs(readings[i] - mean);
            if (dev > maxDev) maxDev = dev;
        }
        
        // Якщо максимальне відхилення >5% від середнього - нестабільно
        return (maxDev < mean * 0.05);
    }
    
    // === Зчитування з оновленням статистики ===
    
    SensorData read() override {
        diagnostics.totalReads++;
        
        if (!ready) {
            diagnostics.failedReads++;
            diagnostics.currentStatus = HealthStatus::NOT_FOUND;
            return {0, 0, 0, millis(), false};
        }
        
        float rp = readRP();
        
        // Перевірка валідності
        if (rp < 0 || rp > 10000) {
            diagnostics.failedReads++;
            diagnostics.currentStatus = HealthStatus::OUT_OF_RANGE;
            diagnostics.lastError = {7, "Reading out of valid range"};
            return {0, 0, 0, millis(), false};
        }
        
        // Успішне зчитування
        diagnostics.lastSuccess = millis();
        diagnostics.currentStatus = HealthStatus::OK;
        
        return {
            .value1 = rp,
            .value2 = 0,
            .confidence = 0.95f,
            .timestamp = millis(),
            .valid = true
        };
    }
    
private:
    uint32_t lastCalibrationTime = 0;
    float calibrationBaseline = 0;
    
    uint8_t readRegister(uint8_t reg) {
        // ✅ CONTRACT §1.5: SPI shared bus (ADR-ST-008) — завжди брати spiMutex.
        // SD карта (SDTransport Logger) і LDC1101 — одна VSPI шина; без mutex → SPI corruption.
        if (!ctx || !ctx->spi || !ctx->spiMutex || csPin < 0) { diagnostics.failedReads++; return 0xFF; }
        if (xSemaphoreTake(ctx->spiMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
            diagnostics.failedReads++;
            return 0xFF;  // Mutex timeout — SDTransport тримає шину, пропустити читання
        }
        ctx->spi->beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
        digitalWrite(csPin, LOW);
        ctx->spi->transfer(reg | 0x80);  // R/W bit = 1 (read)
        uint8_t val = ctx->spi->transfer(0x00);
        digitalWrite(csPin, HIGH);
        ctx->spi->endTransaction();
        xSemaphoreGive(ctx->spiMutex);
        return val;
    }
    
    void writeRegister(uint8_t reg, uint8_t value) {
        if (!ctx || !ctx->spi || !ctx->spiMutex || csPin < 0) return;
        if (xSemaphoreTake(ctx->spiMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;  // Mutex timeout
        ctx->spi->beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
        digitalWrite(csPin, LOW);
        ctx->spi->transfer(reg & 0x7F);  // R/W bit = 0 (write)
        ctx->spi->transfer(value);
        digitalWrite(csPin, HIGH);
        ctx->spi->endTransaction();
        xSemaphoreGive(ctx->spiMutex);
    }
    
    float readRP() {
        // Burst read: CS утримується LOW протягом читання обох регістрів в одній SPI-транзакції.
        // LDC1101 вимагає атомарного burst: спочатку REG_RP_DATA_LSB (0x21), потім MSB (0x22).
        // PA3-1 fix: замінено дві окремі readRegister() на один burst з CS LOW.
        if (!ctx->spiMutex) return 0.0f;
        uint16_t raw = 0;
        if (xSemaphoreTake(ctx->spiMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            ctx->spi->beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
            digitalWrite(csPin, LOW);
            ctx->spi->transfer(LDC1101_RP_LSB | 0x80);  // read bit set; LSB address першим
            uint8_t lsb = ctx->spi->transfer(0x00);
            uint8_t msb = ctx->spi->transfer(0x00);      // CS залишається LOW між байтами
            digitalWrite(csPin, HIGH);
            ctx->spi->endTransaction();
            xSemaphoreGive(ctx->spiMutex);
            raw = (static_cast<uint16_t>(msb) << 8) | lsb;
        }
        return raw * 0.1f;  // Convert to Ohm
    }
};
```

---

## 🖥️ 3. UI - Відображення діагностики

```cpp
void showDiagnosticsScreen() {
    displayPlugin->clear();
    displayPlugin->drawText(10, 5, "=== System Diagnostics ===", TFT_CYAN);
    
    int y = 25;
    auto plugins = pluginSystem->getAllPlugins();
    
    for (auto* plugin : plugins) {
        // Отримати статус
        IPlugin::HealthStatus status = plugin->getHealthStatus();
        
        // Вибрати колір та emoji
        uint32_t color;
        const char* emoji;
        
        switch (status) {
            case IPlugin::HealthStatus::OK:
                color = TFT_GREEN;
                emoji = "✅";
                break;
            case IPlugin::HealthStatus::OK_WITH_WARNINGS:
                color = TFT_YELLOW;
                emoji = "⚠️";
                break;
            case IPlugin::HealthStatus::DEGRADED:
                color = TFT_ORANGE;
                emoji = "🟡";
                break;
            case IPlugin::HealthStatus::NOT_FOUND:
            case IPlugin::HealthStatus::SENSOR_FAULT:
                color = TFT_RED;
                emoji = "❌";
                break;
            case IPlugin::HealthStatus::CALIBRATION_NEEDED:
                color = TFT_MAGENTA;
                emoji = "🔧";
                break;
            default:
                color = TFT_GRAY;
                emoji = "❓";
        }
        
        // Показати статус
        char line[40];
        snprintf(line, sizeof(line), "%s %-12s", emoji, plugin->getName());
        displayPlugin->drawText(10, y, line, color);
        
        // Показати статистику (тільки для IDiagnosticPlugin)
        auto* diagPlugin = dynamic_cast<IDiagnosticPlugin*>(plugin);
        if (diagPlugin) {
            auto stats = diagPlugin->getStatistics();
            snprintf(line, sizeof(line), "%d%% (%d/%d)", 
                stats.stats.successRate,
                stats.stats.totalReads - stats.stats.failedReads,
                stats.stats.totalReads
            );
            displayPlugin->drawText(160, y, line, TFT_WHITE);
        }
        
        y += 20;
    }
    
    displayPlugin->drawText(10, 115, "[M] Main Menu  [T] Run Tests", TFT_GRAY);
}
```

---

## 🔄 4. Автоматичний Health Monitoring

```cpp
// src/main.cpp

void setup() {
    // ...після ініціалізації плагінів...
    
    // Запустити self-test для всіх плагінів
    Serial.println("\n=== Running Self-Tests ===\n");
    
    for (auto* plugin : pluginSystem->getAllPlugins()) {
        Serial.printf("Testing %s...\n", plugin->getName());
        // runSelfTest() — метод IDiagnosticPlugin, не IPlugin.
        // Плагіни без IDiagnosticPlugin пропускаються (getHealthStatus() — default UNKNOWN).
        auto* diagPlugin = dynamic_cast<IDiagnosticPlugin*>(plugin);
        if (!diagPlugin) {
            Serial.printf("  ⏭️ SKIP (no IDiagnosticPlugin): %s\n", plugin->getName());
            continue;
        }
        bool passed = diagPlugin->runSelfTest();
        
        if (!passed) {
            auto error = plugin->getLastError();  // getLastError() — base IPlugin (default impl)
            Serial.printf("  ❌ FAILED: %s\n", error.message);
            
            // Вимкнути несправний плагін
            plugin->shutdown();
        }
    }
}

void loop() {
    static uint32_t lastHealthCheck = 0;
    
    // Кожні 10 секунд - health check
    if (millis() - lastHealthCheck > 10000) {
        lastHealthCheck = millis();
        
        for (auto* plugin : pluginSystem->getAllPlugins()) {
            auto status = plugin->getHealthStatus();
            
            if (status != IPlugin::HealthStatus::OK &&
                status != IPlugin::HealthStatus::OK_WITH_WARNINGS) {
                
                auto error = plugin->getLastError();
                Serial.printf("⚠️ %s: %s (code: %d)\n", 
                    plugin->getName(), 
                    error.message,
                    error.code
                );
                
                // Спробувати автоматично відновити
                if (status == IPlugin::HealthStatus::TIMEOUT ||
                    status == IPlugin::HealthStatus::COMMUNICATION_ERROR) {
                    
                    // PluginSystem зберігає ctx_ для всіх плагінів. Викликаємо attemptRecovery:
                    if (pluginSystem->attemptRecovery(plugin)) {
                        // attemptRecovery послідовність (per CONTRACT §1.1):
                        //   plugin->shutdown();
                        //   vTaskDelay(pdMS_TO_TICKS(100));
                        //   if (plugin->canInitialize()) plugin->initialize(&ctx_);  // ← guard!
                        //   без canInitialize() — infinite retry кожні 10 сек якщо hardware відсутнє
                        ctx->log->info(plugin->getName(), "Recovery successful");
                    }
                }
            }
        }
    }
    
    // Нормальна робота
    pluginSystem->updateAll();
}
```

---

## 📊 5. Logging діагностики

```cpp
// Збереження логів для аналізу

void logDiagnostics(SemaphoreHandle_t spiMutex) {
    // PA3-3 fix: SD.open() захищено spiMutex — LDC1101 та SD card на одній VSPI шині.
    if (!spiMutex) return;
    if (xSemaphoreTake(spiMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        // Не блокуємо loop() — якщо шина зайнята, пропускаємо цей запис
        return;
    }
    
    File log = SD.open("/diagnostics.log", FILE_APPEND);
    
    char timestamp[32];
    sprintf(timestamp, "[%lu] ", millis());
    log.print(timestamp);
    
    for (auto* plugin : pluginSystem->getAllPlugins()) {
        auto* diagPlugin = dynamic_cast<IDiagnosticPlugin*>(plugin);
        if (!diagPlugin) continue;  // Пропустити плагіни без повної діагностики
        auto result = diagPlugin->getStatistics();
        
        log.printf("%s: status=%d, success_rate=%d%%, errors=%d/%d\n",
            plugin->getName(),
            (int)result.status,
            result.stats.successRate,
            result.stats.failedReads,
            result.stats.totalReads
        );
    }
    
    log.close();
    xSemaphoreGive(spiMutex);
}
```

---

## 🎯 Переваги розширеної діагностики

| Аспект | Без діагностики | З діагностикою |
|--------|-----------------|----------------|
| **Помилки** | "Не працює" | "I2C NACK - device not found at 0x2A" |
| **Recovery** | Вимкнути все | Автоматична спроба відновлення |
| **Debugging** | Гадати що не так | Точний код помилки + статистика |
| **Maintenance** | Коли зламається | Попередження перед поломкою |
| **User Experience** | Просто вимикається | "Sensor dirty - please clean" |
| **Production** | ❌ Неприйнятно | ✅ Professional |

---

## 🔮 Додаткові фічі

### 1. Predictive Maintenance
```cpp
// Передбачення поломки
if (plugin->getStatistics().stats.successRate < 80) {
    showWarning("Sensor degrading - maintenance soon");
}
```

### 2. Remote Diagnostics

> ⚠️ WiFi HTTP у фоновому режимі заборонений в embedded plugin context (блокує CPU, порушує TWDT, вимагає WiFi stack).  
> Для зовнішньої передачі діагностики — використовуй BLE notifications через `IConnectivityPlugin`:

```cpp
// ✅ Безпечний варіант: відправка через BLE notification (неблокуючий)
void notifyDiagnosticsViaBLE(IConnectivityPlugin* ble,
                             const IPlugin::DiagnosticResult& result) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s:%d:%lu/%lu",
        result.error.message,
        (int)result.status,
        result.stats.failedReads,
        result.stats.totalReads
    );
    ble->sendNotification("DIAG", buf);  // Неблокуючий, максимум 20 байт
}
```

### 3. Crash Reports
```cpp
// При критичній помилці - зберегти dump
void onCriticalError(IPlugin* plugin) {
    auto diag = plugin->runDiagnostics();
    saveCrashReport(plugin->getName(), diag);
    reboot();
}
```

---

## ✅ Висновок

### Система діагностики дозволяє:

1. ✅ **Точно визначити проблему** - не "не працює", а "I2C timeout at address 0x2A"
2. ✅ **Автоматичне відновлення** - спробувати restart при TIMEOUT/COMM_ERROR
3. ✅ **Predictive maintenance** - попередження перед поломкою
4. ✅ **Production-ready** - професійна система з логами
5. ✅ **User-friendly** - зрозумілі повідомлення + іконки
6. ✅ **Debugging** - статистика + коди помилок
7. ✅ **Self-test** - перевірка при старті
8. ✅ **Health monitoring** - періодична перевірка стану

### Імплементація:

**Базовий рівень (МІНІМУМ):**
- HealthStatus enum
- getHealthStatus()
- getLastError()

**Професійний рівень (РЕКОМЕНДОВАНО):**
- runDiagnostics()
- runSelfTest()
- getStatistics()
- Автоматичний health monitoring в loop()

---

**Статус:** 📝 Розширення базової архітектури  
**Пріоритет:** 🔴 ВИСОКИЙ - критично для production  
**Наступний крок:** Додати HealthStatus до базового IPlugin.h
