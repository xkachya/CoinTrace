# Plugin Architecture — Незалежний Архітектурний Аудит v2.0.0 / v3.0.0

**Документ:** PLUGIN_AUDIT_v2.0.0.md (доповнено v3.0.0 знахідками 2026-03-13)  
**Аудитовані документи (поточний стан після всіх v1.0.0 + v2.0.0 виправлень):**
- `PLUGIN_ARCHITECTURE.md` v1.1.0+ (PA-1..PA-13, PA-19, PA-21..PA-30 закриті)
- `PLUGIN_CONTRACT.md` v1.1.0+ (PA-5, PA-6, PA-10, PA-15 закриті)
- `PLUGIN_INTERFACES_EXTENDED.md` v1.2.0+ (PA-14, PA-27 закриті)
- `PLUGIN_DIAGNOSTICS.md` v1.1.0+ (PA-3, PA-4, PA-8, PA-9, PA-11, PA-17, PA-18, PA-21, PA-22, PA-28, PA-30 закриті)
- `CREATING_PLUGINS.md` v1.1.0+ (PA-1, PA-5, PA-26 закриті)

**Попередній аудит:** PLUGIN_AUDIT_v1.0.0.md → `docs/architecture/obsolete/PLUGIN_AUDIT_v1.0.0.md`  
**Відкриті знахідки з v1.0.0 перенесені:** PA-12 (MEDIUM), PA-16 (LOW) → нові ID: C-PA12, C-PA16  
**PA-20 з v1.0.0** → повністю замінена ширшою PA2-3 (compile error scope)

**Дата v2.0.0 аудиту:** 2026-03-13  
**Дата v3.0.0 доповнень:** 2026-03-13 (зовнішній аудит `docs/external/PLUGIN_ARCHITECTURE_AUDIT_v4_2026-03-13.md` верифікований та інтегрований)  
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

**── v3.0.0 Зовнішній аудит (2026-03-13) ──**

