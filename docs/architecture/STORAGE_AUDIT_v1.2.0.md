# Storage Architecture — Незалежний Архітектурний Аудит

**Документ:** STORAGE_AUDIT_v1.2.0.md  
**Аудитована версія:** STORAGE_ARCHITECTURE.md v1.2.0 (commit `5c32633`)  
**Дата аудиту:** 2026-03-12  
**Методологія:** Formal space budget simulation · FreeRTOS concurrency modeling · Boot state machine verification · API invariant analysis  
**Статус:** ✅ [PRE-1]..[PRE-8] закрито · STORAGE_ARCHITECTURE.md оновлено до v1.3.0 (commit `5973c11`) · ✅ [PRE-9(SHOULD)] · ✅ [PRE-10, PRE-11] закрито (commit `2b7d9c9`) · Перехід до P-3 розблоковано · Другий незалежний аудит → **STORAGE_AUDIT_v1.3.0.md**

---

## Зміст

1. [Загальна оцінка](#1-загальна-оцінка)
2. [Score-карта знахідок](#2-score-карта-знахідок)
3. [Module 1 — LittleFS Space Budget Simulation](#3-module-1--littlefs-space-budget-simulation)
4. [Module 2 — FreeRTOS Concurrency Modeling](#4-module-2--freertos-concurrency-modeling)
5. [Module 3 — Boot Sequence State Machine Verification](#5-module-3--boot-sequence-state-machine-verification)
6. [Module 4 — Ring Buffer & API Invariants](#6-module-4--ring-buffer--api-invariants)
7. [Module 5 — Logger & LittleFSTransport Design](#7-module-5--logger--littlefstransport-design)
8. [Module 6 — NVS Design Issues](#8-module-6--nvs-design-issues)
9. [Module 7 — Додаткові знахідки](#9-module-7--додаткові-знахідки)
10. [Що архітектура зробила ПРАВИЛЬНО](#10-що-архітектура-зробила-правильно)
11. [🚀 Pre-implementation Checklist — що вирішити ДО P-3](#11--pre-implementation-checklist--що-вирішити-до-p-3)
12. [🔮 Backlog — що вирішити в майбутньому](#12--backlog--що-вирішити-в-майбутньому)

---

## 1. Загальна оцінка

Документ STORAGE_ARCHITECTURE v1.2.0 є зрілим для pre-implementation stage. Архітектура демонструє глибоке розуміння embedded trade-offs: write-first invariant, двостороння partition isolation, RAII mutex без heap alloc, коректний coredump boot order після виправлень v1.1.0→v1.2.0.

Виявлено **21 знахідку** (1 критична, 7 високих, 8 середніх, 5 низьких).

**✅ [PRE-1] Вирішено (Variant A, v1.3.0):** Критичний блокер LittleFS space budget закритий — log rotation скорочено до 2×200 KB. Steady-state: 435/448 blocks, margin 52 KB (~3%). Перехід до P-3 розблоковано. Решта 20 знахідок: [PRE-2]..[PRE-8] специфіковано в v1.3.0; [PRE-9]..[PRE-11] відкриті SHOULD; 9 знахідок у Backlog.

---

## 2. Score-карта знахідок

| ID | Модуль | Знахідка | Severity | Статус |
|---|---|---|---|---|
| **M1-C1** | Space Budget | Measurements block overhead: 300×4KB=1200KB vs. 300×700B=210KB | 🔴 CRITICAL | ✅ PRE-1 (v1.3.0) |
| **M2-H1** | Concurrency | spi_vspi_mutex scope в SDTransport охоплює лише `write()`, не `open→write→close` | 🟠 HIGH | ✅ PRE-3 (v1.3.0) |
| **M2-H2** | Concurrency | LDC1101 BUSY polling всередині spi_vspi_mutex scope: до 2 с блокування SDTransport | 🟠 HIGH | PRE-9 (SHOULD) |
| **M3-H1** | Boot | GPIO0 [1.5]: `display()` викликається до ініціалізації LCD (крок [9]) | 🟠 HIGH | ✅ PRE-4 (v1.3.0) |
| **M3-H2** | Boot | GPIO0 [1.5]: `delay(5000)` = TWDT default timeout → reset race | 🟠 HIGH | ✅ PRE-4 (v1.3.0) |
| **M4-H1** | API | `GET /measure/{id}` — відсутня перевірка `id < meas_count - RING_SIZE` → silent wrong data | 🟠 HIGH | ✅ PRE-2 (v1.3.0) |
| **M4-H2** | API | `ring_used` після Soft Reset з видаленням файлів = 300, реально = 0 | 🟠 HIGH | ✅ PRE-5 (v1.3.0) |
| **M5-H1** | Logger | open/close per log entry = 4× flash write overhead vs. keep-open+sync патерн | 🟠 HIGH | ✅ PRE-8 (v1.3.0) |
| **M2-M1** | Concurrency | `LOG_ERROR` у `LittleFSDataGuard` ctor викликає повний dispatch chain (включно SD) під час самого contention | 🟡 MEDIUM | FUTURE-2 (Backlog) |
| **M3-M1** | Boot | `LittleFS_data.format()` на [1.5] — неясна реалізація до `mountData()` (крок [4]) | 🟡 MEDIUM | ✅ PRE-4 (v1.3.0) |
| **M3-M2** | Boot | Нумерація boot sequence пропускає [6]; розробники можуть вважати це помилкою | 🟡 MEDIUM | ✅ PRE-6 (v1.3.0) |
| **M3-M3** | Boot | Coredump filename `<ts>.json` = epoch timestamp після power cycle → перезапис попереднього файлу | 🟡 MEDIUM | ✅ PRE-7 (v1.3.0) |
| **M4-M1** | NVS | `log_gen: UInt8` в NVS namespace `storage` — роль не описана, ймовірно артефакт | 🟡 MEDIUM | ✅ PRE-11 (v1.3.1) |
| **M5-M1** | Logger | Log rotation (4 rename+delete) тримає `lfs_data_mutex_` до 200 ms > WebServer timeout 100 ms | 🟡 MEDIUM | FUTURE-1 (Backlog) |
| **M6-M1** | NVS | `incrementMeasCount()` — TOCTOU: два окремі NVS calls, безпечно тільки з одного task | 🟡 MEDIUM | ✅ PRE-10 (v1.3.1) |
| **M7-M1** | Misc | Hard Reset: `lfs_data.format()` (крок b) не зупиняє LittleFSTransport bg task → pending writes у відформатований FS | 🟡 MEDIUM | FUTURE-4 (Backlog) |
| **M6-L1** | NVS | Немає `nvs_keys` partition reserve; додавання NVS encryption post-v1 = breaking change | 🟢 LOW | Backlog |
| **M7-L1** | Misc | `device_id` з 2 байтів MAC = 65 536 унікальних ID → birthday collision ~68% при 500 пристроях | 🟢 LOW | Backlog |
| **M7-L2** | Misc | SD hot-insertion не специфікована: SD вставлена post-boot → Matching (Deep) недоступний до reboot | 🟢 LOW | Backlog |
| **M7-L3** | Misc | OTA rollback + RING_SIZE зміна між версіями → out-of-bounds slot calculation не специфіковано | 🟢 LOW | Backlog |
| **M7-L4** | Misc | Hard Reset не очищає coredump partition → [7.5] після reboot зберігає стару coredump у щойно відформатований LittleFS | 🟢 LOW | Backlog |

---

## 3. Module 1 — LittleFS Space Budget Simulation

### M1-C1 🔴 CRITICAL — Block Granularity Overhead

**Метод аналізу:** блочна модель esp_littlefs

У `esp_littlefs` (Arduino ESP32 порт) файлова система виділяє пам'ять блоками розміром 4096 байт. Критичний параметр:
```
lfs_config.block_size = 4096 bytes   (= flash sector size)
lfs_config.prog_size  = 256  bytes
lfs_config.inline_max = min(block_size/2, prog_size) = 256 bytes
```

**Правило:** файл > 256 bytes отримує щонайменше один власний data block (4 KB), незалежно від свого логічного розміру.

#### Симуляція steady-state (повне навантаження LittleFS_data):

| Компонент | Файлів | Логічний розмір | Flash блоків | Flash байт |
|---|---|---|---|---|
| `measurements/` (ring повний) | 300 × ~700 B | ~210 KB | **300 блоків** (1 блок/файл) | **1200 KB** |
| `logs/log.0.jsonl` | 1 × 300 KB | 300 KB | ceil(300/4)+1 meta = 76 | 304 KB |
| `logs/log.1.jsonl` | 1 × 300 KB | 300 KB | 76 блоків | 304 KB |
| `logs/log.2.jsonl` | 1 × 300 KB | 300 KB | 76 блоків | 304 KB |
| `cache/index.json` | 1 × 80 KB | 80 KB | ceil(80/4)+1 = 21 | 84 KB |
| Directory metadata | — | — | ~10 блоків | 40 KB |
| LittleFS superblock + lookahead | — | — | ~4 блоки | 16 KB |
| **TOTAL** | | **~1490 KB логічно** | **563 блоки** | **2252 KB** |

**Partition capacity: 448 blocks = 1792 KB**

```
OVERFLOW: 563 блоки needed vs. 448 available
         = 115 блоків = 460 KB перевищення (~26%)
```

Документ правильно рахує логічні байти (210 KB для вимірів), але **не враховує block granularity**. Кожен 700-байтовий файл `>256 bytes` займає повний 4 KB блок. У steady-state filesystem переповнюється.

#### Варіанти вирішення:

| Варіант | Зміна | Результат (blocks) | Ring=300? | Інвазивність |
|---|---|---|---|---|
| **A — Скоротити логи** | 2×200 KB замість 3×300 KB | ~420 блоків ≈ OK | ✅ | Мінімальна |
| **B — Зменшити RING_SIZE** | 150 вимірів | ~413 блоків ≈ OK | ❌ | Середня |
| **C — JSONL для вимірів** | 1 файл замість 300 | ~53+912+84=~262 блоки | ✅ | Значна (random access ускладнено) |
| **D — Збільшити partition** | 0x1C0000 → 0x220000 (2.125 MB) | 531 блоків, OK | ✅ | Потребує оновлення partition table |

**Рекомендація:** Варіант A (2×200 KB логи) — найменш інвазивний. Перед фіналізацією: виміряти `lfs_info()` на реальному прототипі для уточнення.

---

## 4. Module 2 — FreeRTOS Concurrency Modeling

### Task Priority Matrix

```
┌──────────────────────┬────────┬──────────────┬─────────────────────┐
│ Task                 │ Prio   │ lfs_data_mtx │ spi_vspi_mtx        │
├──────────────────────┼────────┼──────────────┼─────────────────────┤
│ MainLoop             │ 3 High │ write m_XXX  │ hold LDC SPI burst  │
│ LittleFSTransport bg │ 2 Norm │ append log   │ via SDTransport     │
│ WebServer handler    │ 1 Low  │ read m_XXX   │ via SDTransport     │
└──────────────────────┴────────┴──────────────┴─────────────────────┘
```

**Deadlock analysis:** жоден task не намагається взяти обидва mutex одночасно в одному scope. `spi_vspi` та `lfs_data` не мають зворотного acquisition order. **Deadlock risk = 0.** ✅

---

### M2-H1 🟠 HIGH — spi_vspi_mutex scope недостатній у SDTransport

Документ: "mutex per write" для `SDTransport`. Але `SD.h` для одного append виконує:
```
SD.open(path, FILE_APPEND)   ← ~5 SPI transactions
file.write(data)             ← N SPI transactions
file.close()                 ← ~3 SPI transactions (FAT update)
```

Якщо mutex береться тільки на один `file.write()`, SPI bus частково вільний між `open→write` та `write→close`. LDC burst може вклинитись між FAT metadata транзакціями → **SD filesystem corruption**.

**Потрібно:** `spi_vspi_mutex` охоплює повний `open → write → close` як атомарну операцію.

---

### M2-H2 🟠 HIGH — LDC1101 BUSY polling всередині mutex scope

Якщо `ldc.readRpL()` включає polling LDC1101 `BUSY` сигналу (GPIO6) до завершення settling time:

```
spi_vspi_mutex held для: SPI clock phase (~µs) + LDC settling (~ms..500ms)
```

При `BUSY = 500 ms × 4 виміри = 2 секунди` де SDTransport повністю заблокований:
- Log rate ~5 записів/с = 10 записів за 2 с
- Якщо `queue_size < 10` → **silent log loss**

**Рекомендація:** специфікувати, чи BUSY polling відбувається до чи після взяття mutex. При interrupt-driven DRDY — mutex тримається лише під час SPI clock phase (мікросекунди).

---

### M2-M1 🟡 MEDIUM — Self-log-on-contention у LittleFSDataGuard

```cpp
LittleFSDataGuard ctor:
    if (!acquired_) LOG_ERROR("lfs_data lock timeout");
//                  ^^^^^^^^^^^
//  Диспатч через всі transports, включно LittleFSTransport bg task
//  що вже в стані contention — самопосилення проблеми
```

`LOG_ERROR` → `SDTransport` → `xQueueSend()`: non-blocking, deadlock немає. Але при частих таймаутах: лог-шторм про власну таймаут-ситуацію збільшує навантаження на той самий transport.

**Рекомендація:** у `LittleFSDataGuard` ctor використовувати `SerialTransport` напряму або тільки atomic flag без LOG.

---

## 5. Module 3 — Boot Sequence State Machine Verification

### Верифікований граф стану

```
[1]   Serial ────────────────────────────────────────────────────────┐
[1.5] GPIO0? ──LOW+5s──→ LittleFS_data.format() → restart            │
      │ HIGH                                                           │
[2]   NVS::begin() ────────────FAIL──→ FATAL (halt)                  │
      │ OK                                                             │
[2.5] TCA8418 ──FAIL──→ DEGRADED (keyboard unavailable)              │
      │                                                                │
[3]   mountSys() ──────────────FAIL──→ FATAL (halt)                  │
      │ OK                                                             │
[4]   mountData() ─────────────FAIL──→ DEGRADED (no measurements)    │
      │                                                                │
[5]   Logger::init() ← LittleFSTransport (потребує /data з [4])      │
      │                                                                │
[7]   SD::tryMount() ──FAIL──→ DEGRADED (no SD matching)             │
      │                                                                │
[7.5] coredump_check() ← потребує SD (або /data fallback)            │
      │                                                                │
[8..12] Normal init ─────────────────────────────────────────────────┘
```

---

### M3-H1 🟠 HIGH — display() на кроці [1.5] викликається до ініціалізації LCD

```pseudo
[1.5] GPIO0 recovery check:
    display("BOOT HELD — Force format? Release to cancel")  ← ❌ LCD не ініціалізовано
    delay(5000)
    if GPIO0 still LOW → LittleFS_data.format() → esp_restart()
```

LCD (ST7789, SPI) ініціалізується у `PluginManager::initAll()` — крок [9]. На кроці [1.5] виклик `display()` або **крашиться** (null pointer), або нічого не робить. Користувач тримає кнопку 5+ секунд без жодного UX фідбеку.

**Виправлення:** замінити `display(...)` → `Serial.println(...)`. Єдиний гарантовано ініціалізований output на [1.5] — UART (крок [1]).

---

### M3-H2 🟠 HIGH — delay(5000) vs. Task Watchdog Timer

ESP-IDF Task Watchdog Timer (`CONFIG_ESP_TASK_WDT_TIMEOUT_S`) за замовчуванням = **5 секунд**. `delay(5000)` = 5000 ms = межа TWDT:

```
delay(5000) без esp_task_wdt_reset() → TWDT може спрацювати → system reset
→ GPIO0 recovery loop ніколи не завершується → brick-подібна поведінка
```

**Виправлення:** або `esp_task_wdt_reset()` у loop кожні 1 с, або зменшити window до 3 с, або використовувати `esp_timer` + non-blocking check.

---

### M3-M1 🟡 MEDIUM — LittleFS_data.format() до mountData()

`LittleFS_data.format()` на кроці [1.5] викликається перед `LittleFSManager::mountData()` (крок [4]). У `esp_littlefs` API:
- `LittleFS.format()` як Arduino wrapper потребує попереднього `begin()` для знання partition label
- Низькорівневий IDF: `esp_spiffs_format("littlefs_data")` не вимагає mount

Конкретна реалізація `LittleFS_data.format()` потребує явної специфікації в коді (або посилання на IDF API).

---

### M3-M2 🟡 MEDIUM — Нумерація boot sequence: відсутній [6]

Послідовність `[5] → [7] → [7.5] → [8]` пропускає номер [6]. При імплементації розробник може вважати це ненавмисним пропуском і вставити код у "слот [6]".

**Виправлення:** додати явний коментар ```; [6] навмисно видалено — coredump перенесено в [7.5]``` або перенумерувати послідовність.

---

### M3-M3 🟡 MEDIUM — Coredump filename = epoch timestamp

```
[7.5] → SD:/CoinTrace/coredumps/<ts>.json
```

На [7.5] WiFi ще не підключено (крок [10]) → NTP не відбулась. ESP32-S3 не має батарейного RTC. Після power cycle: `time() = 0` (Unix epoch 1970-01-01).

Filename `0.json` буде у кожного пристрою після кожного crash + power cycle. При наявності попереднього `0.json` → **перезапис попереднього coredump** без попередження.

**Альтернативи:**
- `coredump_<meas_count>.json` — завжди унікальний (meas_count монотонний)
- `coredump_<ota_ts_nvs>.json` — de timestamp з NVS (ota_ts = час останнього OTA)
- SHA16 від перших 32 байт coredump header — content-addressed, унікальний

---

## 6. Module 4 — Ring Buffer & API Invariants

### M4-H1 🟠 HIGH — Відсутня ID Range Validation

```
GET /api/v1/measure/{id}
slot = id % RING_SIZE
reads m_{slot}.json
```

**Сценарій:** `meas_count = 450`, запит `id = 10`:
- `slot = 10 % 300 = 10`
- Файл `m_010.json` існує, але містить **вимір meas_count=310** (перезаписаний при ring overflow)
- API повертає дані для id=310 клієнту що запитував id=10 — **silent wrong data**

**Потрібна перевірка:**
```cpp
// Специфікація для §12.3
if (meas_count == 0 || id >= meas_count || id < meas_count - RING_SIZE) {
    return HTTP_404;
}
```

---

### M4-H2 🟠 HIGH — ring_used некоректний після Soft Reset з видаленням файлів

Soft Reset §14.1 крок 3: "Опційно: LittleFS_data delete `/measurements/*`"

Після операції:
- `meas_count = 450` (NVS, namespace `storage` не стирається при Soft Reset)
- `ring_used = min(450, 300) = 300` (формула документа)
- Реально: `/measurements/` = **0 файлів**

Клієнт бачить `ring_used=300` → робить `GET /api/v1/measure/150` → HTTP 404. Неявна розбіжність між NVS state та filesystem state.

**Варіанти вирішення:**
- **A:** Soft Reset з видаленням файлів також скидає `meas_count = 0` (ламає монотонність ID, але консистентно)
- **B:** `ring_used` обчислюється як `LittleFS: count_valid_files("/data/measurements/")` при кожному `/api/v1/status` запиті (runtime перевірка, ~10 ms)
- **C:** Soft Reset не видаляє файли — тільки logical clear (файли перезаписуються природньо при нових вимірах)

---

### M4-M1 🟡 MEDIUM — log_gen NVS поле — невизначена роль

NVS namespace `storage` містить `"log_gen": UInt8`. Log rotation у §12.1 описана через `rename .0→.1→.2`, але без посилань на `log_gen`. Поле або є артефактом раннього дизайну, або його роль недокументована.

**Дія:** або задокументувати використання `log_gen`, або видалити з NVS layout §7.2.

---

## 7. Module 5 — Logger & LittleFSTransport Design

### M5-H1 🟠 HIGH — open/close per entry: 4× flash write overhead

Логічний патерн LittleFSTransport background task:
```cpp
// Per-entry антипатерн (implied by document)
while (true) {
    entry = xQueueReceive(queue);
    file.open(APPEND);  // flash write: metadata block update
    file.write(entry);  // flash write: data
    file.close();       // flash write: metadata finalize + dir entry update
}
```

| Патерн | Flash writes/entry | При 576 entries/day |
|---|---|---|
| open/write/close per entry | ~4 ops | **2304 writes/day** |
| Keep-open + fflush per entry | ~1 op | **576 writes/day** |
| Keep-open + fflush кожні N entries | ~1/N + small overhead | **~100–200 writes/day** |

Різниця: 4–12× більше wear на metadata sectors. Плюс: LittleFS journal overhead при кожному `close()` — 2 commit блоки.

**Рекомендація:** тримати `log.0.jsonl` відкритим між write операціями. `lfs_file_sync()` після кожного запису або кожні K мілісекунд для power-fail safety. `lfs_file_close()` тільки при rotation.

---

### M5-M1 🟡 MEDIUM — Log Rotation тримає mutex >100ms

Log rotation sequence (виконується під `lfs_data_mutex_`):
```
1. unlink log.2.jsonl      (~10–50 ms)
2. rename log.1 → log.2   (~10–50 ms)
3. rename log.0 → log.1   (~10–50 ms)
4. create log.0            (~5–10 ms)
─────────────────────────
Total: ~35–160 ms
```

`lfs_data_mutex_` timeout для WebServer = 100 ms. При rotation `~160 ms` → WebServer handler таймаутує → `GET /api/v1/measure/{id}` повертає HTTP 500 або пустий JSON під час rotation.

**Рекомендація:** або збільшити WebServer timeout до 500 ms (з окремим коментарем причини), або виконувати rotation incremental (release + re-take mutex між кожним rename).

---

## 8. Module 6 — NVS Design Issues

### M6-M1 🟡 MEDIUM — incrementMeasCount() TOCTOU

```cpp
void NVSManager::incrementMeasCount() {  // документується як "Atomic: один NVS write"
    uint32_t c = storage_.getUInt("meas_count", 0);  // read  ← call 1
    storage_.putUInt("meas_count", c + 1);           // write ← call 2
}
```

Це **два** окремих NVS API calls. Якщо дві FreeRTOS задачі викличуть метод одночасно:
```
Task A: reads meas_count=42
Task B: reads meas_count=42
Task A: writes meas_count=43
Task B: writes meas_count=43   ← increment Task B втрачено
```

Поточно безпечно тільки тому що `incrementMeasCount()` викликається виключно з MainLoop (prio=3). Але це **не задокументований constraint** — майбутній розробник може зламати інваріант без будь-яких статичних попереджень.

**Виправлення:** додати `// MUST be called only from MainLoop task` або додати внутрішній mutex/atomic.

---

### M6-L1 🟢 LOW — Відсутній nvs_keys partition reserve

Для майбутнього NVS encryption (post-v1) потрібна `nvs_keys` partition (~4 KB). Поточна Option B:
```
Flash Total: 0x800000 — ВИКОРИСТАНО ПОВНІСТЮ
```

Додавання `nvs_keys` post-v1 = **partition table breaking change** = примусовий full flash erase для всіх існуючих пристроїв з втратою даних користувача.

Якщо NVS encryption є реалістичним roadmap item — 4 KB резерв варто закласти зараз за рахунок coredump 128 KB → 124 KB.

---

## 9. Module 7 — Додаткові знахідки

### M7-M1 🟡 MEDIUM — Hard Reset не зупиняє LittleFSTransport bg task

Hard Reset §14.1, порядок:
```
a. NVS nvs_flash_erase()
b. LittleFS_data.format()     ← FS відформатований
c. esp_ota_set_boot_partition(app0)
d. LOG_WARN → Serial only     ← але LittleFSTransport bg task ще існує і має pending queue items
e. esp_restart()
```

LittleFSTransport bg task після кроку `b` намагається дренувати queue → `lfs_file_open()` на відформатованому FS → failure → `LOG_ERROR` → нове item у queue → loop.

Виклик до `esp_restart()` на кроці `e` перерве це, але між `b` та `e` може статись кількасот ітерацій помилок.

**Рекомендація:** на початку Hard Reset sequence (`pre step a`): `Logger::suspend()` або `LittleFSTransport::pause()` щоб зупинити queue processing до restart.

---

### M7-L1 🟢 LOW — device_id Birthday Paradox collision

```cpp
device_id = "CoinTrace-" + hex(esp_efuse_get_custom_mac()[4:6])
//                                                      ^^^^
//                                   лише 2 байти = 65 536 комбінацій
```

Ймовірність collision (birthday paradox):

| Пристроїв у community DB | P(collision) |
|---|---|
| 50 | ~1.9% |
| 100 | ~7.3% |
| 300 | ~50%  |
| 500 | ~68%  |

При community DB з сотнями колекціонерів collision rate стає значущою для трасованості даних.

**Рекомендація:** використовувати 4 байти MAC (16M унікальних ID) або повний 6-байтний MAC.

---

### M7-L2 🟢 LOW — SD hot-insertion не специфікована

`SDCardManager::tryMount()` викликається один раз (boot крок [7]). Немає:
- GPIO interrupt від card-detect pin (чи є він на J3 TF-015?)
- Polling loop для SD re-detection
- API endpoint `POST /api/v1/storage/remount`

Якщо SD вставлена після boot → Matching (Deep) та log archive недоступні до наступного reboot. UX gap для польового використання.

---

### M7-L3 🟢 LOW — OTA rollback + RING_SIZE зміна між версіями

При `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` + автоматичний rollback:
- `meas_count` та всі `m_XXX.json` файли залишаються (LittleFS_data не торкається)
- Якщо нова firmware змінила `RING_SIZE` (наприклад 300→180): старий firmware очікує max 180 слотів, але в `/measurements/` може бути до 300 файлів

`slot = meas_count % 180` — корректно для нових вимірів, але `m_181.json`..`m_299.json` залишаться "невидимими" для старого firmware (orphan files, витрачають місце).

**Рекомендація:** задокументувати `RING_SIZE` як **immutable post-v1 константу** у ADR-ST-006. Зміна RING_SIZE = major version bump зі migration guide.

---

### M7-L4 🟢 LOW — Hard Reset не очищає coredump partition

Після Hard Reset (`nvs_flash_erase()` + `LittleFS_data.format()`) coredump partition залишається незмінною. Після reboot крок [7.5] знайде стару coredump і спробує зберегти summary у вже відформатований (порожній) LittleFS_data. Це коректно (LittleFS порожній, але mounted). Але семантично: Hard Reset мав би означати "чистий старт", включно з coredump.

---

## 10. Що архітектура зробила ПРАВИЛЬНО

Це не формальність — перелічені рішення є нетривіальними і правильними:

| Рішення | Чому правильно |
|---|---|
| **Write-first invariant (ADR-ST-006):** `lfs_file_close()` → `NVS meas_count++` | Ghost-safe при crash. Аргументоване відхилення NVS-first з повним обґрунтуванням. |
| **RAII `LittleFSDataGuard` без heap alloc** | Stack-based RAII — єдино правильний підхід для ESP32 без PSRAM. std::function альтернатива на heap = неприйнятна. |
| **Coredump на [7.5] після SD mount** | Виправлений порядок у v1.2.0 є логічно коректним. SD повинна бути доступна до спроби збереження. |
| **Option B: дві LittleFS partitions** | `uploadfs` isolation — фундаментально правильне рішення. Немає альтернатив для production. |
| **spi_vspi_mutex per burst (R-04)** | Правильний granularity: per-measurement-cycle блокував би SD на 20–120 с. Per-burst дає SD task повну свободу між вимірами. |
| **FreeRTOS priorities (3-2-1)** | Priority inheritance через `xSemaphoreCreateMutex()` гарантує відсутність priority inversion. Обґрунтований порядок. |
| **Partition math** | Всі offsets+sizes коректно складаються до 0x800000 без gaps і overlap. 0x1C0000 = 1792 KB = exact 1.75 MB. |
| **Wear leveling calculations (852+ роки з WA=2)** | Академічно коректні після виправлення sector count 437→448. |
| **`complete: true` sentinel у m_XXX.json** | Надійний crash detection без окремого journal. Файл без поля = invalid = відкидається. |
| **NVS Calibration isolation (ADR-ST-003)** | uploadfs та OTA ніколи не торкаються NVS. Калібрування (45 сек реального часу) захищено. |

---

## 11. 🚀 Pre-implementation Checklist — що вирішити ДО P-3

Наступні пункти **БЛОКУЮТЬ** або **потребують рішення** перед початком фази P-3 (LittleFS Layer). Позначені власником та типом дії.

---

### ✅ MUST — Критичні блокери

#### [PRE-1] ✅ Вирішено: LittleFS Space Budget (M1-C1)

**Рішення:** Варіант A — log rotation зменшено до **2 файли × 200 KB** (замість 3×300 KB).

| | Variant A (обрано) | Variant D |
|---|---|---|
| Hot log storage | 400 KB | 900 KB |
| Steady-state blocks | ~435 / 448 | 563 / 448 (переповнення) |
| Partition changes needed | — | CSV + coredump resize |
| Free margin | ~52 KB (~3%) | Немає |

**Оновлено у STORAGE_ARCHITECTURE.md v1.3.0:** §8.2, OQ-07, §12.1 (LittleFSTransport bullets, OQ-07 note, open-once pattern), §13.1 (wear/block budget table).

**Верифікація:** `lfs_info()`-знімок на реальному пристрої після завантаження 300 вимірів + 2 log файлів по 200 KB. Цільовий показник: `block_count_used ≤ 440`.

---

#### [PRE-2] ✅ Специфіковано: GET /api/v1/measure/{id} ID Range Validation (M4-H1)

**Вирішено в v1.3.0:** §12.3 — рядок `GET /api/v1/measure/{id}` доповнено обов'язковою перевіркою ID range (HTTP 404 для evicted та future IDs). Деталі у STORAGE_ARCHITECTURE.md §12.3.

**Оригінальна дія:** Додати до §12.3 явну специфікацію перевірки:
```
if meas_count == 0 → HTTP 404
if id >= meas_count → HTTP 404 (майбутній вимір)
if meas_count > RING_SIZE AND id < (meas_count - RING_SIZE) → HTTP 404 (overwritten)
```

**Без цього:** клієнти отримують мовчазно невірні дані після кожного ring overflow (meas_count > 300).

---

#### [PRE-3] ✅ Специфіковано: spi_vspi_mutex scope для SDTransport (M2-H1)

**Вирішено в v1.3.0:** ADR-ST-008 Consequences — додано явне формулювання scope `SD.open() → file.write() → file.close()` як атомарна операція з code-comment correct/incorrect. Деталі у STORAGE_ARCHITECTURE.md ADR-ST-008.

**Оригінальна дія:** Додати до ADR-ST-008 та §9.4 явне формулювання:
```
Mutex scope для SDTransport = SD.open() → file.write() → file.close()
як одна атомарна операція. НЕ тільки file.write() call.
```

**Без цього:** FAT metadata corruption можлива при LDC burst між open та close SD file.

---

#### [PRE-4] ✅ Специфіковано: GPIO0 Recovery [1.5] (M3-H1, M3-H2, M3-M1)

**Вирішено в v1.3.0:** §17.2 [1.5] — `display()` → `Serial.println()`, `delay(5000)` → `50×100ms loop + esp_task_wdt_reset()`, умова `millis() < 3000` прибрана, `LittleFS.format()` → `esp_spiffs_format("littlefs_data")` (IDF API, не потребує mount). Закриває M3-H1, M3-H2, M3-M1.

**Оригінальна дія:** Оновити §17.2 boot sequence:
```
[1.5] GPIO0 recovery check
    if GPIO0 == LOW:
        Serial.println("BOOT: GPIO0 HELD — release within 5s to cancel LittleFS_data format")
        for (int i = 0; i < 50; i++) {    // 50 × 100ms = 5s
            esp_task_wdt_reset()
            delay(100)
            if (digitalRead(GPIO_NUM_0) == HIGH) goto skip_format
        }
        esp_spiffs_format("littlefs_data")   // IDF API напряму
        esp_restart()
    skip_format:
```

---

#### [PRE-5] ✅ Специфіковано: ring_used після Soft Reset (M4-H2)

**Вирішено в v1.3.0:** §14.1 — опціональний крок "видалити /measurements/*" прибрано. Задокументовано ризик inconsistency (`ring_used` vs реальні файли) при зовнішньому видаленні без скидання `meas_count`. Обрано Variant C: Soft Reset не видаляє файли — ring overwrite semantics є правильним механізмом.

**Оригінальна дія:** Вибрати один консистентний варіант та зафіксувати у §14.1:
- **Рекомендовано:** не видаляти файли при Soft Reset (перезапишуться природньо). `ring_used` завжди консистентний.
- Або: якщо видалення опціонально — при виборі "видалити виміри" також скидати `meas_count` у NVS.

---

#### [PRE-6] ✅ Специфіковано: нумерація boot sequence [6] (M3-M2)

**Вирішено в v1.3.0:** §17.2 — між кроками [5] і [7] додано `;[6] навмисно відсутній — esp_core_dump_image_check() перенесено у [7.5] (потребує SD mount для збереження)`. Явна документація відсутності [6].

**Оригінальна дія:** Додати коментар або перенумерувати:
```
; [6] навмисно відсутній — esp_core_dump_image_check() перенесено у [7.5]
;     (coredump потребує SD mount для збереження)
```

---

#### [PRE-7] ✅ Специфіковано: coredump filename без timestamp (M3-M3)

**Вирішено в v1.3.0:** §17.2 [7.5] та §17.3 — filename змінено з `<ts>.json` на `coredump_mc<meas_count>.json`. `meas_count` монотонний, унікальний, доступний без RTC.

**Оригінальна дія:** Вибрати та зафіксувати в §17.2:
- **Рекомендовано:** `coredump_mc<meas_count>.json` — унікальний, без RTC dependency.

---

#### [PRE-8] ✅ Специфіковано: Logger open-once патерн (M5-H1)

**Вирішено в v1.3.0:** §12.1 — LittleFSTransport bullets доповнено: `lfs_file_open(APPEND)` один раз при старті/після rotation; `lfs_file_sync()` після кожного запису (power-fail safe); `lfs_file_close()` тільки при rotation або shutdown. Закриває M5-H1.

**Оригінальна дія:** Додати до §12.1 explicit specification:
```
LittleFSTransport: log.0.jsonl тримається ВІДКРИТИМ між записами.
lfs_file_sync() після кожного запису (power-fail safe).
lfs_file_close() + nова 0.jsonl тільки при rotation.
```

---

### ✅ SHOULD — Рекомендовані до P-3

#### [PRE-9] 🟡 Задокументувати LDC1101 BUSY scope (M2-H2)

У §9.4 та ADR-ST-008 уточнити: чи `ldc.readRpL()` включає BUSY polling всередині mutex scope, чи ні. Якщо так — специфікувати максимальний timeout і що відбувається при його перевищенні.

---

#### [PRE-10] ✅ Задокументовано: incrementMeasCount() single-task constraint (M6-M1)

**Вирішено в v1.3.1:** §7.2 `NVSManager` — коментар `incrementMeasCount()` виправлено: видалено некоректне `// Atomic: один NVS write`, додано `// THREADING: MUST be called exclusively from MainLoop task` + пояснення чому Ѧ read+write, а не atomic.

**Оригінальна дія:** У NVSManager.h додати:
```cpp
// THREADING: MUST be called exclusively from MainLoop task.
// Internally performs two NVS operations (read+write) — not atomic
// across concurrent callers. No mutex needed if constraint is respected.
void incrementMeasCount();
```

---

#### [PRE-11] ✅ Видалено: log_gen з NVS layout (M4-M1)

**Вирішено в v1.3.1:** §7.2 namespace `storage` — поле `"log_gen": UInt8` видалено. Rotation повністю визначається через `lfs_file_size()` при відкритті `log.0.jsonl` — LittleFS-native, без NVS, без додаткової точки відмови. Оновлено також §4.2 B-04 (неправильний підрахунок `log_rotate ≈ 3 entries` → `meas_count ≈ 1 entry`).

**Обгрунтування:** `log_gen` був артефактом раннього дизайну — ідея NVS-лічильника rotation замінена прямим підходом. NVS budget namespace `storage`: 3 entries → 1 entry.

**Оригінальна дія:** У §7.2 NVS namespace `storage`: або видалити `"log_gen": UInt8` якщо не використовується, або додати опис: коли читається/записується, як взаємодіє з rotation у §12.1.

---

## 12. 🔮 Backlog — що вирішити в майбутньому

Ці питання не блокують P-3, але **повинні бути адресовані до production release або v2**.

---

### Фаза P-4 / Pre-production

#### [FUTURE-1] 🟠 Log Rotation atomicity — WebServer timeout (M5-M1)

**Проблема:** rotation тримає `lfs_data_mutex_` до ~200 ms. WebServer timeout = 100 ms → HTTP 500 під час rotation.

**Рішення:** збільшити timeout для reads до 500 ms (з явним коментарем в коді), або incremental rotation (release mutex між кожним rename).

**Пріоритет:** реалізувати до першого user testing / beta.

---

#### [FUTURE-2] 🟡 LOG у LittleFSDataGuard ctor → direct Serial (M2-M1)

**Проблема:** `LOG_ERROR("lfs_data lock timeout")` у guard constructor → самопосилення під час contention.

**Рішення:**
```cpp
// Замість LOG_ERROR → SerialTransport напряму або просто прапорець:
if (!acquired_) {
    Serial.println("[ERR] lfs_data lock timeout");   // direct, no dispatch chain
}
```

---

#### [FUTURE-3] 🟡 device_id — розширити до 4+ байтів MAC (M7-L1)

**Проблема:** 2 байти = 65 536 ID. При 300+ пристроях community DB ~50% collision.

**Рішення:**
```cpp
// Поточно:  hex(mac[4:6])   → 4 hex chars = 65K IDs
// Замінити: hex(mac[2:6])   → 8 hex chars = 16M IDs
```

**Пріоритет:** до відкриття community DB для зовнішніх користувачів.

---

#### [FUTURE-4] 🟡 Hard Reset: зупинити LittleFSTransport перед format() (M7-M1)

**Проблема:** bg task продовжує drain queue після `LittleFS_data.format()`.

**Рішення:** в `StorageManager::factoryResetAll()`:
```cpp
LittleFSTransport::pause();    // suspend bg task queue processing
nvs_flash_erase();
lfs_data_.format();
// ...
esp_restart();
```

---

#### [FUTURE-5] 🟢 SD Hot-insertion support (M7-L2)

**Проблема:** SD вставлена post-boot → Matching (Deep) недоступний до reboot.

**Рішення (один із варіантів):**
- Polling: `SDCardManager::checkHotplug()` у MainLoop кожні 5 с
- API: `POST /api/v1/storage/remount` → manual re-mount trigger
- GPIO interrupt від card-detect pin якщо J3 TF-015 має CD line

**Пріоритет:** UX improvement, до першого public release.

---

#### [FUTURE-6] 🟢 nvs_keys partition reserve (M6-L1)

**Проблема:** Flash використано повністю. NVS encryption post-v1 = breaking change.

**Рішення:** При наступній partition table revision зарезервувати 4 KB:
```csv
nvs_keys, data, nvs_keys, 0x18000, 0x1000   # 4 KB (резерв для майбутнього encryption)
```
За рахунок: coredump 128 KB → 124 KB або bootloader gap (24 KB між 0x1A000–0x20000).

**Пріоритет:** перед будь-яким production + security review.

---

### Pre-v2 / Long-term

#### [FUTURE-7] 🟢 Задокументувати RING_SIZE як immutable post-v1 константу (M7-L3)

Додати до ADR-ST-006:
```
RING_SIZE = immutable post-v1. Зміна RING_SIZE = major version bump
зі migration guide (rename/reindex файлів при першому boot нової версії).
```

---

#### [FUTURE-8] 🟢 OTA rollback + schema_ver compatibility matrix (M7-L3)

Документ не специфікує що відбувається при rollback коли `schema_ver` файлів вимірів не сумісна зі старим firmware.

**Рекомендація:** у ADR-ST-007 додати: rollback-сумісний firmware **MUST** розуміти всі `schema_ver ≤ current`. Підвищення `schema_ver` = separate compatibility ADR.

---

#### [FUTURE-9] 🟢 Hard Reset — очищення coredump partition (M7-L4)

```
Hard Reset sequence:
+ f. esp_partition_erase_range(coredump_partition, 0, SIZE)
```

Семантично коректно: Hard Reset = повний чистий старт.

---

## Summary

| Категорія | Кількість | До P-3 | До production | Long-term |
|---|---|---|---|---|
| 🔴 Critical | 1 | 1 | — | — |
| 🟠 High | 7 | 5 | 2 | — |
| 🟡 Medium | 8 | 5 | 3 | — |
| 🟢 Low | 5 | — | 2 | 3 |
| **Разом** | **21** | **11** | **7** | **3** |

**Статус документа STORAGE_ARCHITECTURE v1.2.0 (PRE-1..вирішено, STORAGE_ARCHITECTURE.md оновлено до v1.3.0):** архітектурно зрілий, добре обгрунтований. Пред-імплементаційний чекліст [PRE-1]..[PRE-8] повністю закрито.

---

*Аудит виконано: 2026-03-12 · Версія аудиту: 1.0 · PRE-1..[PRE-8] закрито в v1.3.0 · Наступний аудит: після [PRE-9]..[PRE-11]*
