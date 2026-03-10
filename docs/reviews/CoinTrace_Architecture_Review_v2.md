# Архітектурна рецензія: CoinTrace Plugin System

**Тип документа:** Незалежна архітектурна рецензія — 2-га ітерація  
**Версія звіту:** 2.0.0  
**Попередня версія:** 1.0.0 (10 березня 2026)  
**Дата:** 10 березня 2026  
**Рецензент:** Незалежний архітектурний аудит  
**Статус:** Фінальна версія

---

## Зміст

1. [Методологія та контекст](#1-методологія-та-контекст)
2. [Загальна оцінка прогресу](#2-загальна-оцінка-прогресу)
3. [README.md](#3-readmemd)
4. [PLUGIN_ARCHITECTURE.md](#4-plugin_architecturemd)
5. [PLUGIN_INTERFACES_EXTENDED.md](#5-plugin_interfaces_extendedmd)
6. [PLUGIN_CONTRACT.md](#6-plugin_contractmd)
7. [PLUGIN_DIAGNOSTICS.md](#7-plugin_diagnosticsmd)
8. [CREATING_PLUGINS.md](#8-creating_pluginsmd)
9. [COMPARISON.md](#9-comparisonmd)
10. [Головна системна проблема 2-ї ітерації](#10-головна-системна-проблема-2-ї-ітерації)
11. [Пріоритетний план дій](#11-пріоритетний-план-дій)
12. [Висновок](#12-висновок)

---

## 1. Методологія та контекст

### Що аналізувалось

Проведено детальний аналіз усіх 6 архітектурних документів проекту CoinTrace після першої ітерації доопрацювання:

| Документ | Статус | Ключова зміна |
|---|---|---|
| `README.md` | Оновлений | Додано `PLUGIN_CONTRACT.md` |
| `PLUGIN_ARCHITECTURE.md` | Оновлений | Додано розділ `PluginContext` |
| `PLUGIN_INTERFACES_EXTENDED.md` | Без змін | Не синхронізований з новою архітектурою |
| `PLUGIN_CONTRACT.md` | **Новий** | Формальний контракт система ↔ плагін |
| `PLUGIN_DIAGNOSTICS.md` | Частково оновлений | LDC1101 приклад виправлений |
| `CREATING_PLUGINS.md` | Частково оновлений | Quick Start і I2C шаблон виправлені |
| `COMPARISON.md` | Без змін | Без оновлень |

### Що покращено з першої ітерації

Перша рецензія (v1.0.0) виявила 5 критичних проблем. Ось їх статус:

| Проблема v1.0 | Статус |
|---|---|
| `Wire.begin()` в кожному плагіні | ✅ Вирішено в нових прикладах |
| Відсутній `PluginContext` | ✅ Додано структуру і розділ |
| `SensorData` без семантики полів | ⚠️ Частково (є `SensorMetadata`, але `value1`/`value2` залишились) |
| Дублювання метаданих JSON/код | ⚠️ Не вирішено |
| Thread Safety контракт відсутній | ✅ Закрито `PLUGIN_CONTRACT.md` |

---

## 2. Загальна оцінка прогресу

| Документ | v1.0 | v2.0 | Динаміка | Коментар |
|---|---|---|---|---|
| README.md | 6/10 | 8/10 | ↑↑ | Сильно покращився завдяки CONTRACT |
| PLUGIN_ARCHITECTURE.md | 7/10 | 7/10 | = | PluginContext додано, але два `IPlugin` в одному файлі |
| PLUGIN_INTERFACES_EXTENDED.md | 7/10 | 5/10 | ↓↓ | Приклади не синхронізовані з новою архітектурою |
| PLUGIN_CONTRACT.md | — | 8/10 | новий | Відмінний новий документ, одна критична помилка |
| PLUGIN_DIAGNOSTICS.md | 9/10 | 8/10 | ↓ | LDC1101 виправлено, але recovery зламано |
| CREATING_PLUGINS.md | 6/10 | 7/10 | ↑ | Quick Start відмінний, але "поради" суперечать контракту |
| COMPARISON.md | 6/10 | 6/10 | = | Без змін |
| **Загальна** | **7/10** | **7/10** | = | Прогрес є, але десинхронізація знизила загальну оцінку |

**Ключовий висновок:** Введення `PluginContext` — правильний архітектурний крок, але оновлення документів виявилось **неповним**. З 6 файлів повністю синхронізований з новою архітектурою лише `CREATING_PLUGINS.md` (Quick Start + I2C шаблон). Решта містять старий `initialize()` без `PluginContext` у частині прикладів — це створює суперечливі сигнали для розробника.

---

## 3. README.md

### Оцінка: 8/10 ↑↑

### ✅ Що покращилось

**Додано `PLUGIN_CONTRACT.md`** як окремий документ з правильним описом (рядки 81–104). Особливо цінно що контракт поставлено **першим у шляху розробника** (рядок 119) — це правильна пріоритизація. Перелік що містить контракт повний і точний: Wire.begin() заборонено, PluginContext, thread safety, memory limit 8KB, sanctions.

### 🔴 Проблема 1: Lifecycle діаграма досі не виправлена

Діаграма в рядках 232–252 залишилась незмінною з першої версії:

```
New → Testing → Ready → update() → shutdown()
```

Тепер ситуація гірша ніж раніше — в системі вже є `PLUGIN_CONTRACT.md` і `PLUGIN_DIAGNOSTICS.md` з 15 станами `HealthStatus`, а lifecycle діаграма в README не показує жодного стану помилки. Розробник читає README, бачить спрощену діаграму, потім йде в DIAGNOSTICS і бачить 15 станів — явна суперечність.

**Рекомендація:** Замінити на повну діаграму:

```
              canInitialize()
┌──────┐           ↓          ┌──────────────────────┐
│ New  │ ─────────────────→   │ true                 │
└──────┘                      └──────────────────────┘
                                         ↓
                                   initialize(ctx)
                                   ↙           ↘
                          false (NOT_FOUND,    true
                          INIT_FAILED...)        ↓
                                ↓           ┌────────┐
                           DISABLED     ┌──→│  READY │
                                        │   └────────┘
                                        │       ↓
                                        │   update()/read()
                                        │   ↙         ↘
                                        │ OK_WITH_   DEGRADED
                                        │ WARNINGS      ↓
                                        │            TIMEOUT
                                        │               ↓
                                        └── auto-recovery
                                                       ↓
                                               SENSOR_FAULT
                                                       ↓
                                                  shutdown()
```

### 🔴 Проблема 2: `Service Locator` в глосарії без застереження

Рядок 192: `Service Locator — паттерн для отримання доступу до сервісів/плагінів` — нейтральний опис. Але тепер коли є `PluginContext` з явним Dependency Injection — Service Locator є антипатерном в контексті цього проекту. Без застереження новий розробник може обрати Service Locator як основний підхід.

**Рекомендація:**
```
Service Locator — ⚠️ антипатерн в цьому проекті. Замінений PluginContext (DI).
                  Залишений в глосарії лише для розуміння чому від нього відмовились.
```

### 🟡 Проблема 3: Версіонування документації не оновлено

Таблиця версій (рядки 294–299) залишилась на `1.0.0` від 9 березня, хоча додано цілий новий документ `PLUGIN_CONTRACT.md` — це значна зміна. Дата "Останнє оновлення" внизу теж показує 9 березня.

### 🟡 Проблема 4: `PLUGIN_CONTRACT.md` відсутній у шляхах для Архітектора та QA

Шлях для Архітектора (рядки 140–146) не включає `PLUGIN_CONTRACT.md`. Шлях для QA/DevOps (рядки 130–136) теж. Обидві аудиторії мають знати контракт для своєї роботи.

### 🟡 Проблема 5: Орфографічні помилки

- Рядок 29: `"Checklis"` → `"Checklist"`
- Рядок 30: `"Debugging порадиt"` → `"Debugging поради"`

### 🟡 Проблема 6: Два посилання ведуть на одну URL

Рядки 307–308: і `Service Locator Pattern`, і `Dependency Injection` посилаються на `martinfowler.com/articles/injection.html`. Service Locator має окрему сторінку.

---

## 4. PLUGIN_ARCHITECTURE.md

### Оцінка: 7/10 (без змін)

### ✅ Що покращилось

**Додано розділ PluginContext** (рядки 209–290) — правильно описано проблему `Wire.begin()`, правильна структура `PluginContext`, правильний підпис `initialize(PluginContext* ctx)`. Приклад `MyPlugin` з використанням `ctx->wire` та `ctx->log` написаний коректно.

### 🔴 Проблема 1: Два різних визначення `IPlugin` в одному документі

Це найсерйозніша проблема файлу. На рядках **120–142** є перше визначення:

```cpp
// Крок 1: Базовий інтерфейс плагіна
class IPlugin {
    virtual bool initialize() = 0;  // ← БЕЗ PluginContext
};
```

На рядках **236–258** є друге визначення:

```cpp
// Оновлений інтерфейс IPlugin
class IPlugin {
    virtual bool initialize(PluginContext* ctx) = 0;  // ← З PluginContext
};
```

Розробник який читає документ послідовно бачить спочатку одне, потім інше. Перше визначення треба або прибрати, або замінити посиланням: `"Застарілий варіант — дивіться оновлений нижче"`.

### 🔴 Проблема 2: `canInitialize()` — суперечність між документами

В цьому документі (рядок 269) в прикладі `MyPlugin`:
```cpp
bool canInitialize() override {
    // На цьому етапі context ще немає
    return true;
}
```

В `PLUGIN_CONTRACT.md` (рядок 503) приклад `MinimalPlugin`:
```cpp
bool canInitialize() override {
    ctx->wire->beginTransmission(0x48);  // ← використовує ctx!
```

Одна з двох версій є помилкою. Якщо `canInitialize()` не має context — то `PLUGIN_CONTRACT.md` містить UB (undefined behavior). Потрібно прийняти архітектурне рішення і синхронізувати обидва документи (рекомендація — варіант без ctx, реальна I2C перевірка в `initialize()`).

### 🔴 Проблема 3: Hot-Reload та OTA — досі без застереження

Рядки 1160–1170 залишились незмінними з першої ітерації:

```cpp
pluginSystem->reloadPlugin("QMC5883L");
pluginSystem->installPluginOTA("https://...");
```

ESP32 не підтримує dynamic loading бібліотек в runtime. Ці "фічі" вводять розробника в оману. Потрібно або прибрати, або додати явне застереження: *"Потребує повного перезавантаження пристрою. Hot-Reload не підтримується на bare-metal ESP32."*

### 🟡 Проблема 4: Структура файлів проекту не відображає поточний стан

Рядки 107–111 показують:
```
docs/architecture/
├── PLUGIN_ARCHITECTURE.md
├── CREATING_PLUGINS.md
└── HARDWARE_PROFILES.md
```

`PLUGIN_CONTRACT.md` та `PLUGIN_DIAGNOSTICS.md` відсутні. `HARDWARE_PROFILES.md` — посилання на неіснуючий файл.

### 🟡 Проблема 5: Версія та дата документа не оновлені

`Версія: 1.0.0`, `Дата: 9 березня 2026` — документ отримав значне розширення (розділ PluginContext ~80 рядків), але версія не змінилась.

---

## 5. PLUGIN_INTERFACES_EXTENDED.md

### Оцінка: 5/10 ↓↓

### ✅ Що залишилось добрим

`SensorData.valid`, `SensorMetadata` з полем `unit`, широкий перелік типів сенсорів, таблиця точності вимірювань — все це правильно і актуально.

### 🔴 Проблема 1: `HX711Plugin.initialize()` — старий підпис без PluginContext

Рядок 138:
```cpp
bool initialize() override {  // ← БЕЗ PluginContext!
    scale.begin(DOUT_PIN, SCK_PIN);
```

Це основний приклад вагового сенсора. Розробник який пише ваговий плагін скопіює цей шаблон і отримає код що не компілюється з новим `IPlugin` інтерфейсом.

### 🔴 Проблема 2: `ST7789V2Plugin.initialize()` — старий підпис

Рядок 249:
```cpp
bool initialize() override {  // ← БЕЗ PluginContext!
    display = &M5Cardputer.Display;
```

Та сама проблема для дисплейного плагіна.

### 🔴 Проблема 3: `analyzeCoin()` — глобальний `pluginSystem` після введення PluginContext

Рядки 380–383:
```cpp
void analyzeCoin() {
    auto weightSensor = pluginSystem->getPlugin<ISensorPlugin>("HX711");
```

`pluginSystem` — невизначена глобальна змінна. Після введення `PluginContext` цей приклад особливо невідповідний: контекст є, але приклад головної бізнес-логіки системи його не використовує. Потрібно показати як `analyzeCoin()` отримує плагіни через правильний механізм.

### 🔴 Проблема 4: `calibrate()` в HX711 — пряме звернення до `Serial`

Рядок 162:
```cpp
Serial.println("Place 100g calibration weight...");
```

Після введення `ctx->log` — пряме використання `Serial` є порушенням архітектурного підходу. Має бути `ctx->log->info(getName(), "Place 100g calibration weight...")`.

### 🟡 Проблема 5: `getGraphicsLibrary()` повертає `void*`

Рядок 220:
```cpp
virtual void* getGraphicsLibrary() { return nullptr; }
```

Проблема з першої ітерації залишилась. `void*` ламає типобезпеку C++. Споживач мусить знати конкретну реалізацію щоб правильно кастити — абстракція `IDisplayPlugin` не працює для advanced операцій.

**Рекомендація:**
```cpp
// Варіант з template:
template<typename T>
T* getGraphicsLibrary() { return static_cast<T*>(getLibraryPtr()); }

protected:
    virtual void* getLibraryPtr() { return nullptr; }
```

---

## 6. PLUGIN_CONTRACT.md

### Оцінка: 8/10 (новий документ)

### ✅ Загальна оцінка: відмінний документ

Це найкращий новий документ в наборі. Структура чітка і логічна: що система гарантує → що плагін зобов'язаний → типові контракти → санкції → перевірка → приклади → versioning. Формат "санкції за порушення" (рядки 453–460) — конкретні наслідки замість абстрактних застережень — особливо цінний підхід. Версіонування контракту через `contract_version` в `plugin.json` — зріле архітектурне рішення.

### 🔴 Критична помилка: `canInitialize()` використовує `ctx` до `initialize()`

Рядки 503–507 в прикладі `MinimalPlugin`:

```cpp
bool canInitialize() override {
    ctx->wire->beginTransmission(0x48);  // ← ctx тут nullptr!
    return (ctx->wire->endTransmission() == 0);
}
```

`ctx` зберігається тільки в `initialize()` (рядок 510: `ctx = context`). В момент виклику `canInitialize()` — `ctx` є неініціалізованим покажчиком. Це undefined behavior і краш при старті системи. Приклад що позиціонується як "мінімальний правильний плагін" містить критичну помилку.

**Потрібне архітектурне рішення:**

*Варіант A* — `canInitialize()` теж отримує PluginContext:
```cpp
virtual bool canInitialize(PluginContext* ctx) = 0;
```

*Варіант B (рекомендовано)* — `canInitialize()` нічого не перевіряє через шини, реальна перевірка I2C переноситься в початок `initialize()`:
```cpp
bool canInitialize() override {
    return true;  // Перевірка hardware відбувається в initialize()
}

bool initialize(PluginContext* ctx) override {
    this->ctx = ctx;
    // Тут перевіряємо I2C:
    ctx->wire->beginTransmission(I2C_ADDR);
    if (ctx->wire->endTransmission() != 0) {
        lastError = {2, "I2C NACK at 0x48 - check wiring"};
        return false;
    }
    // ...
}
```

Варіант B не вимагає зміни підпису `canInitialize()` в базовому `IPlugin`, тому він менш руйнівний для всієї архітектури. Але **необхідно задокументувати** це правило явно: *"`canInitialize()` не виконує I2C/SPI операцій — тільки перевірку попередніх умов без шин"*.

### 🟡 Проблема 2: Гарантія 10 Hz для `update()` — умовна

Рядки 55–59:
```
Гарантія: update() викликається з частотою не менше 10 Hz
```

Якщо один плагін порушить контракт і заблокує `update()` на 50ms — частота впаде нижче 10 Hz для всіх. Система може дати таку гарантію лише якщо реалізує timeout per plugin або watchdog. Це архітектурне питання яке потребує відповіді: чи буде система захищати себе від плагінів-порушників контракту, і якщо так — як?

**Рекомендація:** До вирішення питання пом'якшити формулювання: *"за умови дотримання контракту всіма плагінами"*.

### 🟡 Проблема 3: `IInputPlugin` — різні API в двох документах

В `PLUGIN_CONTRACT.md` (рядок 439): `virtual bool pollEvent(InputEvent& event)`

В `PLUGIN_INTERFACES_EXTENDED.md` (рядки 307–309):
```cpp
virtual bool hasEvent() = 0;
virtual InputEvent getEvent() = 0;
```

Два різних API для одного інтерфейсу — суперечність між документами.

---

## 7. PLUGIN_DIAGNOSTICS.md

### Оцінка: 8/10 ↓

### ✅ Що значно покращилось

**LDC1101Plugin повністю переписаний** (рядки 135–208) — це найбільш синхронізований з новою архітектурою приклад у всій документації. Тепер він:
- Зберігає `PluginContext* ctx = nullptr` і використовує `ctx->wire`
- `canInitialize()` повертає `true` без I2C операцій, реальна перевірка в `initialize()`
- Логує через `ctx->log->error()` та `ctx->log->info()`
- Явно показує як встановити `HealthStatus` при різних типах помилок

### 🔴 Проблема 1: Auto-Recovery викликає `initialize()` без PluginContext

Рядок 609 в Health Monitoring:
```cpp
if (plugin->initialize()) {  // ← БЕЗ PluginContext!
```

Це системний код для автоматичного відновлення. Але тепер `initialize()` вимагає `PluginContext*` — звідки система бере контекст для повторного виклику? Це архітектурна прогалина: при першій ініціалізації контекст передається явно, але при auto-recovery — він відсутній.

Є два шляхи вирішення:
1. Система зберігає `PluginContext` і передає повторно: `plugin->initialize(savedCtx)`
2. Плагін сам зберігає `ctx` після першого `initialize()` і reuse при recovery — тобто recovery викликається як `plugin->recover()` без аргументів

### 🔴 Проблема 2: Базовий `IPlugin` у вступному блоці — старий підпис

Рядки 92–93:
```cpp
virtual bool canInitialize() = 0;
virtual bool initialize() = 0;  // ← без PluginContext!
```

Цей блок показує "поточну реалізацію яка є недостатньою" — але `initialize()` без PluginContext вже є саме тим що треба виправити. Документ не кажучи про це явно підриває довіру до нового API. Або треба додати коментар `// ← старий підпис, замінений в PluginContext розділі`, або оновити блок.

### 🟡 Проблема 3: Health Monitoring у `loop()` — `Serial.printf()` замість Logger

Рядки 569–609: весь блок автоматичного recovery використовує `Serial.printf()` напряму, тоді як документ розглядає production-систему. Після введення `ctx->log` це виглядає як непослідовність. Рядок 566 особливо показовий: `Serial.println("\n=== Running Self-Tests ===\n")` — в production системі це має йти через Logger.

### 🟡 Проблема 4: `showDiagnosticsScreen()` — глобальні змінні `pluginSystem`, `displayPlugin`

Рядки 493, 497: `displayPlugin->`, `pluginSystem->` — без пояснення звідки вони. Після введення PluginContext логічно показати як система отримує ці об'єкти через правильний механізм, а не через глобальні змінні.

---

## 8. CREATING_PLUGINS.md

### Оцінка: 7/10 ↑

### ✅ Що значно покращилось — найбільший прогрес серед усіх файлів

**Quick Start приклад BH1750** (рядки 45–136) — відмінно переписаний:
- `PluginContext* ctx = nullptr` зберігається в плагіні
- `#include "PluginContext.h"` добавлено
- `ctx->wire->beginTransmission()` замість `Wire.`
- `ctx->log->error()` та `ctx->log->info()` для логування
- `if (!ctx) return` в `shutdown()` — захист від виклику без initialize

**Шаблон I2C сенсора** (рядки 218–265) — теж правильний, повністю синхронізований.

### 🔴 Проблема 1: SPI шаблон — не оновлений, порушує контракт

Рядки 286–306:
```cpp
bool initialize() override {  // ← БЕЗ PluginContext!
    spi = new SPIClass(VSPI);
    spi->begin();              // ← ЗАБОРОНЕНО контрактом!
```

`SPI.begin()` в плагіні — це пряме порушення `PLUGIN_CONTRACT.md` (розділ 1.2). Розробник який пише SPI плагін скопіює цей шаблон і порушить контракт не підозрюючи про це. SPI шаблон має бути виправлений в першу чергу серед усіх змін у цьому файлі — він стоїть поруч із правильними прикладами і це заплутує.

**Правильний варіант:**
```cpp
bool initialize(PluginContext* context) override {
    ctx = context;
    spi = ctx->spi;  // ← отримуємо вже ініціалізований SPI
    // ❌ НЕ викликати spi->begin()!
    ready = true;
    return true;
}
```

### 🔴 Проблема 2: "Порада №1" в кінці документа — прямо суперечить контракту

Рядки 581–586:
```cpp
// ПОРАДА ДОСВІДЧЕНОГО РОЗРОБНИКА:
bool canInitialize() override {
    Wire.beginTransmission(I2C_ADDR);  // ← глобальний Wire!
    return (Wire.endTransmission() == 0);
}
```

Це найнебезпечніша ділянка документа. "Поради досвідченого розробника" читаються в кінці і мають авторитет. Але ця порада прямо порушує контракт: `Wire` не має використовуватись напряму в плагіні, і в `canInitialize()` немає `ctx`. Розробник прочитав правильні приклади на початку, а "порада" в кінці скасувала це розуміння.

**Виправлення:** Замінити на:
```cpp
bool canInitialize() override {
    // ✅ НЕ використовуємо Wire тут — context ще недоступний
    // Перевірка hardware відбувається на початку initialize()
    return true;
}
```

### 🔴 Проблема 3: Checklist суперечить новій архітектурі

Рядок 199:
```
- [ ] canInitialize() повертає false якщо hardware не підключено
```

В новій архітектурі `canInitialize()` повертає `true` завжди — реальна перевірка hardware тепер в `initialize()`. Checklist прямо суперечить шаблонам в цьому ж документі.

### 🔴 Проблема 4: `ConfigurableSensorPlugin` і `AsyncSensorPlugin` — старий підпис

Рядки 365 і 396:
```cpp
bool initialize() override {  // ← без PluginContext у обох!
```

Обидва приклади "складних випадків" не оновлені. `ConfigurableSensorPlugin` також читає конфіг через `SPIFFS.open()` напряму, а не через `ctx->config`.

### 🔴 Проблема 5: Debug logging приклад — вчить неправильному підходу

Рядки 452–456:
```cpp
void log(const char* msg) {
    if (debugMode) {
        Serial.printf("[%s] %s\n", getName(), msg);
    }
}
```

Після введення `ctx->log` цей приклад навчає антипатерну — власна реалізація логування замість системного Logger. Має бути `ctx->log->debug(getName(), msg)`.

### 🟡 Проблема 6: `testPlugin()` у Debugging — старий підпис `initialize()`

Рядок 494: `if (plugin->initialize())` — без PluginContext. Цей тестовий код не компілюватиметься з новим інтерфейсом.

### 🟡 Проблема 7: `plugin.json` без `contract_version`

У `PLUGIN_CONTRACT.md` (рядок 597) визначено що плагін вказує версію контракту:
```json
{"contract_version": "1.0.0"}
```

Але `plugin.json` шаблон у `CREATING_PLUGINS.md` (рядки 22–32) це поле не містить. Нові розробники не дізнаються про необхідність цього поля якщо не прочитають CONTRACT окремо.

---

## 9. COMPARISON.md

### Оцінка: 6/10 (без змін)

### ✅ Що залишилось добрим

Структура порівняння, приклади Git workflow з конкретними сценаріями, ROI розрахунок як методологічний підхід — все це добре написано для цільової аудиторії (менеджери). Зрозумілі діаграми `#ifdef hell` vs чиста структура плагінів.

### 🟡 Проблема 1: Цифри без методології — не виправлено з v1.0

Висновок документа:
```
-89% часу розробки, -90% ризику багів, +300% швидкість
```

Конкретні відсотки без джерел або методології — маркетинг, не технічний аргумент. Технічний архітектор або досвідчений менеджер поставиться скептично до всього документу. Проблема залишилась незмінною з першої ітерації.

**Рекомендація:** Або прибрати відсотки, або підкріпити конкретним прикладом що є в тому ж документі — він там вже є:
> "Додавання датчика QMC5883L: традиційно — 7 файлів (~4-6 год), Plugin System — 3 файли + 1 рядок конфігу (~30-60 хв). На 10 датчиків: 140 год → 15 год."

Конкретний вимірюваний приклад переконує набагато більше ніж `-89%`.

### 🟡 Проблема 2: `plugin.json` з `"dependencies": ["Wire"]` — анахронізм

Рядок 30 в прикладі `plugin.json`:
```json
"dependencies": ["Wire"]
```

Але `PLUGIN_CONTRACT.md` гарантує що `Wire` вже ініціалізований системою. Навіщо тоді плагін декларує `Wire` як залежність? Якщо `dependencies` означає бібліотеки PlatformIO для компіляції — це треба явно пояснити, бо зараз виглядає як суперечність з контрактом.

---

## 10. Головна системна проблема 2-ї ітерації

### Десинхронізація прикладів коду між документами

Введення `PluginContext` змінило підпис ключового методу:

```
До:    virtual bool initialize() = 0;
Після: virtual bool initialize(PluginContext* ctx) = 0;
```

Ця зміна оновлена **частково**. Стан по файлах:

| Файл | Статус `initialize()` | Критичні приклади |
|---|---|---|
| PLUGIN_ARCHITECTURE.md | ⚠️ Обидва варіанти | Два `IPlugin` в одному файлі |
| PLUGIN_INTERFACES_EXTENDED.md | ❌ Старий | HX711, ST7789V2 — без ctx |
| PLUGIN_CONTRACT.md | ✅ Новий | MinimalPlugin — але ctx в canInitialize() = UB |
| PLUGIN_DIAGNOSTICS.md | ⚠️ Обидва | LDC1101 виправлено, вступний блок — старий |
| CREATING_PLUGINS.md | ⚠️ Змішано | BH1750/I2C — нові, SPI/Async/Config — старі |

**Найпростіший спосіб виявити всі проблемні місця:** глобальний пошук по всіх файлах рядка `bool initialize() override` — кожне входження без `PluginContext*` є місцем що потребує виправлення.

### Суперечність `canInitialize()` — потребує архітектурного рішення

Існують дві несумісні версії `canInitialize()` в різних документах:

**Версія A** (PLUGIN_ARCHITECTURE.md, CREATING_PLUGINS.md — нові приклади):
```cpp
bool canInitialize() override {
    return true;  // Без перевірки hardware
}
```

**Версія B** (PLUGIN_CONTRACT.md — MinimalPlugin):
```cpp
bool canInitialize() override {
    ctx->wire->beginTransmission(0x48);  // UB — ctx не ініціалізовано
}
```

Версія B є помилкою. Архітектор має прийняти офіційне рішення і задокументувати його в `PLUGIN_CONTRACT.md` розділ 1.1:

> *"`canInitialize()` не виконує операцій з I2C/SPI шинами. Метод перевіряє тільки попередні умови без апаратних шин (наприклад наявність необхідної конфігурації). Реальна перевірка доступності hardware відбувається на початку `initialize()` через `ctx->wire`."*

---

## 11. Пріоритетний план дій

### Фаза 1 — Критичні виправлення (до написання будь-якого нового коду)

| # | Дія | Файл | Складність | Пріоритет |
|---|---|---|---|---|
| 1 | Прийняти архітектурне рішення щодо `canInitialize()` + ctx і задокументувати | `PLUGIN_CONTRACT.md` | Низька | 🔴 |
| 2 | Прибрати перший `IPlugin` без PluginContext (рядки 120–142) | `PLUGIN_ARCHITECTURE.md` | Низька | 🔴 |
| 3 | Виправити приклад `MinimalPlugin` — прибрати ctx з canInitialize() | `PLUGIN_CONTRACT.md` | Низька | 🔴 |
| 4 | Виправити SPI шаблон — прибрати `spi->begin()`, додати `ctx` | `CREATING_PLUGINS.md` | Низька | 🔴 |
| 5 | Виправити "Пораду №1" — `Wire.beginTransmission` в canInitialize() | `CREATING_PLUGINS.md` | Низька | 🔴 |
| 6 | Виправити recovery — `plugin->initialize(savedCtx)` або `plugin->recover()` | `PLUGIN_DIAGNOSTICS.md` | Середня | 🔴 |

### Фаза 2 — Синхронізація прикладів (паралельно з реалізацією ядра)

| # | Дія | Файл | Складність |
|---|---|---|---|
| 7 | Оновити HX711Plugin і ST7789V2Plugin — новий підпис initialize() | `PLUGIN_INTERFACES_EXTENDED.md` | Низька |
| 8 | Оновити `analyzeCoin()` — прибрати глобальний pluginSystem | `PLUGIN_INTERFACES_EXTENDED.md` | Середня |
| 9 | Оновити ConfigurableSensorPlugin і AsyncSensorPlugin | `CREATING_PLUGINS.md` | Низька |
| 10 | Оновити debug logging приклад — ctx->log замість Serial | `CREATING_PLUGINS.md` | Низька |
| 11 | Оновити `testPlugin()` — новий підпис initialize() | `CREATING_PLUGINS.md` | Низька |
| 12 | Виправити Checklist — canInitialize() не повертає false | `CREATING_PLUGINS.md` | Низька |
| 13 | Узгодити IInputPlugin API (pollEvent vs hasEvent/getEvent) | `PLUGIN_CONTRACT.md` або `PLUGIN_INTERFACES_EXTENDED.md` | Низька |

### Фаза 3 — Покращення якості документації

| # | Дія | Файл | Складність |
|---|---|---|---|
| 14 | Оновити lifecycle діаграму з усіма станами HealthStatus | `README.md` | Середня |
| 15 | Виправити глосарій — Service Locator як антипатерн | `README.md` | Низька |
| 16 | Оновити таблицю версій (v1.1.0) | `README.md` | Низька |
| 17 | Додати PLUGIN_CONTRACT.md в шляхи для Архітектора та QA | `README.md` | Низька |
| 18 | Прибрати Hot-Reload/OTA або додати застереження | `PLUGIN_ARCHITECTURE.md` | Низька |
| 19 | Оновити структуру файлів проекту в документації | `PLUGIN_ARCHITECTURE.md` | Низька |
| 20 | Замінити відсоткові цифри на конкретний приклад | `COMPARISON.md` | Низька |
| 21 | Додати `contract_version` до шаблону plugin.json | `CREATING_PLUGINS.md` | Низька |
| 22 | Виправити getGraphicsLibrary() `void*` → template | `PLUGIN_INTERFACES_EXTENDED.md` | Середня |
| 23 | Пояснити `"dependencies": ["Wire"]` — що це означає | `COMPARISON.md` | Низька |

---

## 12. Висновок

### Прогрес реальний, але нерівномірний

Друга ітерація зробила правильні кроки: `PLUGIN_CONTRACT.md` є відмінним новим документом, `CREATING_PLUGINS.md` Quick Start повністю синхронізований, `PLUGIN_DIAGNOSTICS.md` LDC1101 приклад — найкращий код у всій документації. Це показує що команда рухається в правильному напрямку.

### Основна проблема — неповне оновлення

Зміна підпису `initialize()` з `bool initialize()` на `bool initialize(PluginContext* ctx)` — це системна зміна що зачіпає кожен плагін і кожен приклад коду. Вона була впроваджена в частині документів але не в усіх. Результат: розробник отримує суперечливі шаблони з одного набору документів.

### Наступний крок

Перед початком кодування необхідно виконати **Фазу 1** (6 пунктів) — це максимум 0.5 дня роботи. Без цього перший розробник плагіна зустріне UB в `PLUGIN_CONTRACT.md` прикладі або скопіює SPI шаблон що порушує контракт.

Після Фази 1 документація досягне узгодженого стану і буде готова до передачі команді для реалізації.

---

*Документ підготовлено для передачі архітектору проекту.*  
*Версія звіту: 2.0.0 | Дата: 10 березня 2026*  
*Попередня версія: CoinTrace_Architecture_Review_v1.0.0.md*
