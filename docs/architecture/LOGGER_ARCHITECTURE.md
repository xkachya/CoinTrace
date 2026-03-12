# CoinTrace Logger: Архітектурний документ

**Тип документа:** Архітектурна специфікація  
**Версія:** 2.0.0  
**Попередня версія:** 1.0.0 (10 березня 2026) — виправлено за результатами рецензії  
**Дата:** 10 березня 2026  
**Контекст:** CoinTrace Plugin System, ESP32-S3 (M5Stack Cardputer-Adv, ESP32-S3FN8)  
**Статус:** ✅ Фаза 1 Implemented (11 березня 2026) — Serial + RingBuffer працюють на девайсі, 40 unit-тестів пройдено

---

## ⚠️ Виправлення відносно v1.0.0

### Баг 1: Рекурсивний deadlock в `dispatch()`

v1.0 форматував і dispatching робив під одним mutex. Якщо транспорт викликав `ctx->log->error()` всередині `write()` — той самий task намагався взяти вже захоплений `xSemaphoreCreateMutex()`. FreeRTOS блокується назавжди.

**Виправлення v2.0:**
1. Форматування виконується **до** захоплення mutex — у stack-local буфері.
2. Mutex захищає тільки dispatch (< 60 мкс).
3. `ILogTransport` отримує жорстке правило контракту: `write()` **НІКОЛИ** не викликає Logger методи.

### Баг 2: `SDTransport::write()` всередині mutex порушує NF1

v1.0 SDTransport писав на SD синхронно всередині dispatch loop поки mutex тримався. SD write = 1–5 мс — пряма суперечність з вимогою < 100 мкс.

**Виправлення v2.0:** SDTransport — повністю асинхронний (черга + FreeRTOS task), як WebSocketTransport. `write()` тільки кладе в чергу (< 5 мкс).

### Дублювання RingBuffer

v1.0 мав і `RingBuffer ringBuffer` всередині Logger, і `RingBufferTransport`. Два буфери, незрозумілий API.

**Виправлення v2.0:** `RingBuffer ringBuffer` і `getRecentEntries()` видалені з Logger. Єдине джерело — `RingBufferTransport`. Доступ до записів — безпосередньо через транспорт.

### `removeTransport()` — race condition

v1.0 не показував реалізацію. Без mutex — corruption масиву під час dispatch.

**Виправлення v2.0:** `removeTransport()` показаний повністю, обов'язково під mutex.

---

## Зміст

