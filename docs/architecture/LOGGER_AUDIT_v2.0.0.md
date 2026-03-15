# Logger Architecture — Незалежний Архітектурний Аудит

**Документ:** LOGGER_AUDIT_v2.0.0.md  
**Аудитована версія:** LOGGER_ARCHITECTURE.md v2.0.0  
**Дата аудиту:** 2026-03-12  
**Методологія:** Spec-vs-implementation gap analysis · FreeRTOS execution simulation · JSON correctness modeling · Stack & memory budget verification · Type layout analysis · Mutex ordering verification  
**Контекст:** Проводиться після завершення Storage audits (v1.2.0 + v1.3.0). Logger є єдиним реалізованим модулем (Phase 1 ✅). Мета — виявити розбіжності між специфікацією та реалізацією, та підготувати Logger до Phase 2 (WebSocketTransport, SDTransport) до початку їх реалізації.

---

## Зміст

1. [Загальна оцінка](#1-загальна-оцінка)
2. [Score-карта знахідок](#2-score-карта-знахідок)
3. [Module A — Sample Code Bug Propagation (B-01)](#3-module-a--sample-code-bug-propagation-b-01)
4. [Module B — Truncation Contract Violation](#4-module-b--truncation-contract-violation)
5. [Module C — JSON Injection in toJSON()](#5-module-c--json-injection-in-tojson)
6. [Module D — LogEntry sizeof() Misrepresentation](#6-module-d--logentry-sizeof-misrepresentation)
7. [Module E — LOGGER_MAX_TRANSPORTS vs Transport Count](#7-module-e--logger_max_transports-vs-transport-count)
8. [Module F — removeTransport() Missing Null-Guard](#8-module-f--removetransport-missing-null-guard)
9. [Module G — addTransport() Thread Safety Contract](#9-module-g--addtransport-thread-safety-contract)
10. [Module H — Minor Issues (LOW)](#10-module-h--minor-issues-low)
11. [Що архітектура зробила ПРАВИЛЬНО](#11-що-архітектура-зробила-правильно)
12. [Pre-implementation Checklist — перед Phase 2](#12-pre-implementation-checklist--перед-phase-2)
13. [Backlog](#13-backlog)
14. [Черга незалежних аудитів — інші документи](#14-черга-незалежних-аудитів--інші-документи)

---

## 1. Загальна оцінка

Logger Phase 1 (SerialTransport + RingBufferTransport) реалізований якісно. FreeRTOS-патерни коректні, контракт `ILogTransport::write()` витриманий, тести повністю покривають happy path. Проте виявлені **10 знахідок** (1 критична, 2 HIGH, 4 MEDIUM, 3 LOW).

**Ключова спостереження:**
- Жодна знахідка не є регресією у вже реалізованій фазі — Phase 1 в production **не крашиться**
- Знахідки LA-2 і LA-3 стають **активними багами** при реалізації Phase 2 (WebSocket/LittleFS), де очікується маркер "..." у повідомленнях і valid JSON
- LA-1/LA-6/LA-7 — latent defects, що проявляться при нетипових сценаріях (pre-begin call, concurrent setup)

**Блокуючі для Phase 2:** LA-2 · LA-3 · LA-5 (перевірка capacity) — **мають бути закриті до написання WebSocketTransport**

---

## 2. Score-карта знахідок

| ID | Модуль | Знахідка | Severity | Статус |
|---|---|---|---|---|
| **LA-1** | LOGGER_ARCH §11 / main.cpp | B-01 `usePsram=true` propagates from sample code into main.cpp | 🔴 CRITICAL | ✅ Виправлено v2.1.1 |
| **LA-2** | `dispatch()` / `LogEntry.h` | Truncation marker `"..."` ніколи не потрапляє в `entry.message[]` | 🟠 HIGH | ✅ Виправлено v2.1.1 |
| **LA-3** | `LogEntry::toJSON()` | JSON injection — `"` та `\` в message/component не escape-яться | 🟠 HIGH | ⚠️ Spec контракт задокументовано; impl Phase 2 |
| **LA-4** | `LogEntry.h` / §9 Memory | `sizeof(LogEntry) = 220`, специфікація каже 217 (tail padding проігнорований) | 🟡 MEDIUM | ✅ Виправлено v2.1.1 |
| **LA-5** | `Logger.h` / вся spec | `LOGGER_MAX_TRANSPORTS = 4`, spec описує 6 транспортів — обґрунтування відсутнє | 🟡 MEDIUM | ✅ Виправлено v2.1.1 |
| **LA-6** | `removeTransport()` | Null-guard `mutex_` відсутній — crash якщо викликати до `begin()` | 🟡 MEDIUM | ✅ Виправлено v2.1.1 |
| **LA-7** | `addTransport()` | Non-thread-safe без mutex, контракт "тільки з setup()" не задокументований | 🟡 MEDIUM | ✅ Виправлено v2.1.1 |
| **LA-8** | `setGlobalMinLevel()` | Atomic dependency на Xtensa LX7 byte ops — не задокументована | 🟢 LOW | ✅ Виправлено v2.1.1 |
| **LA-9** | mutex timeouts | Асиметрія 5 ms / 10 ms без пояснення в специфікації | 🟢 LOW | ✅ Виправлено v2.1.1 |
| **LA-10** | `platformio.ini` | `-DCOINTRACE_DEBUG` відсутній → `LOG_DEBUG()` по-op в **усіх** builds | 🟢 LOW | ✅ Виправлено v2.1.1 |

---

## 3. Module A — Sample Code Bug Propagation (B-01)

### LA-1 🔴 CRITICAL — `usePsram=true` у §11 sample поширив B-01 в main.cpp

**Метод:** cross-reference между spec §11 sample та `src/main.cpp:16`, `boards/m5cardputer-adv.json`

`LOGGER_ARCHITECTURE.md §11` "Рекомендована реалізація" містить:
```cpp
static RingBufferTransport gRingTransport(100, /*usePsram=*/true);
```

Цей зразковий код дослівно скопійований у `src/main.cpp`:
```cpp
// main.cpp:16
static RingBufferTransport gRingTransport(100, /*usePsram=*/true);
```

**Проблема:** ESP32-S3FN8 (`m5cardputer-adv.json`) **не має PSRAM**. `psramFound()` завжди повертає `false`.

**Trace виконання:**
```
RingBufferTransport::begin():
  usePsram_=true → psramFound() → false       [PSRAM відсутній]
  → buffer_ = nullptr після ps_malloc()
  → fallback: buffer_ = malloc(100 × 220)      [22 KB з internal SRAM]
  → SRAM budget: ~250 KB free після FreeRTOS
  → malloc() → succeeds, але from wrong pool
  → capacity_ = 0? NI — capacity_ залишається 100, зберігаємо в heap
```

Код **не крашиться** завдяки коректному fallback у `RingBufferTransport::begin()`. Але:
1. `malloc(22 KB)` з internal SRAM замість коментованого PSRAM — контракт пам'яті порушений
2. `LOGGER_ARCHITECTURE.md §11` — авторитетний зразок коду для всіх майбутніх транспортів → поширює помилку
3. Storage audit вже зафіксував B-01 як окреме знахідку — це **друге джерело** тієї самої помилки

**Зв'язок:** Storage FUTURE-SA2-1 (platformio/boards fix) і цей LA-1 — одна і та сама помилка, різні точки входу.

**Правка spec:** У §11 sample замінити `usePsram=true` → `usePsram=false` і додати коментар з посиланням на апаратне обмеження платформи.

---

## 4. Module B — Truncation Contract Violation

### LA-2 🟠 HIGH — Маркер `"..."` ніколи не з'являється в `LogEntry::message[]`

**Метод:** data flow analysis, `Logger.cpp::dispatch()` повний шлях

`LogEntry.h` коментар:
```cpp
char message[192];  // formatted message, truncated with "..." if longer
```

`LOGGER_ARCHITECTURE.md §9`:
> "повідомлення обрізається з `'...'` якщо більше 192 символів"

**Реальний код `dispatch()`:**
```cpp
char localBuf[256];
int written = vsnprintf(localBuf, sizeof(localBuf), fmt, args);

// Маркер ставиться в localBuf якщо written >= 256:
if (written >= (int)sizeof(localBuf)) {
    memcpy(localBuf + sizeof(localBuf) - 5, "...", 4);  // позиція 251
}

// Потім message будується через strncpy з обрізанням до 191 символу:
strncpy(entry.message, localBuf, sizeof(entry.message) - 1);  // max 191 chars
entry.message[sizeof(entry.message) - 1] = '\0';
```

**Аналіз всіх гілок:**

| Довжина форматованого рядку | Стан `localBuf` | Стан `entry.message` | Маркер |
|---|---|---|---|
| ≤ 191 | повністю | повністю | не потрібен ✓ |
| 192–255 | **повністю** (≤ 255) | **обрізано на 191** | **відсутній** ✗ |
| ≥ 256 | "..." на позиції 251 | **обрізано на 191** | **відсутній** ✗ |

**Висновок:** маркер `"..."` ставиться на позицію 251 у `localBuf`, але `strncpy` копіює лише перших 191 символів. Маркер **ніколи** не потрапляє в `entry.message`. Контракт `LogEntry.h` **завжди** порушений для повідомлень > 191 символів.

**Симуляція:**
```
logger.info("Sensor", "RP=%d, L=%d, freq=%.3f, temp=%.2f, [measurement #%d of batch %d]",
            42180, 15200, 1000.0f, 25.3f, 47, 100);
// formatted = "RP=42180, L=15200, freq=1000.000, temp=25.30, [measurement #47 of batch 100]"
//           = ~80 chars → повністю, no "..." — OK

// Але:
logger.error("LDC1101", "SPI read failed on register 0x%02x after %d retries "
             "during measurement cycle %d/%d (freq=%dHz, timeout=%dms, "
             "last_valid_ts=%lu, reason=%s)", ...);
// formatted = 195 chars
// localBuf[0..194] = повне повідомлення, written=195 < 256 → "..." НЕ ставиться
// entry.message[0..190] = перші 191 символів, message[191]='\0'
// Результат: обрізане повідомлення БЕЗ маркера → читач не знає що текст неповний
```

**Правка:** замінити логіку з `localBuf` на `entry.message`:
```cpp
strncpy(entry.message, localBuf, sizeof(entry.message) - 1);
entry.message[sizeof(entry.message) - 1] = '\0';
// Якщо рядок обрізаний — позначити явно:
if (written >= (int)sizeof(entry.message)) {
    memcpy(entry.message + sizeof(entry.message) - 4, "...", 4);  // позиція 188
}
```

---

## 5. Module C — JSON Injection in toJSON()

### LA-3 🟠 HIGH — Сирі рядки без escape поламають WebSocketTransport

**Метод:** JSON RFC 8259 §7 validation, data flow через `toJSON()` → WebSocket клієнт

**Код `LogEntry.cpp::toJSON()`:**
```cpp
snprintf(buf, bufSize,
    "{\"t\":%u,\"l\":%u,\"c\":\"%s\",\"m\":\"%s\"}\n",
    (unsigned int)timestampMs,
    (unsigned int)static_cast<uint8_t>(level),
    component,   // ← raw string, no escaping
    message);    // ← raw string, no escaping
```

**Concrete injection paths:**

| Input | toJSON() output | JSON парсер |
|---|---|---|
| `component = "LDC\"1101"` | `"c":"LDC"1101"` | `SyntaxError` |
| `message = "path\\file"` | `"m":"path\file"` | `SyntaxError` (unknown escape) |
| `message = "progress\n50%"` | literal newline всередині string | `SyntaxError` |
| `message = "ok"} {injected` | `"m":"ok"} {injected` | JSON injection / broken frame |

**Реалістичний trace:**
```
// Плагін логує шлях до файлу:
logger.info("Storage", "Writing to %s", "/data/sessions\\2026-03-12");

// dispatch() → vsnprintf → message = "Writing to /data/sessions\2026-03-12"
// toJSON() → {"t":1500,"l":1,"c":"Storage","m":"Writing to /data/sessions\2026-03-12"}
//                                                                          ^ literal \2
// JavaScript: JSON.parse(...) → SyntaxError: Bad escaped character

// WebSocketTransport drop frame → silent log loss, no indication to user
```

**Поточний статус:** `SerialTransport` використовує тільки `toText()` — Phase 1 **не зачеплена**. Але `toJSON()` вже написана і буде використана WebSocketTransport (Phase 2) і BLETransport (Phase 3) без змін.

**Правка:** додати escape-функцію перед інтерполяцією `%s` полів. Мінімальний набір для JSON RFC 8259:
- `"` → `\"`
- `\` → `\\`
- `\n` → `\n` (буквальні два символи)
- `\r` → `\r`
- символи з кодами 0x00–0x1F → `\uXXXX`

---

## 6. Module D — LogEntry sizeof() Misrepresentation

### LA-4 🟡 MEDIUM — `sizeof(LogEntry) = 220`, документ вказує 217

**Метод:** C++ struct layout analysis, Xtensa LX7 GCC ABI

**Декларація `LogEntry`:**
```cpp
struct LogEntry {
    uint32_t timestampMs;   // offset 0,  size 4, align 4
    LogLevel level;         // offset 4,  size 1, align 1  (uint8_t enum)
    char     component[20]; // offset 5,  size 20, align 1
    char     message[192];  // offset 25, size 192, align 1
};                          // natural size = 217 bytes
                            // struct alignment = max(4,1,1,1) = 4
                            // tail padding = (4 − 217%4) % 4 = 3 bytes
                            // sizeof(LogEntry) = 220 bytes
```

**Коментар у `LogEntry.h`:**
```cpp
// Size: 4 + 1 + 20 + 192 = 217 bytes.
// RingBufferTransport(100) = ~21 KB — use PSRAM on ESP32-S3.
```

**Дельта:**

| Параметр | Spec/comment | Реальне значення |
|---|---|---|
| `sizeof(LogEntry)` | 217 | **220** |
| `RingBuffer(100)` heap | 21,700 B ≈ 21.2 KB | **22,000 B ≈ 21.5 KB** |
| Різниця | — | +300 B (1.4%) |

**Прямий вплив — зараз:** відносно невеликий. Різниця 300 B не є проблемою для SRAM budget.

**Прямий вплив — Phase 3/4 (LittleFSTransport, SDTransport):** якщо серіалізація буде через `memcpy(&entry, sizeof(LogEntry))` і клієнт очікує 217 байт — reads зміщуватимуться на 3 байти. Для текстових форматів (`toJSON`, `toText`) — не впливає.

**Примітка:** `__attribute__((packed))` усунув би padding, але зnieможливив би DMA-доступ до `uint32_t timestampMs` на Xtensa. Поточна структура без `packed` є **архітектурно коректною** — тільки коментар хибний.

**Правка:** оновити коментар у `LogEntry.h` і §9: `sizeof(LogEntry) = 220 bytes (217 natural + 3 tail padding, GCC ABI)`.

---

## 7. Module E — LOGGER_MAX_TRANSPORTS vs Transport Count

### LA-5 🟡 MEDIUM — Ліміт 4 vs 6 описаних транспортів без пояснення

**Де:** `Logger.h:7`, вся специфікація §6

```cpp
#define LOGGER_MAX_TRANSPORTS 4
```

Специфікація описує шість транспортів: SerialTransport, WebSocketTransport, RingBufferTransport, SDTransport, BLETransport, LittleFSTransport.

**Матриця конфігурацій:**

| Режим | Транспорти | Кількість |
|---|---|---|
| Production | Serial + RingBuffer + SDTransport + LittleFSTransport | **4** ✓ |
| Development | Serial + RingBuffer + WebSocket + LittleFSTransport | **4** ✓ |
| BLE mode | Serial + RingBuffer + BLE | **3** ✓ |
| Hypothetical "all" | Serial + Ring + WS + SD + BLE + LittleFS | **6** ✗ |

**Висновок:** ліміт 4 достатній для **всіх розумних production конфігурацій**. Але специфікація цього **ніде не пояснює** — ні таблиці підтримуваних комбінацій, ні ADR, ні коментаря в `Logger.h`. Наступний розробник може вирішити додати 5-й транспорт і отримати мовчазне `return false` з `addTransport()`.

Тест `test_logger_addTransport_max_four` явно перевіряє відмову на 5-му транспорті, але тест — це не документація архітектурного рішення.

**Правка:** додати в `Logger.h` коментар до `LOGGER_MAX_TRANSPORTS` з обґрунтуванням і таблицею підтримуваних конфігурацій у §4.

---

## 8. Module F — removeTransport() Missing Null-Guard

### LA-6 🟡 MEDIUM — `removeTransport()` крашиться якщо `begin()` не викликаний

**Де:** `Logger.cpp::removeTransport()`

**Код:**
```cpp
void Logger::removeTransport(ILogTransport* transport) {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE) return;  // ← mutex_ може бути nullptr!
    // ...
}
```

**Еквівалентна функція з захистом (dispatch()):**
```cpp
void Logger::dispatch(...) {
    // ...
    if (!mutex_) return;  // ← null-guard є!
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) != pdTRUE) return;
    // ...
}
```

**FreeRTOS поведінка `xSemaphoreTake(nullptr, ...)`:**  
На ESP32-S3 з активованим `configASSERT`: `assert(pxSemaphore)` → `abort()` → watchdog reset. Без `configASSERT` — невизначена поведінка (читання з nullptr → Data Abort exception).

**Симуляція:**
```
// Помилковий сценарій: teardown у reverse порядку, або unit-тест без begin()
Logger logger;
MockTransport t;
logger.addTransport(&t);   // mutex_ ще nullptr → транспорт доданий, але небезпечно
logger.removeTransport(&t);  // ← crash або assert! mutex_ == nullptr
```

**Правка:**
```cpp
void Logger::removeTransport(ILogTransport* transport) {
    if (!mutex_) return;  // ← додати цей рядок
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE) return;
    // ...
}
```

---

## 9. Module G — addTransport() Thread Safety Contract

### LA-7 🟡 MEDIUM — `addTransport()` не під mutex, контракт не задокументований

**Де:** `Logger.cpp::addTransport()`, `Logger.h`

**Код:**
```cpp
bool Logger::addTransport(ILogTransport* transport) {
    if (!transport || transportCount_ >= LOGGER_MAX_TRANSPORTS) return false;
    transport->begin();                           // ← поза mutex
    transports_[transportCount_++] = transport;  // ← non-atomic read-modify-write
    return true;
}
```

**Ризик race condition:**
```
Task A: addTransport(&tFirst)
  reads transportCount_ = 0
  [Task B preempts]
Task B: addTransport(&tSecond)
  reads transportCount_ = 0   ← те саме значення!
  transports_[0] = tSecond
  transportCount_++ = 1
Task A (resumes):
  transports_[0] = tFirst     ← OVERWRITES tSecond!
  transportCount_++ = 1       ← рахунок = 1, але два слоти використані!
```

**Реальна ймовірність:** низька — специфікація неявно передбачає виклик лише з `setup()`. Але контракт `Logger.h` API не містить жодного попередження:
```cpp
bool addTransport(ILogTransport* transport);  // ← без анотації про thread safety
```

**Два варіанти правки:**
1. **Документаційна (мінімальна):** додати у `Logger.h`: `// MUST be called only from setup(), before xTaskCreate()`.
2. **Захисна (рекомендована):** взяти mutex в `addTransport()` аналогічно до `removeTransport()`.

---

## 10. Module H — Minor Issues (LOW)

### LA-8 🟢 LOW — Atomic dependency `setGlobalMinLevel()` не задокументована

`dispatch()` читає `globalMinLevel_` без mutex:
```cpp
if (level < globalMinLevel_) return;  // comment: "read-only"
```
`setGlobalMinLevel()` пише без mutex:
```cpp
void Logger::setGlobalMinLevel(LogLevel level) { globalMinLevel_ = level; }
```

Коректно на Xtensa LX7 — `uint8_t` read/write атомарні. Але §8 Thread Safety не задокументовує цю залежність. При порті на RISC-V або Arm Cortex-M4 (без byte atomicity) — data race без змін коду.

**Правка:** прояснити в §8: *"globalMinLevel_ — `uint8_t`, читається/пишеться без mutex; атомарність гарантує Xtensa LX7 ISA. При порті на інший SoC — перевірити byte atomicity."*

---

### LA-9 🟢 LOW — Асиметрія mutex timeout 5 ms / 10 ms

| Операція | Timeout | Обґрунтування в spec |
|---|---|---|
| `dispatch()` | 5 ms | ✅ є (§8: drop, non-blocking) |
| `removeTransport()` | 10 ms | ❌ відсутнє |
| `RingBuffer::write()` | 5 ms | ✅ є (паралельно з dispatch) |
| `RingBuffer::clear()` | 10 ms | ❌ відсутнє |
| `RingBuffer::getEntries()` | 10 ms | ❌ відсутнє |

**Симуляція:** `removeTransport()` тримає Logger mutex 10 ms → sensor task (dispatch, 5 ms timeout) → timeout → log drop. Довший timeout `removeTransport` гарантовано спричиняє log drops під час hot-swap. Якщо це свідома поступка — потрібно пояснити в §8.

---

### LA-10 🟢 LOW — `-DCOINTRACE_DEBUG` відсутній у `platformio.ini`

`logger_macros.h`:
```cpp
#ifdef COINTRACE_DEBUG
  #define LOG_DEBUG(logger, comp, ...) (logger)->debug(...)
#else
  #define LOG_DEBUG(logger, comp, ...) do {} while(0)  // ← no-op ЗАВЖДИ
#endif
```

`platformio.ini` (всі три env: `cointrace-dev`, `cointrace-production`, `cointrace-test`) — жоден не містить `-DCOINTRACE_DEBUG=1` у `build_flags`. Отже `LOG_DEBUG()` є мертвим кодом **у всіх конфігураціях**, включно з dev-збіркою.

---

## 11. Що архітектура зробила ПРАВИЛЬНО

### ✅ FreeRTOS patterns — відмінна якість

- Mutex створюється в `begin()`, не в конструкторі — правильний підхід для global static objects до `vTaskStartScheduler()`
- `dispatch()` форматує до `xSemaphoreTake` — кожен task форматує в свій stack frame, нульовий shared state до mutex
- 5 ms timeout з silent drop — жодного deadlock-потенціалу

### ✅ NO-LOG-IN-WRITE контракт

`ILogTransport::write()` contract (`// КРИТИЧНИЙ КОНТРАКТ`) — явно задокументований і включений у header файл, де кожен розробник транспорту **неминуче** його побачить. Це найкраще місце для такого контракту.

### ✅ Graceful degradation у `RingBufferTransport::begin()`

```cpp
if (!buffer_) {
    capacity_ = 0;  // buffer fail → transport active but write() is no-op
    return true;    // НЕ false — Logger продовжує працювати без ring
}
```

Транспорт не зупиняє систему при нестачі пам'яті — правильна поведінка для embedded.

### ✅ Idempotent `Logger::begin()`

```cpp
if (mutex_) return true;  // вже ініціалізовано
```

Безпечно викликати двічі — немає ризику подвійного створення mutex.

### ✅ Pre-mutex formatting

Stack-local `localBuf[256]` + stack-local `LogEntry` — нульове heap allocation в критичному шляху. Єдиний heap allocation — mutex через `xSemaphoreCreateMutex()` в `begin()`.

### ✅ Test coverage

Тести покривають: `begin()`, `addTransport()` max limit, null transport, dispatch to all transports, timestamp mock, `globalMinLevel` filter, per-transport filter, truncation, `removeTransport()`, Ring integration + `getLastError()`. Хороший baseline.

---

## 12. Pre-implementation Checklist — перед Phase 2

Ці знахідки **мають бути закриті** до початку реалізації `WebSocketTransport` та `SDTransport`:

| # | Знахідка | Дія | Priority |
|---|---|---|---|
| P2-1 | **LA-2** — truncation marker | Перенести `"..."` logic з `localBuf` в `entry.message` | 🔴 BLOCKER |
| P2-2 | **LA-3** — JSON injection | Додати JSON escape helper у `LogEntry::toJSON()` | 🔴 BLOCKER |
| P2-3 | **LA-1** — B-01 в §11 | Виправити sample code `usePsram=false` + виправити `main.cpp:16` | 🟠 HIGH |
| P2-4 | **LA-6** — null-guard | Додати `if (!mutex_) return;` у `removeTransport()` | 🟠 HIGH |
| P2-5 | **LA-10** — LOG_DEBUG | Додати `-DCOINTRACE_DEBUG=1` до `[env:cointrace-dev]` `build_flags` | 🟡 MEDIUM |
| P2-6 | **LA-7** — addTransport doc | Додати thread safety annotation у `Logger.h` | 🟡 MEDIUM |

Знахідки LA-4, LA-5, LA-8, LA-9 — не блокують Phase 2, але мають бути закриті до першого production build.

---

## 13. Backlog

| ID | Знахідка | Пріоритет | До якої фази |
|---|---|---|---|
| LF-FUTURE-1 | LA-4: `sizeof(LogEntry)=220` → оновити коментар і §9 | LOW | До P-3 |
| LF-FUTURE-2 | LA-5: `LOGGER_MAX_TRANSPORTS=4` → документувати конфігураційну таблицю | LOW | До P-3 |
| LF-FUTURE-3 | LA-7: `addTransport()` → взяти mutex або додати thread-safety doc | MEDIUM | До Phase 2 start |
| LF-FUTURE-4 | LA-8: `setGlobalMinLevel()` → додати SoC-specific atomicity note | LOW | До P-3 |
| LF-FUTURE-5 | LA-9: mutex timeout rationale → задокументувати в §8 | LOW | До P-3 |
| **LF-FUTURE-6** | A-04 (Wave7_P3 external audit 2026-03-15): LittleFSTransport `processEntry()` та `rotate()` мають `portMAX_DELAY` — порушення SA2-8 ("portMAX_DELAY ЗАБОРОНЕНИЙ"). Spec виправлено: `pdMS_TO_TICKS(200)` / `pdMS_TO_TICKS(1000)` з graceful drop/skip. Impl залишається. | HIGH | P-3 (до першого build) |
| **LF-FUTURE-7** | A-05 (Wave7_P3 external audit 2026-03-15): LittleFSTransport `write()` не має null-guard на `queue_`; base `isActive()` повертає `true` за замовчуванням → транспорт вважається активним між ctor і `startTask()`. Impl: `if (!queue_) { droppedCount_++; return; }` + override `isActive()` → `return queue_ != nullptr && taskRunning_`. | MEDIUM | P-3 (до першого build) |

---

## 14. Черга незалежних аудитів — інші документи

Нижче наведені всі аудитовані та ще неаудитовані архітектурні документи з оцінкою пріоритету та ризику.

### Вже проведені аудити

| Документ | Версія | Аудит | Результат |
|---|---|---|---|
| `STORAGE_ARCHITECTURE.md` | v1.3.2 | STORAGE_AUDIT_v1.2.0.md + STORAGE_AUDIT_v1.3.0.md | ✅ 21 знахідка, всі закриті |
| `LOGGER_ARCHITECTURE.md` | v2.0.0 | **Цей документ** | 🔄 10 знахідок, всі відкриті |

---

### Черга аудитів (пріоритизована)

#### 🔴 Tier 1 — Критичний (блокує Phase 2 / зачіпає production дані)

**#1 · `FINGERPRINT_DB_ARCHITECTURE.md` v1.4.0**

- **Ризик:** Найвищий у всій кодобазі. Документ сам застерігає: *"Рішення в цьому документі не можна змінити після появи першого external contributor"*. Backward incompatible зміна схеми = мовчаза несумісність fingerprints по всьому community.
- **Фокус аудиту:** schema versioning (три типи версій в одному полі), frozen physical constants vs calibration drift, git-merge conflicts у community DB, raw measurements precedence contract, migration strategy gaps.
- **Моделювання:** `firmware_v1 + db_v1_record` → `firmware_v2` → correctness. Cross-contributor measurement merge simulation.
- **Доки для перехресного аналізу:** `STORAGE_ARCHITECTURE.md` (де фізично живуть записи), `LDC1101_ARCHITECTURE.md` (чи є calibration offset у measurement).
- **Коли:** **до першого реального збору тренувальних даних**

---

**#2 · `CONNECTIVITY_ARCHITECTURE.md` v1.2.0**

- **Ризик:** WebSocket / HTTP протокол визначає API контракт між пристроєм і клієнтами. Після першого зовнішнього клієнта — breaking change дорого коштує.
- **Фокус аудиту:** WiFi STA → AP fallback state machine completeness, WebSocket disconnect / reconnect під час активного вимірювання, HTTP endpoint versioning strategy, BLE MTU fragmentation для довгих JSON пакетів (toJSON injection — пов'язано з LA-3), одночасна робота WiFi + BLE (radio coexistence на ESP32-S3).
- **Моделювання:** WiFi drop під час SD write → LittleFSTransport buffer overflow simulation. BLE notification MTU vs `LogEntry::toBLECompact()` size.
- **Доки для перехресного аналізу:** `LOGGER_ARCHITECTURE.md §6.2` (WebSocketTransport), `STORAGE_ARCHITECTURE.md` (SD write під час WiFi activity), `LA-3` цього аудиту (JSON injection).
- **Коли:** **до реалізації WebSocketTransport (Phase 2)**

---

#### 🟠 Tier 2 — Важливий (блокує стабільну роботу hardware)

**#3 · `LDC1101_ARCHITECTURE.md` v1.0.0**

- **Ризик:** Primary sensor. SPI timing, concurrent access через `spiMutex`, register configuration — помилки тут = невірні fingerprints назавжди.
- **Фокус аудиту:** SPI bus arbitration completeness (хто ще використовує VSPI крім LDC1101 і SD?), register write sequence vs datasheet SNOSD01D timing requirements, interrupt vs polling tradeoffs, INTB pin handling під FreeRTOS, sensor state machine (ACTIVE_CONVERSION → SLEEP → WAKE) concurrency.
- **Моделювання:** конкурентний доступ SD write + LDC1101 read під час coin measurement. SPI mutex priority inversion (LDC1101 prio=5 заблокований SD prio=2).
- **Доки для перехресного аналізу:** `STORAGE_ARCHITECTURE.md` (spiMutex contract, §8), `FINGERPRINT_DB_ARCHITECTURE.md` (які параметри вимірюються і зберігаються).
- **Коли:** до реалізації LDC1101Plugin

---

**#4 · `PLUGIN_ARCHITECTURE.md` v1.0.0 + `PLUGIN_CONTRACT.md` + `PLUGIN_DIAGNOSTICS.md`**

- **Ризик:** Фундаментальний extensibility contract. Якщо plugin lifecycle (init → measure → sleep → wake) некоректно специфікований — кожен новий плагін буде мати той самий клас багів.
- **Фокус аудиту:** `IPlugin::initialize()` return type vs error handling, plugin teardown під FreeRTOS task suspension, config JSON schema validation, `HealthStatus` enum completeness vs реальні failure modes LDC1101/HX711/BMI270, plugin dependency injection (Logger*, StorageManager*) — lifetime warranties.
- **Моделювання:** plugin panic (nullptr dereference) під час active measurement → system recovery path. Config JSON parse failure → boot degraded mode.
- **Доки для перехресного аналізу:** `PLUGIN_INTERFACES_EXTENDED.md`, `LDC1101_ARCHITECTURE.md`, `STORAGE_ARCHITECTURE.md` (де зберігається plugin state).
- **Коли:** до реалізації першого плагіна (LDC1101Plugin)

---

#### 🟡 Tier 3 — Корисний (coverage перед production release)

**#5 · `PLUGIN_INTERFACES_EXTENDED.md`**

- **Фокус:** ISensor, IStorage, IIMU interfaces — повнота контрактів. Особливо: чи є ISensor::measure() re-entrant? Чи IStorage::write() може бути викликано з ISR?
- **Коли:** разом або відразу після #4

**#6 · `CREATING_PLUGINS.md`**

- **Фокус:** Developer-facing documentation accuracy. Чи sample code компілюється? Чи описані всі обов'язкові методи IPlugin? Чи відповідає `plugin.json` schema реальній системі?
- **Коли:** до першого external contributor (після #4)

**#7 · `COMPARISON.md`**

- **Фокус:** обґрунтування вибору (порівняння альтернатив) — чи не застаріли обґрунтування після прийнятих ADR? Чи відповідають висновки поточному стану проекту?
- **Коли:** low priority, перед public release

---

### Зведена таблиця пріоритетів аудитів

| # | Документ | Tier | Коли | Залежність від |
|---|---|---|---|---|
| 1 | `FINGERPRINT_DB_ARCHITECTURE.md` | 🔴 Critical | До першого реального збору даних | — |
| 2 | `CONNECTIVITY_ARCHITECTURE.md` | 🔴 Critical | До Phase 2 (WebSocket) | LA-3 цього аудиту |
| 3 | `LDC1101_ARCHITECTURE.md` | 🟠 Important | До LDC1101Plugin impl | Storage audit done ✅ |
| 4 | `PLUGIN_ARCHITECTURE.md` + CONTRACT + DIAGNOSTICS | 🟠 Important | До першого плагіна | LDC1101 audit (#3) |
| 5 | `PLUGIN_INTERFACES_EXTENDED.md` | 🟡 Useful | Разом з #4 | #4 |
| 6 | `CREATING_PLUGINS.md` | 🟡 Useful | До external contributor | #4 |
| 7 | `COMPARISON.md` | 🟢 Low | Pre-public release | — |