28. [Нові знахідки v3.0.0 — Score-карта](#28-нові-знахідки-v300--score-карта)
29. [PA3-1 — DIAGNOSTICS readRP(): дві SPI-транзакції, CS не утримується LOW](#29-pa3-1--diagnostics-readrp-дві-spi-транзакції-cs-не-утримується-low)
30. [PA3-2 — INTERFACES_EXTENDED HX711Plugin: get_units(5) блокує loop() на 62–500 ms](#30-pa3-2--interfaces_extended-hx711plugin-get_units5-блокує-loop-на-62500-ms)
31. [PA3-3 — DIAGNOSTICS logDiagnostics(): SD.open() без spiMutex](#31-pa3-3--diagnostics-logdiagnostics-sdopen-без-spimutex)
32. [PA3-4 — BH1750Plugin + MyI2CSensorPlugin: getMetadata() pure virtual не реалізований](#32-pa3-4--bh1750plugin--myi2csensorplugin-getmetadata-pure-virtual-не-реалізований)
33. [PA3-5 — IInputPlugin: API конфлікт hasEvent()/getEvent() vs CONTRACT pollEvent()](#33-pa3-5--iinputplugin-api-конфлікт-haseventgetevent-vs-contract-pollevent)
34. [PA3-6 — checkStability(): блокуючий виклик через runDiagnostics() з loop()](#34-pa3-6--checkstability-блокуючий-виклик-через-rundiagnostics-з-loop)
35. [PA3-7 — HX711Plugin: GPIO pins hardcoded, не використовує ConfigManager](#35-pa3-7--hx711plugin-gpio-pins-hardcoded-не-використовує-configmanager)
36. [PA3-8 — CONTRACT §1.1: ≥10 Hz гарантія нездійсненна при 10+ плагінах](#36-pa3-8--contract-11-10-hz-гарантія-нездійсненна-при-10-плагінах)
37. [PA3-9 — BH1750Plugin::getType() повертає CUSTOM замість LIGHT](#37-pa3-9--bh1750plugingettype-повертає-custom-замість-light)
38. [PA3-10 — checkCalibration(): lastCalibrationTime = 0 після ребуту](#38-pa3-10--checkcalibration-lastcalibrationtime--0-після-ребуту)
39. [PA3-11 — showDiagnosticsScreen(): displayPlugin/pluginSystem як глобальні змінні](#39-pa3-11--showdiagnosticsscreen-displaypluginpluginsystem-як-глобальні-змінні)
40. [PA3-12 — PLUGIN_ARCHITECTURE.md: IPlugin визначений двічі в одному файлі](#40-pa3-12--plugin_architecturemd-iplugin-визначений-двічі-в-одному-файлі)
41. [PA3-13 — MyI2CSensorPlugin template: valid=true в placeholder коді](#41-pa3-13--myi2csensorplugin-template-validtrue-в-placeholder-коді)
42. [PA3-14 — PLUGIN_ARCHITECTURE.md: версія та дата не оновлені після P-2](#42-pa3-14--plugin_architecturemd-версія-та-дата-не-оновлені-після-p-2)
43. [PA3-15 — HARDWARE_PROFILES.md: файл не існує, посилання без [TODO]](#43-pa3-15--hardware_profilesmd-файл-не-існує-посилання-без-todo)
44. [PA3-16 — getGraphicsLibrary(): повертає void* — type-unsafe](#44-pa3-16--getgraphicslibrary-повертає-void--type-unsafe)
45. [PA3-17 — getTypeId(): зазначений з -frtti але не реалізований у прикладах](#45-pa3-17--gettypeid-зазначений-з--frtti-але-не-реалізований-у-прикладах)
46. [Pre-implementation Checklist v3.0.0 (ДОПОВНЕННЯ)](#46-pre-implementation-checklist-v300-доповнення)
47. [Backlog та пріоритети v3.0.0](#47-backlog-та-пріоритети-v300)
48. [Cross-reference: v2.0.0 → v3.0.0](#48-cross-reference-v200--v300)

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
| **PA2-17** | CREATING_PLUGINS §BH1750 `read()` | `ctx->wire->requestFrom()` без `wireMutex` у `read()` — race condition якщо read() викликається з Core 1 (CONTRACT §2.2) | � HIGH *(підвищено з LOW — v3.0.0)* | 🔓 OPEN |
| **PA2-18** | ARCH §Креативні фічі | `reloadPlugin()`, `installPluginOTA()` використовуються як real API але не визначені в `PluginSystem`; OTA registry URL формат суперечить CONNECTIVITY_ARCH (BLE/WS) | 🟢 LOW | 🔓 OPEN |

### Перенесені відкриті знахідки з v1.0.0

| ID | Оригінал | Знахідка | Severity | Статус |
|---|---|---|---|---|
| **C-PA12** | PA-12 v1.0.0 | Factory hardcoding (`if name == "LDC1101"`) суперечить extensibility promise | 🟡 MEDIUM | ✅ CLOSED |
| **C-PA16** | PA-16 v1.0.0 | Plugin dependency injection — механізм не визначено | 🟢 LOW | 🔓 OPEN |

> **PA-20 v1.0.0** (`getStatistics() const` side-effects) → замінена ширшою PA2-3 (methods on wrong type). Окремо не відстежується.

### Підсумок v2.0.0

**Разом нових:** 3 CRITICAL · 5 HIGH *(PA2-17 підвищено LOW→HIGH у v3.0.0)* · 6 MEDIUM · 2 LOW = **16 нових знахідок**  
**Перенесено з v1.0.0:** C-PA12 (MEDIUM) · C-PA16 (LOW) = **2 carried**  
**Всього:** 18 знахідок | **✅ CLOSED: 14** | **🔓 OPEN: 6** (PA2-12, PA2-15, PA2-16, PA2-17, PA2-18, C-PA16)  
*(Примітка: після v3.0.0 зовнішнього аудиту до цього файлу додано 17 нових знахідок PA3-1..PA3-17 — see §28)*

### Нові знахідки (v3.0.0 — зовнішній аудит v4)

| ID | Документ | Знахідка | Severity | Статус |
|---|---|---|---|---|
| **PA3-1** | DIAGNOSTICS `readRP()` | Дві окремі SPI-транзакції при читанні MSB+LSB — CS не утримується LOW між ними → corrupt raw data | 🔴 CRITICAL | ✅ CLOSED |
| **PA3-2** | INTERFACES_EXTENDED `HX711Plugin` | `scale.get_units(5)` блокує loop() на 500 ms (10 Hz) або 62 ms (80 Hz) — CONTRACT §2.1 вимагає ≤10 ms | 🔴 CRITICAL | ✅ CLOSED *(get_units(1) + warning; prod: AsyncSensorPlugin)* |
| **PA3-3** | DIAGNOSTICS `logDiagnostics()` | `SD.open("/diagnostics.log")` без `xSemaphoreTake(ctx->spiMutex)` — SPI bus corruption при паралельному доступі | 🔴 CRITICAL | ✅ CLOSED |
| **PA3-4** | CREATING_PLUGINS `BH1750Plugin` + `MyI2CSensorPlugin` | `getMetadata() const = 0` pure virtual з INTERFACES_EXTENDED не реалізований ані у конкретному прикладі ані у template — compile error | 🟠 HIGH | ✅ CLOSED |
| **PA3-5** | INTERFACES_EXTENDED `IInputPlugin` | `hasEvent()`/`getEvent()`/`clearEvents()` суперечать CONTRACT §3.3 `pollEvent()` (atomic pop) — два несумісних API | 🟠 HIGH | ✅ CLOSED |
| **PA3-6** | DIAGNOSTICS `checkStability()` | `checkStability()` блокує 500+ ms (5 вимірів × 100 ms); `runDiagnostics()` → `runSelfTest()` → `checkStability()` — якщо викликати з loop() → deadline miss | 🟠 HIGH | 🔓 OPEN |
| **PA3-7** | INTERFACES_EXTENDED `HX711Plugin` | `const uint8_t DOUT_PIN = 5; SCK_PIN = 6` hardcoded — не використовує `ctx->config->getInt()`, copyable example насаджує погану практику | 🟠 HIGH | ✅ CLOSED |
| **PA3-8** | ARCH §3.1 + CONTRACT §1.1 | CONTRACT гарантує ≥10 Hz але 10 плагінів × 10 ms update() = 110 ms loop → 9.1 Hz — гарантія математично нездійсненна без уточнення | 🟡 MEDIUM | 🔓 OPEN |
| **PA3-9** | CREATING_PLUGINS `BH1750Plugin::getType()` | Повертає `SensorType::CUSTOM` — повинно бути `SensorType::LIGHT` (BH1750 = optical ambient light sensor) | 🟡 MEDIUM | ✅ CLOSED |
| **PA3-10** | DIAGNOSTICS `checkCalibration()` | `lastCalibrationTime = 0` після ребуту → `millis() - 0 < 30-day threshold` завжди true → 30-денна перевірка ніколи не спрацює (на відміну від PA2-16 який про calibrationBaseline) | 🟡 MEDIUM | 🔓 OPEN |
| **PA3-11** | DIAGNOSTICS `showDiagnosticsScreen()` | Використовує глобальні `displayPlugin`, `pluginSystem` — Service Locator anti-pattern; функція не приймає ці залежності як параметри | 🟡 MEDIUM | 🔓 OPEN |
| **PA3-12** | ARCH Крок1 + §Оновлений IPlugin | `class IPlugin` визначений двічі в PLUGIN_ARCHITECTURE.md (рядки 138 і 272) — залишок після PA2-9 fix (який закрив cross-doc проблему, але не внутрішню дублікацію в одному файлі) | 🟡 MEDIUM | ✅ CLOSED |
| **PA3-13** | CREATING_PLUGINS `MyI2CSensorPlugin` template | `return {0, 0, 1.0f, millis(), true}` — `valid=true` у placeholder failure path → плагін repортує false success при реальному hardware failure | 🟢 LOW | ✅ CLOSED |
| **PA3-13** | CREATING_PLUGINS `MyI2CSensorPlugin` template | `return {0, 0, 1.0f, millis(), true}` — `valid=true` у placeholder failure path → плагін reportує false success при реальному hardware failure | 🟢 LOW | ✅ CLOSED |
| **PA3-15** | ARCH §Апаратні профілі | `HARDWARE_PROFILES.md` посилається як існуючий ресурс — файл не існує, не позначено `[TODO]` | 🟢 LOW | ✅ CLOSED |
| **PA3-16** | DIAGNOSTICS `showDiagnosticsScreen()` | `getGraphicsLibrary()` повертає `void*` — type-unsafe, кожен споживач мусить `reinterpret_cast` без compile-time перевірки | 🔵 INFO | 🔓 OPEN |
| **PA3-17** | INTERFACES_EXTENDED `IPlugin` base | `getTypeId()` задокументований з коментарем про `-frtti`, але жоден конкретний плагін не реалізує його | 🔵 INFO | 🔓 OPEN |

### D-1 (external) → FALSE POSITIVE — не додано як знахідку

> Зовнішній аудит B-D-1 позначив `valid=true` у I2C template як проблему посилаючись тільки на рядок 263 (`return {0, 0, 1.0f, millis(), true}`). Це **дійсно** проблемне місце (→ PA3-13). Але аудит помилково об'єднав його з рядком 498, де `valid=true` є **коректним** — це фактичний успішний результат читання після `Wire.requestFrom()`. Рядок 498 — НЕ помилка.

### Підсумок v3.0.0

**Нові знахідки:** 3 CRITICAL · 4 HIGH · 5 MEDIUM · 3 LOW · 2 INFO = **17 нових знахідок**  
**Всього по документу (v2.0.0 + v3.0.0):** 35 знахідок | **✅ CLOSED: 14** | **🔓 OPEN: 21**

---

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
- [ ] PA2-17 — BH1750 wireMutex *(підвищено до HIGH у v3.0.0)*
- [ ] PA2-18 — NOT IMPLEMENTED comments на OTA/reload
- [ ] C-PA16 — dependency injection lifecycle doc

### v3.0.0 Нові блокуючі (CRITICAL — до написання першого реального плагіна):

- [ ] **PA3-1** — `readRP()` burst transaction: hold CS LOW, об'єднати MSB+LSB в одну транзакцію
- [ ] **PA3-2** — `HX711Plugin` AsyncSensorPlugin pattern: `get_units(1)` або FreeRTOS task
- [ ] **PA3-3** — `logDiagnostics()`: обгорнути `SD.open()` у `spiMutex`

### v3.0.0 HIGH (виправити до першого release):

- [ ] **PA3-4** — Додати `getMetadata()` до `BH1750Plugin` та `MyI2CSensorPlugin` template
- [ ] **PA3-5** — Уніфікувати `IInputPlugin` → `pollEvent()` (CONTRACT wins)
- [ ] **PA2-17** — Додати `wireMutex` навколо `Wire.requestFrom()` у BH1750 `read()`
- [ ] **PA3-6** — `runDiagnostics()` → background FreeRTOS task, не з loop()
- [ ] **PA3-7** — GPIO pins з `ctx->config->getInt("hx711.dout", 5)`

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

*Аудит v2.0.0 проведений: 2026-03-13*  
*Попередній аудит: `docs/architecture/obsolete/PLUGIN_AUDIT_v1.0.0.md`*  
*v3.0.0 знахідки інтегровані нижче (§28–§48)*

---

## 28. Нові знахідки v3.0.0 — Score-карта

Score-карта нових знахідок розміщена у **§2 (Нові знахідки v3.0.0)**. Повні знахідки нижче в §29–§45.

**Джерело:** `docs/external/PLUGIN_ARCHITECTURE_AUDIT_v4_2026-03-13.md`  
**Верифікація:** всі 17 знахідок підтверджені проти актуального тексту архітектурних документів.  
**Виключено:** D-1 (external) — false positive (два різних контексти `valid=true` сплутані — §2 D-1 note).

---

## 29. PA3-1 — DIAGNOSTICS readRP(): дві SPI-транзакції, CS не утримується LOW

**Severity:** 🔴 CRITICAL  
**Статус:** 🔓 OPEN  
**Документ:** `PLUGIN_DIAGNOSTICS.md` §5 `readRP()`

### Evidence

```cpp
// PLUGIN_DIAGNOSTICS.md — readRP() (~рядок 508):
uint16_t raw = (readRegister(LDC1101_RP_MSB) << 8) | readRegister(LDC1101_RP_LSB);
//              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
//              SPI транзакція #1: CS LOW→HIGH        SPI транзакція #2: CS LOW→HIGH
//              ПІСЛЯ цього CS стає HIGH!             читає НАСТУПНИЙ регістр
```

Кожен `readRegister()` виконує окрему SPI транзакцію з CS LOW→HIGH→LOW переходом. LDC1101 вимагає безперервного CS LOW для атомарного читання пов'язаних регістрів. Між транзакцією #1 та #2:
- SD card ISR може перехопити шину
- LDC1101 може оновити вимірювання між читаннями → MSB та LSB відносяться до різних вимірів

Коментар `❌ НЕ для виробничого використання` існує але недостатній — це **єдиний** приклад `readRP()` в документації, і розробники плагінів скопіюють його.

### Рішення

```cpp
// Правильна реалізація — burst read з CS утриманим LOW:
uint16_t readRP() {
    uint16_t raw = 0;
    xSemaphoreTake(ctx->spiMutex, pdMS_TO_TICKS(50));
    digitalWrite(CS_PIN, LOW);
    spi->transfer(LDC1101_RP_MSB | 0x80);  // read bit set
    uint8_t msb = spi->transfer(0x00);
    uint8_t lsb = spi->transfer(0x00);     // CS утримується LOW між MSB і LSB
    digitalWrite(CS_PIN, HIGH);
    xSemaphoreGive(ctx->spiMutex);
    raw = (msb << 8) | lsb;
    return raw;
}
```

Прибрати коментар `❌ НЕ для виробничого використання` і замінити виправленим кодом з поясненням burst read.

---

## 30. PA3-2 — INTERFACES_EXTENDED HX711Plugin: get_units(5) блокує loop() на 62–500 ms

**Severity:** 🔴 CRITICAL  
**Статус:** 🔓 OPEN  
**Документ:** `PLUGIN_INTERFACES_EXTENDED.md` §HX711Plugin `read()`

### Evidence

```cpp
// PLUGIN_INTERFACES_EXTENDED.md (~рядок 161):
SensorData read() override {
    float weight = scale.get_units(5);  // ← BLOCKING: 5 samples averaged
    //                             ^
    //   10 Hz data rate: 5 × 100 ms = 500 ms
    //   80 Hz data rate: 5 × 12.5 ms = 62.5 ms
    //   CONTRACT §2.1: update() ≤ 10 ms — ПОРУШЕННЯ
    return {weight, 0, 1.0f, millis(), weight > 0};
}
```

CONTRACT §2.1 явно вимагає `update()` завершення ≤10 ms. `get_units(5)` на 10 Hz шкалі блокує на **500 ms** — у 50 разів більше ліміту. Навіть на 80 Hz — 62 ms (6× перевищення). Весь `loop()` зупиняється, всі інші плагіни не оновлюються.

### Рішення

**Мінімальний:** `get_units(1)` замість `get_units(5)` → 10 ms або 100 ms залежно від data rate. Не вирішує проблему при 10 Hz.

**Правильний (AsyncSensorPlugin pattern):**

```cpp
class HX711Plugin : public AsyncSensorPlugin {
    // Зчитування в окремій FreeRTOS задачі на Core 0
    std::atomic<float> cachedWeight{0.0f};

    void readTask() override {  // запускається системою в окремій task
        while (!shouldStop) {
            cachedWeight.store(scale.get_units(1));
            vTaskDelay(pdMS_TO_TICKS(50));  // 20 Hz
        }
    }

    SensorData read() override {
        float w = cachedWeight.load();
        return {w, 0, 1.0f, millis(), w > 0};  // НЕ блокуючий
    }
};
```

Оновити `PLUGIN_INTERFACES_EXTENDED.md` прикладом AsyncSensorPlugin для HX711 з поясненням чому blocking sensor вимагає цього паттерну.

---

## 31. PA3-3 — DIAGNOSTICS logDiagnostics(): SD.open() без spiMutex

**Severity:** 🔴 CRITICAL  
**Статус:** 🔓 OPEN  
**Документ:** `PLUGIN_DIAGNOSTICS.md` §5 `logDiagnostics()`

### Evidence

```cpp
// PLUGIN_DIAGNOSTICS.md (~рядок 669):
void logDiagnostics(const DiagnosticResult& result) {
    File log = SD.open("/diagnostics.log", FILE_APPEND);  // ← БЕЗ spiMutex!
    if (log) {
        log.println(result.summary);
        log.close();
    }
}
```

LDC1101Plugin також використовує ту ж VSPI шину. Якщо `LDC1101Plugin::update()` виконується на Core 0 одночасно з `logDiagnostics()` на Core 1 → SPI bus contention → корупція даних на обох (LDC1101 повертає сміттєві значення, SD запис неповний або corrupted).

**Важлива деталь:** зовнішній аудит пропонував замінити `SD.open()` на `ctx->log->info()`. Це **неправильне** рішення — Logger не забезпечує персистентне сховище діагностики. SD файл потрібен для offline діагностики.

### Рішення

```cpp
void logDiagnostics(const DiagnosticResult& result) {
    if (!ctx->spiMutex) return;
    if (xSemaphoreTake(ctx->spiMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        File log = SD.open("/diagnostics.log", FILE_APPEND);
        if (log) {
            log.println(result.summary);
            log.close();
        }
        xSemaphoreGive(ctx->spiMutex);
    } else {
        ctx->log->warning("logDiagnostics: spiMutex timeout, SD write skipped");
    }
}
```

---

## 32. PA3-4 — BH1750Plugin + MyI2CSensorPlugin: getMetadata() pure virtual не реалізований

**Severity:** 🟠 HIGH  
**Статус:** 🔓 OPEN  
**Документ:** `CREATING_PLUGINS.md` §Quick Start BH1750Plugin, §Templates MyI2CSensorPlugin

### Evidence

`PLUGIN_INTERFACES_EXTENDED.md` визначає `ISensorPlugin`:
```cpp
virtual SensorMetadata getMetadata() const = 0;  // pure virtual
```

`CREATING_PLUGINS.md` BH1750Plugin та `MyI2CSensorPlugin` template — жоден не реалізує `getMetadata()`. **Обидва є абстрактними класами** і не можуть бути інстанційовані (compile error).

**Примітка:** PA2-10 (CLOSED) виправив inconsistency у _визначенні_ `ISensorPlugin` між ARCH і INTERFACES. PA3-4 — окрема проблема: конкретні _приклади_ і _templates_ в CREATING_PLUGINS досі не мають реалізації.

### Рішення

Додати до `BH1750Plugin` і до `MyI2CSensorPlugin` template:

```cpp
SensorMetadata getMetadata() const override {
    return {
        .sensorType    = SensorType::LIGHT,
        .manufacturer  = "ROHM Semiconductor",
        .model         = "BH1750FVI",
        .resolution    = 1.0f,   // lux
        .minValue      = 0.0f,
        .maxValue      = 65535.0f,
        .updateRateHz  = 10
    };
}
```

Для `MyI2CSensorPlugin` template — залишити `TODO` коментар з поясненням полів.

---

## 33. PA3-5 — IInputPlugin: API конфлікт hasEvent()/getEvent() vs CONTRACT pollEvent()

**Severity:** 🟠 HIGH  
**Статус:** 🔓 OPEN  
**Документ:** `PLUGIN_INTERFACES_EXTENDED.md` §IInputPlugin + `PLUGIN_CONTRACT.md` §3.3

### Evidence

`PLUGIN_INTERFACES_EXTENDED.md`:
```cpp
class IInputPlugin : public IPlugin {
    virtual bool hasEvent() const = 0;
    virtual InputEvent getEvent() = 0;
    virtual void clearEvents() = 0;
};
```

`PLUGIN_CONTRACT.md` §3.3:
```cpp
virtual std::optional<InputEvent> pollEvent() = 0;  // атомарний pop
```

`hasEvent()` + `getEvent()` = _check-then-act_, не атомарний → TOCTOU race condition у multi-core середовищі. `pollEvent()` = атомарний pop → thread-safe за визначенням.

### Рішення

`pollEvent()` є правильним вибором (CONTRACT §3.3 авторитетний для thread safety). Оновити `PLUGIN_INTERFACES_EXTENDED.md`:

```cpp
class IInputPlugin : public IPlugin {
    // Атомарний pop — CONTRACT §3.3: реалізація повинна бути thread-safe
    virtual std::optional<InputEvent> pollEvent() = 0;
    virtual ~IInputPlugin() = default;
};
```

Прибрати `hasEvent()`, `getEvent()`, `clearEvents()`.

---

## 34. PA3-6 — checkStability(): блокуючий виклик через runDiagnostics() з loop()

**Severity:** 🟠 HIGH  
**Статус:** 🔓 OPEN  
**Документ:** `PLUGIN_DIAGNOSTICS.md` §2 `checkStability()`, `runDiagnostics()`

### Evidence

```cpp
// checkStability() (~рядок 408):
// ⚠️ УВАГА: НЕ викликати з loop() — блокується на ~500 ms!
bool checkStability(int samples = 5, int delayMs = 100) {
    for (int i = 0; i < samples; i++) {
        readings[i] = readRP();
        delay(delayMs);  // 100ms × 5 = 500 ms blocking
    }
}
```

`runDiagnostics()` (~рядок 246) викликає `runSelfTest()` → `checkStability()`. Попередження `НЕ викликати з loop()` існує для `checkStability()`, але **відсутнє** для `runDiagnostics()` — і саме `runDiagnostics()` є публічним API.

### Рішення

Додати попередження до `runDiagnostics()`:
```cpp
// ⚠️ runDiagnostics() блокує ~500 ms (checkStability). НЕ викликати з loop()!
// Запускати з окремої FreeRTOS task або по кнопці.
DiagnosticResult runDiagnostics();
```

Рекомендований pattern виклику:
```cpp
xTaskCreate([](void* p) {
    auto result = static_cast<LDC1101DiagnosticPlugin*>(p)->runDiagnostics();
    vTaskDelete(nullptr);
}, "diag_task", 4096, this, 1, nullptr);
```

---

## 35. PA3-7 — HX711Plugin: GPIO pins hardcoded, не використовує ConfigManager

**Severity:** 🟠 HIGH  
**Статус:** 🔓 OPEN  
**Документ:** `PLUGIN_INTERFACES_EXTENDED.md` §HX711Plugin

### Evidence

```cpp
// (~рядки 118–119):
const uint8_t DOUT_PIN = 5;
const uint8_t SCK_PIN  = 6;
```

Hardcoded пін-розводка унеможливлює config-driven pin assignment і суперечить патерну ConfigManager вже використовуваному в `LDC1101Plugin`. Розробник змушений редагувати бібліотечний код.

### Рішення

```cpp
bool initialize(PluginContext* ctx) override {
    uint8_t doutPin = ctx->config->getInt("hx711.dout_pin", 5);
    uint8_t sckPin  = ctx->config->getInt("hx711.sck_pin",  6);
    scale.begin(doutPin, sckPin);
}
```

Config файл (`plugins/hx711.json`):
```json
{ "hx711.dout_pin": 5, "hx711.sck_pin": 6, "hx711.calibration_factor": -7050.0 }
```

---

## 36. PA3-8 — CONTRACT §1.1: ≥10 Hz гарантія нездійсненна при 10+ плагінах

**Severity:** 🟡 MEDIUM  
**Статус:** 🔓 OPEN  
**Документ:** `PLUGIN_CONTRACT.md` §1.1 + `PLUGIN_ARCHITECTURE.md` §3.1

### Evidence

Симуляція (§3.1 цього аудиту): 10 × 10 ms + delay(10) = 110 ms → 9.1 Hz.  
CONTRACT §1.1: "update() викликається регулярно з частотою ≥10 Hz" — безумовна обіцянка.

### Рішення

Уточнити CONTRACT §1.1 з explicit умовами (max plugins, max update() time). Зазначити що AsyncSensorPlugin pattern є шляхом до складніших конфігурацій.

---

## 37. PA3-9 — BH1750Plugin::getType() повертає CUSTOM замість LIGHT

**Severity:** 🟡 MEDIUM  
**Статус:** 🔓 OPEN  
**Документ:** `CREATING_PLUGINS.md` §Quick Start BH1750Plugin

### Evidence

```cpp
SensorType getType() const override { return SensorType::CUSTOM; }  // має бути LIGHT
```

`getPluginsByType<ISensorPlugin>()` з фільтрацією по типу буде отримувати некоректні результати для BH1750. `CUSTOM` — для нестандартних датчиків.

### Рішення

```cpp
SensorType getType() const override { return SensorType::LIGHT; }
```

---

## 38. PA3-10 — checkCalibration(): lastCalibrationTime = 0 після ребуту

**Severity:** 🟡 MEDIUM  
**Статус:** 🔓 OPEN  
**Документ:** `PLUGIN_DIAGNOSTICS.md` §2 `checkCalibration()`  
**Пов'язано:** PA2-16 (той самий метод, різна змінна)

### Evidence

```cpp
uint32_t calibrationAge = millis() - lastCalibrationTime;  // lastCalibrationTime=0 після ребуту
if (calibrationAge > 30UL * 24 * 60 * 60 * 1000) return false;
// millis()≈0, lastCalibrationTime=0 → age≈0 → 30-денна перевірка НІКОЛИ не triggerується після ребуту
```

Протилежна помилка до PA2-16: PA2-16 → завжди `CALIBRATION_NEEDED`; PA3-10 → 30-денний trigger ніколи не спрацює після cold boot (система вважає калібрування свіжим).

### Рішення

Персистувати `lastCalibrationTime` у storage (NVS/LittleFS) та завантажувати при `initialize()`. Додати примітку щодо обмежень `millis()` без RTC.

---

## 39. PA3-11 — showDiagnosticsScreen(): displayPlugin/pluginSystem як глобальні змінні

**Severity:** 🟡 MEDIUM  
**Статус:** 🔓 OPEN  
**Документ:** `PLUGIN_DIAGNOSTICS.md` §3  
**Пов'язано:** PA2-15, C-PA16

### Evidence

`showDiagnosticsScreen()` використовує глобальні `displayPlugin` та `pluginSystem` — Service Locator anti-pattern. Залежності не видні зі сигнатури функції, тестування неможливе без ініціалізації globals.

### Рішення

```cpp
// Передавати залежності явно:
void showDiagnosticsScreen(
    const DiagnosticResult& result,
    IDisplayPlugin* display,
    PluginSystem* system
);
// або використовувати ctx якщо функція є методом плагіна
```

---

## 40. PA3-12 — PLUGIN_ARCHITECTURE.md: IPlugin визначений двічі в одному файлі

**Severity:** 🟡 MEDIUM  
**Статус:** 🔓 OPEN  
**Документ:** `PLUGIN_ARCHITECTURE.md` §Крок1 (~рядок 138) та §Оновлений IPlugin (~рядок 272)  
**Пов'язано:** PA2-9 CLOSED — виправив cross-doc, але внутрішня дублікація залишилась

### Evidence

Два `class IPlugin { ... }` блоки в тому ж файлі. Читач не знає яке визначення актуальне. Може реалізувати плагін на основі застарілого блоку.

### Рішення

Видалити дублікат, замінити на: `// → Canonical IPlugin визначено вище (~рядок 138); повне визначення: include/IPlugin.h`. Оновити ARCH версію (→ PA3-14).

---

## 41. PA3-13 — MyI2CSensorPlugin template: valid=true в placeholder коді

**Severity:** 🟢 LOW  
**Статус:** 🔓 OPEN  
**Документ:** `CREATING_PLUGINS.md` §Templates `MyI2CSensorPlugin`

### Evidence

```cpp
// TODO: implement actual I2C read
return {0, 0, 1.0f, millis(), true};  // ← valid=true у placeholder!
```

Незавершений template репортує false success. Правильно: `valid=false` до реальної реалізації.

### Рішення

```cpp
return {0, 0, 0.0f, millis(), false};  // valid=false до реальної реалізації
```

---

## 42. PA3-14 — PLUGIN_ARCHITECTURE.md: версія та дата не оновлені після P-2

**Severity:** 🟢 LOW  
**Статус:** 🔓 OPEN  
**Документ:** `PLUGIN_ARCHITECTURE.md` header

### Evidence

`Версія: 1.0.0`, `Дата: 9 березня 2026` — не оновлені після двох раундів виправлень.

### Рішення

`Версія: 2.0.0`, `Дата: 13 березня 2026`.

---

## 43. PA3-15 — HARDWARE_PROFILES.md: файл не існує, посилання без [TODO]

**Severity:** 🟢 LOW  
**Статус:** 🔓 OPEN  
**Документ:** `PLUGIN_ARCHITECTURE.md` §Апаратні профілі (~рядок 1240)

### Evidence

`HARDWARE_PROFILES.md` посилається без `[TODO]` маркера — файл відсутній.

### Рішення

Додати `**[TODO — файл не створено]**` до посилання.

---

## 44. PA3-16 — getGraphicsLibrary(): повертає void* — type-unsafe

**Severity:** 🔵 INFO  
**Статус:** 🔓 OPEN  
**Документ:** `PLUGIN_DIAGNOSTICS.md` `showDiagnosticsScreen()`

### Evidence

```cpp
void* getGraphicsLibrary() override { return &display; }
// Вимагає reinterpret_cast<M5GFX*> без compile-time перевірки типу
```

### Рішення (не блокуючий)

```cpp
virtual M5GFX* getGraphicsLibrary() { return &display; }  // typed return
// або template<typename T> T* getGraphicsLibrary() { return static_cast<T*>(&display); }
```

---

## 45. PA3-17 — getTypeId(): зазначений з -frtti але не реалізований у прикладах

**Severity:** 🔵 INFO  
**Статус:** 🔓 OPEN  
**Документ:** `PLUGIN_INTERFACES_EXTENDED.md` `IPlugin` base  
**Пов'язано:** PA-7 CLOSED

### Evidence

`getTypeId()` описаний як RTTI-based helper, але жоден плагін не перевизначує його. `-frtti` overhead (~1-2 KB/polymorphic class) важливий при 10+ плагінах на ESP32.

### Рішення (не блокуючий)

Задокументувати що default `typeid(*this)` прийнятна і `-frtti` обов'язковий, або надати string-based альтернативу без RTTI:
```cpp
virtual const char* getTypeId() const { return getName(); }
```

---

## 46. Pre-implementation Checklist v3.0.0 (ДОПОВНЕННЯ)

> Це доповнення до §25. Основний checklist (PA2-xx items) залишається у §25.

### Перед написанням першого виробничого плагіна (CRITICAL blocking):

- [ ] **PA3-1** — `readRP()` burst transaction (CS LOW через весь read)
- [ ] **PA3-2** — `HX711Plugin` → AsyncSensorPlugin або мінімум `get_units(1)`
- [ ] **PA3-3** — `logDiagnostics()` → `spiMutex` навколо `SD.open()`

### Перед першим release (HIGH blocking):

- [ ] **PA3-4** — `getMetadata()` у `BH1750Plugin` та `MyI2CSensorPlugin` template
- [ ] **PA3-5** — `IInputPlugin` → уніфікувати на `pollEvent()` (CONTRACT §3.3)
- [ ] **PA2-17** — BH1750 `read()` + `wireMutex` *(підвищено до HIGH)*
- [ ] **PA3-6** — `runDiagnostics()` → FreeRTOS task, не з `loop()`
- [ ] **PA3-7** — HX711 GPIO з ConfigManager

### P-3 / Cleanup (MEDIUM, виправити до P-3):

- [ ] PA3-8 — CONTRACT §1.1 уточнення гарантії
- [ ] PA3-9 — BH1750 `getType()` → `SensorType::LIGHT`
- [ ] PA3-10 — `lastCalibrationTime` персистентність (разом з PA2-16)
- [ ] PA3-11 — `showDiagnosticsScreen()` dependency injection
- [ ] PA3-12 — ARCH: `IPlugin` double definition cleanup

### Backlog (LOW/INFO):

- [ ] PA3-13 — template `valid=false` placeholder
- [ ] PA3-14 — ARCH version header → v2.0.0
- [ ] PA3-15 — HARDWARE_PROFILES.md `[TODO]` marker
- [ ] PA3-16 — `getGraphicsLibrary()` typed return
- [ ] PA3-17 — `getTypeId()` documentation/implementation

---

## 47. Backlog та пріоритети v3.0.0

### Залежності між новими знахідками

```
PA3-1 (readRP burst)
    └─ пов'язано: PA3-3 (SD.open mutex — та сама VSPI шина, той самий mutex pattern)

PA3-2 (HX711 blocking)
    └─ блокує: PA3-7 (hardcoded pins — виправити разом з async refactor)
    └─ пов'язано: PA3-8 (CONTRACT timing — HX711 async = рішення для timing проблеми)

PA3-4 (getMetadata missing)
    └─ пов'язано: PA3-9 (getType=CUSTOM — той самий BH1750Plugin, виправити разом)

PA3-5 (IInputPlugin API conflict)
    └─ блокує: будь-який input plugin розробник

PA3-6 (checkStability blocking)
    └─ пов'язано: PA3-11 (showDiagnosticsScreen globals — обидва в DIAGNOSTICS §3)

PA3-10 (lastCalibrationTime)
    └─ пов'язано: PA2-16 (calibrationBaseline — той самий checkCalibration(), виправити разом)

PA3-12 (IPlugin double def)
    └─ залежить від: PA2-9 CLOSED (cross-doc canonical reference вже є)
```

### Пріоритети (по бізнес-впливу)

| Пріоритет | Знахідки | Чому |
|-----------|---------|------|
| **CRITICAL blocking** | PA3-1, PA3-2, PA3-3 | SPI corruption, loop freeze — safety issues |
| **HIGH (до release)** | PA3-4, PA3-5, PA2-17↑, PA3-6, PA3-7 | Compile errors або critical runtime bugs |
| **MEDIUM cleanup** | PA3-8..PA3-12 | Documentation consistency, wrong types |
| **LOW/INFO backlog** | PA3-13..PA3-17 | Minor improvements |

---

## 48. Cross-reference: v2.0.0 → v3.0.0

### Відповідність зовнішній аудит v4 ↔ внутрішні ID

| External v4 ID | Severity (external) | Внутрішній ID | Примітка |
|----------------|---------------------|---------------|---------|
| B-1 | 🔴 CRITICAL | **PA3-1** | Підтверджено: `readRP()` рядок ~508 |
| B-2 | 🔴 CRITICAL | **PA3-2** | Підтверджено; рекомендована AsyncSensorPlugin (не тільки `get_units(1)`) |
| B-3 | 🔴 CRITICAL | **PA3-3** | Підтверджено; **FIX: spiMutex** (не Logger — persistent storage потрібен) |
| A-1 | 🟠 HIGH | **PA3-4** | Підтверджено; PA2-10 CLOSED ≠ PA3-4 (різний scope) |
| A-2 | 🟠 HIGH | **PA3-5** | Підтверджено |
| A-3 | 🟠 HIGH | **PA3-6** | Підтверджено: warning у `checkStability()`, але відсутнє у `runDiagnostics()` |
| A-4 | 🟠 HIGH | **PA3-7** | Підтверджено |
| C-1 | 🟡 MEDIUM | **PA3-12** | Підтверджено як residual від PA2-9 |
| C-2 | 🟡 MEDIUM | **PA3-8** | Вже задокументовано в §3.1; тепер як офіційна знахідка |
| C-3 | 🟡 MEDIUM | **PA3-9** | Підтверджено |
| C-4 | 🟡 MEDIUM | **PA3-10** | Підтверджено; відрізняється від PA2-16 (різні змінні, протилежний ефект) |
| C-5 | 🟡 MEDIUM | **PA3-11** | Підтверджено; пов'язано з PA2-15 |
| D-1 | 🟢 LOW | *(false positive)* | Рядок 263 → PA3-13; рядок 498 коректний (`valid=true` після успішного read) |
| D-2 | 🟢 LOW | **PA3-14** | Підтверджено |
| D-3 | 🟢 LOW | **PA3-15** | Підтверджено |
| E-1 | 🔵 INFO | **PA3-16** | Підтверджено |
| E-2 | 🔵 INFO | **PA3-17** | Підтверджено; пов'язано з PA-7 CLOSED |
| *(missed by v4)* | 🟠 HIGH | **PA2-17↑** | BH1750 `read()` без `wireMutex` — знайдено внутрішньо; підвищено LOW→HIGH |

### Стан v3.0.0 знахідок

**17 нових PA3-xx знахідок:** всі 🔓 OPEN (не виправлені на момент інтеграції аудиту)

---

*Аудит v2.0.0 проведений: 2026-03-13*  
*Аудит v3.0.0 (доповнення) інтегровані: 2026-03-13*  
*Попередній аудит: `docs/architecture/obsolete/PLUGIN_AUDIT_v1.0.0.md`*  
*Зовнішній аудит: `docs/external/PLUGIN_ARCHITECTURE_AUDIT_v4_2026-03-13.md`*  
*Наступна дія: виправлення PA3-1, PA3-2, PA3-3 (CRITICAL) → PA3-4..PA3-7 (HIGH)*
