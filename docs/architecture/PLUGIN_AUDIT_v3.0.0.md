# Plugin Architecture — Незалежний Архітектурний Аудит v3.0.0

**Документ:** PLUGIN_AUDIT_v3.0.0.md  
**Дата аудиту:** 2026-03-13  
**Аудитовані документи:**
- `PLUGIN_ARCHITECTURE.md` v2.0.0 (2026-03-13)
- `PLUGIN_CONTRACT.md` v1.0.0 (2026-03-10)
- `PLUGIN_INTERFACES_EXTENDED.md` v1.1.0 (2026-03-10)
- `PLUGIN_DIAGNOSTICS.md` v1.0.0 (2026-03-10)
- `CREATING_PLUGINS.md` (без версії)

**Попередні аудити (архів):**  
- `obsolete/PLUGIN_AUDIT_v1.0.0.md` — базовий аудит  
- `obsolete/PLUGIN_AUDIT_v2.0.0.md` — другий аудит + зовнішній аудит v4 (PA3-xx)

**Методологія (незалежний аналіз):**
1. **Cross-document compilation simulation** — всі 5 документів як єдиний header-set; virtual трасування використання типів
2. **FreeRTOS execution timeline modeling** — update loop budget, task priority analysis, blocking simulation
3. **Memory allocation simulation** — ESP32-S3FN8 337 KB heap, з BLE, без PSRAM, 10+ плагінів
4. **SPI bus contention probability model** — VSPI: LDC1101 + SD card на одній шині
5. **Plugin lifecycle FSM tracing** — повний сценарій `canInit → init → update → shutdown` для кожного прикладного плагіна
6. **Behavioral path simulation** — мануальне виконання ключових code paths (AsyncSensorPlugin, attemptRecovery, runDiagnostics)
7. **Interface compatibility matrix** — кожне визначення методу в кожному документі порівняно по-парно
8. **Implementation readiness scoring** — 9 категорій × 0–10 балів

**Платформа:** ESP32-S3FN8, M5Stack Cardputer-ADV, ~337 KB heap, PSRAM відсутній

---

## Зміст

