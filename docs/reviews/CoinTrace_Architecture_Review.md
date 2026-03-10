# Архітектурна рецензія: CoinTrace Plugin System

**Тип документа:** Незалежна архітектурна рецензія  
**Версія:** 1.0.0  
**Дата:** 10 березня 2026  
**Рецензент:** Незалежний архітектурний аудит  
**Статус:** Фінальна версія

---

## Зміст

1. [Вступ та методологія](#1-вступ-та-методологія)
2. [Загальна оцінка](#2-загальна-оцінка)
3. [Що зроблено добре](#3-що-зроблено-добре)
4. [Критичні проблеми](#4-критичні-проблеми--блокують-реалізацію)
5. [Середні проблеми](#5-середні-проблеми--знижують-якість)
6. [Додаткові рекомендації](#6-додаткові-рекомендації)
7. [Пріоритетний план дій](#7-пріоритетний-план-дій)
8. [Висновок](#8-висновок)

---

## 1. Вступ та методологія

### Що аналізувалось

Проведено детальний аналіз усіх 5 архітектурних документів проекту CoinTrace:

| Документ | Опис | Рядків |
|---|---|---|
| `PLUGIN_ARCHITECTURE.md` | Головний архітектурний документ | ~950 |
| `PLUGIN_INTERFACES_EXTENDED.md` | Повний список інтерфейсів | ~495 |
| `PLUGIN_DIAGNOSTICS.md` | Система self-diagnostics | ~720 |
| `CREATING_PLUGINS.md` | Практичний гайд для розробників | ~632 |
| `COMPARISON.md` | Порівняльний аналіз підходів | ~300 |

**Загальний обсяг:** ~3100 рядків документації та коду прикладів

### Контекст проекту

CoinTrace — портативний пристрій на базі ESP32-S3 (M5Stack Cardputer / CoreS3) для нумізматичної ідентифікації монет через комплекс фізичних вимірювань: вага, індуктивність, магнетизм, діаметр, товщина. Система плагінів забезпечує підтримку різного hardware без зміни основного коду.

**Поточний статус:** Архітектура спроектована, очікує імплементації.

---

## 2. Загальна оцінка

| Критерій | Оцінка | Коментар |
|---|---|---|
| Концепція та ідея | **9/10** | Правильна для embedded з кількома платформами |
| Якість документації | **7/10** | Добре структурована, є gaps |
| Технічна глибина | **6/10** | Поверхнево в concurrency, versioning, memory |
| Готовність до реалізації | **5/10** | Відсутні критичні деталі для імплементора |
| Тестованість архітектури | **4/10** | Майже не описана |
| **Загальна оцінка** | **7/10** | Вище середнього для embedded проекту |

**Загальний висновок:** Концепція правильна і амбітна. `PLUGIN_DIAGNOSTICS.md` показує зрілість мислення на production рівні. Критичні проблеми (відсутній `PluginContext`, `Wire.begin()` в кожному плагіні, відсутній thread safety контракт) вирішуються відносно легко — вони в документації, не в реалізації. Виправити їх зараз, до початку кодування, набагато дешевше.

---

## 3. Що зроблено добре

### 3.1 PLUGIN_DIAGNOSTICS.md — найсильніший документ

`HealthStatus` enum з 15+ станами (`NOT_FOUND`, `TIMEOUT`, `SENSOR_FAULT`, `DEGRADED`, `CALIBRATION_NEEDED`...) — це правильне enterprise-мислення.

Особливо цінно:
- Розрізнення `NOT_FOUND` (I2C NACK) від `COMMUNICATION_ERROR` (CRC fail) від `SENSOR_FAULT` (внутрішня помилка чіпа) — це те, що відрізняє production систему від учнівського проекту
- Автоматичне відновлення через `shutdown() → initialize()` при `TIMEOUT` — грамотне рішення
- `DiagnosticResult.stats` зі `successRate` та `failedReads` — готовність до predictive maintenance
- Чітке розмежування "базового" і "професійного" рівня імплементації діагностики

### 3.2 Поле `SensorData.valid` в розширеному інтерфейсі

В `PLUGIN_INTERFACES_EXTENDED.md` правильно додано поле `bool valid` в `SensorData` порівняно з базовим документом де його не було. Це показує що архітектура еволюціонує правильно.

### 3.3 Приклад AsyncSensorPlugin

В `CREATING_PLUGINS.md` наведено приклад з FreeRTOS mutex та окремим task для асинхронного зчитування — технічно правильний підхід для embedded:

```cpp
SemaphoreHandle_t dataMutex;
TaskHandle_t readTask;
// mutex + окремий task = правильно для ESP32-S3
```

### 3.4 Configuration-Driven підхід

Відокремлення hardware-специфіки у JSON профілі — правильна абстракція для embedded з кількома платформами. Приклади міграції Cardputer → CoreS3 через зміну одного рядка конфігу переконливо демонструють цінність підходу.

### 3.5 Plugin Lifecycle: наявність `canInitialize()`

Окремий крок перевірки доступності hardware до ініціалізації — грамотне рішення для Graceful Degradation. Дозволяє системі стартувати навіть якщо частина hardware відсутня.

### 3.6 Структура документів за аудиторіями

README.md розділяє читачів (Менеджер / Розробник / QA / Архітектор) з оцінкою часу читання. Це суттєво полегшує onboarding нових учасників.

### 3.7 plugin.json як self-документація

Метадані плагіна в JSON файлі (ім'я, версія, I2C адреса, залежності, hardware вимоги) — правильна практика яка дозволяє системі і людям розуміти плагін без читання коду.

---

## 4. Критичні проблеми — блокують реалізацію

### 4.1 🔴 `Wire.begin()` в кожному плагіні — системна помилка

**Де виявлено:** `PLUGIN_ARCHITECTURE.md`, `CREATING_PLUGINS.md`, всі шаблони I2C плагінів

**Проблема:** Кожен I2C плагін виконує `Wire.begin()` у своєму `canInitialize()`:

```cpp
// Повторюється в КОЖНОМУ плагіні!
bool canInitialize() override {
    Wire.begin();  // ← ПРОБЛЕМА
    Wire.beginTransmission(I2C_ADDR);
    return (Wire.endTransmission() == 0);
}
```

Якщо є 5 I2C плагінів — `Wire.begin()` викликається 5 разів. На ESP32-S3 це може перезаписувати піни I2C шини і скидати вже встановлені з'єднання. Крім того, якщо різні плагіни викличуть `Wire.begin()` з різними параметрами — поведінка непередбачувана.

**Рішення:**

```cpp
// PluginSystem або ConfigManager ініціалізує Wire один раз:
Wire.begin(config.i2c_sda, config.i2c_scl);

// Плагіни отримують вже ініціалізований Wire через PluginContext:
bool initialize(PluginContext* ctx) override {
    Wire.beginTransmission(I2C_ADDR);  // без Wire.begin()!
    return (Wire.endTransmission() == 0);
}
```

---

### 4.2 🔴 Відсутній `PluginContext` — приховані глобальні залежності

**Де виявлено:** `PLUGIN_INTERFACES_EXTENDED.md` (функція `analyzeCoin()`)

**Проблема:** В прикладі коду:

```cpp
void analyzeCoin() {
    auto weightSensor = pluginSystem->getPlugin<ISensorPlugin>("HX711");
    // ...звідки pluginSystem? Глобальна змінна? Синглтон?
}
```

Ніде не пояснено як плагіни та бізнес-логіка отримують доступ до `PluginSystem`. Це веде до прихованих глобальних залежностей або Service Locator антипатерну.

**Рішення:** Ввести явний об'єкт контексту який передається явно:

```cpp
struct PluginContext {
    TwoWire*       wire;    // Ініціалізований I2C
    SPIClass*      spi;     // Ініціалізований SPI
    ConfigManager* config;  // Доступ до конфігурації
    Logger*        log;     // Уніфіковане логування
};

// Базовий інтерфейс отримує контекст при ініціалізації:
class IPlugin {
    virtual bool initialize(PluginContext* ctx) = 0;
};
```

---

### 4.3 🔴 `SensorData` — один struct для 15+ типів сенсорів

**Де виявлено:** `PLUGIN_ARCHITECTURE.md`, `PLUGIN_INTERFACES_EXTENDED.md`

**Проблема:**

```cpp
struct SensorData {
    float value1;   // Для LDC1101 = RP (Ohm)
    float value2;   // Для LDC1101 = L (uH)
    // Для HX711: value1 = вага (g), value2 = ?
    // Для QMC5883L: value1 = ?, value2 = ?
};
```

Споживач плагіна мусить **знати реалізацію** щоб правильно трактувати `value1`. Це порушує принцип абстракції і робить код крихким при зміні реалізації.

**Рішення — мінімальне:** Додати поле `unit`:

```cpp
struct SensorData {
    float value1;
    float value2;
    const char* unit;    // "g", "Gauss", "Ohm", "lux"
    const char* unit2;   // для value2
    float confidence;
    uint32_t timestamp;
    bool valid;
};
```

**Рішення — повне (рекомендовано):** Типізовані структури для кожного типу сенсора або використання `SensorMetadata` для опису семантики полів.

---

### 4.4 🔴 Дублювання метаданих між `plugin.json` і C++ кодом

**Де виявлено:** Всі приклади плагінів

**Проблема:** Одні й ті самі дані в двох місцях:

```json
// plugin.json
{"name": "QMC5883L", "version": "1.0.0", "i2c_address": "0x0D"}
```

```cpp
// QMC5883LPlugin.h
const char* getName() const override { return "QMC5883L"; }
static const uint8_t I2C_ADDR = 0x0D;
```

Дві точки правди = гарантована розсинхронізація. Розробник оновить версію в JSON, забуде оновити в коді — баг знайдуть тільки в runtime.

**Рішення:** Або генерувати частину коду з JSON як build step, або визначити яке джерело є головним і явно задокументувати це правило в `CREATING_PLUGINS.md`.

---

### 4.5 🔴 Thread Safety — контракт відсутній в архітектурі

**Де виявлено:** `PLUGIN_ARCHITECTURE.md` (відсутність), `CREATING_PLUGINS.md` (лише в прикладі)

**Проблема:** ESP32-S3 має 2 ядра. FreeRTOS може викликати `plugin->update()` та `plugin->read()` з різних tasks одночасно. Базовий `IPlugin` не має жодних вимог до thread safety — розробник плагіна не знає що від нього очікується.

**Рішення:** Додати явний контракт в `IPlugin.h`:

```cpp
class IPlugin {
    // === THREAD SAFETY CONTRACT ===
    // update() — called from main task (Core 0)
    // read()   — may be called from any task
    // Implementors MUST ensure thread safety for read()
    // Use xSemaphoreCreateMutex() if accessing shared state
    
    virtual void update() = 0;  // Main task only
    virtual SensorData read() = 0;  // Must be thread-safe
};
```

---

## 5. Середні проблеми — знижують якість

### 5.1 🟡 Lifecycle діаграма не відповідає HealthStatus

**Де виявлено:** `PLUGIN_ARCHITECTURE.md` vs `PLUGIN_DIAGNOSTICS.md`

В головному документі lifecycle:
```
New → Testing → Ready → update() → shutdown()
```

В `PLUGIN_DIAGNOSTICS.md` є 15 станів (DEGRADED, TIMEOUT, RECOVERING...). Ці дві моделі суперечать одна одній. Розробник читає одне в головному документі, інше в діагностиці.

**Рекомендація:** Замінити спрощену діаграму на повну що включає всі стани з HealthStatus:

```
New → [canInitialize()] → INITIALIZATION_FAILED
                        ↓ (true)
                   [initialize()]
                        ↓
                      READY (OK)
                        ↓
                    update()/read()
                   ↙         ↘
          OK_WITH_WARNINGS   DEGRADED
                ↓               ↓
             TIMEOUT ──→ [auto-recovery]
                ↓           ↓
            DISABLED     SENSOR_FAULT
                ↓
           shutdown()
```

---

### 5.2 🟡 `initialize()` дублює виклик `canInitialize()`

**Де виявлено:** `PLUGIN_ARCHITECTURE.md`, всі приклади

```cpp
bool initialize() override {
    if (!canInitialize()) return false;  // ПОВТОРНИЙ I2C scan!
    // ...
}
```

Система викликає `canInitialize()` → якщо true → `initialize()`. Повторна перевірка всередині `initialize()` — це або зайва I2C операція, або потенційна розбіжність (між двома викликами hardware міг відключитись і поведінка стає непередбачуваною).

**Рекомендація:** `initialize()` не повинен повторювати перевірку. Система гарантує виклик `canInitialize()` перед `initialize()`. Задокументувати цей контракт явно.

---

### 5.3 🟡 `getGraphicsLibrary()` повертає `void*` — зламана типобезпека

**Де виявлено:** `PLUGIN_INTERFACES_EXTENDED.md`

```cpp
virtual void* getGraphicsLibrary() { return nullptr; }
```

Споживач мусить кастити до конкретного типу (`M5GFX*`, `TFT_eSPI*`...) — а значить знає реалізацію. Абстракція `IDisplayPlugin` не працює для advanced операцій.

**Рекомендація:** Або розширити `IDisplayPlugin` достатньою кількістю примітивів щоб покрити 95% use cases, або замість `void*` використати template:

```cpp
template<typename T>
T* getGraphicsLibrary() { return static_cast<T*>(getLibraryPtr()); }
```

---

### 5.4 🟡 Hot-Reload та OTA — нереалістично для ESP32

**Де виявлено:** `PLUGIN_ARCHITECTURE.md` (розділ "Креативні фічі")

```cpp
pluginSystem->reloadPlugin("QMC5883L");        // Hot-Reload
pluginSystem->installPluginOTA("https://...");  // OTA install
```

ESP32 не підтримує dynamic loading бібліотек в runtime без повного перезавантаження. Ці "фічі" або потребують окремого bootloader з OTA розділами і завжди спричинять reboot, або не реалізуються взагалі.

**Рекомендація:** Або прибрати з документації, або чесно написати: *"OTA оновлення вимагає перезавантаження пристрою. Hot-Reload не підтримується bare-metal ESP32."*

---

### 5.5 🟡 Цифри без методології в COMPARISON.md

**Де виявлено:** `COMPARISON.md`

```
-89% часу розробки, -90% ризику багів, +300% швидкість
```

Конкретні відсотки без методології розрахунку — це маркетинг, не технічний аргумент. Технічний архітектор або менеджер з досвідом поставиться скептично до всього документу.

**Рекомендація:** Замінити відсотки на конкретні приклади з вимірюваними показниками:

> "Додавання датчика QMC5883L: традиційний підхід — 7 файлів змінити (~4-6 год), Plugin System — 3 нові файли + 1 рядок конфігу (~30-60 хв). Тест: тільки новий плагін vs вся система."

---

### 5.6 🟡 Memory Management — повністю відсутній

**Де виявлено:** Відсутність у всіх документах

ESP32-S3 має 512KB SRAM (+ 8MB PSRAM на деяких моделях). Питання що не описані:
- Де живуть плагіни в пам'яті? Heap? Static?
- Хто відповідає за `delete` плагіна?
- Чи використовуються smart pointers (`std::unique_ptr`) чи ручний менеджмент?
- Чи враховується фрагментація heap при dynamic allocation?

**Рекомендація:** Додати розділ Memory Management в `PLUGIN_ARCHITECTURE.md` з явними відповідями.

---

## 6. Додаткові рекомендації

Ці питання не є помилками в поточній документації, але їх вирішення на етапі проектування значно спростить реалізацію і майбутню підтримку.

### 6.1 Версіонування інтерфейсів

Поточна документація не описує що станеться коли `ISensorPlugin` зміниться. Якщо додати новий обов'язковий метод — всі існуючі плагіни перестануть компілюватись.

**Рекомендація:** Додати стратегію backward compatibility:

```cpp
// Варіант: Default implementation для нових методів
class ISensorPlugin : public IPlugin {
    // Новий метод з default — не ламає старі плагіни
    virtual bool supportsInterrupts() const { return false; }
    virtual void onInterrupt() {}
};
```

Або ввести `PLUGIN_API_VERSION` константу і перевірку сумісності при завантаженні плагіна.

---

### 6.2 Відсутній документ `PLUGIN_CONTRACT.md`

Найважливіший відсутній документ — формальний контракт між системою і плагіном. Не "як написати плагін", а "що система гарантує плагіну і що плагін зобов'язаний гарантувати системі".

**Рекомендована структура:**

```
Система гарантує плагіну:
- canInitialize() завжди викликається до initialize()
- initialize() не викликається якщо canInitialize() повернув false
- update() не викликається якщо initialize() повернув false
- shutdown() завжди викликається перед деструктором
- Wire вже ініціалізований до першого виклику

Плагін зобов'язаний гарантувати системі:
- update() не блокує довше ніж 10ms (або вказати свій ліміт)
- read() є thread-safe
- Не кидати exceptions з жодного методу
- Повертати валідний HealthStatus з getHealthStatus()
- Не викликати Wire.begin(), SPI.begin() самостійно
```

---

### 6.3 Відсутній `TESTING_GUIDE.md`

README згадує `MockSensorPlugin` та Unity Test Framework, але немає документа про те як тестувати плагін без реального hardware. Це критично для CI/CD і для розробників без конкретного датчика під рукою.

**Рекомендований мінімум:**

```cpp
// MockSensorPlugin — базовий шаблон для тестів
class MockSensorPlugin : public ISensorPlugin {
    float mockValue = 0.0f;
public:
    void setMockValue(float v) { mockValue = v; }
    SensorData read() override {
        return {mockValue, 0, 1.0f, millis(), true};
    }
    bool canInitialize() override { return true; }
    bool initialize() override { return true; }
    // ...
};

// Тест:
MockSensorPlugin mock;
mock.setMockValue(7.5f);  // 7.5 грам
auto result = coinIdentifier.identify(mock);
assert(result.confidence > 0.8f);
```

---

### 6.4 Dependency Resolution між плагінами

В `PLUGIN_ARCHITECTURE.md` описано dependency в `plugin.json` для бібліотек. В `CREATING_PLUGINS.md` є приклад `FusionSensorPlugin` з залежністю від двох інших плагінів. Але механізм вирішення залежностей між плагінами не описаний.

**Питання які потрібно вирішити:**
- Якщо `FusionPlugin` залежить від `HX711Plugin` — як він його отримує?
- Чи враховується порядок ініціалізації залежно від залежностей?
- Що відбувається якщо `HX711Plugin` не ініціалізувався — `FusionPlugin` вимикається?

---

### 6.5 Стратегія логування

В прикладах використовується `Serial.printf()` напряму. Для production системи це означає що логи завжди виводяться навіть у release build, і немає можливості змінити рівень логування без перекомпіляції.

**Рекомендація:** Ввести простий `Logger` в `PluginContext`:

```cpp
struct Logger {
    enum Level { DEBUG, INFO, WARNING, ERROR };
    void log(Level level, const char* plugin, const char* msg);
};

// В плагіні:
ctx->log->log(Logger::WARNING, getName(), "Calibration needed");
```

---

### 6.6 Plugin Registry — формат та валідація

`plugin.json` використовується як метадані, але схема JSON не визначена. Без JSON Schema валідація відсутня і помилки знайдуться тільки в runtime.

**Рекомендація:** Додати JSON Schema для `plugin.json` і валідацію при старті. Це можна зробити через `ArduinoJson` з перевіркою обов'язкових полів:

```cpp
bool validatePluginJson(JsonDocument& doc) {
    return doc.containsKey("name") &&
           doc.containsKey("version") &&
           doc.containsKey("type");
}
```

---

## 7. Пріоритетний план дій

### Фаза 1 — До початку кодування (критично)

| # | Дія | Де | Складність |
|---|---|---|---|
| 1 | Ввести `PluginContext` структуру | `IPlugin.h` | Низька |
| 2 | Прибрати `Wire.begin()` з усіх плагінів, задокументувати | `CREATING_PLUGINS.md` | Низька |
| 3 | Додати Thread Safety контракт в `IPlugin.h` | `PLUGIN_ARCHITECTURE.md` | Низька |
| 4 | Уніфікувати lifecycle діаграму з HealthStatus | `PLUGIN_ARCHITECTURE.md` | Середня |
| 5 | Вирішити питання версіонування інтерфейсів | Новий розділ | Середня |

### Фаза 2 — Паралельно з реалізацією ядра

| # | Дія | Де | Складність |
|---|---|---|---|
| 6 | Написати `PLUGIN_CONTRACT.md` | Новий документ | Середня |
| 7 | Додати `unit` поле в `SensorData` | `IPlugin.h` | Низька |
| 8 | Визначити стратегію Memory Management | `PLUGIN_ARCHITECTURE.md` | Середня |
| 9 | Вирішити дублювання метаданих JSON/код | `CREATING_PLUGINS.md` | Середня |

### Фаза 3 — Перед першим release

| # | Дія | Де | Складність |
|---|---|---|---|
| 10 | Написати `TESTING_GUIDE.md` з MockSensorPlugin | Новий документ | Середня |
| 11 | Додати Logger в PluginContext | `PluginContext` | Низька |
| 12 | Замінити відсоткові цифри на конкретні приклади | `COMPARISON.md` | Низька |
| 13 | Прибрати або уточнити Hot-Reload/OTA | `PLUGIN_ARCHITECTURE.md` | Низька |
| 14 | Додати JSON Schema для plugin.json | Нова схема | Середня |

---

## 8. Висновок

### Сильні сторони проекту

CoinTrace Plugin System — це архітектурно правильне рішення для embedded системи з підтримкою кількох hardware платформ. Рівень документації значно вищий за середній для проектів подібного масштабу. `PLUGIN_DIAGNOSTICS.md` демонструє production-рівень мислення: structured error codes, auto-recovery, predictive maintenance.

### Основний ризик

Головний ризик для успішної реалізації — не концептуальний, а деталей реалізації. Без `PluginContext`, без явного Thread Safety контракту і без вирішення питання `Wire.begin()` перші ж спроби написати реальний код виявлять ці проблеми. Краще вирішити їх у документації зараз, ніж у коді потім.

### Рекомендація

**Не починати кодування поки не виконана Фаза 1** (5 пунктів). Це 1-2 дні роботи архітектора, але заощадить тиждень debugging під час реалізації.

---

*Документ підготовлено для передачі архітектору проекту.*  
*Версія: 1.0.0 | Дата: 10 березня 2026*