1. [Чому Logger — перший сервіс](#1-чому-logger—перший-сервіс)
2. [Вимоги](#2-вимоги)
3. [Архітектурний огляд](#3-архітектурний-огляд)
4. [Transport Layer: контракт і абстракція](#4-transport-layer-контракт-і-абстракція)
5. [Logger API та реалізація dispatch](#5-logger-api-та-реалізація-dispatch)
6. [Транспорти: детальна специфікація](#6-транспорти-детальна-специфікація)
7. [Log Entry структура і серіалізація](#7-log-entry-структура-і-серіалізація)
8. [Thread Safety і FreeRTOS](#8-thread-safety-і-freertos)
9. [Управління пам'яттю](#9-управління-памяттю)
10. [Конфігурація](#10-конфігурація)
11. [Рекомендована реалізація](#11-рекомендована-реалізація)
12. [Порядок імплементації](#12-порядок-імплементації)

---

## 1. Чому Logger — перший сервіс

Logger — єдиний сервіс що потрібен **до** всіх інших. Ініціалізація `Wire`, `SPI`,
плагінів, конфіг менеджера — кожен з цих кроків може впасти, і без Logger ця подія
або губиться в `Serial.println()` розкиданих по коду, або взагалі мовчки зникає.

В поточній кодовій базі CoinTrace є два паралельних механізми логування:
- `ctx->log->error/info/warning()` — в плагінах
- `Serial.printf/println()` — в `main.cpp`, `PluginSystem`, `loop()`

Це не просто неузгодженість стилю. Якщо пристрій відправляє дані на сайт —
`Serial.printf()` туди не потрапить. Якщо підключений SD Logger — `Serial.printf()`
не запишеться.

**Правило:** після ініціалізації Logger — `Serial.printf()` і `Serial.println()`
зникають з кодової бази назавжди. Всі повідомлення ідуть через `log->`.

---

## 2. Вимоги

### Функціональні

| # | Вимога |
|---|---|
| F1 | Підтримка рівнів: `DEBUG`, `INFO`, `WARNING`, `ERROR`, `FATAL` |
| F2 | Кожен запис містить: рівень, мітку часу (ms від boot), ім'я компонента, повідомлення |
| F3 | Форматування з `printf`-стилем (`log->info("LDC1101", "RP=%.2f, L=%d", rp, l)`) |
| F4 | Мінімум два одночасних транспорти (наприклад Serial + WebSocket) |
| F5 | Кожен транспорт має власний мінімальний рівень фільтрації |
| F6 | Thread-safe: виклики з будь-якого FreeRTOS task |
| F7 | Передача назовні (WebSocket, BLE, UART) без блокування main loop |
| F8 | Буферизація записів якщо транспорт тимчасово недоступний (WiFi reconnect) |
| F9 | Structured logging: опціональний JSON формат для машинної обробки |

### Нефункціональні

| # | Вимога |
|---|---|
| NF1 | Вплив на main loop: **< 100 мкс** на виклик `log->info()` при всіх транспортах |
| NF2 | RAM footprint: < 4 KB статично + кільцевий буфер (конфігурований, default 2 KB) |
| NF3 | Без динамічної аллокації в hot path (`log->info()` викликається ~50 разів/сек) |
| NF4 | Компіляційне вимкнення DEBUG рівня для production build (`#ifdef COINTRACE_DEBUG`) |
| NF5 | Graceful degradation: якщо транспорт недоступний — мовчить, не блокує Logger |

---

## 3. Архітектурний огляд

```
┌──────────────────────────────────────────────────────────────┐
│                      ВИКЛИК КОДУ                             │
│  ctx->log->info("BMI270", "Accel: %.2f g", ax)              │
└────────────────────────┬─────────────────────────────────────┘
                         │
                         ▼
┌──────────────────────────────────────────────────────────────┐
│                    Logger::dispatch()                        │
│                                                              │
│  1. Фільтр рівня (без mutex — read-only)                     │
│  2. vsnprintf в STACK-LOCAL буфер (без mutex!)               │
│  3. Побудувати LogEntry на стеку                             │
│  4. xSemaphoreTake(mutex, 5ms) — або drop якщо timeout       │
│  5. Пройти по транспортах → transport->write(entry)          │
│  6. xSemaphoreGive(mutex)                                    │
└──────┬───────────────────────────────────────────────────────┘
       │  Mutex тримається тільки під час кроків 4–6 (~60 мкс)
       │
       ├───────────────────┬───────────────────┬───────────────────┐
       ▼                   ▼                   ▼                   ▼
┌────────────┐  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐
│  Serial    │  │  WebSocket       │  │  RingBuffer      │  │  SD              │
│  Transport │  │  Transport       │  │  Transport       │  │  Transport       │
│            │  │                  │  │                  │  │                  │
│ Sync write │  │ → Queue → Task   │  │ → Ring в RAM     │  │ → Queue → Task   │
│ < 60 мкс  │  │ < 5 мкс          │  │ < 5 мкс          │  │ < 5 мкс          │
│            │  │                  │  │                  │  │                  │
│ КОНТРАКТ:  │  │ КОНТРАКТ:        │  │ КОНТРАКТ:        │  │ КОНТРАКТ:        │
│ НЕ викликає│  │ НЕ викликає      │  │ НЕ викликає      │  │ НЕ викликає      │
│ Logger::*  │  │ Logger::*        │  │ Logger::*        │  │ Logger::*        │
└────────────┘  └──────────────────┘  └──────────────────┘  └──────────────────┘
                        │                                           │
                        ▼                                           ▼
               ┌─────────────────┐                       ┌──────────────────┐
               │  Веб-сайт       │                       │  SD карта        │
               │  (JSON over WS) │                       │  (з spiMutex!)   │
               └─────────────────┘                       └──────────────────┘
```

### Ключові архітектурні рішення v2.0

**Форматування поза mutex.** `vsnprintf` виконується в stack-local буфер до захоплення
mutex. Mutex тримається тільки під час dispatch loop. Це скорочує критичну секцію
і унеможливлює рекурсивний deadlock.

**Контракт "NO LOG IN WRITE".** Будь-який транспорт що порушить це правило
(тобто викличе `Logger::*` з `write()`) — спричинить deadlock. Це правило має
бути перевірено code review і задокументоване в кожному транспорті.

**Timeout з drop замість portMAX_DELAY.** 5 мс timeout — якщо mutex недоступний,
запис скидається. Logger ніколи не блокує задачу на невизначений час.

**Async для повільних транспортів.** WebSocket, SD — всі з чергою + task.
SerialTransport — єдиний синхронний, час write < 60 мкс (прийнятно всередині mutex).

**Єдиний RingBuffer — тільки RingBufferTransport.** Немає дублювання. Код що
потребує доступу до логів (UI, тести) звертається до `RingBufferTransport` напряму.

---

## 4. Transport Layer: контракт і абстракція

```cpp
// include/ILogTransport.h
#pragma once
#include "LogEntry.h"

class ILogTransport {
public:
    virtual ~ILogTransport() = default;

    virtual bool        begin()  { return true; }
    virtual void        end()    {}

    // === КРИТИЧНИЙ КОНТРАКТ ===
    // write() НІКОЛИ не повинен:
    //   1. Викликати Logger::debug/info/warning/error/fatal()
    //   2. Викликати будь-який метод що веде до Logger::dispatch()
    //
    // Порушення → рекурсивний deadlock (Logger mutex вже захоплений).
    //
    // Для внутрішніх помилок транспорту → зберігати в errorCount/lastError,
    // НЕ логувати через Logger.
    //
    // write() НЕ ПОВИНЕН блокуватись довше 200 мкс.
    // Повільні транспорти (SD, WS) — кладуть в чергу і повертаються.
    virtual void write(const LogEntry& entry) = 0;

    virtual LogLevel    getMinLevel() const          { return minLevel; }
    virtual void        setMinLevel(LogLevel level)  { minLevel = level; }
    virtual const char* getName()  const = 0;
    virtual bool        isActive() const { return true; }

    // Кількість скинутих записів (через повну чергу або помилку)
    // НЕ логується через Logger — читається з зовні для діагностики
    virtual uint32_t getDroppedCount() const { return 0; }

protected:
    LogLevel minLevel = LogLevel::DEBUG;
};
```

---

## 5. Logger API та реалізація dispatch

### Заголовний файл

```cpp
// include/Logger.h
#pragma once
#include "ILogTransport.h"
#include "LogEntry.h"
#include <freertos/semphr.h>

#define LOGGER_MAX_TRANSPORTS      4
#define LOGGER_FORMAT_BUFFER_SIZE  256  // Stack-local — не член класу

class Logger {
public:
    Logger();
    ~Logger();

    // === Управління транспортами ===

    // Logger НЕ бере ownership transport*. Caller управляє lifetime.
    // Якщо transport->begin() повертає false — транспорт додається
    // але позначається inactive (isActive() == false).
    bool addTransport(ILogTransport* transport);

    // Thread-safe: може викликатись з будь-якого task.
    void removeTransport(ILogTransport* transport);

    void setGlobalMinLevel(LogLevel level);

    // === Logging API ===

    void debug  (const char* component, const char* fmt, ...);
    void info   (const char* component, const char* fmt, ...);
    void warning(const char* component, const char* fmt, ...);
    void error  (const char* component, const char* fmt, ...);
    void fatal  (const char* component, const char* fmt, ...);
    void log    (LogLevel level, const char* component, const char* fmt, ...);

    uint8_t getTransportCount() const;

private:
    ILogTransport*    transports[LOGGER_MAX_TRANSPORTS] = {};
    uint8_t           transportCount = 0;
    LogLevel          globalMinLevel = LogLevel::DEBUG;
    SemaphoreHandle_t mutex          = nullptr;

    void dispatch(LogLevel level, const char* component,
                  const char* fmt, va_list args);
};
```

### Реалізація dispatch (ключова частина)

```cpp
// lib/Logger/Logger.cpp

void Logger::dispatch(LogLevel level, const char* component,
                      const char* fmt, va_list args) {

    // 1. Глобальний фільтр — без mutex (read-only атомік)
    if (level < globalMinLevel) return;

    // 2. Форматування В СТЕК, до захоплення mutex
    //    Кожна задача форматує у ВЛАСНИЙ stack frame — без shared state.
    char localBuf[LOGGER_FORMAT_BUFFER_SIZE];
    int written = vsnprintf(localBuf, sizeof(localBuf), fmt, args);

    // Якщо повідомлення обрізане — позначити явно
    if (written >= (int)sizeof(localBuf)) {
        // Замінити останні 4 символи на "..." щоб читач побачив обрізання
        memcpy(localBuf + sizeof(localBuf) - 5, "...", 4);
    }

    // 3. LogEntry будується на стеку — без heap allocation
    LogEntry entry;
    entry.timestampMs = millis();
    entry.level       = level;
    strncpy(entry.component, component, sizeof(entry.component) - 1);
    entry.component[sizeof(entry.component) - 1] = '\0';
    strncpy(entry.message, localBuf, sizeof(entry.message) - 1);
    entry.message[sizeof(entry.message) - 1] = '\0';

    // 4. Mutex тільки навколо dispatch loop
    //    Timeout 5 мс: якщо система перевантажена — drop запису, не блокування.
    //    portMAX_DELAY ЗАБОРОНЕНИЙ: Logger ніколи не повинен зависати.
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        // Drop this entry — перевантаження або deadlock detection
        return;
    }

    // 5. Dispatch до транспортів
    //
    //    ІНВАРІАНТ: transports[i]->write() НЕ ВИКЛИКАЄ Logger::*
    //    (гарантовано контрактом ILogTransport)
    //    SerialTransport: sync, ~60 мкс — прийнятно всередині mutex
    //    Інші: async queue put, ~5 мкс
    for (uint8_t i = 0; i < transportCount; i++) {
        ILogTransport* t = transports[i];
        if (t && level >= t->getMinLevel()) {
            t->write(entry);
        }
    }

    xSemaphoreGive(mutex);
}
```

### removeTransport — thread-safe реалізація

```cpp
void Logger::removeTransport(ILogTransport* transport) {
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;

    for (uint8_t i = 0; i < transportCount; i++) {
        if (transports[i] == transport) {
            // Компактуємо масив під mutex — dispatch не ітерує в цей момент
            for (uint8_t j = i; j < transportCount - 1; j++) {
                transports[j] = transports[j + 1];
            }
            transports[--transportCount] = nullptr;
            break;
        }
    }

    xSemaphoreGive(mutex);
}
```

### Макроси для production build

```cpp
// include/logger_macros.h

#ifdef COINTRACE_DEBUG
  #define LOG_DEBUG(logger, comp, ...) (logger)->debug(comp, __VA_ARGS__)
#else
  #define LOG_DEBUG(logger, comp, ...) do {} while(0)  // Zero cost в release
#endif

#define LOG_INFO(logger, comp, ...)    (logger)->info(comp, __VA_ARGS__)
#define LOG_WARNING(logger, comp, ...) (logger)->warning(comp, __VA_ARGS__)
#define LOG_ERROR(logger, comp, ...)   (logger)->error(comp, __VA_ARGS__)
#define LOG_FATAL(logger, comp, ...)   (logger)->fatal(comp, __VA_ARGS__)
```

---

## 6. Транспорти: детальна специфікація

### 6.1 SerialTransport — синхронний, завжди перший

Єдиний синхронний транспорт. Write time ~60 мкс при 115200 baud для рядка
~80 символів — прийнятно всередині dispatch mutex.

```cpp
// lib/Logger/SerialTransport.h
class SerialTransport : public ILogTransport {
public:
    enum class Format { TEXT, JSON };

    // Print& замість HardwareSerial& — необхідно для ESP32-S3 з USB CDC:
    // на ESP32-S3 Serial має тип HWCDC (не HardwareSerial), але обидва
    // успадковують Print. Використання Print& забезпечує сумісність.
    explicit SerialTransport(Print&   serial   = Serial,
                             Format   format   = Format::TEXT,
                             uint32_t baudRate = 115200);

    bool        begin()  override;
    void        write(const LogEntry& entry) override;
    const char* getName() const override { return "Serial"; }

    // КОНТРАКТ: ця реалізація НЕ ВИКЛИКАЄ Logger::* — Serial.write() напряму

private:
    Print&   serial_;
    Format   format_;
    uint32_t baudRate_;
};
```

**TEXT формат** (для Serial Monitor / PlatformIO terminal):
```
[     0ms] INFO  System         | CoinTrace v1.0.0 starting
[    12ms] INFO  PluginSystem   | Loading 4 plugins...
[    45ms] INFO  LDC1101        | Initialized. CS=5, RESP_TIME=0x07
[    46ms] INFO  BMI270         | Initialized. 200Hz, ±8G
[    47ms] ERROR HX711          | I2C NACK — hardware not found
[    48ms] WARN  PluginSystem   | 1 plugin failed, continuing
[  1289ms] INFO  CoinAnalyzer   | RP=42180, L=31450, match=1UAH_2023 (0.94)
```

**JSON формат** (для парсингу сторонніми утилітами):
```json
{"t":45,"l":1,"c":"LDC1101","m":"Initialized. CS=5, RESP_TIME=0x07"}
```

---

### 6.2 WebSocketTransport — асинхронний, для веб-сайту

```cpp
// lib/Logger/WebSocketTransport.h
#include <AsyncWebSocket.h>

class WebSocketTransport : public ILogTransport {
public:
    explicit WebSocketTransport(AsyncWebSocket& wsServer,
                                uint16_t        queueSize = 64);

    bool        begin()  override;
    void        end()    override;
    void        write(const LogEntry& entry) override;  // Non-blocking: → queue
    const char* getName()  const override { return "WebSocket"; }
    bool        isActive() const override;  // true якщо є ≥1 підключений клієнт
    uint32_t    getDroppedCount() const override { return droppedCount_; }

    void startTask(uint8_t coreId = 0, UBaseType_t priority = 1);
    void stopTask();

    // КОНТРАКТ: write() НЕ ВИКЛИКАЄ Logger::*

private:
    AsyncWebSocket& ws_;
    QueueHandle_t   queue_      = nullptr;
    TaskHandle_t    taskHandle_ = nullptr;
    uint16_t        queueSize_;
    uint32_t        droppedCount_ = 0;

    static void taskFunc(void* param);

    // При переповненні черги: видаляємо найстаріший запис (FIFO drop)
    // droppedCount_ інкрементується — видно через getDroppedCount()
    void enqueue(const LogEntry& entry);
};
```

**Поведінка при різних станах:**

| Стан | Поведінка `write()` | `isActive()` |
|------|---------------------|--------------|
| Клієнт підключений | Черга → task → ws.textAll() | `true` |
| Клієнт відключений | Черга накопичується до queueSize | `false` |
| Черга повна | FIFO drop, `droppedCount_++` | `false` |
| При новому підключенні | Накопичені записи доставляються клієнту | `true` |

---

### 6.3 RingBufferTransport — in-memory, для UI і тестів

Єдине сховище логів в пам'яті. Замінює видалений `RingBuffer ringBuffer` з Logger.
Для доступу до записів (UI, `runDiagnostics()`, unit tests) — звертатись до
`RingBufferTransport` напряму, а не через Logger.

```cpp
// lib/Logger/RingBufferTransport.h
class RingBufferTransport : public ILogTransport {
public:
    explicit RingBufferTransport(uint16_t capacity = 100,
                                 bool     usePsram  = true);
    ~RingBufferTransport();

    bool        begin()  override;
    void        write(const LogEntry& entry) override;  // Thread-safe
    const char* getName() const override { return "RingBuffer"; }

    // === Читання (для UI, діагностики, тестів) ===

    // Thread-safe копія записів в buf.
    // Повертає кількість скопійованих записів.
    uint16_t getEntries(LogEntry* buf, uint16_t maxCount,
                        LogLevel  minLevel = LogLevel::DEBUG) const;

    uint16_t getCount()    const;
    void     clear();

    // Знайти останній ERROR/FATAL запис (для statusbar на дисплеї)
    bool getLastError(LogEntry& out) const;

    // КОНТРАКТ: write() НЕ ВИКЛИКАЄ Logger::*

private:
    LogEntry*         buffer_   = nullptr;
    uint16_t          capacity_ = 0;
    uint16_t          head_     = 0;
    uint16_t          count_    = 0;
    mutable SemaphoreHandle_t mutex_ = nullptr;
};
```

**Виділення пам'яті:**

```cpp
bool RingBufferTransport::begin() {
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) return false;

    // Спробувати PSRAM, fallback на звичайний heap
    if (usePsram_ && psramFound()) {
        buffer_ = (LogEntry*)ps_malloc(capacity_ * sizeof(LogEntry));
    }
    if (!buffer_) {
        buffer_ = (LogEntry*)malloc(capacity_ * sizeof(LogEntry));
    }
    if (!buffer_) {
        capacity_ = 0;  // Graceful degradation — транспорт активний але не зберігає
        return true;    // begin() не повертає false — Logger продовжує без storage
    }
    memset(buffer_, 0, capacity_ * sizeof(LogEntry));
    return true;
}
```

---

### 6.4 SDTransport — асинхронний, файл на SD карті

> **⚠️ Cardputer-Adv: SDTransport НЕ в Logger dispatch chain (Модель S)**
> SDTransport є reference implementation для інших платформ.
> Для Cardputer-Adv SD-архівування виконується через `LittleFSTransport::rotate()` —
> batch copy `log.1.jsonl` → SD при ротації (spi_vspi_mutex).
> `SDTransport::startTask()` **не викликається** в Cardputer-Adv setup.

**Критична інтеграційна деталь для Cardputer-Adv:**

На M5Stack Cardputer-Adv SD карта і LDC1101 **ділять одну SPI шину** (G40/G14/G39).
SDTransport background task **зобов'язаний** брати `spiMutex` перед кожним записом
на SD карту, так само як і `LDC1101Plugin` перед читанням датчика.

```cpp
// lib/Logger/SDTransport.h
#include <SD.h>
#include <freertos/semphr.h>

class SDTransport : public ILogTransport {
public:
    explicit SDTransport(const char*       filename     = "/logs/cointrace.log",
                         uint32_t          maxFileSizeKB = 512,
                         uint16_t          queueSize    = 32);

    bool        begin()  override;
    void        end()    override;  // Flush черги + close file
    void        write(const LogEntry& entry) override;  // Non-blocking: → queue
    const char* getName()  const override { return "SD"; }
    bool        isActive() const override;
    uint32_t    getDroppedCount() const override { return droppedCount_; }

    // spiMutex — той самий mutex що в PluginContext.
    // ОБОВ'ЯЗКОВИЙ якщо є будь-який SPI плагін з async task (LDC1101, тощо).
    // Якщо nullptr — SDTransport не бере mutex (тільки якщо SD пристрій на шині один).
    void startTask(SemaphoreHandle_t spiMutex,
                   uint8_t          coreId   = 0,
                   UBaseType_t      priority = 1);
    void stopTask();

    // КОНТРАКТ: write() НЕ ВИКЛИКАЄ Logger::*

private:
    const char*       filename_;
    uint32_t          maxFileSizeKB_;
    QueueHandle_t     queue_       = nullptr;
    TaskHandle_t      taskHandle_  = nullptr;
    uint16_t          queueSize_;
    uint32_t          droppedCount_ = 0;
    SemaphoreHandle_t spiMutex_    = nullptr;
    bool              sdReady_     = false;

    static void taskFunc(void* param);
    void processEntry(const LogEntry& entry);
    void rotate();

    // Буфер для batch write (змен. кількість фізичних write() до SD)
    char     writeBuf_[512];
    uint16_t writeBufLen_ = 0;

    void flushWriteBuf(File& file);
    void enqueue(const LogEntry& entry);
};
```

**Task реалізація з spiMutex:**

```cpp
void SDTransport::processEntry(const LogEntry& entry) {
    // Взяти spiMutex перед будь-яким зверненням до SD
    bool hasMutex = false;
    if (spiMutex_) {
        hasMutex = (xSemaphoreTake(spiMutex_, pdMS_TO_TICKS(50)) == pdTRUE);
        if (!hasMutex) {
            droppedCount_++;  // SD недоступна — drop і рухатись далі
            return;
        }
    }

    File file = SD.open(filename_, FILE_APPEND);
    if (file) {
        char line[256];
        entry.toText(line, sizeof(line));
        file.print(line);
        file.close();
    }

    if (hasMutex) {
        xSemaphoreGive(spiMutex_);
    }
}
```

---

### 6.5 BLETransport — асинхронний, для мобільного

```cpp
// lib/Logger/BLETransport.h
class BLETransport : public ILogTransport {
public:
    BLETransport(const char* serviceUUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E",
                 const char* charUUID    = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E",
                 uint16_t    queueSize   = 32);

    bool        begin()  override;
    void        write(const LogEntry& entry) override;  // Non-blocking: → queue
    const char* getName()  const override { return "BLE"; }
    bool        isActive() const override;
    uint32_t    getDroppedCount() const override { return droppedCount_; }

    void startTask(uint8_t coreId = 0);
    void stopTask();

    // КОНТРАКТ: write() НЕ ВИКЛИКАЄ Logger::*
    // Формат: компактний текст для економії BLE bandwidth:
    // "I|1234|BMI270|Accel: 0.12 g"

private:
    QueueHandle_t queue_       = nullptr;
    TaskHandle_t  taskHandle_  = nullptr;
    uint16_t      queueSize_;
    uint32_t      droppedCount_ = 0;
};
```

---

### 6.6 LittleFSTransport — асинхронний, hot-log на LittleFS_data

**CoinTrace-specific transport.** Є частиною Storage Architecture (STORAGE_ARCHITECTURE.md §12.1). Реалізує «гарячий» log-шар: останні 400 KB записів доступні без SD карти.

**Ключові проектні рішення:**

| Рішення | Обґрунтування |
|---|---|
| **Open-once pattern** | `lfs_file_open()` один раз при старті або після rotation. `lfs_file_sync()` після кожного запису. `lfs_file_close()` тільки при rotation або shutdown. Різниця: 4 flash write/entry (open+write+close+FAT) → 1 write/entry. |
| **`lfs_data_mutex_`** | Той самий mutex що у `MeasurementStore`. Охоплює весь rotation cycle (rename+copy+delete). WebServer timeout = 500 ms (збільшено від 100 ms через [FUTURE-1]). |
| **Rotation: 2 файли × 200 KB** | Variant A рішення [PRE-1]. Steady-state: ~100 LittleFS blocks для логів із загальних 448 доступних. |
| **SD copy при rotation** | При наявності SD: `log.1.jsonl` копіюється на `SD:/CoinTrace/logs/archive/<date>.jsonl` до видалення — cold archive. Якщо SD відсутня: rotation без копіювання (log.1 видаляється). |

```cpp
// lib/Logger/LittleFSTransport.h
#include <LittleFS.h>
#include <freertos/semphr.h>

class LittleFSTransport : public ILogTransport {
public:
    explicit LittleFSTransport(SemaphoreHandle_t lfsDataMutex,
                                uint32_t          maxLogKB   = 200,
                                uint16_t          queueSize  = 64);

    bool        begin()  override;  // Opens log.0.jsonl (append), keeps it open
    void        end()    override;  // lfs_file_sync() + lfs_file_close()
    void        write(const LogEntry& entry) override; // Non-blocking: → queue
    const char* getName()  const override { return "LittleFS"; }
    bool        isActive() const override;
    uint32_t    getDroppedCount() const override { return droppedCount_; }

    // SD-transport для cold-archive при rotation (optional).
    // Якщо nullptr — rotation виконується без копіювання на SD.
    void setSdArchive(const char* archivePath, SemaphoreHandle_t spiMutex);

    void startTask(uint8_t coreId = 0, UBaseType_t priority = 2);
    void stopTask();

    // КОНТРАКТ: write() НЕ ВИКЛИКАЄ Logger::*

private:
    static constexpr const char* LOG_CURRENT = "/logs/log.0.jsonl";
    static constexpr const char* LOG_ARCHIVE = "/logs/log.1.jsonl";

    SemaphoreHandle_t lfsDataMutex_;
    uint32_t          maxLogKB_;
    QueueHandle_t     queue_       = nullptr;
    TaskHandle_t      taskHandle_  = nullptr;
    uint16_t          queueSize_;
    uint32_t          droppedCount_ = 0;

    lfs_file_t        currentFile_;       // Тримається відкритим між write()
    bool              fileOpen_  = false;
    uint32_t          currentSizeBytes_ = 0;

    const char*       sdArchivePath_ = nullptr;
    SemaphoreHandle_t spiMutex_      = nullptr;

    static void taskFunc(void* param);
    void processEntry(const LogEntry& entry);
    void rotate();   // Тримає lfsDataMutex_ на весь цикл rename+copy+delete
    bool openCurrentFile();
};
```

**Task реалізація — open-once з rotation:**

```cpp
void LittleFSTransport::processEntry(const LogEntry& entry) {
    if (!fileOpen_ && !openCurrentFile()) {
        droppedCount_++;
        return;
    }

    char line[256];
    uint16_t len = entry.toJsonLine(line, sizeof(line));  // JSON Lines format

    xSemaphoreTake(lfsDataMutex_, portMAX_DELAY);
    lfs_file_write(&lfs_data, &currentFile_, line, len);
    lfs_file_sync(&lfs_data, &currentFile_);  // power-fail safe без close overhead
    currentSizeBytes_ += len;
    xSemaphoreGive(lfsDataMutex_);

    if (currentSizeBytes_ >= maxLogKB_ * 1024U) {
        rotate();
    }
}

void LittleFSTransport::rotate() {
    // Тримаємо mutex на весь цикл — атомарна операція для WebServer
    xSemaphoreTake(lfsDataMutex_, portMAX_DELAY);

    lfs_file_close(&lfs_data, &currentFile_);  // тільки тут!
    fileOpen_ = false;

    // Cold archive: copy log.1 → SD перед видаленням (якщо SD доступна)
    if (sdArchivePath_ && spiMutex_) {
        // SD copy під spiMutex_ (окремий mutex — немає вкладеності з lfsDataMutex_)
        // Реалізується через callback щоб не тягнути SD.h до Logger lib
        copyToSdArchive_();  // user-supplied callback via setSdArchive()
    }

    lfs_remove(&lfs_data, LOG_ARCHIVE);   // delete log.1 якщо є
    lfs_rename(&lfs_data, LOG_CURRENT, LOG_ARCHIVE);  // log.0 → log.1
    // log.0 тепер не існує — openCurrentFile() створить новий
    currentSizeBytes_ = 0;

    xSemaphoreGive(lfsDataMutex_);

    openCurrentFile();  // відкрити новий log.0.jsonl
}
```

> **⚠️ Порядок mutex при rotation з SD copy:**
> `lfsDataMutex_` береться першим і тримається весь rotation цикл.
> `spiMutex_` для SD copy береться і звільняється всередині, поки `lfsDataMutex_` утримується.
> Це не deadlock: жодна інша задача не бере `lfsDataMutex_` поки тримає `spiMutex_`
> (SDTransport та MeasurementStore беруть лише свій власний mutex).

**Ініціалізація у головному файлі:**

```cpp
// У StorageManager::init() або main setup():
static LittleFSTransport lfsTransport(lfs_data_mutex_, /*maxLogKB=*/200, /*queue=*/64);
lfsTransport.setSdArchive("SD:/CoinTrace/logs/archive", spi_vspi_mutex);
lfsTransport.startTask(/*core=*/0, /*prio=*/2);

Logger::init(serialTransport, ringTransport, lfsTransport);
// SDTransport для Logger НЕ використовується — SD archive керується LittleFSTransport при rotation
```

---

## 7. Log Entry структура і серіалізація

```cpp
// include/LogEntry.h
#pragma once
#include <stdint.h>

enum class LogLevel : uint8_t {
    DEBUG   = 0,
    INFO    = 1,
    WARNING = 2,
    ERROR   = 3,
    FATAL   = 4
};

// Фіксований розмір для RingBuffer без heap аллокації
struct LogEntry {
    uint32_t timestampMs;        // millis() від boot
    LogLevel level;
    char     component[20];      // Ім'я плагіна: "LDC1101", "BMI270", ...
                                 // Максимум 19 символів + null terminator
    char     message[192];       // Форматоване повідомлення (якщо довше — "...")

    // Розмір: 4 + 1 + 20 + 192 = 217 байт
    // RingBuffer(100) = ~21 KB — рекомендовано в PSRAM

    void toText(char* buf, uint16_t bufSize) const;
    void toJSON(char* buf, uint16_t bufSize) const;
    void toBLECompact(char* buf, uint16_t bufSize) const;

    static const char* levelToString(LogLevel l);  // "DEBUG", "INFO", ...
    static const char* levelToChar(LogLevel l);    // "D", "I", "W", "E", "F"
};
```

### Формати виводу

**TEXT** (Serial, SD):
```
[  1289ms] INFO  CoinAnalyzer   | RP=42180, L=31450, match=1UAH_2023 (0.94)
[  1290ms] ERROR LDC1101        | CHIP_ID mismatch: got 0x00, expected 0xD4
```

**JSON** (WebSocket):
```json
{"t":1289,"l":1,"c":"CoinAnalyzer","m":"RP=42180, match=1UAH_2023 (0.94)"}
{"t":1290,"l":3,"c":"LDC1101","m":"CHIP_ID mismatch: got 0x00, expected 0xD4"}
```

**BLE Compact**:
```
I|1289|CoinAnalyzer|RP=42180, match=1UAH_2023
E|1290|LDC1101|CHIP_ID mismatch
```

---

## 8. Thread Safety і FreeRTOS

### Чому попередня версія мала deadlock

```
Задача А: log->error("LDC1101", "fault")
  → dispatch()
  → xSemaphoreTake(mutex)          ← бере mutex
  → SDTransport::write()
    → SD.open() — впала!
    → SDTransport внутрішньо: log->error("SD", "open failed")  ← ПОМИЛКА!
      → dispatch()
      → xSemaphoreTake(mutex)      ← вже захоплений Задачею А → DEADLOCK
```

### Рішення v2.0: два рівні захисту

**Рівень 1 (контракт):** Кожен транспорт задокументований з явною забороною
викликати Logger. Транспорт зберігає помилки в `errorCount`/`lastError` — дані
читаються зовні, не логуються.

**Рівень 2 (timeout):** Навіть якщо контракт буде порушений — `pdMS_TO_TICKS(5)`
timeout в dispatch гарантує що Задача Б поверне `pdFALSE` за 5 мс і не зависне.
Запис при цьому — drop.

### Аналіз потоків в CoinTrace

```
Core 0 (main loop, PluginSystem::update()):
  └─ LDC1101Plugin::update()  → log->info()  ← ок
  └─ BMI270Plugin::update()   → log->info()  ← ок

Core 1 (якщо є async tasks):
  └─ AsyncPlugin::task()      → log->warning() ← ок (mutex вирішує конкуренцію)

Core 0 (FreeRTOS, background):
  └─ WebSocketTransport::task → ws.textAll()  ← БЕЗ виклику Logger
  └─ SDTransport::task        → SD.write()    ← БЕЗ виклику Logger, з spiMutex
```

Максимальне очікування на mutex при двох конкурентних задачах:
- Критична секція = dispatch loop = SerialTransport::write() ≈ 60 мкс
- Інші транспорти async — < 5 мкс кожен
- При 4 транспортах: ~70–80 мкс → NF1 (< 100 мкс) виконується

### Виклики з ISR

ISR не може брати FreeRTOS mutex. Для CoinTrace v1 ISR logging не критичний.
API закладено але без реалізації:

```cpp
// ISR-safe: тільки ERROR/FATAL, без форматування
// Майбутня реалізація через xQueueSendToBackFromISR()
void Logger::errorFromISR(const char* component, const char* msg);
```

---

## 9. Управління пам'яттю

### Відсутність heap allocation в hot path

| Місце | v1.0 | v2.0 |
|-------|------|------|
| `dispatch()` — форматування | `formatBuf` — член Logger (sync) | Stack-local `localBuf[256]` |
| `dispatch()` — LogEntry | Stack | Stack |
| `RingBuffer` | Член Logger + TransportBuffer | Тільки RingBufferTransport |

### Розподіл пам'яті

```
Logger статичні поля:          ~72 байт   (без formatBuf — він на стеку)
transports[4] pointers:         32 байт
mutex handle:                    8 байт
──────────────────────────────────────
Logger об'єкт:                ~112 байт

Stack per log->info() call:    256 байт   (localBuf, тимчасово)

RingBufferTransport(100):      ~21 KB     (PSRAM: 8 MB доступно)
WebSocketTransport queue(64):  ~14 KB     (PSRAM або heap)
SDTransport queue(32):          ~7 KB
BLETransport queue(32):         ~7 KB
──────────────────────────────────────
ВСЬОГО (4 транспорти):         ~49 KB
Якщо ring + WS + SD в PSRAM:  SRAM < 1 KB (тільки mutex і pointers)
```

### Logger::dispatch() stack usage

```cpp
// На стеку при кожному виклику log->info():
char localBuf[256];    // 256 байт
LogEntry entry;        // 217 байт
va_list args;          //   4 байт
──────────────────────
Всього на call stack:  ~480 байт
```

Стандартний FreeRTOS task stack = 4 KB. 480 байт на log call — прийнятно.

---

## 10. Конфігурація

```json
// data/logger.json
{
  "global_min_level": "DEBUG",
  "transports": {
    "serial": {
      "enabled": true,
      "baud_rate": 115200,
      "format": "TEXT",
      "min_level": "DEBUG"
    },
    "websocket": {
      "enabled": true,
      "path": "/logs",
      "queue_size": 64,
      "min_level": "INFO"
    },
    "ble": {
      "enabled": false,
      "queue_size": 32,
      "min_level": "WARNING"
    },
    "ring_buffer": {
      "enabled": true,
      "capacity": 100,
      "use_psram": true,
      "min_level": "DEBUG"
    },
    "sd": {
      "enabled": false,
      "path": "/logs/cointrace.log",
      "max_size_kb": 512,
      "queue_size": 32,
      "min_level": "INFO"
    }
  }
}
```

**Логіка фільтрації:** запис проходить транспорт якщо:
```
entry.level >= globalMinLevel  AND  entry.level >= transport.min_level
```

---

## 11. Рекомендована реалізація

### Ініціалізація в `main.cpp`

```cpp
// src/main.cpp

#include "Logger.h"
#include "SerialTransport.h"
#include "RingBufferTransport.h"
#include "WebSocketTransport.h"
#include "SDTransport.h"

// Глобальні об'єкти (Logger живе весь час роботи пристрою)
static Logger              gLogger;
static SerialTransport     gSerialTransport(Serial, SerialTransport::Format::TEXT);
static RingBufferTransport gRingTransport(100, /*usePsram=*/true);
static AsyncWebSocket      gWsSocket("/logs");
static WebSocketTransport  gWsTransport(gWsSocket, 64);
static SDTransport         gSdTransport("/logs/cointrace.log", 512, 32);

void setup() {
    Serial.begin(115200);

    // ═══════════════════════════════════════════════════
    // 1. Logger ініціалізується ПЕРШИМ — до будь-чого
    // ═══════════════════════════════════════════════════
    gSerialTransport.setMinLevel(LogLevel::DEBUG);
    gLogger.addTransport(&gSerialTransport);
    gLogger.addTransport(&gRingTransport);

    gLogger.info("System", "CoinTrace v1.0.0 starting");
    gLogger.info("System", "CPU: %d MHz, Heap: %d bytes, PSRAM: %d MB",
                 ESP.getCpuFreqMHz(),
                 ESP.getFreeHeap(),
                 ESP.getPsramSize() / 1024 / 1024);

    // 2. Config Manager
    ConfigManager config;
    if (!config.begin("/config.json")) {
        gLogger.warning("System", "Config not found, using defaults");
    }

    // 3. PluginContext з Logger і mutex-ами
    PluginContext ctx;
    ctx.log       = &gLogger;
    ctx.wire      = &Wire;
    ctx.spi       = &SPI;
    ctx.wireMutex = xSemaphoreCreateMutex();
    ctx.spiMutex  = xSemaphoreCreateMutex();
    ctx.config    = &config;

    if (!ctx.wireMutex || !ctx.spiMutex) {
        gLogger.fatal("System", "Failed to create bus mutex — halt");
        while (true) { delay(1000); }
    }

    // 4. WiFi (якщо налаштований)
    if (config.getBool("wifi.enabled", false)) {
        gLogger.info("WiFi", "Connecting to %s...", config.getString("wifi.ssid"));
        // ... підключення ...
        if (WiFi.isConnected()) {
            gLogger.addTransport(&gWsTransport);
            gWsTransport.startTask(/*coreId=*/0);
            gLogger.info("WiFi", "WebSocket logging active on /logs");
        } else {
            gLogger.warning("WiFi", "Connection failed, WebSocket transport skipped");
        }
    }

    // 5. SD Logger (якщо ввімкнений і SD доступна)
    if (config.getBool("sd_log.enabled", false)) {
        if (gSdTransport.begin()) {
            gLogger.addTransport(&gSdTransport);
            // spiMutex обов'язковий — SD і LDC1101 на одній SPI шині!
            gSdTransport.startTask(ctx.spiMutex, /*coreId=*/0);
            gLogger.info("System", "SD logging active");
        } else {
            gLogger.warning("System", "SD transport init failed, skipping");
        }
    }

    // 6. Plugin System
    gLogger.info("System", "Loading plugin system...");
    PluginSystem pluginSystem(ctx);
    pluginSystem.loadFromConfig(config);
    pluginSystem.initializeAll();

    gLogger.info("System", "CoinTrace Ready");
}
```

### Використання в плагіні

```cpp
// Стандартне використання:
ctx->log->info(getName(),    "Initialized. CS=%d, RESP_TIME=0x%02X", csPin, respTimeBits);
ctx->log->error(getName(),   "CHIP_ID mismatch: got 0x%02X, expected 0xD4", chipId);
ctx->log->warning(getName(), "Calibration baseline not set");

// З макросами (LOG_DEBUG зникає в release):
LOG_DEBUG(ctx->log, getName(), "SPI read reg=0x%02X val=0x%02X", reg, val);
LOG_INFO (ctx->log, getName(), "Measurement: RP=%d, L=%d", rpRaw, lRaw);
```

### Використання для UI (дисплей M5Stack)

```cpp
// Показати останні ERROR/FATAL на M5Stack display:
void showRecentErrors(RingBufferTransport& ring, IDisplayPlugin* display) {
    LogEntry entries[5];
    uint16_t count = ring.getEntries(entries, 5, LogLevel::ERROR);

    display->clear();
    for (uint16_t i = 0; i < count; i++) {
        char line[48];
        snprintf(line, sizeof(line), "[%s] %s",
                 entries[i].component, entries[i].message);
        display->drawText(0, i * 16, line);
    }
}

// Показати статус транспортів (для debug screen):
void showLoggerStatus(Logger& logger, RingBufferTransport& ring,
                      WebSocketTransport& ws, SDTransport& sd) {
    // Скинуті записи за сесію:
    Serial.printf("WS dropped: %u, SD dropped: %u\n",
                  ws.getDroppedCount(), sd.getDroppedCount());
}
```

### Unit test приклад

```cpp
// RingBufferTransport — для перевірки що плагін логує правильно:
void testLdc1101ErrorLogging() {
    Logger             testLogger;
    SerialTransport    serial;
    RingBufferTransport ring(20);

    testLogger.addTransport(&serial);
    testLogger.addTransport(&ring);

    PluginContext mockCtx;
    mockCtx.log = &testLogger;
    // ... mockCtx.spi = &mockSpi ...

    LDC1101Plugin plugin;
    plugin.initialize(&mockCtx);  // Очікуємо failure бо немає реального SPI

    LogEntry last;
    TEST_ASSERT_TRUE(ring.getLastError(last));
    TEST_ASSERT_EQUAL_STRING("LDC1101", last.component);
    // Перевірити що повідомлення містить "CHIP_ID"
    TEST_ASSERT_NOT_NULL(strstr(last.message, "CHIP_ID"));
}
```

---

## 12. Порядок імплементації

### Фаза 1 — MVP: Serial і RingBuffer ✅ DONE (11 березня 2026)

Замінює всі `Serial.printf()`, дає Logger в `PluginContext`.

| Крок | Файл | Що робити | Статус |
|------|------|-----------|--------|
| 1 | `lib/Logger/src/LogLevel.h` | `enum class LogLevel` | ✅ |
| 2 | `lib/Logger/src/LogEntry.h/cpp` | struct + `toText()` + `toJSON()` + `toBLECompact()` | ✅ |
| 3 | `lib/Logger/src/ILogTransport.h` | Інтерфейс + контракт NO-LOG-IN-WRITE | ✅ |
| 4 | `lib/Logger/src/SerialTransport.h/cpp` | Sync TEXT/JSON, використовує `Print&` (HWCDC сумісність) | ✅ |
| 5 | `lib/Logger/src/RingBufferTransport.h/cpp` | Ring з fallback на heap (Cardputer-Adv не має PSRAM) | ✅ |
| 6 | `lib/Logger/src/Logger.h/cpp` | `begin()` pattern (mutex не в конструкторі!), dispatch, removeTransport | ✅ |
| 7 | `src/main.cpp` | `gLogger.begin()` → `addTransport()` в `setup()` | ✅ |
| 8 | `lib/Logger/src/logger_macros.h` | `LOG_DEBUG` zero-cost макрос для production | ✅ |
| 9 | `test/test_log_entry/`, `test/test_ring_buffer/`, `test/test_logger/` | 40 unit-тестів, `pio test -e native-test` | ✅ |

> ⚠️ **Урок з імплементації:** `xSemaphoreCreateMutex()` ЗАБОРОНЕНО в конструкторі глобального об'єкта — FreeRTOS scheduler ще не запущений. Mutex створюється тільки в `Logger::begin()`, який викликається з `setup()`.

> ⚠️ **ESP32-S3 специфіка:** `Serial` на ESP32-S3 з `ARDUINO_USB_CDC_ON_BOOT=1` має тип `HWCDC`, а не `HardwareSerial`. Обидва успадковують `Print` — `SerialTransport(Print& serial)` вирішує невідповідність.

**Результат Фази 1:** всі повідомлення в одному місці, Serial виводить `[  672ms] INFO  System | CoinTrace 1.0.0-dev starting`, Ring доступний для UI і тестів.

### Фаза 2 — WebSocket + SD (1–2 дні, після WiFi і SD)

| Крок | Файл | Примітка |
|------|------|----------|
| 1 | `lib/Logger/WebSocketTransport.cpp` | Черга + task на Core 0 |
| 2 | `lib/Logger/SDTransport.cpp` | Черга + task + **spiMutex інтеграція** |
| 3 | `data/www/logs.html` | Мінімальний dashboard (HTML нижче) |
| 4 | `main.cpp` | Активувати транспорти після WiFi/SD init |

### Фаза 3 — BLE (опціонально)

| Крок | Файл | Коли потрібно |
|------|------|---------------|
| 1 | `lib/Logger/BLETransport.cpp` | Якщо потрібен debug без WiFi |

---

## Додаток: Мінімальний HTML dashboard

```html
<!DOCTYPE html>
<html>
<head>
  <title>CoinTrace Logs</title>
  <style>
    body { background: #111; color: #eee; font-family: monospace; font-size: 13px; }
    table { width: 100%; border-collapse: collapse; }
    th { background: #222; padding: 4px 8px; text-align: left; }
    td { padding: 2px 8px; border-bottom: 1px solid #222; }
    .D { color: #666; } .I { color: #4f4; } .W { color: #fa0; }
    .E { color: #f44; } .F { color: #f0f; font-weight: bold; }
    #status { color: #888; padding: 8px; }
    #filter { background: #222; color: #eee; border: 1px solid #444; padding: 4px; }
  </style>
</head>
<body>
  <div style="display:flex;gap:16px;align-items:center;padding:8px;background:#1a1a1a">
    <b>CoinTrace Logs</b>
    <span id="status">Connecting...</span>
    <select id="filter" onchange="applyFilter()">
      <option value="0">All (DEBUG+)</option>
      <option value="1">INFO+</option>
      <option value="2" selected>WARNING+</option>
      <option value="3">ERROR+</option>
    </select>
    <span id="dropped" style="color:#fa0"></span>
    <button onclick="clearTable()"
            style="background:#333;color:#eee;border:1px solid #555;padding:4px 8px">
      Clear
    </button>
  </div>
  <table>
    <thead><tr><th>Time</th><th>Level</th><th>Component</th><th>Message</th></tr></thead>
    <tbody id="log-body"></tbody>
  </table>
  <script>
    const LEVELS = ['D','I','W','E','F'];
    const LNAMES = ['DEBUG','INFO','WARN','ERROR','FATAL'];
    let minLevel = 2;
    let rows = [];
    const MAX_ROWS = 500;

    const ws = new WebSocket(`ws://${location.host}/logs`);
    const status  = document.getElementById('status');
    const dropped = document.getElementById('dropped');

    ws.onopen    = () => { status.textContent = 'Connected ✓'; status.style.color='#4f4'; };
    ws.onclose   = () => {
        status.textContent = 'Disconnected — reconnecting...';
        status.style.color = '#f44';
        setTimeout(() => location.reload(), 3000);
    };
    ws.onerror   = () => { status.textContent = 'Error'; status.style.color='#f44'; };
    ws.onmessage = (e) => {
        const entry = JSON.parse(e.data);
        // Спеціальні службові повідомлення від пристрою
        if (entry.dropped !== undefined) {
            dropped.textContent = `Dropped: ${entry.dropped}`;
            return;
        }
        rows.push(entry);
        if (rows.length > MAX_ROWS) rows.shift();
        if (entry.l >= minLevel) appendRow(entry);
    };

    function appendRow(entry) {
        const cls = LEVELS[entry.l];
        const tr  = document.createElement('tr');
        tr.className = cls;
        tr.innerHTML = `<td>${entry.t}ms</td><td>${LNAMES[entry.l]}</td>
                        <td>${entry.c}</td><td>${entry.m}</td>`;
        const tbody = document.getElementById('log-body');
        tbody.insertBefore(tr, tbody.firstChild);
        while (tbody.rows.length > MAX_ROWS) tbody.deleteRow(-1);
    }

    function applyFilter() {
        minLevel = parseInt(document.getElementById('filter').value);
        clearTable();
        rows.filter(e => e.l >= minLevel).slice(-MAX_ROWS).forEach(appendRow);
    }

    function clearTable() {
        document.getElementById('log-body').innerHTML = '';
    }
  </script>
</body>
</html>
```

---

## Зведена таблиця змін v1.0 → v2.0

| # | Зміна | Причина |
|---|-------|---------|
| 1 | `formatBuf` видалено з Logger — stack-local в dispatch | Усуває shared state, зменшує critical section |
| 2 | Mutex timeout 5 мс замість portMAX_DELAY | Logger не може зависати нескінченно |
| 3 | Контракт `ILogTransport::write()` — NO LOG IN WRITE | Усуває рекурсивний deadlock (Баг 1) |
| 4 | SDTransport — async черга + task + spiMutex | Усуває порушення NF1 (Баг 2), SPI safety |
| 5 | `RingBuffer ringBuffer` видалено з Logger класу | Усуває дублювання, єдине джерело |
| 6 | `removeTransport()` — показаний під mutex | Усуває race condition при видаленні |
| 7 | `getDroppedCount()` в ILogTransport | Видимість drop-ів без логування через Logger |
| 8 | SDTransport отримує `spiMutex` в startTask() | Cardputer-Adv: SD і LDC1101 на одній шині |

---

*Документ підготовлено для архітектора та імплементора Logger.*  
*Версія: 2.0.0 | Дата: 10 березня 2026*  
*Попередня версія: CoinTrace_Logger_Architecture.md (v1.0.0)*