1. [Загальна оцінка та висновок готовності](#1-загальна-оцінка-та-висновок-готовності)
2. [Score-карта: всі знахідки](#2-score-карта-всі-знахідки)
3. [Симуляційне моделювання v3.0.0](#3-симуляційне-моделювання-v300)
4. [N-1 — SensorMetadata: поля у реалізаціях не збігаються з визначенням struct](#4-n-1--sensormetadata-поля-у-реалізаціях-не-збігаються-з-визначенням-struct)
5. [N-2 — attemptRecovery(): canInitialize() з вже встановленим ctx — CONTRACT ambiguity](#5-n-2--attemptrecovery-caninitialize-з-вже-встановленим-ctx--contract-ambiguity)
6. [N-3 — CONTRACT §2.4 vs ARCH §Memory: "20 плагінів" vs "max 10 плагінів"](#6-n-3--contract-24-vs-arch-memory-20-плагінів-vs-max-10-плагінів)
7. [N-4 — IInputPlugin::pollEvent(): різні сигнатури в CONTRACT §3.3 та INTERFACES_EXTENDED §3](#7-n-4--iinputpluginpollevent-різні-сигнатури-в-contract-33-та-interfaces_extended-3)
8. [N-5 — logDiagnostics(): spiMutex передається параметром, але caller не має до нього доступу](#8-n-5--logdiagnostics-spimutex-передається-параметром-але-caller-не-має-до-нього-доступу)
9. [N-6 — HX711Plugin: get_units(1) на 10 Hz = 100 ms — CONTRACT §2.1 порушено після fix](#9-n-6--hx711plugin-get_units1-на-10-hz--100-ms--contract-21-порушено-після-fix)
10. [N-7 — AsyncSensorPlugin: xTaskCreate() не перевіряється — initialize() повертає true при помилці](#10-n-7--asyncsensorplugin-xtaskcreate-не-перевіряється--initialize-повертає-true-при-помилці)
11. [N-8 — AsyncSensorPlugin: xSemaphoreCreateMutex() не перевіряється — null mutex crash](#11-n-8--asyncsensorplugin-xsemaphorecreatemutex-не-перевіряється--null-mutex-crash)
12. [N-9 — QMC5883LPlugin::readRegister16(): ctx->wire без wireMutex](#12-n-9--qmc5883lpluginreadregister16-ctx-wire-без-wiremutex)
13. [N-10 — AsyncSensorPlugin: TaskHandle_t readTask не ініціалізований як nullptr](#13-n-10--asyncsensorplugin-taskhandle_t-readtask-не-ініціалізований-як-nullptr)
14. [Підтверджені OPEN знахідки (перенесено з v2.0.0 аудиту)](#14-підтверджені-open-знахідки-перенесено-з-v200-аудиту)
15. [Підтверджені CLOSED знахідки (верифіковані незалежно)](#15-підтверджені-closed-знахідки-верифіковані-незалежно)
16. [Що архітектура зробила ПРАВИЛЬНО](#16-що-архітектура-зробила-правильно)
17. [Pre-implementation Checklist v3.0.0](#17-pre-implementation-checklist-v300)
18. [Implementation Readiness Scoring](#18-implementation-readiness-scoring)
19. [Backlog та залежності між знахідками](#19-backlog-та-залежності-між-знахідками)
20. [Cross-reference: попередні аудити → v3.0.0](#20-cross-reference-попередні-аудити--v300)

---

## 1. Загальна оцінка та висновок готовності

### Оцінка архітектури: 6.5 / 10

Архітектура суттєво покращилась з v1.0.0 → v2.0.0: закрито усі базові compile errors (PA2-1..PA2-14), виправлено critical hardware bugs (SPI mutex, burst read, task leak). Концепція залишається **сильною**: PluginContext DI, canonical three-JSON config, clear lifecycle FSM, mutex protocol.

**Проте виявлено нову критичну проблему:** PA3-4 fix (`getMetadata()` додано до BH1750Plugin та MyI2CSensorPlugin) вніс **НОВИЙ compile error** — реалізації використовують поля `.sensorType`, `.manufacturer`, `.model`, `.updateRateHz`, яких немає у struct `SensorMetadata` (визначений як `typeName, unit, minValue, maxValue, resolution, sampleRate`). Цей factory example не скомпілюється.

Додатково: `pollEvent()` в `IInputPlugin` має різні сигнатури в двох документах (`bool pollEvent(InputEvent& event)` vs `std::optional<InputEvent> pollEvent()`).

### Результат після виправлень (2026-03-13): ✅ Compile blockers закриті — P-2 можна починати

> **Хвиля 1+2 виконана:** N-1 ✅ N-4 ✅ N-7 ✅ N-8 ✅ N-10 ✅ PA2-17 ✅ PA3-6 ✅  
> Оновлена оцінка: **7.1/10** (було 6.5/10 на момент аудиту). Відкрито: N-3/6/9, PA2-12/15/16/18, PA3-8/10/11 — не блокують P-2.

### Готовність до імплементації: ✅ UNBLOCKED (після виправлень 2026-03-13)

| Категорія | Оцінка | Стан |
|---|---|---|
| Core IPlugin interface | 9/10 | ✅ Готово |
| Lifecycle FSM | 9/10 | ✅ Готово |
| SPI/I2C bus management | 8/10 | ✅ Готово (LDC1101) |
| Documentation consistency | 5/10 | ❌ N-1, N-3, N-4 блокують |
| Template quality | 4/10 | ❌ N-1 compile error, N-8/N-10 crash |
| Thread safety | 6/10 | ⚠️ N-9 відкрита (PA2-17 закрито 2026-03-13) |
| Timing compliance | 5/10 | ⚠️ N-6, PA3-6, PA3-8 |
| Error handling | 7/10 | ✅ LDC1101 зразковий |
| Testability | 4/10 | ⚠️ PA2-12 mock nullptr |

---

## 2. Score-карта: всі знахідки

### Нові знахідки v3.0.0 (незалежний аудит 2026-03-13)

| ID | Документ | Знахідка | Severity | Статус |
|---|---|---|---|---|
| **N-1** | CREATING_PLUGINS `BH1750Plugin` + `MyI2CSensorPlugin` | `getMetadata()` реалізації використовують `.sensorType`, `.manufacturer`, `.model`, `.updateRateHz` — поля не існують у struct `SensorMetadata` → compile error (введено PA3-4 fix) | 🔴 CRITICAL | 🔓 OPEN |
| **N-2** | ARCH `attemptRecovery()` + CONTRACT §1.1 | `canInitialize()` викликається на плагіні що вже має `ctx` з попереднього `initialize()` — CONTRACT не документує цей сценарій recovery | 🟢 LOW | 🔓 OPEN |
| **N-3** | CONTRACT §2.4 vs ARCH §Memory | CONTRACT: "до **20** плагінів", ARCH після PA2-13 fix: "максимум **10** плагінів" — пряма суперечність, misleads авторів плагінів | 🟡 MEDIUM | 🔓 OPEN |
| **N-4** | CONTRACT §3.3 vs INTERFACES_EXTENDED §3 | `IInputPlugin::pollEvent()`: CONTRACT = `bool pollEvent(InputEvent& event)`, INTERFACES = `std::optional<InputEvent> pollEvent()` — несумісні сигнатури → compile error при cross-reference | 🟠 HIGH | ✅ CLOSED 2026-03-13 |
| **N-5** | DIAGNOSTICS §4/§5 `logDiagnostics(spiMutex)` | Після PA3-3 fix функція приймає `SemaphoreHandle_t spiMutex` параметром, але caller в main.cpp не має прямого доступу до `ctx_.spiMutex` (приватний член PluginSystem) | 🟢 LOW | 🔓 OPEN |
| **N-6** | INTERFACES_EXTENDED `HX711Plugin` | `get_units(1)` на 10 Hz = **100 ms** blocking — CONTRACT §2.1 (≤10 ms) порушено. PA3-2 поліпшив зі 500→100 ms але фундаментальне порушення залишилось | 🟠 HIGH | 🔓 OPEN |
| **N-7** | CREATING_PLUGINS `AsyncSensorPlugin` | `xTaskCreate()` return value не перевіряється → `initialize()` повертає `true` навіть якщо task не створено → плагін у неконсистентному стані | 🟡 MEDIUM | ✅ CLOSED 2026-03-13 |
| **N-8** | CREATING_PLUGINS `AsyncSensorPlugin` | `xSemaphoreCreateMutex()` return value не перевіряється → `dataMutex = nullptr` при нестачі heap → `xSemaphoreTake(nullptr, ...)` в `read()` → crash | 🟠 HIGH | ✅ CLOSED 2026-03-13 |
| **N-9** | ARCH §4 `QMC5883LPlugin` `readRegister16()` | `ctx->wire->requestFrom()` та `ctx->wire->read()` без `wireMutex` → race condition якщо `read()` викликається з будь-якого task (CONTRACT §2.2) | 🟡 MEDIUM | 🔓 OPEN |
| **N-10** | CREATING_PLUGINS `AsyncSensorPlugin` | `TaskHandle_t readTask;` оголошено без `= nullptr` → якщо `xTaskCreate()` fails, `readTask` має garbage value → `if (readTask) vTaskDelete(readTask)` → vTaskDelete з undefined handle → crash | 🟡 MEDIUM | ✅ CLOSED 2026-03-13 |

### Підтверджені OPEN (перенесені з v2.0.0 аудиту)

| ID | Severity | Статус | Примітка |
|---|---|---|---|
| **PA2-12** | 🟡 MEDIUM | 🔓 OPEN | mock `PluginContext.config = nullptr` в CREATING_PLUGINS §Debugging |
| **PA2-15** | 🟡 MEDIUM | 🔓 OPEN | `ctx->log` та `ctx` в `loop()` scope — out of scope reference |
| **PA2-16** | 🟢 LOW | 🔓 OPEN | `calibrationBaseline = 0` при старті → `checkCalibration()` завжди false |
| **PA2-17** | 🟠 HIGH | ✅ CLOSED 2026-03-13 | BH1750 `read()`: `wireMutex` додано навколо I2C транзакції |
| **PA2-18** | 🟢 LOW | 🔓 OPEN | `reloadPlugin()`, `installPluginOTA()` — NOT IMPLEMENTED не позначено |
| **C-PA16** | 🟢 LOW | 🔓 OPEN | FusionSensorPlugin setDependencies() — lifecycle порядок не документований |
| **PA3-6** | 🟠 HIGH | ✅ CLOSED 2026-03-13 | `runDiagnostics()` ⚠️ BLOCKING попередження додано на IDiagnosticPlugin публічний метод |
| **PA3-8** | 🟡 MEDIUM | 🔓 OPEN | CONTRACT §1.1 ≥10 Hz гарантія: 10+ плагінів × 10ms = >100ms loop → 9.1 Hz (порушення) |
| **PA3-10** | 🟡 MEDIUM | 🔓 OPEN | `lastCalibrationTime = 0` після ребуту → 30-денний trigger ніколи не спрацює |
| **PA3-11** | 🟡 MEDIUM | 🔓 OPEN | `showDiagnosticsScreen()` використовує глобальні `displayPlugin`, `pluginSystem` |
| **PA3-16** | 🔵 INFO | 🔓 OPEN | `getGraphicsLibrary()` повертає `void*` — type-unsafe |
| **PA3-17** | 🔵 INFO | 🔓 OPEN | `getTypeId()` не реалізований у жодному прикладі |

### Підсумок v3.0.0

**Нових знахідок:** 1 CRITICAL · 3 HIGH · 4 MEDIUM · 2 LOW = **10 нових знахідок**  
**Перенесено open:** 2 HIGH · 5 MEDIUM · 3 LOW · 2 INFO = **12 перенесено**  
**Всього відкритих:** 22 знахідки  

**Blocker для P-2:** N-1 (CRITICAL), N-4 (HIGH) → **2 compile blockers**

---

## 3. Симуляційне моделювання v3.0.0

### 3.1 Cross-Document Compilation Simulation

Трасуємо тип `SensorMetadata` через всі документи:

```
INTERFACES_EXTENDED §1 визначає struct SensorMetadata:
  struct SensorMetadata {
      const char* typeName;       ← "Weight Sensor"
      const char* unit;           ← "g"
      float minValue;
      float maxValue;
      float resolution;
      uint16_t sampleRate;        ← Частота в Hz
  };

HX711Plugin (INTERFACES_EXTENDED) реалізує коректно:
  return { .typeName = "Weight Sensor", .unit = "g", .sampleRate = 10, ... }; ✅

BH1750Plugin (CREATING_PLUGINS) реалізує НЕПРАВИЛЬНО:
  return {
      .sensorType   = SensorType::LIGHT,   // ← ПОЛЕ НЕ ІСНУЄ → compile error
      .manufacturer = "ROHM...",           // ← ПОЛЕ НЕ ІСНУЄ → compile error
      .model        = "BH1750FVI",         // ← ПОЛЕ НЕ ІСНУЄ → compile error
      .updateRateHz = 10                   // ← ПОЛЕ НЕ ІСНУЄ (є sampleRate) → compile error
  };

MyI2CSensorPlugin template НЕПРАВИЛЬНО:
  return { .sensorType = SensorType::CUSTOM, .manufacturer = "Unknown",
           .model = "MyI2CSensor", .updateRateHz = 10 };  // ← всі поля НЕІСНУЮЧІ → compile error
```

**Compile error підсумок:**
```
BH1750Plugin.h:XX: error: 'sensorType' is not a member of 'SensorMetadata'
BH1750Plugin.h:XX: error: 'manufacturer' is not a member of 'SensorMetadata'
BH1750Plugin.h:XX: error: 'model' is not a member of 'SensorMetadata'
BH1750Plugin.h:XX: error: 'updateRateHz' is not a member of 'SensorMetadata'
MyI2CSensorPlugin.h:XX: error: 'sensorType' is not a member of 'SensorMetadata'
[...same pattern...]
```

**Першопричина:** PA3-4 fix додав `getMetadata()` реалізації до CREATING_PLUGINS, але автор використав **поля іншої структури** (можливо, з початкового прото-дизайну), а не з актуального struct у INTERFACES_EXTENDED.

**Рішення:** Або (A) оновити SensorMetadata struct щоб включала `sensorType, manufacturer, model, updateRateHz` (кращий варіант — більш описово), або (B) виправити field names у CREATING_PLUGINS реалізаціях щоб збігались з поточним struct.

---

### 3.2 Interface Compatibility Matrix — IInputPlugin::pollEvent()

| Документ | Метод | Return type | Параметри |
|---|---|---|---|
| `PLUGIN_CONTRACT.md §3.3` | `pollEvent` | `bool` | `InputEvent& event` (out-param) |
| `PLUGIN_INTERFACES_EXTENDED.md §3` | `pollEvent` | `std::optional<InputEvent>` | (none) |

Обидва документи посилаються на `IInputPlugin`. Неможливо реалізувати плагін що задовольняє обоє.

**Рішення:** `std::optional<InputEvent> pollEvent()` є кращим (atomic pop, thread-safe, C++17 idiom). CONTRACT §3.3 потрібно оновити.

---

### 3.3 FreeRTOS Execution Timeline — Повна симуляція

**Конфігурація:** M5Cardputer-ADV, 4 плагіни (LDC1101, BMI270, QMC5883L, SDCard).

```
╔══════════════════════════════════════════════════════════════════╗
║  TIMELINE loop() — 4 sensor plugins (задовільна конфігурація)   ║
╠══════════════════════════════════════════════════════════════════╣
║  t=  0 ms │ pluginSystem->updateAll()                           ║
║  t=  0..5 │ LDC1101Plugin::update() → readRP() [SPI burst]     ║
║  t=  5..9 │ BMI270Plugin::update() [I2C batch read]             ║
║  t=  9..11│ QMC5883LPlugin::update() [empty]                    ║
║  t= 11..12│ SDCardPlugin::update() [flush buffer if needed]     ║
║  t= 12..22│ delay(10) — business logic                          ║
║  t= 22 ms │ наступний виток                                     ║
║            │ → Частота: 45 Hz ✅ (CONTRACT: ≥10 Hz)             ║
╠══════════════════════════════════════════════════════════════════╣
║  TIMELINE loop() — якщо HX711Plugin активний (10 Hz data rate)  ║
╠══════════════════════════════════════════════════════════════════╣
║  t=  0 ms │ pluginSystem->updateAll()                           ║
║  t=  0..5 │ LDC1101Plugin::update() ≤5ms ✅                     ║
║  t=  5..9 │ BMI270Plugin::update() ≤4ms ✅                      ║
║  t=  9..109│ HX711Plugin::update() → get_units(1)   ← 100ms ❌  ║
║            │   БЛОКУЄ: всі інші плагіни чекають 100ms           ║
║            │   CONTRACT §2.1 порушено (≤10ms)                   ║
║  t=109..119│ delay(10)                                          ║
║  t=119 ms │ наступний виток → 8.4 Hz ❌                         ║
╠══════════════════════════════════════════════════════════════════╣
║  TIMELINE — runDiagnostics() викликано з loop()                  ║
╠══════════════════════════════════════════════════════════════════╣
║  checkHardwarePresence() →  1 SPI read     ≈  2 ms              ║
║  checkCommunication()    →  3 SPI reads    ≈  5 ms              ║
║  checkCalibration()      →  arithmetic     ≈  0 ms              ║
║  checkStability()        →  10× delay(5ms) ≈ 50 ms  ← BLOCKS   ║
║  TOTAL runDiagnostics(): ~57 ms → loop зупиняється на 57 ms!   ║
║  → 17 Hz → 5.7 Hz якщо кожен плагін ← CONTRACT VIOLATION ❌    ║
╚══════════════════════════════════════════════════════════════════╝
```

---

### 3.4 Memory Allocation Simulation

**Платформа:** ESP32-S3FN8, heap = 337 KB, PSRAM = 0.

```
КОМПОНЕНТ                              ОЦІНКА     ПРИМІТКА
─────────────────────────────────────────────────────────
FreeRTOS kernel + scheduler                ~8 KB
Main task stack (8 KB default)             ~8 KB
Arduino framework + M5Cardputer.begin()    ~15 KB
Display framebuffer 240×135×RGB565         ~63 KB  ← завжди в heap (без PSRAM)
LittleFS metadata + VFS layer              ~8 KB
Config JSON parse buffer (ArduinoJson)     ~4 KB   ← тимчасово при loadFromConfig
ConfigManager persisted data               ~4 KB
Logger ring buffer                         ~4 KB
PluginSystem + pluginMap                   ~2 KB
BLE NimBLE stack (CONNECTIVITY_ARCH)       ~30 KB  ← якщо активний
─────────────────────────────────────────────────────────
Overhead (без BLE): ~116 KB
Overhead (з BLE):   ~146 KB
─────────────────────────────────────────────────────────
Доступно для плагінів (без BLE): 337 - 116 = 221 KB
Доступно для плагінів (з BLE):   337 - 146 = 191 KB
─────────────────────────────────────────────────────────

При 10 плагінах × 8 KB:
  Без BLE: 221 - 80 = 141 KB залишок ✅
  З BLE:   191 - 80 =  111 KB залишок ✅

При 20 плагінах × 8 KB:
  Без BLE: 221 - 160 = 61 KB залишок ⚠️ (фрагментація — ризик)
  З BLE:   191 - 160 = 31 KB залишок ❌ (HEAP OVERFLOW при фрагментації)
─────────────────────────────────────────────────────────
ВИСНОВОК:
  ARCH "max 10 плагінів" → ✅ Safe margin навіть з BLE
  CONTRACT "до 20 плагінів" → ❌ Небезпечно з BLE активним
  Рекомендація: 10 плагінів як hard limit (ARCH правильно, CONTRACT треба виправити → N-3)
```

---

### 3.5 SPI Bus Contention Model

**Шина VSPI** (G40/G14/G39): LDC1101 + SD card (Logger SDTransport) — обидва захищені `spiMutex`.

```
Учасники VSPI:
  A: LDC1101Plugin::readRP()         — кожні ~5ms, транзакція ~2ms
  B: SDTransport (Logger flush)      — кожні ~1000ms, транзакція ~5ms
  C: logDiagnostics()                — on-demand, транзакція ~10ms

З spiMutex (поточний стан):
  Max wait = max transaction time = 10ms < mutex timeout pdMS_TO_TICKS(50)
  → Deadline miss rate: near 0 ✅
  
Без spiMutex (template user без fix):
  P(A∩B collision) в секунду = (2/5000) × (5/1000) = 2% за 1 секунду
  За 10 хвилин: 1 - (1 - 0.002)^600 > 70% гарантована корупція

ВИСНОВОК: spiMutex критичний. MySPISensorPlugin template (PA2-6, CLOSED) виправлений.
LDC1101 та logDiagnostics — обидва правильно захищені.
```

---

### 3.6 Plugin Lifecycle FSM — Behavioral Simulation

#### LDC1101Plugin (найповніший приклад)

```
[CREATED]
  new LDC1101Plugin() → ctx=nullptr, ready=false, enabled=false

[canInitialize()]
  return true // без hardware перевірки ✅ CONTRACT правило

[initialize(ctx)]
  ctx = context ✅
  csPin = ctx->config->getInt("ldc1101.spi_cs_pin", 5) ✅
  chipId = readRegister(DEVICE_ID) // через spiMutex ✅
  if chipId != 0xD4 → return false → [FAILED]
  writeRegister(CONFIG, 0x01) // через spiMutex ✅
  readBack = readRegister(CONFIG) // verify ✅
  if readBack != 0x01 → return false → [FAILED]
  ready=true, enabled=true → return true → [READY]

[update()] x N:
  read() → readRP() → burst SPI з spiMutex ✅
  diagnostics.totalReads++, lastSuccess = millis() ✅

[shutdown()]
  if !initialized → return ✅ (PA2-3 guard)
  enabled=false, ready=false ✅

[FAILED] → shutdown() викликається CONTRACT:
  ctx вже встановлений → можна логувати
  initialized=false → рання вихід ✅

FSM оцінка: ✅ БЕЗДОГАННА РЕАЛІЗАЦІЯ
```

#### AsyncSensorPlugin template (CREATING_PLUGINS)

```
[CREATED]
  TaskHandle_t readTask; // ⚠️ UNINITIALIZED! (N-10)
  dataMutex = nullptr;   // ← припускаємо nullptr, не гарантовано

[initialize(ctx)]
  dataMutex = xSemaphoreCreateMutex();
  // ⚠️ N-8: результат НЕ перевіряється → dataMutex може бути nullptr

  xTaskCreate(readTaskFunc, ..., &readTask);
  // ⚠️ N-7: результат НЕ перевіряється

  return true; // повертає true навіть якщо обидва вище провалились!

[readTask (FreeRTOS task) якщо запущений]:
  while(true) {
    xSemaphoreTake(ctx->spiMutex, ...) ✅ // але!
    // якщо initialize() повернув true але task не запустився →
    // ця гілка не виконується, latestData = {} завжди
    vTaskDelay(100ms)
  }

[read()]:
  xSemaphoreTake(dataMutex, ...) // якщо dataMutex=nullptr → CRASH (N-8)!

[shutdown()]:
  if (readTask) vTaskDelete(readTask); // якщо readTask=garbage → CRASH (N-10)!
  if (dataMutex) vSemaphoreDelete(dataMutex);

FSM оцінка: ❌ CRITICAL runtime crash scenarios
```

---

## 4. N-1 — SensorMetadata: поля у реалізаціях не збігаються з визначенням struct

**Severity:** 🔴 CRITICAL  
**Статус:** 🔓 OPEN  
**Документи:** `CREATING_PLUGINS.md` §BH1750Plugin, §MyI2CSensorPlugin template + `PLUGIN_INTERFACES_EXTENDED.md` §1  
**Введено:** PA3-4 fix (додавання getMetadata() без перевірки актуальних полів struct)

### Evidence

`PLUGIN_INTERFACES_EXTENDED.md §1` — канонічне визначення struct `SensorMetadata`:
```cpp
struct SensorMetadata {
    const char* typeName;    // "Weight Sensor"
    const char* unit;        // "g"
    float minValue;
    float maxValue;
    float resolution;
    uint16_t sampleRate;     // ← ПРАВИЛЬНА НАЗВА
};
```

`CREATING_PLUGINS.md` BH1750Plugin (PA3-4 fix):
```cpp
SensorMetadata getMetadata() const override {
    return {
        .sensorType   = SensorType::LIGHT,   // ❌ поля немає
        .manufacturer = "ROHM Semiconductor", // ❌ поля немає
        .model        = "BH1750FVI",          // ❌ поля немає
        .resolution   = 1.0f,                 // ✅
        .minValue     = 0.0f,                 // ✅
        .maxValue     = 65535.0f,             // ✅
        .updateRateHz = 10                    // ❌ поле sampleRate, не updateRateHz
    };
}
```

### Два можливих рішення

**Варіант A — Оновити struct дати більше полів (рекомендовано):**
```cpp
struct SensorMetadata {
    SensorType  sensorType;          // Enum type (LDC1101→INDUCTIVE, HX711→WEIGHT...)
    const char* typeName;            // "Weight Sensor" (human-readable)
    const char* manufacturer;        // "Bosch Sensortec"
    const char* model;               // "BMI270"
    const char* unit;                // "g", "mm", "lux"
    float       minValue;
    float       maxValue;
    float       resolution;
    uint16_t    sampleRate;          // залишити sampleRate, або:
    uint16_t    updateRateHz;        // або перейменувати в updateRateHz для ясності
};
```
Тоді всі реалізації (HX711, BH1750, template) потребують оновлення — але struct стає описовішим.

**Варіант B — Виправити реалізації під поточний struct:**
```cpp
// BH1750Plugin:
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
```
Швидший fix, але struct менш описовий.

---

## 5. N-2 — attemptRecovery(): canInitialize() з вже встановленим ctx

**Severity:** 🟢 LOW  
**Статус:** 🔓 OPEN  
**Документ:** `PLUGIN_ARCHITECTURE.md` §PluginSystem `attemptRecovery()`, `PLUGIN_CONTRACT.md` §1.1

### Evidence

```cpp
bool attemptRecovery(IPlugin* plugin) {
    plugin->shutdown();
    vTaskDelay(pdMS_TO_TICKS(100));
    if (!plugin->canInitialize()) return false;  // ← canInitialize() після попереднього initialize()!
    return plugin->initialize(&ctx_);
}
```

CONTRACT §1.1 визначає: `canInitialize()` **ніколи не має доступу до ctx** — він викликається **до** `initialize()`. Але в `attemptRecovery()` — плагін вже зберіг `ctx` з попереднього `initialize()`. CONTRACT не уточнює поведінку на recovery.

**Практичний наслідок:** Якщо розробник плагіна вирішить використати збережений `ctx` в `canInitialize()` (порушуючи CONTRACT), на recovery це дасть несподівано "успішний" `canInitialize()` навіть при відсутності hardware, просто тому що `Wire.beginTransmission()` або інший виклик повертає поведінку з кешованого стану.

### Рішення

Додати у CONTRACT §1.1 та в коментар `canInitialize()`:
```
// INVARIANT: canInitialize() called both at initial load AND during attemptRecovery().
// MUST NOT access ctx (ptr may have stale value from prior initialize()).
// MUST NOT call Wire/SPI operations.
// Only perform: configuration file checks, version compatibility, static capability checks.
```

---

## 6. N-3 — CONTRACT §2.4 vs ARCH §Memory: "20 плагінів" vs "max 10 плагінів"

**Severity:** 🟡 MEDIUM  
**Статус:** 🔓 OPEN  
**Документи:** `PLUGIN_CONTRACT.md §2.4` vs `PLUGIN_ARCHITECTURE.md §Memory Management`

### Evidence

**CONTRACT §2.4** (не оновлено після PA2-13):
```
ESP32-S3FN8 має ~337 KB вільного heap. Система підтримує до 20 плагінів одночасно.
8 KB на плагін = 160 KB загалом, залишається ~177 KB для системи.
```

**ARCH §Memory Management** (виправлено PA2-13):
```
⚠️ Ліміт: максимум 10 плагінів. Display framebuffer ST7789V2 (~63 KB) постійно використовує heap.
20 плагінів × 8 KB = 160 KB > available → HEAP OVERFLOW на практиці.
```

Симуляція §3.4 підтверджує: з BLE активним, 20 плагінів → 31 KB залишок → ризик heap overflow при фрагментації.

### Рішення

Оновити CONTRACT §2.4:
```
ESP32-S3FN8: ~337 KB heap. Рекомендований ліміт: 10 плагінів.
Display framebuffer (~63 KB) + BLE stack (~30 KB, якщо активний) скорочують доступну пам'ять.
Детальний бюджет: PLUGIN_ARCHITECTURE.md §Memory Management.
```

---

## 7. N-4 — IInputPlugin::pollEvent(): різні сигнатури в CONTRACT §3.3 та INTERFACES_EXTENDED §3

**Severity:** 🟠 HIGH  
**Статус:** 🔓 OPEN  
**Документи:** `PLUGIN_CONTRACT.md §3.3` vs `PLUGIN_INTERFACES_EXTENDED.md §3`

### Evidence

**CONTRACT §3.3:**
```cpp
class IInputPlugin : public IPlugin {
    virtual bool pollEvent(InputEvent& event) = 0;  // ← bool + out-parameter
};
```

**PLUGIN_INTERFACES_EXTENDED.md §3 (після PA3-5 fix):**
```cpp
virtual std::optional<InputEvent> pollEvent() = 0;  // ← optional + no out-param
```

Розробник що читає обидва документи отримує дві несумісні вимоги. Будь-яка реалізація виконає одну і порушить іншу.

**Технічний аналіз:**

```cpp
// CONTRACT версія:
class TCA8418KeyboardPlugin : public IInputPlugin {
    bool pollEvent(InputEvent& event) override { ... }  // OK за CONTRACT
    // but VIOLATED per INTERFACES_EXTENDED (wrong signature)
};

// INTERFACES версія:
class TCA8418KeyboardPlugin : public IInputPlugin {
    std::optional<InputEvent> pollEvent() override { ... }  // OK per INTERFACES
    // but VIOLATED per CONTRACT (wrong signature)
};
```

**Яка версія правильна?**

`std::optional<InputEvent> pollEvent()` — краща з обох причин:
1. Atomic pop — не має TOCTOU race condition (CONTRACT §3.3 required atomic)
2. Идіоматичний C++17 — кращий ABI, без out-parameter
3. CONTRACT §3.3 сам вимагав "atomic pop" і `std::optional` є правильним інструментом

### Рішення

Оновити CONTRACT §3.3 на сигнатуру INTERFACES_EXTENDED:
```cpp
// CONTRACT §3.3 — оновлено:
// pollEvent() — атомарний pop: removes AND returns event atomically.
// Thread-safe: implementation MUST use mutex or lock-free queue.
// Returns nullopt if no event available.
virtual std::optional<InputEvent> pollEvent() = 0;
```

---

## 8. N-5 — logDiagnostics(): spiMutex передається параметром, але caller не має доступу

**Severity:** 🟢 LOW  
**Статус:** 🔓 OPEN  
**Документ:** `PLUGIN_DIAGNOSTICS.md §5`  
**Пов'язано:** PA2-15

### Evidence

Після PA3-3 fix `logDiagnostics()` визначена як:
```cpp
void logDiagnostics(SemaphoreHandle_t spiMutex) {
    if (!spiMutex) return;
    xSemaphoreTake(spiMutex, pdMS_TO_TICKS(100));
    File log = SD.open("/diagnostics.log", FILE_APPEND);
    // ...
}
```

Але `spiMutex` зберігається у `PluginSystem::ctx_` (приватне поле). З `main.cpp` немає чистого способу отримати його без:
1. `pluginSystem->getContext().spiMutex` — метод `getContext()` не визначений
2. Зберігати окремо у глобальній змінній — anti-pattern

**Рішення:** Або зробити `logDiagnostics()` методом плагіна (тоді він має `ctx`), або додати `SemaphoreHandle_t PluginSystem::getSpiMutex() const`. Другий варіант простіший.

---

## 9. N-6 — HX711Plugin: get_units(1) на 10 Hz = 100 ms — CONTRACT §2.1 порушено після fix

**Severity:** 🟠 HIGH  
**Статус:** 🔓 OPEN  
**Документ:** `PLUGIN_INTERFACES_EXTENDED.md §1 HX711Plugin`

### Evidence

Після PA3-2 fix (5→1 sample):
```cpp
float weight = scale.get_units(1);
// ⚠️ CONTRACT §2.1: update() ≤ 10 ms. Навіть get_units(1) на 10 Hz = 100 ms — порушення!
// Правильне рішення для production: AsyncSensorPlugin
```

PA3-2 зменшив blockage з 500ms → **максимум 100ms** (при 10 Hz data rate). Але CONTRACT §2.1 вимагає ≤10 ms. Попередження є — але **прикладний код все ще contract-violating**.

Будь-який розробник що скопіює HX711Plugin як шаблон отримає plug-in що порушує контракт.

### Рішення

Замінити приклад на AsyncSensorPlugin pattern (PA3-2 "Правильне рішення"):
```cpp
class HX711Plugin : public AsyncSensorPlugin {
    std::atomic<float> cachedWeight{0.0f};

    void readTask() override {
        while (taskRunning) {
            if (scale.is_ready()) {
                cachedWeight.store(scale.get_units(1));
            }
            vTaskDelay(pdMS_TO_TICKS(50));  // 20 Hz
        }
    }

    SensorData read() override {
        float w = cachedWeight.load();  // non-blocking, atomic
        return {w, 0, 1.0f, millis(), w > 0.1f};
    }
};
```

---

## 10. N-7 — AsyncSensorPlugin: xTaskCreate() не перевіряється

**Severity:** 🟡 MEDIUM  
**Статус:** 🔓 OPEN  
**Документ:** `CREATING_PLUGINS.md §Складні §2 AsyncSensorPlugin`

### Evidence

```cpp
bool initialize(PluginContext* context) override {
    ctx = context;
    dataMutex = xSemaphoreCreateMutex();       // N-8: не перевіряється!
    xTaskCreate(readTaskFunc, "SensorRead",    // N-7: повернутий код НЕ перевіряється!
                4096, this, tskIDLE_PRIORITY + 2, &readTask);
    ctx->log->info(getName(), "Async task started");
    return true;  // ← true навіть якщо task не створено!
}
```

Якщо FreeRTOS не може виділити task stack (heap повна) → `xTaskCreate()` повертає `pdFAIL`. Plugin повертає `true`, ніяких помилок. `read()` завжди повертатиме stale/zero data. `diagnostics.lastSuccess` = 0 завжди → `getHealthStatus()` повертатиме `TIMEOUT` після 5 секунд, але plugin вважається ініціалізованим.

### Рішення

```cpp
bool initialize(PluginContext* context) override {
    ctx = context;

    dataMutex = xSemaphoreCreateMutex();
    if (!dataMutex) {                            // N-8 fix
        ctx->log->error(getName(), "Failed to create data mutex");
        return false;
    }

    taskRunning = true;
    BaseType_t res = xTaskCreate(readTaskFunc, "SensorRead",
                                 4096, this, tskIDLE_PRIORITY + 2, &readTask);
    if (res != pdPASS) {                         // N-7 fix
        taskRunning = false;
        ctx->log->error(getName(), "Failed to create FreeRTOS task (heap full?)");
        vSemaphoreDelete(dataMutex);  // cleanup
        dataMutex = nullptr;
        return false;
    }

    ctx->log->info(getName(), "Async task started");
    return true;
}
```

---

## 11. N-8 — AsyncSensorPlugin: xSemaphoreCreateMutex() не перевіряється — null mutex crash

**Severity:** 🟠 HIGH  
**Статус:** 🔓 OPEN  
**Документ:** `CREATING_PLUGINS.md §Складні §2 AsyncSensorPlugin`

### Evidence

```cpp
dataMutex = xSemaphoreCreateMutex();  // може повернути NULL при нестачі heap!

// Пізніше в read():
if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {  // dataMutex=NULL → CRASH
```

На ESP32, `xSemaphoreTake(NULL, ...)` викликає `configASSERT(pxQueue)` → `assert fail` → reboot або undefined behavior залежно від config.

На системах з ~221 KB доступного heap при нормальних умовах це рідко. Але після ~20 хвилин роботи з heap фрагментацією → можливо. При активній BLE → вірогідніше.

### Рішення

Виправлення в §10 (N-7 рішення) включає перевірку `dataMutex`. Також:
```cpp
class AsyncSensorPlugin : public ISensorPlugin {
private:
    SemaphoreHandle_t dataMutex = nullptr;  // ← явна ініціалізація
    TaskHandle_t readTask = nullptr;        // ← явна ініціалізація (N-10 fix)
```

---

## 12. N-9 — QMC5883LPlugin::readRegister16(): ctx->wire без wireMutex

**Severity:** 🟡 MEDIUM  
**Статус:** 🔓 OPEN  
**Документ:** `PLUGIN_ARCHITECTURE.md §4 QMC5883LPlugin`  
**Пов'язано:** PA2-17 (BH1750 — та сама проблема)

### Evidence

```cpp
int16_t readRegister16(uint8_t reg) {
    ctx->wire->beginTransmission(I2C_ADDR);  // ← без wireMutex!
    ctx->wire->write(reg);
    ctx->wire->endTransmission();
    ctx->wire->requestFrom(I2C_ADDR, (uint8_t)2);  // ← без wireMutex!
    // ...
}
```

CONTRACT §2.2: `read()` може викликатись з будь-якого task. Якщо будь-який async плагін або фоновий task використовує I2C шину одночасно — race condition.

**Відмінність від PA2-17 (BH1750):** QMC5883L у `PLUGIN_ARCHITECTURE.md` — це flagship example, а не в CREATING_PLUGINS. Це означає що є два primary examples без wireMutex.

### Рішення

Аналогічно до PA2-17 рішення — або додати wireMutex:
```cpp
int16_t readRegister16(uint8_t reg) {
    if (xSemaphoreTake(ctx->wireMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return 0;  // timeout
    }
    ctx->wire->beginTransmission(I2C_ADDR);
    ctx->wire->write(reg);
    ctx->wire->endTransmission();
    ctx->wire->requestFrom(I2C_ADDR, (uint8_t)2);
    int16_t low  = ctx->wire->read();
    int16_t high = ctx->wire->read();
    xSemaphoreGive(ctx->wireMutex);
    return (high << 8) | low;
}
```

Або додати comment: "Strategy A — no async task, no mutex needed. Add mutex if extending to async."

---

## 13. N-10 — AsyncSensorPlugin: TaskHandle_t readTask не ініціалізований як nullptr

**Severity:** 🟡 MEDIUM  
**Статус:** 🔓 OPEN  
**Документ:** `CREATING_PLUGINS.md §Складні §2 AsyncSensorPlugin`

### Evidence

```cpp
class AsyncSensorPlugin : public ISensorPlugin {
private:
    TaskHandle_t readTask;  // ← оголошено без = nullptr!
```

C++ не ініціалізує non-static class members автоматично. `readTask` має undefined value при конструкції.

**Сценарій краш (якщо xTaskCreate fails):**
```
xTaskCreate(..., &readTask);   // fails → повертає pdFAIL, readTask = UNCHANGED (garbage!)
// ...
void shutdown() override {
    if (readTask) {            // garbage != nullptr → true
        vTaskDelete(readTask); // vTaskDelete(garbage pointer) → CRASH
    }
}
```

**Рішення:**
```cpp
class AsyncSensorPlugin : public ISensorPlugin {
private:
    SemaphoreHandle_t dataMutex = nullptr;  // ← explicit nullptr
    TaskHandle_t      readTask  = nullptr;  // ← explicit nullptr
    bool taskRunning = false;               // ← atomic shutdown flag
```

---

## 14. Підтверджені OPEN знахідки (перенесено з v2.0.0 аудиту)

### PA2-15 — ctx->log в loop() scope

`PLUGIN_DIAGNOSTICS.md §4` loop():
```cpp
if (pluginSystem->attemptRecovery(plugin)) {
    ctx->log->info(plugin->getName(), "Recovery successful");  // ctx не визначено в main.cpp!
}
```
**Fix:** `logger->info("System", "Plugin %s recovered", plugin->getName());` — `logger` є глобальним в main.cpp.

---

### PA2-16 — calibrationBaseline = 0 при старті

`calibrationBaseline = 0` → `checkCalibration()` → `if (calibrationBaseline < 100)` → `return false` → завжди `CALIBRATION_NEEDED` при першому старті до `calibrate()`.  
**Fix:** Додати `bool calibrationPerformed = false` guard.

---

### PA2-17 — BH1750 read() без wireMutex (підвищено до HIGH)

`BH1750Plugin::read()` → `ctx->wire->requestFrom()` без `wireMutex`. CONTRACT §2.2: read() може викликатись з будь-якого task.

---

### PA2-18 — reloadPlugin() та installPluginOTA() не позначені NOT IMPLEMENTED

ARCH §Креативні фічі §2 §3 — методи використовуються як real API без позначки.

---

### PA3-6 — runDiagnostics() блокує 50+ ms через checkStability()

`runDiagnostics()` → `checkStability()` → 10×delay(5ms) = 50 ms. Попередження є тільки на `checkStability()`, але **публічний API** — `runDiagnostics()`. Якщо викликати з loop() → catastrophic deadline miss.

**Рекомендоване рішення:**
```cpp
// ⚠️ BLOCKING ~57 ms (checkStability: 10×5ms). НЕ викликати з loop() або update()!
// Використовуй окремий FreeRTOS task або запуск по кнопці.
DiagnosticResult runDiagnostics();
```

---

### PA3-8 — CONTRACT §1.1: ≥10 Hz нездійсненна гарантія при 10+ active плагінах

10 плагінів × 10ms max update() = 100ms + delay(10) = 110ms → 9.1 Hz < 10 Hz.  
**Fix:** Уточнити CONTRACT §1.1:
> "≥10 Hz гарантується за умови: кількість активних плагінів × max(update()) + overhead ≤ 90 ms."

---

### PA3-10 — lastCalibrationTime = 0 → 30-денний поріг ніколи не спрацює

`millis() - 0 = millis()`. Після ребуту millis() ≈ 0..декілька хвилин → age < 30 days → check passes. 30-денний поріг завжди "свіжий". Мусить персистуватись через NVS.

---

### PA3-11 — showDiagnosticsScreen() з глобальними залежностями

Service Locator anti-pattern. Тестування неможливе без ініціалізованих globals.  
**Fix:** `void showDiagnosticsScreen(const DiagnosticResult& result, IDisplayPlugin* display, PluginSystem* system);`

---

### C-PA16 — FusionSensorPlugin: lifecycle залежностей не документований

`setDependencies()` — невідомо хто викликає, коли, як hot-reload безпечно.

---

### PA3-16 — getGraphicsLibrary() повертає void*

```cpp
virtual void* getGraphicsLibrary() { return nullptr; }
// → reinterpret_cast<M5GFX*> без compile-time type check
```

---

### PA3-17 — getTypeId() не реалізований у прикладах

Задокументований, `-frtti` overhead ~1-2 KB/клас, але жоден приклад не перевизначає.

---

## 15. Підтверджені CLOSED знахідки (верифіковані незалежно)

Наступні знахідки перевірені по поточному тексту документів і підтверджені закритими:

| ID | Верифіковано | Примітка |
|---|---|---|
| PA2-1 | ✅ CLOSED | `IPlugin` default `getHealthStatus()`/`getLastError()` присутні в ARCH + DIAGNOSTICS |
| PA2-2 | ✅ CLOSED | `PluginSystem::getAllPlugins()` визначено |
| PA2-3 | ✅ CLOSED | DIAGNOSTICS §4 використовує `dynamic_cast<IDiagnosticPlugin*>` |
| PA2-4 | ✅ CLOSED | `PluginSystem::attemptRecovery()` визначено |
| PA2-5 | ✅ CLOSED | `PluginContext.storage` присутній в обох ARCH і CONTRACT |
| PA2-6 | ✅ CLOSED | `MySPISensorPlugin` template використовує `spiMutex` |
| PA2-7 | ✅ CLOSED | `IIMUPlugin::calibrate()` повертає `bool` |
| PA2-8 | ✅ PARTIAL | `shutdown()` видаляє task — але N-7, N-8, N-10 нові проблеми в тому ж template |
| PA2-9 | ✅ CLOSED | `IPlugin` — одне канонічне визначення в ARCH (дублікат прибрано) |
| PA2-10 | ✅ CLOSED | `ISensorPlugin::getMetadata()` в ARCH посилається на INTERFACES_EXTENDED — але PA3-4 fix вніс N-1 |
| PA2-11 | ✅ CLOSED | `readWithTimeout()` використовує `ctx->log->error` |
| PA2-12 CLOSED? | ❌ OPEN | mock `.config = nullptr` залишається в CREATING_PLUGINS §Debugging |
| PA2-13 | ✅ CLOSED | ARCH оновлено: max 10 плагінів — але CONTRACT §2.4 не оновлено (→ N-3) |
| PA2-14 | ✅ CLOSED | inline "config" поле прибрано з config.json прикладу |
| C-PA12 | ✅ CLOSED | Factory з явним коментарем-попередженням про обмеження |
| PA3-1 | ✅ CLOSED | `readRP()` — burst read, CS LOW для обох байт |
| PA3-2 | ✅ PARTIAL | `get_units(1)` + warning — але все ще CONTRACT violation (→ N-6) |
| PA3-3 | ✅ CLOSED | `logDiagnostics(spiMutex)` — SD.open захищено mutex |
| PA3-4 | ✅ PARTIAL | `getMetadata()` додано — але поля struct неправильні (→ N-1 CRITICAL) |
| PA3-5 | ✅ PARTIAL | INTERFACES_EXTENDED оновлено → `pollEvent()` — але CONTRACT не оновлено (→ N-4) |
| PA3-7 | ✅ CLOSED | HX711 GPIO з `ctx->config->getInt()` |
| PA3-9 | ✅ CLOSED | BH1750 `getType()` → `SensorType::LIGHT` |
| PA3-12 | ✅ CLOSED | Другий `class IPlugin` прибрано, redirect comment додано |
| PA3-13 | ✅ CLOSED | template `return {0, 0, 0.0f, millis(), false}` (valid=false) |
| PA3-14 | ✅ CLOSED | ARCH version 2.0.0, date 2026-03-13 |
| PA3-15 | ✅ CLOSED | HARDWARE_PROFILES.md посилання позначено `[TODO]` |

---

## 16. Що архітектура зробила ПРАВИЛЬНО

Незалежний аудит підтверджує такі сильні сторони (стабільні після всіх виправлень):

1. **PluginContext Dependency Injection** — правильний. Всі ресурси (Wire, SPI, mutex, config, log, storage) передаються централізовано. Плагін не знає про глобальний стан.

2. **Canonical Three-JSON Config Model** — чітко визначена модель: `lib/plugin.json` (build-time) → `data/plugins/name.json` (runtime params) → `data/config.json` (active list). Consistent між ARCH, CONTRACT, CREATING_PLUGINS.

3. **SPI Mutex Protocol** — LDC1101 правильно бере/відпускає `spiMutex` в `readRegister()`/`writeRegister()`. `logDiagnostics()` також виправлено (PA3-3). `MySPISensorPlugin` template виправлено (PA2-6).

4. **Plugin Lifecycle FSM** — `canInit → init → update → shutdown` чітко задокументована послідовність. CONTRACT гарантує порядок. `shutdown()` завжди викликається перед деструктором.

5. **LDC1101Plugin як reference implementation** — найповніший приклад: перевірка CHIP_ID, burst SPI read, mutex, діагностика, статистика, graceful failure mode. Це зразок для інших плагінів.

6. **IDiagnosticPlugin mixin** — правильне архітектурне рішення: базовий `IPlugin` не змушує всі плагіни реалізовувати повну діагностику. Opt-in через mixin.

7. **-frtti задокументовано** — `getPluginsByType<T>()` з `dynamic_cast` коректно позначений як потребуючий `-frtti`, альтернатива без RTTI згадана.

8. **Memory контракт** — ARCH оновлено до реалістичного ліміту 10 плагінів після PA2-13. Display framebuffer врахований.

9. **ConfigManager API задокументований** — CONTRACT §1.3б містить повний список методів (getInt, getFloat, getBool, getString). Ключ-формат `"plugin.param"` задокументований.

10. **IIMUPlugin::calibrate() уніфіковано** — тепер повертає `bool` аналогічно ISensorPlugin (PA2-7).

---

## 17. Pre-implementation Checklist v3.0.0

### 🔴 BLOCKER: вирішити до першого рядка виробничого коду

- [ ] **N-1** — Узгодити `SensorMetadata` struct поля (або оновити struct + всі реалізації, або виправити реалізації під існуючий struct). **COMPILE BLOCKER.**
- [ ] **N-4** — Узгодити `IInputPlugin::pollEvent()` сигнатуру між CONTRACT §3.3 та INTERFACES_EXTENDED §3. Рекомендовано: `std::optional<InputEvent> pollEvent()`.

### 🟠 HIGH: виправити до першого комміту з реальним кодом

- [ ] **N-6** — `HX711Plugin::read()` замінити на AsyncSensorPlugin pattern (зчитування у FreeRTOS task)
- [ ] **N-8** — `AsyncSensorPlugin`: перевірити `xSemaphoreCreateMutex()` результат → `return false` при null
- [ ] **N-10** — `AsyncSensorPlugin`: `TaskHandle_t readTask = nullptr` + `SemaphoreHandle_t dataMutex = nullptr`
- [ ] **PA2-17** — `BH1750Plugin::read()`: додати `wireMutex` навколо `ctx->wire->requestFrom()`
- [ ] **PA3-6** — `runDiagnostics()`: додати ⚠️ BLOCKING коментар на публічний метод

### 🟡 MEDIUM: виправити до першого release

- [ ] **N-3** — CONTRACT §2.4: "20 плагінів" → "max 10 плагінів"
- [ ] **N-7** — `AsyncSensorPlugin`: перевірити `xTaskCreate()` результат
- [ ] **N-9** — `QMC5883LPlugin::readRegister16()`: додати `wireMutex` або пояснювальний коментар
- [ ] **PA2-12** — mock `PluginContext` в CREATING_PLUGINS: `config = new ConfigManager()` або warning
- [ ] **PA2-15** — `ctx->log` в loop() scope: замінити на `logger->info()`
- [ ] **PA3-8** — CONTRACT §1.1: уточнити умову ≥10 Hz (залежить від кількості плагінів)
- [ ] **PA3-10** — `lastCalibrationTime`: персистувати через NVS/LittleFS (разом з PA2-16)
- [ ] **PA3-11** — `showDiagnosticsScreen()`: передавати залежності як параметри

### 🟢 LOW/INFO: backlog (не блокують)

- [ ] N-2 — CONTRACT: явна документація `canInitialize()` в контексті recovery
- [ ] N-5 — `PluginSystem::getSpiMutex()` або logDiagnostics() як метод плагіна
- [ ] PA2-16 — `calibrationPerformed` guard у `checkCalibration()`
- [ ] PA2-18 — `// NOT IMPLEMENTED` на `reloadPlugin()`, `installPluginOTA()`
- [ ] C-PA16 — FusionSensorPlugin: документувати порядок setDependencies() lifecycle
- [ ] PA3-16 — `getGraphicsLibrary()` → typed return (template або concrete type)
- [ ] PA3-17 — `getTypeId()` default implementation + документація

---

## 18. Implementation Readiness Scoring

### Детальна оцінка готовності

| # | Категорія | Оцінка | Проблеми | Деталі |
|---|---|---|---|---|
| 1 | **Core IPlugin interface** | 9/10 | — | Lifecycle чіткий, default diagnostics, consistent між docs |
| 2 | **Lifecycle FSM** | 9/10 | N-2 (LOW) | canInit→init→update→shutdown гарантований; recovery minor gap |
| 3 | **SPI/I2C bus management** | 7/10 | PA2-17, N-9 | LDC1101 ✅; BH1750 + QMC5883L відкриті без wireMutex |
| 4 | **Documentation consistency** | 4/10 | N-1 🔴, N-3, N-4 🟠 | Struct fields mismatch + 2 contract contradictions |
| 5 | **Template quality** | 4/10 | N-1 🔴, N-7, N-8, N-10 🟠 | Compile error + 3 crash scenarios в AsyncSensorPlugin |
| 6 | **Thread safety** | 6/10 | PA2-17 🟠, N-9 🟡 | Core protected; 2 primary examples без mutex |
| 7 | **Timing compliance** | 5/10 | N-6 🟠, PA3-6, PA3-8 | HX711 100ms blocking; runDiagnostics() blocking |
| 8 | **Error handling** | 7/10 | N-7, PA2-16 | LDC1101 excellent; template incomplete |
| 9 | **Testability** | 4/10 | PA2-12 🟡 | mock ctx nullptr; no test infra documented |

**Загальна оцінка: 6.5/10**

### Інтерпретація

```
10/10 — Готово до продакшн імплементації
 8/10 — Готово з мінорними виправленнями
 6.5  — НЕ готово: 2 compile blockers + 3 crash scenarios в templates
        Виправити N-1, N-4 (CRITICAL/HIGH) + N-8, N-10 (HIGH) → тоді 8/10
        Виправити всі HIGH → 9/10
        Виправити всі MEDIUM → 9.5/10 (решта LOW/INFO — не блокують)
```

### Шлях до 9/10 (мінімум для початку P-2)

1. N-1: Fix SensorMetadata fields (1-2 год, 2 файли)
2. N-4: Update CONTRACT §3.3 signature (30 хв, 1 файл)
3. N-8 + N-10: AsyncSensorPlugin null-checks + nullptr init (1 год, 1 файл)
4. PA2-17: BH1750 wireMutex (30 хв, 1 файл)

**Загальний час виправлень до P-2 старту: ~4 години**

---

## 19. Backlog та залежності між знахідками

### Граф залежностей

```
N-1 (SensorMetadata fields) ─────────────────────────────┐
    Блокує: будь-який ISensorPlugin плагін                │ COMPILE BLOCKER
    Пов'язано: PA3-4 CLOSED (пов'язаний fix)              │ → Fix спочатку
                                                          │
N-4 (pollEvent signature) ────────────────────────────────┘
    Блокує: будь-який IInputPlugin плагін
    Пов'язано: PA3-5 CLOSED (неповний fix — CONTRACT не оновлено)

N-6 (HX711 blocking) ─────────────────────────────────────┐
    Пов'язано: PA3-2 CLOSED (мінімальний fix)             │ AsyncSensorPlugin issues
    Рішення вимагає: AsyncSensorPlugin pattern            │
N-7 (xTaskCreate unchecked) ─────────────────────────────┤
    Пов'язано: N-8, N-10 (всі в AsyncSensorPlugin)       │
N-8 (null mutex crash) ────────────────────────────────── ┤
    Блокує: будь-яке використання AsyncSensorPlugin      │
N-10 (TaskHandle uninitialized) ──────────────────────────┘

PA2-17 (BH1750 wireMutex)
    Пов'язано: N-9 (QMC5883L — той самий pattern)
    → Fix разом в одному commit

PA2-15 (ctx->log scope) ──────────────────────────────────
    Пов'язано: N-5 (logDiagnostics spiMutex caller)
    → Fix разом

PA2-16 + PA3-10 (calibration time) ──────────────────────
    Обидва в checkCalibration(), різні змінні
    → Fix разом в одному commit з NVS persistence

PA3-8 (CONTRACT timing) ─────────────────────────────────
    Пов'язано: N-6 (HX711), N-3 (plugin count)
    → Fix разом при CONTRACT update
```

### Рекомендований порядок виправлень

| Хвиля | ID | Час | Обґрунтування |
|---|---|---|---|
| **Хвиля 1 (P-2 blocker)** | N-1, N-4 | 2.5 год | Compile blockers — нічого не запустить |
| **Хвиля 2 (перед першим плагіном)** | N-8, N-10, N-7, PA2-17, PA3-6 | 3 год | Crash prevention + critical warning |
| **Хвиля 3 (перед першим release)** | N-3, N-6, N-9, PA2-12, PA2-15, PA3-8 | 3 год | Documentation consistency + contract accuracy |
| **Хвиля 4 (P-3 backlog)** | PA2-16, PA3-10, PA3-11, C-PA16, N-2, N-5 | 2 год | Correctness improvements |
| **Хвиля 5 (cleanup)** | PA2-18, PA3-16, PA3-17 | 1 год | Polish |

---

## 20. Cross-reference: попередні аудити → v3.0.0

### Статус знахідок з v1.0.0

Всі PA-1..PA-30 підтверджені CLOSED (перевірено перехресно через v2.0.0 аудит, який сам перевіряв v1.0.0 regресії). Виправлені регресії від неповного поширення (PA-25→PA2-5 CLOSED, PA-12→C-PA12 CLOSED).

### Статус знахідок з v2.0.0

| ID | v2.0.0 статус | v3.0.0 статус | Примітка |
|---|---|---|---|
| PA2-1..PA2-11 | CLOSED | ✅ CLOSED | Підтверджено незалежно |
| PA2-12 | OPEN | 🔓 OPEN | Підтверджено — mock ctx nullptr залишається |
| PA2-13 | CLOSED | ✅ CLOSED | ARCH оновлено. CONTRACT не — N-3 |
| PA2-14 | CLOSED | ✅ CLOSED | Підтверджено |
| PA2-15 | OPEN | 🔓 OPEN | Підтверджено — ctx в loop() scope |
| PA2-16 | OPEN | 🔓 OPEN | Підтверджено — calibrationBaseline=0 |
| PA2-17 | OPEN (HIGH) | ✅ CLOSED 2026-03-13 | wireMutex додано в BH1750Plugin::read() |
| PA2-18 | OPEN | 🔓 OPEN | Підтверджено — NOT IMPLEMENTED не позначено |
| C-PA12 | CLOSED | ✅ CLOSED | Factory warning коментар присутній |
| C-PA16 | OPEN | 🔓 OPEN | Підтверджено |

### Статус знахідок з зовнішнього аудиту v4 (PA3-xx)

| ID | v3.0.0 статус (з v2) | v3.0.0 current | Примітка |
|---|---|---|---|
| PA3-1 | CLOSED | ✅ CLOSED | burst read підтверджено |
| PA3-2 | CLOSED (partial) | 🔓 OPEN → N-6 | minimum fix, CONTRACT violation залишається |
| PA3-3 | CLOSED | ✅ CLOSED | spiMutex в logDiagnostics підтверджено |
| PA3-4 | CLOSED | ✅ CLOSED → N-1 CLOSED | fix вніс N-1; N-1 виправлено 2026-03-13 |
| PA3-5 | CLOSED | ✅ CLOSED → N-4 CLOSED | INTERFACES_EXTENDED оновлено; CONTRACT §3.3 виправлено 2026-03-13 |
| PA3-6 | OPEN | ✅ CLOSED 2026-03-13 | ⚠️ BLOCKING попередження додано на IDiagnosticPlugin |
| PA3-7 | CLOSED | ✅ CLOSED | pins з ConfigManager |
| PA3-8 | OPEN | 🔓 OPEN | CONTRACT ≥10Hz все ще непідготовлено |
| PA3-9 | CLOSED | ✅ CLOSED | getType()→LIGHT |
| PA3-10 | OPEN | 🔓 OPEN | lastCalibrationTime не persistується |
| PA3-11 | OPEN | 🔓 OPEN | globals в showDiagnosticsScreen |
| PA3-12 | CLOSED | ✅ CLOSED | дублікат IPlugin прибрано |
| PA3-13 | CLOSED | ✅ CLOSED | valid=false |
| PA3-14 | CLOSED | ✅ CLOSED | version 2.0.0 |
| PA3-15 | CLOSED | ✅ CLOSED | [TODO] marker |
| PA3-16 | OPEN | 🔓 OPEN | void* return |
| PA3-17 | OPEN | 🔓 OPEN | getTypeId() |

### Нові знахідки v3.0.0 (не були в жодному попередньому аудиті)

N-1..N-10 — виявлені ВПЕРШЕ незалежним аудитом 2026-03-13.  
Особливо важливо: N-1 показує що **виправлення PA3-4 саме по собі введло нову CRITICAL знахідку** — це підкреслює необхідність незалежної верифікації кожного fix'у.

---

## Висновок

Архітектура плагінів CoinTrace має **сильну концептуальну основу** і більшість критичних bugs закрита. Проте **два compile blockers** (N-1, N-4) та кілька crash-сценаріїв в шаблонному коді (N-8, N-10) роблять P-2 імплементацію передчасною.

**Необхідний мінімум перед стартом P-2:** виправити N-1 + N-4 (compile blockers) та N-8 + N-10 (crash prevention) = **~4 години роботи** → оцінка 8+/10 → можна починати.

---

*Аудит v3.0.0 проведений: 2026-03-13*  
*Методологія: повний незалежний аналіз без перенесення висновків з попередніх аудитів*  
*Попередній аудит: `docs/architecture/obsolete/PLUGIN_AUDIT_v2.0.0.md`*  
*Наступна дія: Хвиля 1 виправлень (N-1, N-4) → Хвиля 2 (N-8, N-10, PA2-17) → старт P-2*
