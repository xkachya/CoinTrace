# Plugin Architecture — Незалежний Архітектурний Аудит v2.0.0

**Документ:** PLUGIN_AUDIT_v2.0.0.md  
**Аудитовані документи (поточний стан після всіх v1.0.0 виправлень):**
- `PLUGIN_ARCHITECTURE.md` v1.1.0+ (PA-1..PA-13, PA-19, PA-21..PA-30 закриті)
- `PLUGIN_CONTRACT.md` v1.1.0+ (PA-5, PA-6, PA-10, PA-15 закриті)
- `PLUGIN_INTERFACES_EXTENDED.md` v1.2.0+ (PA-14, PA-27 закриті)
- `PLUGIN_DIAGNOSTICS.md` v1.1.0+ (PA-3, PA-4, PA-8, PA-9, PA-11, PA-17, PA-18, PA-21, PA-22, PA-28, PA-30 закриті)
- `CREATING_PLUGINS.md` v1.1.0+ (PA-1, PA-5, PA-26 закриті)

**Попередній аудит:** PLUGIN_AUDIT_v1.0.0.md → `docs/architecture/obsolete/PLUGIN_AUDIT_v1.0.0.md`  
**Відкриті знахідки з v1.0.0 перенесені:** PA-12 (MEDIUM), PA-16 (LOW) → нові ID: C-PA12, C-PA16  
**PA-20 з v1.0.0** → повністю замінена ширшою PA2-3 (compile error scope)

**Дата аудиту:** 2026-03-13  
**Методологія:**
- Cross-document compilation simulation (всі 5 docs як єдиний header-set)
- FreeRTOS execution timeline modeling (update loop budget, priority inversion analysis)
- Memory allocation simulation (ESP32-S3FN8, 337 KB heap, no PSRAM)
- SPI bus contention probability modeling (LDC1101 + SD card on VSPI)
- Plugin lifecycle FSM tracing (canInit → init → update → shutdown)
- ADR consistency check (ADR-ST-008, ADR-ST-001, CONNECTIVITY §1.5)
- New: template code path analysis (all copyable examples executed mentally)

**Контекст:** Другий незалежний аудит. Фокус — знахідки введені або пропущені під час v1.0.0 виправлень. Всі v1.0.0 CRITICAL/HIGH враховані як закриті; повторно перевірено тільки ті що відкриті.

---

## Зміст

