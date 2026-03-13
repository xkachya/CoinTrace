# Plugin Architecture — Незалежний Архітектурний Аудит v1.0.0

**Документ:** PLUGIN_AUDIT_v1.0.0.md  
**Аудитовані документи:**
- `PLUGIN_ARCHITECTURE.md` v1.1.0 (виправлено PA-1,2,5,6,7)
- `PLUGIN_CONTRACT.md` v1.1.0 (виправлено PA-5,6,10,15)
- `PLUGIN_INTERFACES_EXTENDED.md` v1.2.0 (виправлено PA-14)
- `PLUGIN_DIAGNOSTICS.md` v1.1.0 (виправлено PA-3,4,8,9,11,17,18)
- `CREATING_PLUGINS.md` v1.1.0 (виправлено PA-1,5)

**Дата аудиту:** 2026-03-13  
**Методологія:** Cross-model compilation simulation · FreeRTOS execution modeling · Hardware register correctness · Memory budget verification with real ESP32-S3FN8 heap · ADR consistency check (ADR-ST-008, CONNECTIVITY §1.5) · Plugin lifecycle state-machine tracing · Dependency graph analysis  
**Контекст:** Перший аудит Plugin System перед початком імплементації (P-2). Всі 5 документів мають статус "Проектування (не імплементовано)". Аудит проводиться як передумова для: P-2 (Plugin System interfaces), P-3 (LDC1101Plugin rewrite), P-4 (Connectivity інтеграція). `LDC1101_ARCHITECTURE.md` v1.0.0 (аудитований окремо, ✅) використовується як reference implementation — він написаний з урахуванням виправленої Plugin System і є еталоном.

---

## Зміст

1. [Загальна оцінка](#1-загальна-оцінка)
2. [Score-карта знахідок](#2-score-карта-знахідок)
3. [PA-1 — SensorData struct: два несумісних визначення](#3-pa-1--sensordata-struct-два-несумісних-визначення)
4. [PA-2 — PluginSystem::initializeAll() без PluginContext*](#4-pa-2--pluginsysteminitializeall-без-plugincontext)
5. [PA-3 — IPlugin vs IDiagnosticPlugin: несумісні ієрархії класів](#5-pa-3--iplugin-vs-idiagnosticplugin-несумісні-ієрархії-класів)
6. [PA-4 — LDC1101 SPI burst order (hardware bug у DIAGNOSTICS)](#6-pa-4--ldc1101-spi-burst-order-hardware-bug-у-diagnostics)
7. [PA-5 — portMAX_DELAY у 5 місцях офіційних прикладів](#7-pa-5--portmax_delay-у-5-місцях-офіційних-прикладів)
8. [PA-6 — Бюджет пам'яті: 512 KB / PSRAM недосяжні на платформі](#8-pa-6--бюджет-памяті-512-kb--psram-недосяжні-на-платформі)
9. [PA-7 — dynamic_cast без гарантії RTTI](#9-pa-7--dynamic_cast-без-гарантії-rtti)
10. [PA-8 — pluginSystem->getContext() не існує](#10-pa-8--pluginsystemgetcontext-не-існує)
11. [PA-9 — runSelfTest() логує через Serial напряму](#11-pa-9--runselftest-логує-через-serial-напряму)
12. [PA-10 — ConfigManager API не визначено](#12-pa-10--configmanager-api-не-визначено)
13. [PA-11 — LDC1101_CONFIG write 0x15: неіснуюче значення режиму](#13-pa-11--ldc1101_config-write-0x15-неіснуюче-значення-режиму)
14. [PA-12 — Factory Pattern hardcoding суперечить extensibility](#14-pa-12--factory-pattern-hardcoding-суперечить-extensibility)
15. [PA-13 — Два підходи до конфігурації плагінів: не узгоджені](#15-pa-13--два-підходи-до-конфігурації-плагінів-не-узгоджені)
16. [PA-14 — IStoragePlugin vs STORAGE_ARCHITECTURE: межа не визначена](#16-pa-14--istorageplugin-vs-storage_architecture-межа-не-визначена)
17. [PA-15 — calibrate() відсутній timing contract у CONTRACT.md](#17-pa-15--calibrate-відсутній-timing-contract-у-contractmd)
18. [PA-16 — Plugin dependency injection: механізм не визначено](#18-pa-16--plugin-dependency-injection-механізм-не-визначено)
19. [PA-17 — millis() overflow у checkCalibration() age check](#19-pa-17--millis-overflow-у-checkcalibration-age-check)
20. [PA-18 — WiFi HTTP endpoint суперечить Connectivity Architecture](#20-pa-18--wifi-http-endpoint-суперечить-connectivity-architecture)
21. [PA-19 — plugin.json у lib/ vs data/plugins/: дублювання не пояснено](#21-pa-19--pluginjson-у-lib-vs-dataplugins-дублювання-не-пояснено)
22. [PA-20 — getStatistics() const + side-effect методи](#22-pa-20--getstatistics-const--side-effect-методи)
23. [Що архітектура зробила ПРАВИЛЬНО](#23-що-архітектура-зробила-правильно)
24. [Pre-implementation Checklist — перед P-2](#24-pre-implementation-checklist--перед-p-2)
25. [Backlog та залежності](#25-backlog-та-залежності)

---

## 1. Загальна оцінка

Документи Plugin System описують амбітну і концептуально правильну Plugin архітектуру для embedded: PluginContext dependency injection, IPlugin lifecycle, спеціалізовані інтерфейси, JSON-конфігурація. Ядро ідеї — sound.

Проте 5 документів писалися паралельно і не пройшли cross-check між собою: знайдено **3 compilation-blocking дефекти** (PA-1..PA-3), **8 serious runtime / correctness дефектів** (PA-4..PA-11), і **9 architectural / quality issues** (PA-12..PA-20).

`LDC1101_ARCHITECTURE.md` §8 (reference implementation) випереджає Plugin docs — він написаний так, ніби PA-1..PA-5 вже виправлені. Це є позитивний сигнал: виправлення Plugin docs є "наздоганянням" до рівня LDC1101_ARCH, а не переробкою концепції.

**Загальна оцінка:** 6.5 / 10 (концепція strong, cross-doc consistency weak)

**Блокуючі для P-2 реалізації:** PA-1 · PA-2 · PA-3 · PA-10

---

## 2. Score-карта знахідок

| ID | Документ | Знахідка | Severity | Статус |
|---|---|---|---|---|
| **PA-1** | ARCH §2, INTERFACES §1, CREATING | `SensorData` 4 vs 5 полів — два несумісних визначення | 🔴 CRITICAL | ✅ CLOSED |
| **PA-2** | ARCH §"Внутрішня структура" | `initializeAll()` викликає `plugin->initialize()` без `PluginContext*` | 🔴 CRITICAL | ✅ CLOSED |
| **PA-3** | ARCH §2, DIAGNOSTICS §1 | `IPlugin` vs `IDiagnosticPlugin`: два документи визначають несумісні ієрархії | 🔴 CRITICAL | ✅ CLOSED |
| **PA-4** | DIAGNOSTICS §2 | LDC1101 `readRP()`: MSB першим — hardware data corruption | 🟠 HIGH | ✅ CLOSED |
| **PA-5** | ARCH §"Thread Safety", CONTRACT §2.2, CREATING §"Async" | `portMAX_DELAY` у 5 місцях — порушує ADR-ST-008 | 🟠 HIGH | ✅ CLOSED |
| **PA-6** | ARCH §Memory, CONTRACT §2.4 | Heap budget 512 KB / PSRAM недосяжні на ESP32-S3FN8 без PSRAM | 🟠 HIGH | ✅ CLOSED |
| **PA-7** | ARCH §"Внутрішня структура" | `dynamic_cast` без гарантії RTTI (`-fno-rtti` default) | 🟠 HIGH | ✅ CLOSED |
| **PA-8** | DIAGNOSTICS §4 | `pluginSystem->getContext(plugin)` — метод не існує ніде | 🟠 HIGH | ✅ CLOSED |
| **PA-9** | DIAGNOSTICS §2 | `runSelfTest()` → `Serial.println()` — порушує CONTRACT §2.4 | 🟡 MEDIUM | ✅ CLOSED |
| **PA-10** | ARCH §PluginContext, CREATING §"Конфіг" | `ConfigManager` API (getInt, getUInt8, getFloat...) ніде не визначено | 🟡 MEDIUM | ✅ CLOSED |
| **PA-11** | DIAGNOSTICS §2 | `writeRegister(LDC1101_CONFIG, 0x15)` — значення 0x15 не існує в register map | 🟡 MEDIUM | ✅ CLOSED |
| **PA-12** | ARCH §"createPlugin" | Factory hardcoding (`if name == "LDC1101"`) суперечить extensibility promise | 🟡 MEDIUM | 🔓 OPEN |
| **PA-13** | ARCH §"data/", LDC1101_ARCH §7, CREATING | Три різних підходи до конфігурації плагінів без canonical рішення | 🟡 MEDIUM | 🔓 OPEN |
| **PA-14** | INTERFACES §5 | `IStoragePlugin` vs `STORAGE_ARCHITECTURE` standalone — межа не визначена | 🟡 MEDIUM | ✅ CLOSED |
| **PA-15** | CONTRACT §2.1 | `calibrate()` може блокуватись (delay 2–5 сек) — не задокументовано в CONTRACT | 🟡 MEDIUM | ✅ CLOSED |
| **PA-16** | CREATING §"Складні випадки" | Plugin dependency injection — механізм не визначено | 🟢 LOW | 🔓 OPEN |
| **PA-17** | DIAGNOSTICS §2 | `millis() - lastCalibrationTime > 30 days` — millis() overflow + non-persistent | 🟢 LOW | ✅ CLOSED |
| **PA-18** | DIAGNOSTICS §"Додаткові фічі" | `WiFiClient HTTP POST` суперечить Connectivity Architecture (BLE+WS) | 🟢 LOW | ✅ CLOSED |
| **PA-19** | ARCH §"data/", CREATING §Checklist | `lib/<Plugin>/plugin.json` vs `data/plugins/name.json` — overlap не пояснено | 🟢 LOW | 🔓 OPEN |
| **PA-20** | DIAGNOSTICS §2 | `getStatistics() const` визначення vs non-const side effects у суміжних методах | 🟢 LOW | 🔓 OPEN |

**Разом:** 3 CRITICAL · 5 HIGH · 7 MEDIUM · 5 LOW = **20 знахідок**  
**Закрито:** PA-1..PA-11 · PA-14 · PA-15 · PA-17 · PA-18 = **15 ✅ CLOSED**  
**Відкрито:** PA-12 · PA-13 · PA-16 · PA-19 · PA-20 = **5 🔓 OPEN**

---

## 3. PA-1 — SensorData struct: два несумісних визначення

**Severity:** 🔴 CRITICAL  
**Статус:** ✅ CLOSED — виправлено docs(plugins) v1.1.0  
**Документи:** `PLUGIN_ARCHITECTURE.md` §2 §4, `PLUGIN_INTERFACES_EXTENDED.md` §1, `CREATING_PLUGINS.md`

### Evidence

`PLUGIN_ARCHITECTURE.md` §2 (`ISensorPlugin`) — **4 поля:**
```cpp
struct SensorData {
    float value1;
    float value2;
    float confidence;
    uint32_t timestamp;
    // bool valid — ВІДСУТНЄ
};
```

`PLUGIN_INTERFACES_EXTENDED.md` §1 (`ISensorPlugin` розширений) — **5 полів:**
```cpp
struct SensorData {
    float value1;
    float value2;
    float confidence;
    uint32_t timestamp;
    bool valid;   // ← НОВЕ поле
};
```

`CREATING_PLUGINS.md` BH1750 приклад:
```cpp
SensorData read() override {
    return {0, 0, 0, 0};  // ← 4 поля, компіліруватиметься тільки з ARCH версією
}
```

`PLUGIN_DIAGNOSTICS.md` §2:
```cpp
return {0, 0, 0, millis(), false};  // ← 5 полів, компілюється тільки з INTERFACES версією
```

`LDC1101_ARCHITECTURE.md` §8 (⭐ canonical):
```cpp
return {0, 0, 0.0f, millis(), false};  // ← 5-польова, з valid
```

### Аналіз

`sizeof(SensorData)` відрізняється між версіями (з врахуванням padding: 4-field = 16 bytes, 5-field з bool = 20 bytes залежно від компілятора). Якщо два .cpp файли включать різні версії `ISensorPlugin.h` — undefined behavior. На практиці: один заголовочний файл `ISensorPlugin.h`, але він не може бути одночасно обома версіями.

### Рішення

**Canonical:** 5-польова версія з `bool valid` з `PLUGIN_INTERFACES_EXTENDED.md` §1 та `LDC1101_ARCHITECTURE.md` §8.  
1. Видалити 4-польовий `struct SensorData` з `PLUGIN_ARCHITECTURE.md` §2 §4 — замінити посиланням: *"Повне визначення SensorData: `PLUGIN_INTERFACES_EXTENDED.md` §1"*  
2. Оновити всі приклади в `CREATING_PLUGINS.md` що повертають 4-польову `{v1, v2, conf, ts}` → `{v1, v2, conf, ts, valid}`

---

## 4. PA-2 — PluginSystem::initializeAll() без PluginContext*

**Severity:** 🔴 CRITICAL  
**Статус:** ✅ CLOSED — виправлено docs(plugins) v1.1.0  
**Документ:** `PLUGIN_ARCHITECTURE.md` §"Внутрішня структура"

### Evidence

`IPlugin` інтерфейс в тому самому документі:
```cpp
virtual bool initialize(PluginContext* ctx) = 0;  // ← аргумент обов'язковий
```

`PluginSystem::initializeAll()` в тому самому документі:
```cpp
void initializeAll() {
    for (auto* plugin : plugins) {
        if (plugin->canInitialize()) {
            if (plugin->initialize()) {  // ← нема аргументу! compile error
                Serial.println("✅");
```

### Аналіз

`plugin->initialize()` без `PluginContext*` — compilation error: `error: too few arguments to function`. PluginSystem ніколи не передає ресурси в плагін. Вся Plugin System не компілюється у поточному вигляді.

Навіть якщо ігнорувати compile error — де і як будується `PluginContext`? Документ показує `Wire`, `SPI`, `ConfigManager`, `Logger` у структурі, але ніде не описано хто, де і коли їх ініціалізує та збирає в `PluginContext`.

### Рішення

1. `PluginSystem` повинен зберігати `PluginContext ctx_` (або `PluginContext* ctx_`), побудований у `PluginSystem::begin()` або `PluginSystem::loadFromConfig()`.
2. `initializeAll()` передає `&ctx_` кожному плагіну:
   ```cpp
   if (plugin->initialize(&ctx_)) { ... }
   ```
3. Додати у документ `PluginSystem::begin(Wire& wire, SPIClass& spi, ConfigManager& cfg, Logger& log)` — точка збору всіх ресурсів.

---

## 5. PA-3 — IPlugin vs IDiagnosticPlugin: несумісні ієрархії класів

**Severity:** 🔴 CRITICAL  
**Статус:** ✅ CLOSED — виправлено docs(plugins) v1.1.0  
**Документи:** `PLUGIN_ARCHITECTURE.md` §3, `PLUGIN_DIAGNOSTICS.md` §1, `LDC1101_ARCHITECTURE.md` §8

### Evidence

`PLUGIN_DIAGNOSTICS.md` §1 — методи діагностики додаються **безпосередньо в `IPlugin`**:
```cpp
class IPlugin {
public:
    // Базові методи (як раніше)
    virtual bool canInitialize() = 0;
    virtual bool initialize(PluginContext* ctx) = 0;
    virtual void update() = 0;
    virtual void shutdown() = 0;
    // ...

    // === НОВІ методи діагностики — в IPlugin ===
    virtual HealthStatus getHealthStatus() const = 0;
    virtual DiagnosticResult runDiagnostics() = 0;
    virtual ErrorCode getLastError() const = 0;
    virtual bool runSelfTest() = 0;
    virtual DiagnosticResult getStatistics() const = 0;
    virtual bool checkHardwarePresence() = 0;
    virtual bool checkCommunication() = 0;
    virtual bool checkCalibration() = 0;
};
```

`LDC1101_ARCHITECTURE.md` §8 (⭐ canonical) — **окремий mixin**:
```cpp
class LDC1101Plugin : public ISensorPlugin, public IDiagnosticPlugin {
    // IDiagnosticPlugin — окремий інтерфейс, не частина IPlugin
```

### Аналіз

Якщо взяти `PLUGIN_DIAGNOSTICS.md` підхід:
- **Кожен** `IPlugin` — навіть `IDisplayPlugin`, `IInputPlugin` — повинен реалізувати `runDiagnostics()`, `runSelfTest()`, `checkCalibration()` тощо.
- Проста клавіатура `TCA8418Plugin : IInputPlugin` змушена реалізувати `checkCalibration()` яка нічого не означає для клавіатури.

Якщо взяти `LDC1101_ARCH` підхід:
- `IDiagnosticPlugin` — опційний mixin. Тільки складні сенсори його реалізують.
- `PluginManager` може перевіряти: `if (auto* diag = dynamic_cast<IDiagnosticPlugin*>(plugin)) { diag->runSelfTest(); }`

### Рішення

**Canonical:** `LDC1101_ARCH` підхід — `IDiagnosticPlugin` є опційним mixin.  
1. В `PLUGIN_DIAGNOSTICS.md` §1: замінити "методи додаються в `IPlugin`" на визначення окремого `IDiagnosticPlugin` інтерфейсу.  
2. Базовий `IPlugin` залишає тільки `HealthStatus getHealthStatus() const` та `ErrorCode getLastError() const` як мінімальну діагностику — вони прості для будь-якого плагіна.  
3. `runDiagnostics()`, `runSelfTest()`, `checkHardwarePresence()`, `checkCommunication()`, `checkCalibration()`, `getStatistics()` — в `IDiagnosticPlugin`, опційно.

---

## 6. PA-4 — LDC1101 SPI burst order (hardware bug у DIAGNOSTICS)

**Severity:** 🟠 HIGH  
**Статус:** ✅ CLOSED — виправлено docs(plugins) v1.1.0  
**Документ:** `PLUGIN_DIAGNOSTICS.md` §2

### Evidence

`PLUGIN_DIAGNOSTICS.md` §2, метод `readRP()`:
```cpp
float readRP() {
    uint16_t raw = (readRegister(LDC1101_RP_MSB) << 8) | readRegister(LDC1101_RP_LSB);
    //              ^^^ MSB (0x22) читається ПЕРШИМ
    return raw * 0.1;
}
```

`LDC1101_ARCHITECTURE.md` §3 (verified by TI datasheet SNOSD01D):
> *REG_RP_DATA_LSB (0x21) **ОБОВ'ЯЗКОВО першим** — hardware auto-increment gate: читання LSB розблоковує доступ до MSB і L_DATA. Якщо читати MSB першим — MSB повертає 0xFF або стале значення, LSB містить попередній цикл.*

Також у тому самому `PLUGIN_DIAGNOSTICS.md` §2:
```cpp
static const uint8_t LDC1101_RP_MSB = 0x22;  // RP_DATA_MSB
static const uint8_t LDC1101_RP_LSB = 0x21;  // RP_DATA_LSB
// ^^^ правильні адреси, але порядок читання неправильний
```

### Аналіз

Наслідок: `raw` матиме silent data corruption. Показник RP буде неправильним без жодних видимих помилок — сенсор "працює" але дані ненадійні. Проявляється тільки на реальному LDC1101, у тестах з mock SPI непомітно.

Крім того, `readRP()` виконує **два окремих SPI транзакції** — між ними можливий preemption іншим плагіном що також використовує SPI. Потрібна **єдина burst транзакція** `CS_LOW → read LSB → read MSB → CS_HIGH`.

### Рішення

`PLUGIN_DIAGNOSTICS.md` §2 — замінити весь приклад LDC1101 посиланням:

> *Для LDC1101 використовуйте canonical implementation: `LDC1101_ARCHITECTURE.md` §8 — `readMeasurementBurst()`. Приклад в цьому документі містить неправильний порядок SPI читання і є illustrative only.*

---

## 7. PA-5 — portMAX_DELAY у 5 місцях офіційних прикладів

**Severity:** 🟠 HIGH  
**Статус:** ✅ CLOSED — виправлено docs(plugins) v1.1.0  
**Документи:** `PLUGIN_ARCHITECTURE.md`, `PLUGIN_CONTRACT.md` §2.2, `CREATING_PLUGINS.md`

### Evidence

| Місце | Код | Документ |
|---|---|---|
| 1 | `xSemaphoreTake(mutex, portMAX_DELAY)` в `SafePlugin::update()` | PLUGIN_ARCHITECTURE.md §"Thread Safety" |
| 2 | `xSemaphoreTake(mutex, portMAX_DELAY)` в `SafePlugin::read()` | PLUGIN_ARCHITECTURE.md §"Thread Safety" |
| 3 | `xSemaphoreTake(mutex, portMAX_DELAY)` в `SafePlugin::update()` | PLUGIN_CONTRACT.md §2.2 |
| 4 | `xSemaphoreTake(mutex, portMAX_DELAY)` в `SafePlugin::read()` | PLUGIN_CONTRACT.md §2.2 |
| 5 | `xSemaphoreTake(self->dataMutex, portMAX_DELAY)` в `AsyncSensor::read()` | CREATING_PLUGINS.md §"Async" |

### Аналіз

**ADR-ST-008** (закрито commit `7d6c314`): `portMAX_DELAY` заборонено у всьому проекті. При FreeRTOS TWDT (Task Watchdog Timer) активному — `portMAX_DELAY` в mutex take може спричинити TWDT reset якщо mutex не звільнений вчасно (наприклад, інший плагін завис).

`PLUGIN_CONTRACT.md` §2.1 забороняє `delay()` в `update()` → 10 ms. Але зразкові приклади у тому самому документі використовують `portMAX_DELAY` — повна суперечність.

Ці приклади є **зразковими для копіювання** — кожен новий плагін успадкує прихований TWDT дефект.

### Рішення

Замінити всі 5 місць:
```cpp
// БУЛО:
xSemaphoreTake(mutex, portMAX_DELAY);

// СТАЛО:
if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    // mutex timeout — пропустити цикл
    return;  // або return {0, 0, 0.0f, millis(), false};
}
```
Додати примітку поруч: `// portMAX_DELAY заборонено (ADR-ST-008) — використовуйте pdMS_TO_TICKS(50)`

---

## 8. PA-6 — Бюджет пам'яті: 512 KB / PSRAM недосяжні на платформі

**Severity:** 🟠 HIGH  
**Статус:** ✅ CLOSED — виправлено docs(plugins) v1.1.0  
**Документи:** `PLUGIN_ARCHITECTURE.md` §Memory, `PLUGIN_CONTRACT.md` §2.4

### Evidence

`PLUGIN_ARCHITECTURE.md` §Memory:
```
ESP32-S3 має: 512 KB SRAM (основна пам'ять), 8 MB PSRAM (опційно)
350 KB — система (PluginSystem, ConfigManager, Logger, FreeRTOS)
160 KB — плагіни (20 плагінів × 8 KB кожен)
```

`PLUGIN_ARCHITECTURE.md` §Memory — приклади:
```cpp
bigBuffer = (float*)ps_malloc(100 * 1024);  // 100 KB в PSRAM
if (!bigBuffer) {
    ctx->log->warning(getName(), "PSRAM not available, using SD card");
```

**Реальність платформи ESP32-S3FN8 (CoinTrace):**
- Модель: **ESP32-S3FN8** — Flash 8 MB, **PSRAM = 0** (N у назві = No PSRAM)
- Вільний heap після ESP-IDF/Arduino старту: **~337 KB** (підтверджено вимірами)
- `ps_malloc()` на цій платформі **завжди повертає nullptr**

### Аналіз

1. **"8 MB PSRAM (опційно)"** — на конкретній платі проекту PSRAM відсутній. Будь-який плагін що викликає `ps_malloc()` отримує `nullptr` без попередження з документу.
2. **512 KB SRAM** — загальний SRAM чіпа. Реальний heap значно менший (~337 KB) через ESP-IDF внутрішні структури, стеки задач, FreeRTOS heaps.
3. **"350 KB sistema + 160 KB plugins = 510 KB"** — перевищує реальний вільний heap (~337 KB). Бюджет в документі фізично неможливий.

### Рішення

1. Переписати §Memory з реальними цифрами:
   - Платформа: ESP32-S3FN8, **PSRAM відсутній**
   - Реально доступний heap: **~337 KB** (вимірювати `ESP.getFreeHeap()` після старту)
   - Heap warning threshold: `ESP.getFreeHeap() < 80 KB` → `LOG_WARN`
2. Видалити/замінити всі `ps_malloc()` приклади — вони не працюють на цій платі.
3. CONTRACT §2.4: замінити "512 KB SRAM" → "~337 KB вільного heap після старту".

---

## 9. PA-7 — dynamic_cast без гарантії RTTI

**Severity:** 🟠 HIGH  
**Статус:** ✅ CLOSED — виправлено docs(plugins) v1.1.0  
**Документ:** `PLUGIN_ARCHITECTURE.md` §"Внутрішня структура"

### Evidence

```cpp
template<typename T>
std::vector<T*> getPluginsByType() {
    std::vector<T*> result;
    for (auto* plugin : plugins) {
        T* typed = dynamic_cast<T*>(plugin);  // ← потребує RTTI
        if (typed && typed->isReady()) {
            result.push_back(typed);
        }
    }
    return result;
}
```

### Аналіз

PlatformIO + ESP32 Arduino framework компілює з `-fno-rtti` за замовчуванням (перевірено: Xtensa GCC ESP-IDF toolchain). `dynamic_cast` без RTTI:
- При LLVM/Clang: повертає `nullptr` — тихо не працює
- При GCC без RTTI: може дати неправильний pointer або compile-time warning, залежно від версії

Якщо увімкнути `-frtti`, це:
- Додає ~30–50 KB до flash binary (RTTI tables)
- Може збільшити heap usage

`getPluginsByType<ISensorPlugin>()` в `performMeasurement()` — центральний API для application code. Якщо повертає порожній вектор через RTTI failure — вся функціональність вимірювань мовчки не працює.

### Рішення

**Варіант A (рекомендований):** Type-tag pattern без RTTI:
```cpp
// В IPlugin:
enum class PluginCategory { SENSOR, DISPLAY, INPUT, AUDIO, STORAGE, IMU, CONNECTIVITY, OTHER };
virtual PluginCategory getCategory() const = 0;

// В ISensorPlugin:
PluginCategory getCategory() const override { return PluginCategory::SENSOR; }

// В PluginSystem:
template<typename T>
std::vector<T*> getPluginsByType(PluginCategory category) {
    std::vector<T*> result;
    for (auto* plugin : plugins) {
        if (plugin->getCategory() == category) {
            result.push_back(static_cast<T*>(plugin));  // static_cast — безпечно після перевірки category
        }
    }
    return result;
}
```

**Варіант B:** Явно додати в `platformio.ini`:
```ini
build_flags = -frtti
```
З коментарем: `; Required for dynamic_cast in PluginSystem (adds ~40KB flash)`

Зафіксувати вибір у документі.

---

## 10. PA-8 — pluginSystem->getContext() не існує

**Severity:** 🟠 HIGH  
**Статус:** ✅ CLOSED — виправлено docs(plugins) v1.1.0  
**Документ:** `PLUGIN_DIAGNOSTICS.md` §4

### Evidence

```cpp
// PLUGIN_DIAGNOSTICS.md §4 — auto-recovery:
void loop() {
    if (status == IPlugin::HealthStatus::TIMEOUT || ...) {
        plugin->shutdown();
        delay(100);
        PluginContext* savedCtx = pluginSystem->getContext(plugin);  // ← не існує
        if (plugin->initialize(savedCtx)) {
```

### Аналіз

`PluginSystem::getContext()` відсутній в жодному документі. Компіляція неможлива. При цьому ідея recovery є valuable — треба коректна реалізація.

### Рішення

Правильна реалізація recovery:

```cpp
// PluginManager зберігає ctx_ і expose метод:
void PluginManager::attemptRecovery(IPlugin* plugin) {
    ctx->log->info("PluginManager", "Attempting recovery: %s", plugin->getName());
    plugin->shutdown();
    vTaskDelay(pdMS_TO_TICKS(100));
    if (plugin->initialize(ctx_)) {  // ctx_ зберігається в PluginManager
        ctx->log->info("PluginManager", "Recovery OK: %s", plugin->getName());
    } else {
        ctx->log->error("PluginManager", "Recovery FAILED: %s", plugin->getName());
    }
}
```

Примітка: кожен плагін також зберігає `ctx` і може бути повторно ініціалізований без `PluginManager`.

---

## 11. PA-9 — runSelfTest() логує через Serial напряму

**Severity:** 🟡 MEDIUM  
**Статус:** ✅ CLOSED — виправлено docs(plugins) v1.1.0  
**Документ:** `PLUGIN_DIAGNOSTICS.md` §2

### Evidence

```cpp
bool runSelfTest() override {
    Serial.println("[LDC1101] Running self-test...");;         // ← пряме Serial
    Serial.printf("  ❌ Device ID mismatch: 0x%02X\n", id);  // ← пряме Serial
    Serial.println("  ✅ Device ID correct");
```

`PLUGIN_CONTRACT.md` §2.4: *"Плагін повинен використовувати `ctx->log`, а не `Serial`"*

### Аналіз

На production build `Serial` може бути вимкнений, перенаправлений, або просто не ініціалізований. Особливо у sleep/low-power mode. `ctx->log` проходить через RingBuffer і може бути збережений на SD nav BLE навіть без Serial.

### Рішення

Замінити всі `Serial.println/printf` на `ctx->log->info(getName(), ...)` / `ctx->log->error(getName(), ...)`.

---

## 12. PA-10 — ConfigManager API не визначено

**Severity:** 🟡 MEDIUM  
**Статус:** ✅ CLOSED — виправлено docs(plugins) v1.1.0  
**Документи:** `PLUGIN_ARCHITECTURE.md` §PluginContext, `CREATING_PLUGINS.md` §"Конфіг"

### Evidence

`PLUGIN_ARCHITECTURE.md` §PluginContext:
```cpp
struct PluginContext {
    ConfigManager* config;  // ← тільки оголошення, без API
};
```

Методи що викликаються в різних документах без оголошення:
```cpp
ctx->config->getInt("ldc1101.spi_cs_pin", 5)         // LDC1101_ARCH §8
ctx->config->getUInt8("ldc1101.resp_time_bits", 0x07) // LDC1101_ARCH §8
ctx->config->getUInt32("ldc1101.clkin_freq_hz", ...)  // LDC1101_ARCH §8
ctx->config->getFloat("mysensor.threshold", 0.5)      // CREATING_PLUGINS
ctx->config->getBool("mysensor.autoCalibrate", false)  // CREATING_PLUGINS
```

### Аналіз

Розробник `lib/PluginSystem/` не знає які методи писати в `ConfigManager`. Кожен приклад вигадує сигнатуру на свій розсуд. Результат: несумісні виклики між плагінами, неможливість написати єдину реалізацію `ConfigManager`.

### Рішення

Додати в `PLUGIN_CONTRACT.md` §1.3 або окремий §1.7 мінімальний `ConfigManager` API:

```cpp
class ConfigManager {
public:
    // Читання параметрів з data/plugins/<name>.json
    // key формат: "<plugin_name>.<param>" або просто "<param>"
    int32_t  getInt    (const char* key, int32_t defaultVal)    const;
    uint8_t  getUInt8  (const char* key, uint8_t defaultVal)    const;
    uint32_t getUInt32 (const char* key, uint32_t defaultVal)   const;
    float    getFloat  (const char* key, float defaultVal)      const;
    bool     getBool   (const char* key, bool defaultVal)       const;
    const char* getString(const char* key, const char* defaultVal) const;
};
```

Namespace правило: `ctx->config->getInt("ldc1101.spi_cs_pin", 5)` → зчитує `"spi_cs_pin"` з `data/plugins/ldc1101.json`.

---

## 13. PA-11 — LDC1101_CONFIG write 0x15: неіснуюче значення режиму

**Severity:** 🟡 MEDIUM  
**Статус:** ✅ CLOSED — виправлено docs(plugins) v1.1.0  
**Документ:** `PLUGIN_DIAGNOSTICS.md` §2

### Evidence

```cpp
static const uint8_t LDC1101_CONFIG = 0x0B;  // REG_START_CONFIG
// ...
writeRegister(LDC1101_CONFIG, 0x15);  // ← magic number 0x15 = 0b00010101
```

REG_START_CONFIG (0x0B) FUNC_MODE поле (bits [1:0]):
- `0x00` = Active (continuous conversion)
- `0x01` = Sleep (конфігурація тільки в Sleep mode)
- `0x02` = Shutdown

`0x15 = 0b00010101` — bits [1:0] = `0b01` (Sleep), але bits [7:2] = `0b000101` — undefined behavior по TI datasheet.

### Аналіз

Якщо розробник скопіює цей код — сенсор може не перейти в Active mode або поводитись непередбачувано.

### Рішення

Замінити `writeRegister(LDC1101_CONFIG, 0x15)` на:
```cpp
writeRegister(REG_START_CONFIG, FUNC_MODE_SLEEP);  // 0x0B = 0x01
```
І додати в документ: *"Для конфігурації LDC1101 dивися canonical implementation: `LDC1101_ARCHITECTURE.md` §5 та §8"*.

---

## 14. PA-12 — Factory Pattern hardcoding суперечить extensibility promise

**Severity:** 🟡 MEDIUM  
**Статус:** 🔓 OPEN  
**Документ:** `PLUGIN_ARCHITECTURE.md` §"Внутрішня структура"

### Evidence

Головна обіцянка документу (§"Мета"):
> *"Зовнішнім розробникам достатньо додати свою папку з кодом"*  
> *"Жодного редагування main.cpp"*

Реальний `createPlugin()`:
```cpp
IPlugin* createPlugin(const std::string& name) {
    if (name == "LDC1101")  return new LDC1101Plugin();
    if (name == "BMI270")   return new BMI270Plugin();
    if (name == "QMC5883L") return new QMC5883LPlugin();
    if (name == "SDCard")   return new SDCardPlugin();
    return nullptr;
}
```

Зовнішній розробник **мусить редагувати** `PluginSystem::createPlugin()` — суперечність.

### Рішення

**Варіант A — Self-registration macro** (рекомендований для embedded, без dynamic loading):
```cpp
// В кожному plugin .cpp:
REGISTER_PLUGIN(LDC1101Plugin, "LDC1101")

// Macro визначення:
#define REGISTER_PLUGIN(ClassName, PluginName) \
    static bool _registered_##ClassName = \
        PluginRegistry::instance().registerFactory(PluginName, []{ return new ClassName(); })
```

**Варіант B — Явно задокументувати обмеження:**
> *"Додавання нового плагіна потребує одного рядка в `PluginSystem::createPlugin()`. Це обмеження embedded середовища (без dynamic linking)."*

Хоча б усунути суперечність між обіцянкою і реальністю.

---

## 15. PA-13 — Два підходи до конфігурації плагінів: не узгоджені

**Severity:** 🟡 MEDIUM  
**Статус:** 🔓 OPEN  
**Документи:** `PLUGIN_ARCHITECTURE.md`, `LDC1101_ARCHITECTURE.md` §7, `CREATING_PLUGINS.md`

### Evidence

| Підхід | Джерело | Вміст |
|---|---|---|
| `lib/<Plugin>/plugin.json` | PLUGIN_ARCH §"Структура проекту" | `{name, version, author, i2c_address, min_ram, ...}` — static metadata |
| `data/config.json` | PLUGIN_ARCH §"Конфіг" | `{"plugins": [{"type": "sensor", "name": "LDC1101", "enabled": true}]}` — список плагінів |
| `data/plugins/ldc1101.json` | LDC1101_ARCH §7 | `{spi_cs_pin, resp_time_bits, rp_set, clkin_freq_hz}` — runtime parameters |

### Аналіз

Три JSON для одного плагіна. Жоден документ не пояснює:
- яка різниця між `lib/<Plugin>/plugin.json` і `data/plugins/ldc1101.json`
- хто читає `lib/<Plugin>/plugin.json` в рантаймі (LittleFS не може читати `lib/` — це compile-time)
- як `data/config.json` корелює з `data/plugins/ldc1101.json`

### Рішення

Зафіксувати canonical підхід (узгоджений з `LDC1101_ARCH`):

| Файл | Роль | Читається як |
|---|---|---|
| `lib/<Plugin>/plugin.json` | Static build-time metadata (version, i2c_address, hw_requirements) | Тільки для CI/IDE tooling — **не копіюється на флеш** |
| `data/plugins/<name>.json` | Runtime config (cs_pin, thresholds, calibration) | `ConfigManager` з LittleFS |
| `data/config.json` | Список активних плагінів + hardware profile | `PluginSystem::loadFromConfig()` |

---

## 16. PA-14 — IStoragePlugin vs STORAGE_ARCHITECTURE: межа не визначена

**Severity:** 🟡 MEDIUM  
**Статус:** ✅ CLOSED — виправлено docs(plugins) v1.1.0  
**Документи:** `PLUGIN_INTERFACES_EXTENDED.md` §5, `STORAGE_ARCHITECTURE.md` v1.5.0

### Evidence

`PLUGIN_INTERFACES_EXTENDED.md` §5:
```cpp
class IStoragePlugin : public IPlugin {
    virtual bool save(const char* key, const void* data, size_t size) = 0;
    virtual bool load(const char* key, void* data, size_t size) = 0;
    virtual bool remove(const char* key) = 0;
    virtual size_t getAvailableSpace() = 0;
};
```

`STORAGE_ARCHITECTURE.md` v1.5.0 (аудитований, ✅):
- `LittleFSManager` — standalone клас, не є IPlugin
- `NVSManager` — standalone клас, не є IPlugin
- `MeasurementStore` — standalone клас, не є IPlugin

### Аналіз

Два паралельних підходи без документованої межі. Чи повинен `LittleFSManager` реалізовувати `IStoragePlugin`? Якщо так — він буде залежним від Plugin System (circular dependency). Якщо ні — навіщо `IStoragePlugin`?

### Рішення

Додати у `PLUGIN_INTERFACES_EXTENDED.md` §5 примітку:
> *`IStoragePlugin` призначений для **зовнішніх** storage модулів (EEPROM, NOR flash breakout boards). Системні сховища (`LittleFSManager`, `NVSManager`, `MeasurementStore`, `SDCardManager`) є standalone класами, визначеними в `STORAGE_ARCHITECTURE.md`, і **не** реалізують `IStoragePlugin`. `SDCardPlugin` (якщо потрібен) — єдиний кандидат для `IStoragePlugin`.*

---

## 17. PA-15 — calibrate() відсутній timing contract у CONTRACT.md

**Severity:** 🟡 MEDIUM  
**Статус:** ✅ CLOSED — виправлено docs(plugins) v1.1.0  
**Документ:** `PLUGIN_CONTRACT.md` §2.1, `CREATING_PLUGINS.md`, `LDC1101_ARCHITECTURE.md` §8

### Evidence

`PLUGIN_CONTRACT.md` §2.1 — чітко обмежує:
- `update()`: max 10 ms
- `read()`: max 5 ms

`calibrate()` — нічого.

`CREATING_PLUGINS.md` HX711 приклад:
```cpp
bool calibrate() override {
    ctx->log->info(getName(), "Place 100g calibration weight...");
    delay(5000);  // ← 5 секунд блокування, без застереження
```

`LDC1101_ARCHITECTURE.md` §8 (правильно):
```cpp
// ⚠ Увага: calibrate() використовує delay() — викликати поза update() циклом.
bool calibrate() override {
    delay(2000);
```

### Рішення

Додати в `PLUGIN_CONTRACT.md` новий розділ §3.4:

> **§3.4 calibrate() — Blocking Allowed**
>
> `calibrate()` може виконувати `delay()` або блокуючі операції. Але:
> - **Заборонено** викликати `calibrate()` з main update loop — тільки з application code за явним запитом користувача
> - Тривалість документується в коментарі реалізації та/або в `SensorMetadata`
> - `calibrate()` не повинен залишати систему в невизначеному стані при перериванні

---

## 18. PA-16 — Plugin dependency injection: механізм не визначено

**Severity:** 🟢 LOW  
**Статус:** 🔓 OPEN  
**Документ:** `CREATING_PLUGINS.md` §"Складні випадки"

### Evidence

```cpp
class FusionSensorPlugin : public ISensorPlugin {
    void setDependencies(ISensorPlugin* s1, ISensorPlugin* s2) {
        sensor1 = s1; sensor2 = s2;
    }
```

### Аналіз

Хто викликає `setDependencies()`? `PluginSystem`? application? Не описано. Якщо `PluginSystem` — він мусить знати про конкретний `FusionSensorPlugin`. Якщо application — порушується ідея "не змінних main.cpp".

### Рішення

Або: явно заборонити plugin-to-plugin dependencies (розмежування відповідальностей — кожен плагін незалежний). Або: описати dependency через `PluginContext` розширення — `ctx->getPlugin("LDC1101")`.

Зафіксувати рішення в документі.

---

## 19. PA-17 — millis() overflow у checkCalibration() age check

**Severity:** 🟢 LOW  
**Статус:** ✅ CLOSED — виправлено docs(plugins) v1.1.0  
**Документ:** `PLUGIN_DIAGNOSTICS.md` §2

### Evidence

```cpp
bool checkCalibration() override {
    uint32_t calibrationAge = millis() - lastCalibrationTime;
    if (calibrationAge > 30UL * 24 * 60 * 60 * 1000) {  // 30 днів
        return false;
    }
```

### Аналіз

1. `millis()` переповнюється на ~49.7 добу. Wrap-around при uint32 арифметиці коректний ([millis - last] завжди правильний навіть при overflow), але `30UL * 24 * 60 * 60 * 1000 = 2,592,000,000` — менше `UINT32_MAX` (4,294,967,295), тому overflow у самій перевірці некритичний.
2. Критичніше: `lastCalibrationTime` — не персистентне поле. Скидається до 0 при кожному reboot. `calibrationAge = millis() - 0 = millis()` → через 30 днів після першого старту починає постійно повертати `false`. Але після перезавантаження `millis()=0` знову, і калібрування здається "свіжим".

### Рішення

Або видалити перевірку часу калібрування (вона не надійна без RTC/NVS), або зберігати `lastCalibrationTime` в NVS (STORAGE_ARCHITECTURE §5 NVSManager). Задокументувати обмеження.

---

## 20. PA-18 — WiFi HTTP endpoint суперечить Connectivity Architecture

**Severity:** 🟢 LOW  
**Статус:** ✅ CLOSED — виправлено docs(plugins) v1.1.0  
**Документ:** `PLUGIN_DIAGNOSTICS.md` §"Додаткові фічі"

### Evidence

```cpp
void sendDiagnosticsToCloud() {
    WiFiClient client;
    client.post("https://your-server.com/api/diagnostics", json);  // ← HTTP POST
}
```

`CONNECTIVITY_ARCHITECTURE.md` v1.4.0: планова connectivity = BLE GATT + WebSocket. WiFi HTTP POST відсутній в жодному dokumentі плануванні.

### Рішення

Видалити секцію "Remote Diagnostics" або замінити на BLE notification відповідно до Connectivity Architecture.

---

## 21. PA-19 — plugin.json у lib/ vs data/plugins/: дублювання не пояснено

**Severity:** 🟢 LOW  
**Статус:** 🔓 OPEN  
**Документи:** `CREATING_PLUGINS.md` §Checklist, `LDC1101_ARCHITECTURE.md` §7

### Evidence

`CREATING_PLUGINS.md` §Checklist → вимагає `lib/BH1750Plugin/plugin.json` з параметрами:
```json
{"name": "BH1750", "i2c_address": "0x23", "dependencies": ["Wire"], "configuration": {"sample_rate": 200}}
```

`LDC1101_ARCHITECTURE.md` §7 → runtime config у `data/plugins/ldc1101.json`:
```json
{"spi_cs_pin": 5, "resp_time_bits": 7, "rp_set": 7}
```

Обидва JSON для одного "плагіна" — але в різних місцях і з різним вмістом. Не пояснено чим вони відрізняються.

### Рішення

Пояснення (закривається рішенням PA-13): `lib/<Plugin>/plugin.json` = build-time metadata (не на флеші), `data/plugins/name.json` = runtime user-configurable parameters (на флеші).

---

## 22. PA-20 — getStatistics() const + side-effect методи

**Severity:** 🟢 LOW  
**Статус:** 🔓 OPEN  
**Документ:** `PLUGIN_DIAGNOSTICS.md` §2

### Evidence

```cpp
DiagnosticResult getStatistics() const override {  // ← const
    // ...зчитує diagnostics поля, millis() — OK
}
```

Поруч у тому ж класі:
```cpp
bool checkStability() {  // ← non-const, викликає readRegister() і delay()
    // 10 зчитувань через SPI, delay(5) між ними
}
```

`checkStability()` викликається з `runDiagnostics()` (non-const) — коректно. Але якщо у майбутньому `getStatistics()` буде викликати `checkStability()` — const violation.

### Аналіз

Незначна знахідка — поточний код коректний. Але архітектурно `checkStability()` не повинен бути публічним або публічним без `const`. Варто зробити `private`.

### Рішення

Зробити `checkStability()` приватним або `const`-aware. Minor — виправити при реалізації.

---

## 23. Що архітектура зробила ПРАВИЛЬНО

| Аспект | Оцінка |
|---|---|
| **PluginContext dependency injection** — шини передаються готовими, не ініціалізуються в плагінах | ✅ Excellent |
| **Lifecycle гарантії** — `canInitialize()→initialize()→update()→shutdown()` порядок чіткий | ✅ Excellent |
| **PLUGIN_CONTRACT.md** — окрема специфікація що гарантує та що плагін зобов'язаний | ✅ Professional |
| **HealthStatus enum** — детальний (15 значень), покриває всі практичні стани сенсора | ✅ Excellent |
| **SpiMutex та wireMutex у PluginContext** — правильний підхід до shared bus | ✅ Correct |
| **Config через ctx→config** — не хардкод, не глобальні змінні | ✅ Good |
| **Logging через ctx→log** — контракт задокументований (хоч і порушується в прикладах) | ✅ Good |
| **SensorMetadata** — одиниці вимірювання, діапазони, sampleRate як частина інтерфейсу | ✅ Good |
| **Приклади в CREATING_PLUGINS.md** — I2C, SPI, async, конфіг, dependency — широке покриття | ✅ Good |
| **IDiagnosticPlugin checklist** в §10 LDC1101_ARCH — compliance table | ✅ Professional |

---

## 24. Pre-implementation Checklist — перед P-2

Перед початком реалізації `lib/PluginSystem/` всі P-1 та P-2 знахідки повинні бути закриті в документах.

### Обов'язкові (блокують компіляцію):
- [ ] **PA-1** — `SensorData` уніфікована в `PLUGIN_INTERFACES_EXTENDED.md` §1 (5 полів, canonical)
- [ ] **PA-2** — `PluginSystem::initializeAll()` передає `PluginContext*`; документ показує `PluginSystem::begin()`
- [ ] **PA-3** — `IDiagnosticPlugin` оголошений як опційний mixin; `IPlugin` містить тільки `getHealthStatus()` + `getLastError()`
- [ ] **PA-10** — `ConfigManager` мінімальний API визначений в `PLUGIN_CONTRACT.md`

### Обов'язкові (запобігають hardware/runtime багам):
- [ ] **PA-4** — приклад LDC1101 в DIAGNOSTICS замінений на посилання до LDC1101_ARCH §8
- [ ] **PA-5** — всі `portMAX_DELAY` замінені на `pdMS_TO_TICKS(50)` у всіх 5 місцях
- [ ] **PA-6** — бюджет пам'яті переписаний з реальними числами (~337 KB, no PSRAM)
- [ ] **PA-7** — рішення RTTI зафіксоване (`-frtti` або type-tag enum)
- [ ] **PA-8** — recovery приклад виправлений (ctx зберігається в PluginManager)
- [ ] **PA-11** — `writeRegister(0x0B, 0x15)` виправлений

### Рекомендовані (до P-3):
- [ ] **PA-9** — `Serial.println` → `ctx->log->info` в DIAGNOSTICS prикладах
- [ ] **PA-13** — canonical конфіг підхід задокументований
- [ ] **PA-15** — CONTRACT §3.4 для `calibrate()` написаний

### Backlog (до production):
- [ ] PA-12, PA-14, PA-16, PA-17, PA-18, PA-19, PA-20

---

## 25. Backlog та залежності

### Залежності між знахідками

```
PA-1 (SensorData)
    └─ блокує: всі ISensorPlugin реалізації (P-3)

PA-2 (initializeAll PluginContext*)
    └─ блокує: PluginManager::begin() spec

PA-3 (IPlugin vs IDiagnosticPlugin)
    └─ блокує: IDiagnosticPlugin definition в DIAGNOSTICS §1
    └─ залежить від: LDC1101_ARCH §8 як reference (вже правильний ✅)

PA-7 (dynamic_cast / RTTI)
    └─ блокує: getPluginsByType() implementation
    └─ рішення впливає на: platformio.ini

PA-10 (ConfigManager API)
    └─ блокує: будь-яка реалізація що читає конфіг
    └─ пов'язано з: PA-13 (canonical config approach)

PA-13 (canonical config)
    └─ пов'язано з: PA-19 (plugin.json vs data/plugins/)
    └─ впливає на: ConfigManager implementation
```

### Cross-reference з закритими ADR

| PA | Пов'язаний ADR/знахідка | Статус |
|---|---|---|
| PA-5 | ADR-ST-008 [`portMAX_DELAY` forbidden] | ADR закрито ✅, Plugin docs не оновлені ❌ |
| PA-4 | LDC1101_ARCH §3 [LSB-first hardware requirement] | LDC1101_ARCH ✅, DIAGNOSTICS ❌ |
| PA-6 | CONNECTIVITY §8.7 heap table [~337 KB real heap] | CONNECTIVITY v1.4.0 ✅, PLUGIN docs ❌ |
| PA-9 | PLUGIN_CONTRACT §2.4 [no direct Serial] | CONTRACT written ✅, приклад порушує ❌ |

---

*PLUGIN_AUDIT_v1.0.0.md*  
*Аудитор: GitHub Copilot (Claude Sonnet 4.6) — незалежний embedded архітектор*  
*Дата: 2026-03-13*  
*Наступний крок: закрити PA-1..PA-3, PA-10 (4 блокери P-2) → commit → починати `lib/PluginSystem/`*