1. [Загальна оцінка](#1-загальна-оцінка)
2. [Score-карта знахідок](#2-score-карта-знахідок)
3. [Симуляційне моделювання](#3-симуляційне-моделювання)
4. [PA2-1 — IPlugin: getHealthStatus() та getLastError() не реалізовані у прикладах](#4-pa2-1--iplugin-gethealthstatus-та-getlasterror-не-реалізовані-у-прикладах)
5. [PA2-2 — PluginSystem::getAllPlugins() — метод не визначено](#5-pa2-2--pluginsystemgetallplugins--метод-не-визначено)
6. [PA2-3 — IDiagnosticPlugin методи викликаються на IPlugin* — compile error](#6-pa2-3--idiagnosticplugin-методи-викликаються-на-iplugin--compile-error)
7. [PA2-4 — PluginSystem::attemptRecovery() — метод не визначено](#7-pa2-4--pluginsystemattemptrecovery--метод-не-визначено)
8. [PA2-5 — CONTRACT §1.3 PluginContext struct не оновлено після PA-25](#8-pa2-5--contract-13-plugincontext-struct-не-оновлено-після-pa-25)
9. [PA2-6 — MySPISensorPlugin template: SPI без spiMutex](#9-pa2-6--myspisensorplugin-template-spi-без-spimutex)
10. [PA2-7 — IIMUPlugin::calibrate() повертає void, ISensorPlugin — bool](#10-pa2-7--iimuplagin-calibrate-повертає-void-isensorplugin--bool)
11. [PA2-8 — AsyncSensorPlugin: TaskHandle_t не видаляється в shutdown()](#11-pa2-8--asyncsensorplugin-taskhandle_t-не-видаляється-в-shutdown)
12. [PA2-9 — IPlugin: три несумісних визначення в різних документах](#12-pa2-9--iplugin-три-несумісних-визначення-в-різних-документах)
13. [PA2-10 — ISensorPlugin::getMetadata() pure virtual відсутній у ARCH definition](#13-pa2-10--isensorplugingetmetadata-pure-virtual-відсутній-у-arch-definition)
14. [PA2-11 — CREATING_PLUGINS §Поради: Serial.println() у copyable example](#14-pa2-11--creating_plugins-поради-serialprintln-у-copyable-example)
15. [PA2-12 — Mock PluginContext у тестах: .config = nullptr → crash](#15-pa2-12--mock-plugincontext-у-тестах-config--nullptr--crash)
16. [PA2-13 — Memory budget: display framebuffer не врахований](#16-pa2-13--memory-budget-display-framebuffer-не-врахований)
17. [PA2-14 — ARCH config.json: inline "config" поле суперечить canonical PA-13 моделі](#17-pa2-14--arch-configjson-inline-config-поле-суперечить-canonical-pa-13-моделі)
18. [PA2-15 — DIAGNOSTICS §4: ctx->log в main() loop() — out-of-scope reference](#18-pa2-15--diagnostics-4-ctx-log-в-main-loop--out-of-scope-reference)
19. [PA2-16 — checkCalibration(): calibrationBaseline = 0 при старті — завжди false](#19-pa2-16--checkcalibration-calibrationbaseline--0-при-старті--завжди-false)
20. [PA2-17 — CREATING_PLUGINS example: BH1750 read() без wireMutex з Core 1](#20-pa2-17--creating_plugins-example-bh1750-read-без-wiremutex-з-core-1)
21. [PA2-18 — Plugin Registry/OTA механізми посилаються на невизначені методи](#21-pa2-18--plugin-registryota-механізми-посилаються-на-невизначені-методи)
22. [C-PA12 (перенесено) — Factory hardcoding суперечить extensibility promise](#22-c-pa12-перенесено--factory-hardcoding-суперечить-extensibility-promise)
23. [C-PA16 (перенесено) — Plugin dependency injection: механізм не визначено](#23-c-pa16-перенесено--plugin-dependency-injection-механізм-не-визначено)
24. [Що архітектура зробила ПРАВИЛЬНО](#24-що-архітектура-зробила-правильно)
25. [Pre-implementation Checklist — перед P-2](#25-pre-implementation-checklist--перед-p-2)
26. [Backlog та пріоритети v2.0.0](#26-backlog-та-пріоритети-v200)
27. [Cross-reference: v1.0.0 → v2.0.0](#27-cross-reference-v100--v200)

---

## 1. Загальна оцінка

v1.0.0 аудит закрив усі блокуючі для компіляції базові дефекти (PA-1..PA-3), hardware bugs (PA-4, PA-11) та critical regressions (PA-21..PA-23). Архітектура значно зміцнена: PluginContext правильний, SPI mutex додано, canonical config model задокументована.

**Проте:**

Документи розроблялись як три паралельних шари (`базова архітектура + контракт + діагностика`) і при інтеграції виник новий клас проблем — **шари не компілюються разом**. Конкретно: PLUGIN_DIAGNOSTICS розширив `IPlugin` двома чистими віртуальними методами (`getHealthStatus()`, `getLastError()`), але жоден з 10+ прикладних плагінів в ARCH, CREATING_PLUGINS та CONTRACT не реалізує ці методи. Результат: всі плагіни є абстрактними класами і не можуть бути інстанційовані.

Додатково: DIAGNOSTICS §3–5 викликає методи `getAllPlugins()`, `attemptRecovery()`, `runSelfTest()`, `getStatistics()` — жоден з цих методів не визначений або викликається на неправильному типі.

Якщо в v1.0.0 головна проблема була **неправильний код**, то в v2.0.0 головна проблема — **неінтегровані шари документації**.

**Загальна оцінка:** 7.5 / 10 (концепція strong, implementations cross-checked, але шари не зімкнуті)

**Нових блокуючих для P-2:** PA2-1, PA2-2, PA2-3 (3 compile-blocking)

---

## 2. Score-карта знахідок

### Нові знахідки (v2.0.0)

| ID | Документ | Знахідка | Severity | Статус |
|---|---|---|---|---|
| **PA2-1** | DIAGNOSTICS §1 + ARCH/CREATING | `IPlugin` +2 pure virtual (`getHealthStatus`, `getLastError`) — всі 10+ плагінів-прикладів абстрактні | 🔴 CRITICAL | ✅ CLOSED |
| **PA2-2** | DIAGNOSTICS §3,§4,§5 + ARCH §PluginSystem | `pluginSystem->getAllPlugins()` — метод не оголошений у `PluginSystem` | 🔴 CRITICAL | ✅ CLOSED |
| **PA2-3** | DIAGNOSTICS §3,§4,§5 | `plugin->runSelfTest()`, `plugin->getStatistics()` на `IPlugin*` — методи тільки на `IDiagnosticPlugin*` — compile error | 🔴 CRITICAL | ✅ CLOSED |
| **PA2-4** | DIAGNOSTICS §4 + ARCH §PluginSystem | `pluginSystem->attemptRecovery(plugin)` — метод не оголошений у `PluginSystem` | 🟠 HIGH | ✅ CLOSED |
| **PA2-5** | CONTRACT §1.3 vs ARCH §PluginContext | `PluginContext` в CONTRACT відсутнє поле `IStorageManager* storage` (PA-25 fix не поширений) | 🟠 HIGH | ✅ CLOSED |
| **PA2-6** | CREATING_PLUGINS §SPI template | `MySPISensorPlugin::read()`: `spi->transfer16()` без `xSemaphoreTake(ctx->spiMutex)` — violates ADR-ST-008 | 🟠 HIGH | ✅ CLOSED |
| **PA2-7** | ARCH §IIMUPlugin vs CONTRACT §3.4 | `IIMUPlugin::calibrate()` → `void`; `ISensorPlugin::calibrate()` → `bool` — асиметрія не задокументована, CONTRACT неоднозначний | 🟠 HIGH | ✅ CLOSED |
| **PA2-8** | CREATING_PLUGINS §AsyncSensorPlugin | `TaskHandle_t readTask` не видаляється в `shutdown()` — FreeRTOS task leak при plugin reload | 🟠 HIGH | ✅ CLOSED |
| **PA2-9** | ARCH Крок1 / ARCH §Оновлений / DIAGNOSTICS §1 | `IPlugin` визначений тричі з різними наборами методів — немає єдиного канонічного визначення | 🟡 MEDIUM | ✅ CLOSED |
| **PA2-10** | INTERFACES_EXTENDED §1 vs ARCH §ISensorPlugin | `ISensorPlugin::getMetadata() const = 0` в INTERFACES — pure virtual — відсутній в ARCH definition → абстракція на abstragi, неузгодженість | 🟡 MEDIUM | ✅ CLOSED |
| **PA2-11** | CREATING_PLUGINS §Поради §3 | `readWithTimeout()`: `Serial.println("ERROR: …")` — copyable example порушує CONTRACT §2.4 | 🟡 MEDIUM | ✅ CLOSED |
| **PA2-12** | CREATING_PLUGINS §Debugging | Mock `PluginContext`: `.config = nullptr` — плагіни що викликають `ctx->config->getInt()` в initialize() крашать тест | 🟡 MEDIUM | 🔓 OPEN |
| **PA2-13** | ARCH §Memory Management | Memory budget не враховує display framebuffer (~63 KB heap) — документований залишок 0 KB насправді −63 KB | 🟡 MEDIUM | ✅ CLOSED |
| **PA2-14** | ARCH §data/config.json example | Inline `"config": "plugins/ldc1101.json"` поле у plugin entry суперечить PA-13 canonical model | 🟡 MEDIUM | ✅ CLOSED |
| **PA2-15** | DIAGNOSTICS §4 §5 | `ctx->log->info(...)` та `SD.open()` безпосередньо у `main.cpp` loop() — `ctx` є private членом PluginSystem, недоступний у main scope | 🟡 MEDIUM | 🔓 OPEN |
| **PA2-16** | DIAGNOSTICS §2 `checkCalibration()` | `calibrationBaseline = 0` при старті → перша перевірка `< 100` завжди `false` → `runDiagnostics()` завжди `CALIBRATION_NEEDED` до явного calibrate() | 🟢 LOW | 🔓 OPEN |
| **PA2-17** | CREATING_PLUGINS §BH1750 `read()` | `ctx->wire->requestFrom()` без `wireMutex` у `read()` — race condition якщо read() викликається з Core 1 (CONTRACT §2.2) | 🟢 LOW | 🔓 OPEN |
| **PA2-18** | ARCH §Креативні фічі | `reloadPlugin()`, `installPluginOTA()` використовуються як real API але не визначені в `PluginSystem`; OTA registry URL формат суперечить CONNECTIVITY_ARCH (BLE/WS) | 🟢 LOW | 🔓 OPEN |

### Перенесені відкриті знахідки з v1.0.0

| ID | Оригінал | Знахідка | Severity | Статус |
|---|---|---|---|---|
| **C-PA12** | PA-12 v1.0.0 | Factory hardcoding (`if name == "LDC1101"`) суперечить extensibility promise | 🟡 MEDIUM | ✅ CLOSED |
| **C-PA16** | PA-16 v1.0.0 | Plugin dependency injection — механізм не визначено | 🟢 LOW | 🔓 OPEN |

> **PA-20 v1.0.0** (`getStatistics() const` side-effects) → замінена ширшою PA2-3 (methods on wrong type). Окремо не відстежується.

### Підсумок v2.0.0

**Разом нових:** 3 CRITICAL · 4 HIGH · 6 MEDIUM · 3 LOW = **16 нових знахідок**  
**Перенесено з v1.0.0:** C-PA12 (MEDIUM) · C-PA16 (LOW) = **2 carried**  
**Всього:** 18 знахідок | **✅ CLOSED: 14** | **🔓 OPEN: 5** (PA2-12, PA2-15, PA2-16, PA2-17, PA2-18, C-PA16)

---

## 3. Симуляційне моделювання

### 3.1 FreeRTOS Update Loop — Timing Simulation

**Налаштування:** 4 плагіни (LDC1101, BMI270, QMC5883L, SDCard). Кожен `update()` ≤ 10 ms за CONTRACT §2.1. `delay(10)` в main loop.

```
Цикл loop() → хронологічна симуляція:

t=0      ms: pluginSystem->updateAll() розпочато
t=0..10  ms: LDC1101Plugin::update()   [SPI read, ≤10 ms]
t=10..20 ms: BMI270Plugin::update()    [I2C read, ≤10 ms]
t=20..30 ms: QMC5883LPlugin::update()  [I2C read, ≤10 ms]
t=30..40 ms: SDCardPlugin::update()    [SPI flush, ≤10 ms]
t=40     ms: updateAll() завершено
t=40..50 ms: delay(10)                 [business logic]
t=50     ms: наступний виток loop()

→ Фактична частота: 1/0.050 = 20 Hz ← OK (CONTRACT гарантує ≥10 Hz)
```

**Проблема: 10 плагінів (майбутнє масштабування):**
```
10 плагінів × 10 ms = 100 ms + delay(10) = 110 ms → 9.1 Hz ← ПОРУШЕННЯ CONTRACT §1.1!
```

> **⚠️ Висновок:** CONTRACT §1.1 гарантує `update()` ≥ 10 Hz, але при 10+ плагінах з максимальним 10 ms update() це фізично неможливо без паралелізації. **CONTRACT повинен уточнити гарантію відносно кількості плагінів.**

---

### 3.2 Memory Allocation Simulation — M5Stack Cardputer-ADV

**Фізична RAM:** ESP32-S3FN8 = 337 KB вільного heap (без PSRAM).

```
КОМПОНЕНТ                          ОЦІНКА (KB)    ПРИМІТКА
─────────────────────────────────────────────────────────────
FreeRTOS kernel + tick ISR          ~8 KB
Main task stack (8 KB × 1)          ~8 KB
WiFi task (навіть неактивний)      ~12 KB         esp_wifi_init() резервує
BLE NimBLE stack (якщо активний)   ~20 KB         CONNECTIVITY_ARCH uses BLE
Arduino framework overhead          ~15 KB
M5Cardputer display driver          ~5 KB
Display framebuffer 240×135×RGB565  ~63 KB  ← ⚠️ НЕ ВРАХОВАНИЙ В ПОТОЧНОМУ БЮДЖЕТІ
LittleFS mmap metadata              ~8 KB
ConfigManager + Logger              ~15 KB
PluginSystem core                   ~5 KB
ArduinoJson buffer                  ~8 KB
─────────────────────────────────────────────────────────────
OVERHEAD SUBTOTAL                  ~167 KB
─────────────────────────────────────────────────────────────
Залишок для плагінів:   337 - 167 = 170 KB
4 plugin × 8 KB limit:              ~32 KB
─────────────────────────────────────────────────────────────
HEAP ЗАЛИШОК:           170 - 32 = ~138 KB (Safety buffer)
─────────────────────────────────────────────────────────────
BEЗ WiFi/BLE:           337 - 147 = ~190 KB (більше простору)
```

**Поточний документований бюджет** (ARCH §Memory):
```
~177 KB — система
~160 KB — плагіни (20 × 8 KB)  ← НЕРЕАЛІСТИЧНО: 20 плагінів > 168 KB не залишається
~0 KB   — залишок             ← НЕ враховує display buffer (~63 KB)
```

> **⚠️ Висновок:** 20 плагінів × 8 KB = 160 KB для плагінів при залишку 170 KB (з display) — теоретично можливо але без резерву. Реальный ліміт: **10-12 плагінів** для safe margin. **Display framebuffer (~63 KB) відсутній у бюджеті** → PA2-13.

---

### 3.3 SPI Bus Contention — Probability Model

**Конфігурація:** LDC1101 + SD card (Logger SDTransport) на одній VSPI шині.

```
Шина VSPI (G40/G14/G39) — учасники:
  A: LDC1101Plugin::readRP()        — triggered від update() ~100ms, ≤1 ms
  B: SDTransport (Logger flush)     — triggered async, writes щосекунди, ~5 ms
  С: AsyncSensorPlugin (future)     — triggered від background task, ≤20 ms

Mutex-захищені транзакції (після PA-22 fix):
  Кожна транзакція бере spiMutex з timeout pdMS_TO_TICKS(50)

Без mutex (MySPISensorPlugin template — PA2-6):
  P(collision) за 1 секунду = 1 - (1 - p_A)(1 - p_B)
  p_A = 10ms/1000ms = 0.01 (10% duty якщо читаємо в кожному loop)
  p_B = 5ms/1000ms  = 0.005
  P(collision) ≈ 0.015 = 1.5% за секунду
  За 10 хвилин: 1 - (1-0.015)^600 = >99.9% шанс SPI corruption
```

> **⚠️ Висновок:** Template код без mutex (PA2-6) призводить до SPI corruption з ~99.9% ймовірністю протягом 10 хвилин роботи. **Критично для всіх plugin авторів.**

---

### 3.4 Plugin Lifecycle FSM — State Machine Analysis

```
                    ┌─────────┐
         new()      │ CREATED │
    ──────────────→ └────┬────┘
                         │ canInitialize() → true
                         ↓
                    ┌─────────────┐
                    │  PRE_INIT   │
                    └──────┬──────┘
                           │ initialize(ctx) → true
               ┌───────────┴────────────┐
               ↓  init fails            ↓
         ┌──────────┐             ┌──────────┐
         │  FAILED  │             │  READY   │←──────┐
         └──────┬───┘             └──────┬───┘       │
                │                        │ update()   │
                │                        └────────────┘
                │
                └──→ shutdown() [CONTRACT: завжди викликається]
                          ↓
                     ┌──────────┐
                     │ SHUTDOWN │
                     └──────────┘
                          │ delete plugin
                          ↓
                       [DESTROYED]

ПРОБЛЕМА: PA2-8 — у FAILED стані AsyncSensorPlugin вже запустив readTask
АЛЕ initialize() міг повернути false після xTaskCreate().
→ Task продовжує жити, звертається до ctx після delete plugin → use-after-free.
```

> **⚠️ Висновок:** AsyncSensorPlugin template має race: якщо `initialize()` повертає `false` після `xTaskCreate()`, shutdown() все одно викликається але `readTask` вже працює і читає `ctx` / `this` після знищення → **use-after-free** crash.

---

## 4. PA2-1 — IPlugin: getHealthStatus() та getLastError() не реалізовані у прикладах

**Severity:** 🔴 CRITICAL  
**Статус:** 🔓 OPEN  
**Документи:** `PLUGIN_DIAGNOSTICS.md` §1, `PLUGIN_ARCHITECTURE.md` §3 §4 §5, `CREATING_PLUGINS.md`, `PLUGIN_CONTRACT.md` §6

### Evidence

`PLUGIN_DIAGNOSTICS.md` §1 розширює `IPlugin` двома **pure virtual** методами:

```cpp
// include/IPlugin.h (DIAGNOSTICS version)
class IPlugin {
public:
    // ... lifecycle methods ...

    // === Базова діагностика (мінімально обов'язкова для всіх плагінів) ===
    virtual HealthStatus getHealthStatus() const = 0;  // ← PURE VIRTUAL
    virtual ErrorCode    getLastError()    const = 0;  // ← PURE VIRTUAL
    virtual ~IPlugin() = default;
};
```

Жоден з 10+ прикладних плагінів не реалізує ці методи:

| Клас | Документ | Реалізує getHealthStatus()? | Реалізує getLastError()? |
|------|----------|----------------------------|--------------------------|
| `BH1750Plugin` | CREATING_PLUGINS §Quick Start | ❌ | ❌ |
| `QMC5883LPlugin` | ARCH §3 | ❌ | ❌ |
| `MyPlugin` (PluginContext example) | ARCH §3 | ❌ | ❌ |
| `ThreadSafePlugin` | ARCH §Thread Safety | ❌ | ❌ |
| `MyI2CSensorPlugin` template | CREATING_PLUGINS §Templates | ❌ | ❌ |
| `MySPISensorPlugin` template | CREATING_PLUGINS §Templates | ❌ | ❌ |
| `MyIMUPlugin` template | CREATING_PLUGINS §Templates | ❌ | ❌ |
| `ConfigurableSensorPlugin` | CREATING_PLUGINS §Складні | ❌ | ❌ |
| `AsyncSensorPlugin` | CREATING_PLUGINS §Складні | ❌ | ❌ |
| `FusionSensorPlugin` | CREATING_PLUGINS §Складні | ❌ | ❌ |
| `RobustPlugin` | CONTRACT §2.3 | ❌ | ❌ |
| `SafePlugin` | CONTRACT §2.2 | ❌ | ❌ |

**Compile simulation:**
```cpp
IPlugin* plugin = new BH1750Plugin();
// error: cannot allocate an object of abstract type 'BH1750Plugin'
// note: unimplemented pure virtual method 'getHealthStatus' in 'BH1750Plugin'
// note: unimplemented pure virtual method 'getLastError'   in 'BH1750Plugin'
```

### Рішення

**Варіант A (рекомендовано):** `getHealthStatus()` і `getLastError()` перенести в `IDiagnosticPlugin` (вони діагностика, не core lifecycle). В `IPlugin` лишити тільки lifecycle + `isReady()`. Це не ломає жоден plugin.

```cpp
// include/IPlugin.h — CANONICAL (без getHealthStatus/getLastError)
class IPlugin {
public:
    virtual const char* getName()    const = 0;
    virtual const char* getVersion() const = 0;
    virtual const char* getAuthor()  const = 0;
    virtual bool canInitialize() = 0;
    virtual bool initialize(PluginContext* ctx) = 0;
    virtual void update() = 0;
    virtual void shutdown() = 0;
    virtual bool isEnabled() const = 0;
    virtual bool isReady()   const = 0;
    virtual ~IPlugin() = default;
};

// include/IDiagnosticPlugin.h — OPTIONAL MIXIN
// Реалізують тільки плагіни з повною діагностикою (напр. LDC1101)
class IDiagnosticPlugin {
public:
    virtual IPlugin::HealthStatus   getHealthStatus() const = 0;
    virtual IPlugin::ErrorCode      getLastError()    const = 0;
    virtual IPlugin::DiagnosticResult runDiagnostics() = 0;
    virtual bool runSelfTest() = 0;
    virtual IPlugin::DiagnosticResult getStatistics() const = 0;
    // ...
    virtual ~IDiagnosticPlugin() = default;
};
```

**Варіант B:** Додати default implementations в `IPlugin`:
```cpp
virtual HealthStatus getHealthStatus() const { return HealthStatus::UNKNOWN; }
virtual ErrorCode    getLastError()    const { return {0, "Not implemented"}; }
```
Менш чистий, але швидко вирішує compile errors.

---

## 5. PA2-2 — PluginSystem::getAllPlugins() — метод не визначено

**Severity:** 🔴 CRITICAL  
**Статус:** 🔓 OPEN  
**Документи:** `PLUGIN_DIAGNOSTICS.md` §3, §4, §5 + `PLUGIN_ARCHITECTURE.md` §PluginSystem

### Evidence

`PLUGIN_DIAGNOSTICS.md` використовує `getAllPlugins()` у трьох окремих місцях:

```cpp
// §3 UI (рядок ~535):
auto plugins = pluginSystem->getAllPlugins();

// §4 Health monitoring (рядок ~618):
for (auto* plugin : pluginSystem->getAllPlugins()) {

// §5 Logging (рядок ~663):
for (auto* plugin : pluginSystem->getAllPlugins()) {
```

`PLUGIN_ARCHITECTURE.md` §PluginSystem визначає тільки:
```cpp
class PluginSystem {
public:
    void begin(...)
    void loadFromConfig(...)
    void registerPlugin(IPlugin*)
    void initializeAll()
    void updateAll()
    IPlugin* getPlugin(const char*)
    template<typename T> std::vector<T*> getPluginsByType()
    void printStatus()
private:
    IPlugin* createPlugin(...)
};
```

**`getAllPlugins()` ніде не визначений.**

### Рішення

Додати до `PluginSystem`:
```cpp
// Повертає всі зареєстровані плагіни
const std::vector<IPlugin*>& getAllPlugins() const {
    return plugins;  // plugins — приватний член std::vector<IPlugin*>
}
```

---

## 6. PA2-3 — IDiagnosticPlugin методи викликаються на IPlugin* — compile error

**Severity:** 🔴 CRITICAL  
**Статус:** 🔓 OPEN  
**Документи:** `PLUGIN_DIAGNOSTICS.md` §3, §4, §5

### Evidence

**§4 setup() — `runSelfTest()` на `IPlugin*`:**
```cpp
for (auto* plugin : pluginSystem->getAllPlugins()) {
    bool passed = plugin->runSelfTest();  // ← runSelfTest() тільки на IDiagnosticPlugin*, не IPlugin*
    // error: 'class IPlugin' has no member named 'runSelfTest'
}
```

**§3 UI — `getStatistics()` на `IPlugin*`:**
```cpp
auto stats = plugin->getStatistics();
// error: 'class IPlugin' has no member named 'getStatistics'
```

**§5 Logging — повторно:**
```cpp
auto result = plugin->getStatistics();  // Той самий compile error
```

**Правильний підхід з dynamic_cast:**
```cpp
for (auto* plugin : pluginSystem->getAllPlugins()) {
    // Only run self-test for diagnostic-capable plugins
    auto* diag = dynamic_cast<IDiagnosticPlugin*>(plugin);  // Потребує -frtti (PA-7 fix)
    if (diag) {
        bool passed = diag->runSelfTest();
        auto stats  = diag->getStatistics();
    }
}
```

### Зв'язок з PA-20 (v1.0.0)

PA-20 фіксував `getStatistics() const` side-effect проблему — вона є підмножиною PA2-3 (type mismatch is the primary blocker).

---

## 7. PA2-4 — PluginSystem::attemptRecovery() — метод не визначено

**Severity:** 🟠 HIGH  
**Статус:** 🔓 OPEN  
**Документи:** `PLUGIN_DIAGNOSTICS.md` §4 + `PLUGIN_ARCHITECTURE.md` §PluginSystem

### Evidence

```cpp
// DIAGNOSTICS §4:
if (pluginSystem->attemptRecovery(plugin)) {
// error: 'class PluginSystem' has no member named 'attemptRecovery'
```

`PluginSystem` у ARCH не визначає цього методу (перелік з PA2-2 вище).

### Рішення

Додати до `PluginSystem` — логіка recovery вже задокументована у DIAGNOSTICS §4 коментарі:

```cpp
bool attemptRecovery(IPlugin* plugin) {
    if (!plugin) return false;
    plugin->shutdown();
    vTaskDelay(pdMS_TO_TICKS(100));
    if (!plugin->canInitialize()) return false;          // Guard: PA-30 fix
    return plugin->initialize(&ctx_);
}
```

---

## 8. PA2-5 — CONTRACT §1.3 PluginContext struct не оновлено після PA-25

**Severity:** 🟠 HIGH  
**Статус:** 🔓 OPEN  
**Документи:** `PLUGIN_CONTRACT.md` §1.3 vs `PLUGIN_ARCHITECTURE.md` §PluginContext

### Evidence

**PLUGIN_CONTRACT.md §1.3** (поточний стан):
```cpp
struct PluginContext {
    TwoWire*          wire;
    SemaphoreHandle_t wireMutex;
    SPIClass*         spi;
    SemaphoreHandle_t spiMutex;
    ConfigManager*    config;
    Logger*           log;
    // ← IStorageManager* storage ВІДСУТНЄ
};
```

**PLUGIN_ARCHITECTURE.md §PluginContext** (після PA-25 fix):
```cpp
struct PluginContext {
    TwoWire*          wire;
    SemaphoreHandle_t wireMutex;
    SPIClass*         spi;
    SemaphoreHandle_t spiMutex;
    ConfigManager*    config;
    Logger*           log;
    IStorageManager*  storage = nullptr;  // ← ДОДАНО PA-25
};
```

CONTRACT є авторитетним контрактним документом — якщо розробник читає тільки CONTRACT, він не знає про `ctx->storage`. Якщо він очікує що `storage` не `nullptr` (без перевірки), може краш.

### Рішення

Синхронізувати CONTRACT §1.3 struct з ARCH. Додати в §1.3б (після ConfigManager API):

```
**`ctx->storage`:** може бути `nullptr` якщо не передано в `PluginSystem::begin()`.
Завжди перевіряти перед використанням:
if (ctx->storage) ctx->storage->save(...);
```

---

## 9. PA2-6 — MySPISensorPlugin template: SPI без spiMutex

**Severity:** 🟠 HIGH  
**Статус:** 🔓 OPEN  
**Документи:** `CREATING_PLUGINS.md` §Templates "Шаблон сенсора SPI"

### Evidence

```cpp
// CREATING_PLUGINS.md §Templates — SPI template (copyable by developers):
SensorData read() override {
    if (!ready) return {0, 0, 0.0f, millis(), false};
    
    digitalWrite(csPin, LOW);
    uint16_t data = spi->transfer16(0x0000);  // ← SPI без spiMutex!
    digitalWrite(csPin, HIGH);
    
    return {(float)data, 0, 1.0f, millis(), true};
}
```

Порушення ADR-ST-008: SD карта (SDTransport Logger) на тій же шині. Як показує симуляція §3.3, при 10-хвилинній роботі P(corruption) > 99.9%.

### Рішення

```cpp
SensorData read() override {
    if (!ready) return {0, 0, 0.0f, millis(), false};
    
    // ✅ ADR-ST-008: завжди брати spiMutex перед SPI транзакцією
    if (!ctx->spiMutex || xSemaphoreTake(ctx->spiMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return {0, 0, 0.0f, millis(), false};  // Timeout — шина зайнята
    }
    
    digitalWrite(csPin, LOW);
    uint16_t data = spi->transfer16(0x0000);
    digitalWrite(csPin, HIGH);
    
    xSemaphoreGive(ctx->spiMutex);
    return {(float)data, 0, 1.0f, millis(), true};
}
```

---

## 10. PA2-7 — IIMUPlugin::calibrate() повертає void, ISensorPlugin — bool

**Severity:** 🟠 HIGH  
**Статус:** 🔓 OPEN  
**Документи:** `PLUGIN_ARCHITECTURE.md` §IIMUPlugin vs §ISensorPlugin, `PLUGIN_CONTRACT.md` §3.4

### Evidence

```cpp
// PLUGIN_ARCHITECTURE.md §ISensorPlugin:
class ISensorPlugin : public IPlugin {
    virtual bool calibrate() = 0;  // ← bool
};

// PLUGIN_ARCHITECTURE.md §IIMUPlugin:
class IIMUPlugin : public IPlugin {
    virtual void calibrate() = 0;  // ← void!
};
```

**CONTRACT §3.4** документує:
> `calibrate()` може блокуватись (delay 2–5 сек). Повертає `false` при помилці.

Але `IIMUPlugin::calibrate()` не може повернути `false` — void return. IMU calibration failure silently ignored.

**Майбутня fusion проблема:**
```cpp
class FusionIMUSensorPlugin : public ISensorPlugin, public IIMUPlugin {
    // ambiguous override — bool calibrate() vs void calibrate()
    // error: conflicting return type specified for virtual function
};
```

### Рішення

Уніфікувати всі `calibrate()` → `bool`:
```cpp
class IIMUPlugin : public IPlugin {
    virtual bool calibrate() = 0;  // ← void → bool для consistency + error signaling
};
```

---

## 11. PA2-8 — AsyncSensorPlugin: TaskHandle_t не видаляється в shutdown()

**Severity:** 🟠 HIGH  
**Статус:** 🔓 OPEN  
**Документи:** `CREATING_PLUGINS.md` §Складні, §2 AsyncSensorPlugin

### Evidence

```cpp
class AsyncSensorPlugin : public ISensorPlugin {
    TaskHandle_t readTask;

    bool initialize(PluginContext* context) override {
        ctx = context;
        dataMutex = xSemaphoreCreateMutex();
        xTaskCreate(readTaskFunc, "SensorRead", 4096, this, 5, &readTask);  // ← Task запущено
        // ...
    }

    // shutdown() не показаний — ВІДСУТНІЙ у прикладі!
    // Або якщо існує:
    void shutdown() override {
        // ← readTask НЕ видаляється → task leak
    }
};
```

**Додаткові проблеми:**
1. `xTaskCreate(readTaskFunc, "SensorRead", 4096, this, **5**, &readTask)` — priority 5 > main loop (зазвичай 1) → потенційне starving main loop → порушення CONTRACT §1.1 (update ≥ 10 Hz)
2. Якщо `initialize()` повертає false після `xTaskCreate()`, task вже запущений → use-after-free при delete plugin (FSM simulation §3.4)
3. Stack 4096 bytes = 4 KB. Якщо readTaskFunc викликає SPI + ArduinoJson + log — ризик stack overflow

### Рішення

```cpp
bool initialize(PluginContext* context) override {
    ctx = context;
    dataMutex = xSemaphoreCreateMutex();
    if (!dataMutex) return false;

    // Запуск task — після всіх перевірок
    taskRunning = true;
    BaseType_t res = xTaskCreate(readTaskFunc, "SensorRead",
        4096,          // Stack: 4 KB — перевірити чи достатньо
        this,
        tskIDLE_PRIORITY + 2,  // ← Не вище main loop priority (зазвичай 1)
        &readTask);
    if (res != pdPASS) { taskRunning = false; return false; }
    return true;
}

void shutdown() override {
    taskRunning = false;                    // Signal task to exit
    if (readTask) {
        vTaskDelete(readTask);              // ✅ Видалити task
        readTask = nullptr;
    }
    if (dataMutex) {
        vSemaphoreDelete(dataMutex);        // ✅ Видалити mutex
        dataMutex = nullptr;
    }
}
```

---

## 12. PA2-9 — IPlugin: три несумісних визначення в різних документах

**Severity:** 🟡 MEDIUM  
**Статус:** 🔓 OPEN  
**Документи:** `PLUGIN_ARCHITECTURE.md` §Крок1, §Оновлений інтерфейс, `PLUGIN_DIAGNOSTICS.md` §1

### Evidence

| Визначення | Документ | Методи |
|---|---|---|
| **IPlugin v1** (§Крок1) | ARCH | getName, getVersion, getAuthor, canInitialize, initialize, update, shutdown, isEnabled, isReady |
| **IPlugin v2** (§Оновлений) | ARCH | + вужчий contract comment на initialize() — ідентичні методи, різний контекст |
| **IPlugin v3** (§1) | DIAGNOSTICS | + getHealthStatus() const = 0, getLastError() const = 0, HealthStatus, ErrorCode, DiagnosticResult |

Розробник читає ARCH → реалізує 9 методів. Читає DIAGNOSTICS → виявляє ще 2 обов'язкових. Читає CONTRACT → бачить третій варіант без diagnostics.

### Рішення

Додати єдиний cross-reference заголовок в ARCH §Крок1:

```markdown
> ⚠️ **Актуальне визначення `IPlugin` — `include/IPlugin.h`:**
> Lifecycle: canInitialize, initialize, update, shutdown, isEnabled, isReady.
> Diagnostics (обов'язкові тільки для IDiagnosticPlugin, optional для базових):
> → за PA2-1 fix: getHealthStatus і getLastError перенесено в IDiagnosticPlugin.
```

---

## 13. PA2-10 — ISensorPlugin::getMetadata() pure virtual відсутній у ARCH definition

**Severity:** 🟡 MEDIUM  
**Статус:** 🔓 OPEN  
**Документи:** `PLUGIN_INTERFACES_EXTENDED.md` §1 vs `PLUGIN_ARCHITECTURE.md` §ISensorPlugin

### Evidence

**PLUGIN_INTERFACES_EXTENDED.md §1:**
```cpp
class ISensorPlugin : public IPlugin {
    // ...
    virtual SensorMetadata getMetadata() const = 0;  // ← PURE VIRTUAL
    virtual SensorData read() = 0;
    virtual bool calibrate() = 0;
};
```

**PLUGIN_ARCHITECTURE.md §ISensorPlugin:**
```cpp
class ISensorPlugin : public IPlugin {
    // SensorMetadata struct визначено але...
    // getMetadata() — ВІДСУТНІЙ
    virtual SensorData read() = 0;
    virtual bool calibrate() = 0;
};
```

Якщо INTERFACES_EXTENDED є canonical ISensorPlugin (воно так задумано — "розширений"), то `HX711Plugin` (INTERFACES) реалізує `getMetadata()`, але `QMC5883LPlugin` (ARCH) та `BH1750Plugin` (CREATING) — ні → abstract class.

### Рішення

Уточнити в обох документах: INTERFACES_EXTENDED §1 = **розширена версія** для production plugins (з metadata). ARCH §ISensorPlugin = **базова** для minimal examples. Додати heading:

```markdown
// ARCH ISensorPlugin — БАЗОВИЙ (для прикладів):
// getMetadata() — NOT required for minimal plugins

// INTERFACES_EXTENDED ISensorPlugin — PRODUCTION (full API):
// getMetadata() REQUIRED для реєстрації у Plugin Registry
```

---

## 14. PA2-11 — CREATING_PLUGINS §Поради: Serial.println() у copyable example

**Severity:** 🟡 MEDIUM  
**Статус:** 🔓 OPEN  
**Документи:** `CREATING_PLUGINS.md` §"Поради досвідчених розробників" §3

### Evidence

```cpp
// CREATING_PLUGINS.md §Поради §3 "Timeout для I2C/SPI" — copyable code:
uint16_t readWithTimeout(uint32_t timeoutMs = 100) {
    uint32_t start = millis();
    while (!dataReady()) {
        if (millis() - start > timeoutMs) {
            Serial.println("ERROR: Read timeout");  // ← Порушує CONTRACT §2.4!
            return 0;
        }
        delay(1);
    }
    return readData();
}
```

CONTRACT §2.4: "Не логувати напряму через `Serial` — використовувати `ctx->log`".

Три проблеми: (1) CONTRACT violation, (2) `ctx` поза scope функції (member), (3) `delay(1)` у `read()` accumulates → blocking time grows.

### Рішення

```cpp
uint16_t readWithTimeout(uint32_t timeoutMs = 100) {
    uint32_t start = millis();
    while (!dataReady()) {
        if (millis() - start > timeoutMs) {
            ctx->log->warning(getName(), "Read timeout after %u ms", timeoutMs);  // ✅ CONTRACT §2.4
            return 0;
        }
        vTaskDelay(pdMS_TO_TICKS(1));  // ✅ FreeRTOS yield замість delay()
    }
    return readData();
}
```

---

## 15. PA2-12 — Mock PluginContext у тестах: .config = nullptr → crash

**Severity:** 🟡 MEDIUM  
**Статус:** 🔓 OPEN  
**Документи:** `CREATING_PLUGINS.md` §Debugging "Перевірка через Serial Monitor"

### Evidence

```cpp
// CREATING_PLUGINS.md — рекомендований мок для тестування:
PluginContext mockCtx = {
    .wire      = &Wire,
    .wireMutex = xSemaphoreCreateMutex(),
    .spi       = &SPI,
    .spiMutex  = xSemaphoreCreateMutex(),
    .config    = nullptr,  // ← TODO: підключити ConfigManager
    .log       = &testLogger
};
```

Рекомендований патерн `§Складні → loadConfig()`:
```cpp
void loadConfig() {
    config.sampleRate = ctx->config->getInt("mysensor.sampleRate", 100);  // ← crash: ctx->config = nullptr!
}
```

Якщо розробник копіює mock з §Debugging і реалізує плагін per §Складні → crash на першому тесті.

### Рішення

Замінити `.config = nullptr` на minimal ConfigManager stub, або додати guard:

```cpp
// Option A: guard у mock документі
PluginContext mockCtx = {
    // ...
    .config = new ConfigManager(),  // ✅ або mock implementation
    .log    = &testLogger,
    .storage = nullptr   // ✅ storage може бути nullptr, але config — ніколи
};

// Option B (у тексті): Документувати що config повинен бути ненульовим:
// ⚠️ .config НЕ може бути nullptr якщо плагін викликає ctx->config->getInt()
// Використовуйте тестовий ConfigManager або мок.
```

---

## 16. PA2-13 — Memory budget: display framebuffer не врахований

**Severity:** 🟡 MEDIUM  
**Статус:** 🔓 OPEN  
**Документи:** `PLUGIN_ARCHITECTURE.md` §Memory Management

### Evidence

Поточна таблиця бюджету:
```
~177 KB — система (PluginSystem, ConfigManager, Logger, FreeRTOS stacks)
~160 KB — плагіни (20 × 8 KB)
~0 KB   — залишок
```

Невраховані компоненти:
- **Display framebuffer** ST7789V2 (240×135×2 = 64.8 KB ≈ **~63 KB**) — в heap, PSRAM відсутній
- **FreeRTOS stacks** більш ніж 1 task (main + async plugins = реально 5-8 tasks × ~4-8 KB)
- Результат симуляції §3.2: **реальний залишок при 4 плагінах ≈ 138 KB**, не 0 KB

Небезпечно що "~0 KB залишок" дає хибне відчуття critical headroom при 20 плагінах, але display buffer підриває цей budget перед першим плагіном.

### Рішення

Замінити таблицю бюджету результатами simulation §3.2:
```
~167 KB — overhead (FreeRTOS + framework + display + LittleFS + Json)
~138 KB — доступно для плагінів (при 4 плагінах)
Реальний ліміт: 10-12 плагінів (з safety margin)
20 плагінів × 8 KB = 160 KB > available → HEAP OVERFLOW
```

---

## 17. PA2-14 — ARCH config.json: inline "config" поле суперечить canonical PA-13 моделі

**Severity:** 🟡 MEDIUM  
**Статус:** 🔓 OPEN  
**Документи:** `PLUGIN_ARCHITECTURE.md` §data/config.json example

### Evidence

`PLUGIN_ARCHITECTURE.md` §"Приклади конфігураційних файлів" → `data/config.json`:

```json
{
  "plugins": [
    {
      "type": "sensor",
      "name": "LDC1101",
      "enabled": true,
      "config": "plugins/ldc1101.json"  // ← INLINE config поле!
    }
  ]
}
```

PA-13 canonical model (закрита) чітко визначає:
> Runtime параметри — у `data/plugins/<name>.json`. `ConfigManager` читає їх автоматично.

Inline `"config"` поле — паралельний альтернативний механізм, і він ніде не обробляється у `PluginSystem::loadFromConfig()`. Дві несумісні моделі конфігурації в одному документі.

### Рішення

Видалити `"config"` поле з прикладу, або задокументувати що воно ігнорується:

```json
{
  "plugins": [
    {
      "type": "sensor",
      "name": "LDC1101",
      "enabled": true
      // Runtime параметри у data/plugins/ldc1101.json — автоматично через ctx->config
    }
  ]
}
```

---

## 18. PA2-15 — DIAGNOSTICS §4 §5: ctx->log та SD.open() у main() loop() — out-of-scope

**Severity:** 🟡 MEDIUM  
**Статус:** 🔓 OPEN  
**Документи:** `PLUGIN_DIAGNOSTICS.md` §4, §5

### Evidence

**§4 loop() recovery:**
```cpp
void loop() {
    // ...
    if (pluginSystem->attemptRecovery(plugin)) {
        ctx->log->info(plugin->getName(), "Recovery successful");  // ← ctx не визначений у main.cpp!
    }
}
```

**§5 logDiagnostics():**
```cpp
void logDiagnostics() {
    File log = SD.open("/diagnostics.log", FILE_APPEND);  // ← SD.open() без spiMutex!
    // ...
}
```

`ctx` — приватний член `PluginSystem`. Доступний у `main.cpp` тільки через `pluginSystem->getContext()` (не визначений) або через `logger` global (є). Та `SD.open()` — пряме SPI звернення без mutex (ADR-ST-008).

### Рішення

```cpp
// §4: замінити ctx->log на logger (global з main.cpp):
logger->info("SystemRecovery", "Plugin %s recovered", plugin->getName());

// §5: SD.open() через Storage architecture (IStorageManager), не напряму:
if (ctx_.storage) {
    ctx_.storage->append("/diagnostics.log", buffer, len);  // via spiMutex всередині
}
```

---

## 19. PA2-16 — checkCalibration(): calibrationBaseline = 0 при старті — завжди false

**Severity:** 🟢 LOW  
**Статус:** 🔓 OPEN  
**Документи:** `PLUGIN_DIAGNOSTICS.md` §2 `checkCalibration()`

### Evidence

```cpp
private:
    float calibrationBaseline = 0;  // ← ініціалізовано 0

bool checkCalibration() override {
    // ...
    if (calibrationBaseline < 100 || calibrationBaseline > 10000) {
        return false;  // ← 0 < 100 → ЗАВЖДИ false на першому call!
    }
    return true;
}
```

При першому завантаженні (до будь-якого `calibrate()`) → `runDiagnostics()` крок 3 → `CALIBRATION_NEEDED`. Якщо `runSelfTest()` в setup() не включає цю перевірку — OK. Але якщо хтось викликає `runDiagnostics()` при completion → false positive першого старту.

### Рішення

Документувати очікувану поведінку:
```cpp
private:
    float    calibrationBaseline   = 0;
    bool     calibrationPerformed  = false;  // ← Guard: не перевіряти до першого calibrate()

bool checkCalibration() override {
    if (!calibrationPerformed) return false;  // ✅ Явно: calibrate() ще не викликано
    if (calibrationBaseline < 100 || calibrationBaseline > 10000) return false;
    // ...
}
```

---

## 20. PA2-17 — CREATING_PLUGINS BH1750::read() без wireMutex з Core 1

**Severity:** 🟢 LOW  
**Статус:** 🔓 OPEN  
**Документи:** `CREATING_PLUGINS.md` §Quick Start BH1750Plugin

### Evidence

```cpp
SensorData read() override {
    if (!ready) return {0, 0, 0.0f, millis(), false};
    
    ctx->wire->requestFrom(I2C_ADDR, (uint8_t)2);  // ← I2C без wireMutex!
    if (ctx->wire->available() != 2) { ... }
    // ...
}
```

CONTRACT §2.2: `read()` може викликатись з будь-якого task (Core 1). Якщо паралельно `update()` або інший плагін використовує I2C з Core 0 → race condition.

> **Зауваження:** Quick Start приклад навмисно спрощений (без async task). Якщо BH1750Plugin запускається тільки зі стратегією A (без background task) — mutex не потрібен. Але документ не вказує це обмеження → LOW, не HIGH.

### Рішення

Додати коментар до `read()`:

```cpp
SensorData read() override {
    if (!ready) return {0, 0, 0.0f, millis(), false};
    
    // ⚠️ Strategy A: цей плагін не має async task — race condition неможливий.
    // Якщо ви додасте background task → обов'язково взяти wireMutex (CONTRACT §1.5)
    ctx->wire->requestFrom(I2C_ADDR, (uint8_t)2);
    // ...
}
```

---

## 21. PA2-18 — Plugin Registry/OTA механізми посилаються на невизначені методи

**Severity:** 🟢 LOW  
**Статус:** 🔓 OPEN  
**Документи:** `PLUGIN_ARCHITECTURE.md` §Креативні фічі

### Evidence

```cpp
// §2 Hot-Reload (помічений ⚠️ Застереженням — OK):
pluginSystem->reloadPlugin("QMC5883L");  // ← НЕ ВИЗНАЧЕНО в PluginSystem

// §3 OTA (помічений ⚠️ Застереженням — OK):
pluginSystem->installPluginOTA("https://github.com/...");  // ← НЕ ВИЗНАЧЕНО; CONNECTIVITY_ARCH: HTTP заборонено

// §5 Dependency Resolution (у plugin.json — build-time, не runtime):
{"dependencies": [{"name": "ArduinoJson", "version": "^6.0"}]}  // PlatformIO не парсить цей формат
```

§1 ("Auto-Discovery") вже правильно позначений як `// НЕ РЕАЛІЗОВАНО` після PA-13 fix.
§2-§5 ще застосовують API як ніби це реальний код.

### Рішення

Додати `// NOT IMPLEMENTED` коментарі аналогічно §1:
```cpp
// pluginSystem->reloadPlugin("QMC5883L");  // NOT IMPLEMENTED — концептуальний API
// pluginSystem->installPluginOTA(url);     // NOT IMPLEMENTED; HTTP: CONNECTIVITY_ARCH §1.5
```

---

## 22. C-PA12 (перенесено) — Factory hardcoding суперечить extensibility promise

**Severity:** 🟡 MEDIUM  
**Статус:** 🔓 OPEN (перенесено з PA-12 v1.0.0)  
**Документ:** `PLUGIN_ARCHITECTURE.md` §PluginSystem `createPlugin()`

### Evidence

```cpp
IPlugin* createPlugin(const std::string& type, const std::string& name) {
    if (name == "LDC1101")  return new LDC1101Plugin();
    if (name == "BMI270")   return new BMI270Plugin();
    if (name == "QMC5883L") return new QMC5883LPlugin();
    if (name == "SDCard")   return new SDCardPlugin();
    return nullptr;
}
```

ARCH §"Переваги" обіцяє: "Зовнішнім розробникам достатньо додати свою папку". Але `createPlugin()` hardcoded — четвертий плагін потребує редагування `PluginSystem.cpp`. Extensibility promise порушено.

### Рішення (варіанти)

**Варіант A — Self-registration macro (рекомендовано):**
```cpp
// include/PluginFactory.h
using PluginCreator = std::function<IPlugin*()>;
class PluginFactory {
    static std::map<std::string, PluginCreator>& registry() {
        static std::map<std::string, PluginCreator> reg;
        return reg;
    }
public:
    static void registerPlugin(const std::string& name, PluginCreator creator) {
        registry()[name] = creator;
    }
    static IPlugin* create(const std::string& name) {
        auto it = registry().find(name);
        return (it != registry().end()) ? it->second() : nullptr;
    }
};

// lib/LDC1101Plugin/LDC1101Plugin.cpp:
static bool _reg = []() {
    PluginFactory::registerPlugin("LDC1101", []() { return new LDC1101Plugin(); });
    return true;
}();
```

**Варіант B — Задокументувати обмеження явно:**
Якщо self-registration поза scope P-2, чесно задокументувати:
```
// ⚠️ ВІДОМИЙ ОБМЕЖЕННЯ: Зовнішні плагіни потребують реєстрації тут.
// Для повної extensibility: Варіант A (PluginFactory self-registration) — C-PA12.
```

---

## 23. C-PA16 (перенесено) — Plugin dependency injection: механізм не визначено

**Severity:** 🟢 LOW  
**Статус:** 🔓 OPEN (перенесено з PA-16 v1.0.0)  
**Документ:** `CREATING_PLUGINS.md` §FusionSensorPlugin

### Evidence

```cpp
class FusionSensorPlugin : public ISensorPlugin {
    ISensorPlugin* sensor1;
    ISensorPlugin* sensor2;
    
    void setDependencies(ISensorPlugin* s1, ISensorPlugin* s2) {
        sensor1 = s1;
        sensor2 = s2;
    }
    // ...
};
```

Хто викликає `setDependencies()`? PluginSystem? Де задокументований lifecycle порядок: до чи після `canInitialize()`? Якщо LDC1101Plugin перезавантажений (hot-reload) → `sensor1` стає dangling pointer.

### Рішення

Мінімальний підхід (до P-3): задокументувати що fusion plugins вимагають ручного wiring у `main.cpp`:
```cpp
// main.cpp (задокументувати цей pattern):
auto* fusion = dynamic_cast<FusionSensorPlugin*>(pluginSystem->getPlugin("Fusion"));
if (fusion) {
    fusion->setDependencies(
        dynamic_cast<ISensorPlugin*>(pluginSystem->getPlugin("LDC1101")),
        dynamic_cast<ISensorPlugin*>(pluginSystem->getPlugin("QMC5883L"))
    );
}
```
Власний lifecycle і hot-reload safety — до P-3.

---

## 24. Що архітектура зробила ПРАВИЛЬНО

### ✅ Збережені та підтверджені після v2.0.0 аудиту:

1. **PluginContext DI pattern** — правильний. Всі ресурси передаються, `wire`/`spi` ініціалізовані системою.
2. **Canonical three-JSON config model** (PA-13 closed) — чітко визначено, LDC1101_ARCH as reference — consistent.
3. **SPI mutex після PA-22** — `LDC1101Plugin` правильно бере `spiMutex` в `readRegister`/`writeRegister`.
4. **`spiMutex` null-check** — `if (!ctx->spiMutex)` guard в LDC1101 — defensive programming.
5. **`portMAX_DELAY` заборонений** документально (PA-5 closed) — весь mutex код використовує `pdMS_TO_TICKS(50)`.
6. **PSRAM = 0 MB** задокументовано (PA-6, PA-29 closed) — `ps_malloc()` явно заборонений.
7. **`dynamic_cast` needs `-frtti`** задокументовано (PA-7 closed) — comment у `getPluginsByType()`.
8. **Lifecycle FSM** чітко визначений в CONTRACT §1.1 — `canInit → init → update → shutdown` гарантована.
9. **`ctx->log->warning()` (не `warn()`)** виправлено (PA-21 closed) — API консистентний.
10. **`scanForPlugins("lib/")` позначено** як NOT IMPLEMENTED (PA-13 fix) — архітектурне пояснення збережено.
11. **IDiagnosticPlugin** — концепція правильна (optional mixin). Проблема тільки у placement `getHealthStatus`/`getLastError`.

---

## 25. Pre-implementation Checklist — перед P-2

### P-2 Блокуючі (мають бути closed до початку):

- [x] **PA2-1** — визначити canonical `IPlugin.h` (без getHealthStatus/getLastError або з default impl)
- [x] **PA2-2** — додати `getAllPlugins()` до `PluginSystem`
- [x] **PA2-3** — виправити DIAGNOSTICS §3,§4,§5 на `dynamic_cast<IDiagnosticPlugin*>`
- [x] **PA2-4** — додати `attemptRecovery()` до `PluginSystem`
- [x] **PA2-5** — синхронізувати CONTRACT §1.3 PluginContext struct
- [x] **C-PA12** — або self-registration, або явно задокументувати обмеження

### P-2 Рекомендовані (виправити в тому ж PR):

- [x] **PA2-6** — SPI template + `spiMutex`
- [x] **PA2-7** — `IIMUPlugin::calibrate()` → `bool`
- [x] **PA2-8** — AsyncSensorPlugin shutdown() + task delete
- [x] **PA2-9** — єдиний canonical IPlugin cross-reference
- [x] **PA2-10** — уточнити базова vs extended ISensorPlugin
- [x] **PA2-14** — видалити inline `"config"` поле з config.json example

### P-3 / Backlog:

- [ ] PA2-11 — ~~Serial.println у §Поради → ctx->log~~ ✅ CLOSED
- [ ] PA2-12 — mock ctx config = nullptr
- [x] **PA2-13** — ~~memory budget revision~~ ✅ CLOSED (20→10 плагінів)
- [ ] PA2-15 — ctx->log у loop() scope
- [ ] PA2-16 — calibrationBaseline boot behavior
- [ ] PA2-17 — BH1750 wireMutex comment
- [ ] PA2-18 — NOT IMPLEMENTED comments на OTA/reload
- [ ] C-PA16 — dependency injection lifecycle doc

---

## 26. Backlog та пріоритети v2.0.0

### Залежності між знахідками

```
PA2-1 (IPlugin pure virtual)
    └─ блокує: PA2-3 (помилка виклику методів — по суті це одна первопричина)
    └─ блокує: PA2-9 (три визначення — PA2-1 = root cause)
    └─ пов'язано: C-PA12 (factory rewrite useful разом з IPlugin cleanup)

PA2-2 (getAllPlugins missing)
    └─ блокує: PA2-3 (перед типами — спочатку треба метод)
    └─ блокує: PA2-4 (attemptRecovery — самостійна, але DIAGNOSTICS §4 використовує обидва)

PA2-6 (SPI template no mutex)
    └─ пов'язано: PA2-17 (BH1750 I2C — той самий pattern, різна шина)
    └─ пов'язано: PA2-15 (SD.open без mutex — логічна пара)

PA2-7 (calibrate void/bool)
    └─ пов'язано: C-PA16 (dependency injection — рішення C-PA16 вплине на calibrate usecases)

PA2-8 (task leak)
    └─ пов'язано: C-PA16 (hot-reload requires proper task cleanup first)
```

### Пріоритети (по бізнес-впливу)

| Пріоритет | Знахідки | Чому |
|-----------|---------|------|
| **Блокуючі P-2** | PA2-1, PA2-2, PA2-3, PA2-4, PA2-5, C-PA12 | Compile errors або contract mismatch |
| **Виправити в P-2 PR** | PA2-6, PA2-7, PA2-8, PA2-9, PA2-10, PA2-14 | Low risk, high value, same codebase area |
| **Перед P-3** | PA2-11, PA2-12, PA2-13, PA2-15, C-PA16 | Relevant для розробників plugins |
| **Backlog** | PA2-16, PA2-17, PA2-18 | Non-blocking correctness |

---

## 27. Cross-reference: v1.0.0 → v2.0.0

### Статус всіх v1.0.0 знахідок

| v1.0.0 ID | Severity | Закрито | Примітка v2.0.0 |
|-----------|----------|---------|-----------------|
| PA-1 | 🔴 | ✅ | SensorData 5 полів — підтверджено consistent |
| PA-2 | 🔴 | ✅ | initialize(PluginContext*) — підтверджено |
| PA-3 | 🔴 | ✅ | IDiagnosticPlugin mixin — але PA2-1 нова проблема тієї ж зони |
| PA-4 | 🟠 | ✅ | RP burst order — виправлено (comment+redirect) |
| PA-5 | 🟠 | ✅ | portMAX_DELAY → pdMS_TO_TICKS(50) — підтверджено |
| PA-6 | 🟠 | ✅ | PSRAM = 0 MB — але PA2-13 display buffer новий budget issue |
| PA-7 | 🟠 | ✅ | -frtti comment — підтверджено |
| PA-8 | 🟠 | ✅ | getContext() removed — підтверджено |
| PA-9 | 🟡 | ✅ | Serial→ctx→log, але PA-21 regression була → також closed |
| PA-10 | 🟡 | ✅ | ConfigManager API в CONTRACT §1.3б — підтверджено |
| PA-11 | 🟡 | ✅ | LDC1101_CONFIG 0x01 — підтверджено |
| PA-12 | 🟡 | 🔓 | → C-PA12 (перенесено) |
| PA-13 | 🟡 | ✅ | Canonical three-JSON — підтверджено |
| PA-14 | 🟡 | ✅ | IStoragePlugin boundary — підтверджено |
| PA-15 | 🟡 | ✅ | calibrate() timing CONTRACT §3.4 — але PA2-7 new (void vs bool) |
| PA-16 | 🟢 | 🔓 | → C-PA16 (перенесено) |
| PA-17 | 🟢 | ✅ | millis() NVS note — підтверджено |
| PA-18 | 🟢 | ✅ | WiFi removed — але PA2-18 new (OTA URLs not yet cleaned up) |
| PA-19 | 🟢 | ✅ | Side-effect PA-13 — підтверджено |
| PA-20 | 🟢 | ✅ (superseded) | → замінена PA2-3 (broader type mismatch issue) |
| PA-21 | 🔴 | ✅ | warn→warning regression — підтверджено |
| PA-22 | 🔴 | ✅ | spiMutex in readRegister/writeRegister — підтверджено |
| PA-23 | 🔴 | ✅ | QMC5883L .valid — підтверджено |
| PA-24 | 🟠 | ✅ | begin() before loadFromConfig() — підтверджено |
| PA-25 | 🟠 | ✅ (partial) | storage в ARCH — але PA2-5: CONTRACT не оновлено |
| PA-26 | 🟠 | ✅ | FusionSensor .valid guard — підтверджено |
| PA-27 | 🟠 | ✅ | CoinAnalyzer MeasurementStore — підтверджено |
| PA-28 | 🟠 | ✅ | checkStability() blocking warning — підтверджено |
| PA-29 | 🟠 | ✅ | psram 0MB — підтверджено |
| PA-30 | 🟡 | ✅ | attemptRecovery canInitialize() — але PA2-4 метод відсутній |

**Підтверджено closed і consistent:** 25/27 (PA-3 → PA2-1 new issue; PA-25 → PA2-5 partial)  
**Регресія через неповне поширення:** PA-25 → PA2-5 (CONTRACT struct not updated)  
**Перенесено:** PA-12 → C-PA12, PA-16 → C-PA16

---

*Аудит проведений: 2026-03-13*  
*Попередній аудит: `docs/architecture/obsolete/PLUGIN_AUDIT_v1.0.0.md`*  
*Наступний аудит: після закриття PA2-1..PA2-6 (P-2 completion)*
