# Storage Architecture — Другий Незалежний Архітектурний Аудит

**Документ:** STORAGE_AUDIT_v1.3.0.md  
**Аудитована версія:** STORAGE_ARCHITECTURE.md v1.3.1 (commits `5973c11`→`2b7d9c9`)  
**Дата аудиту:** 2026-03-12  
**Методологія:** Concurrency re-audit · Boot state machine verification · FAT/LittleFS write model simulation · Lock ordering analysis · Residual inconsistency sweep  
**Контекст:** Проводиться після закриття [PRE-1]..[PRE-11]. Мета — виявити знахідки, що виникли внаслідок PRE-патчів або залишились поза увагою першого аудиту.

---

## Зміст

1. [Загальна оцінка](#1-загальна-оцінка)
2. [Score-карта знахідок](#2-score-карта-знахідок)
3. [Module A — Residual Concurrency Inconsistencies (PRE-10 sweep)](#3-module-a--residual-concurrency-inconsistencies-pre-10-sweep)
4. [Module B — Boot Sequence State Machine Re-verification](#4-module-b--boot-sequence-state-machine-re-verification)
5. [Module C — Hard Reset Safety Analysis](#5-module-c--hard-reset-safety-analysis)
6. [Module D — Write Frequency Model Consistency](#6-module-d--write-frequency-model-consistency)
7. [Module E — Lock Ordering & Mutex Scope Gaps](#7-module-e--lock-ordering--mutex-scope-gaps)
8. [Module F — Minor Documentation Inconsistencies](#8-module-f--minor-documentation-inconsistencies)
9. [Що архітектура зробила ПРАВИЛЬНО після v1.3.x](#9-що-архітектура-зробила-правильно-після-v13x)
10. [Pre-implementation Checklist — що вирішити ДО P-3 (другий раунд)](#10-pre-implementation-checklist--що-вирішити-до-p-3-другий-раунд)
11. [Backlog — додаткові знахідки](#11-backlog--додаткові-знахідки)

---

## 1. Загальна оцінка

Після закриття [PRE-1]..[PRE-11] архітектура значно дозріла. Більшість критичних блокерів знято. Проте при детальному повторному скануванні виявлені **10 нових знахідок** (0 критичних, 4 високих, 4 середніх, 2 низьких).

**Характер знахідок:** переважно «залишкові ефекти» PRE-патчів — виправлення в одному місці не поширилось на всі посилання (PRE-10 scope incomplete), або нові секції (§12.1, §14.1) потребують доповнення після введення open-once LittleFSTransport pattern.

**Блокуючі для P-3:** A-H1 (false confidence in concurrency) + B-H1 (crash в DEGRADED mode) + C-H1 (filesystem corruption при Hard Reset). Решта — medium/low, не блокують, але повинні бути виправлені до першого production build.

---

## 2. Score-карта знахідок

| ID | Модуль | Знахідка | Severity | Статус |
|---|---|---|---|---|
| **A-H1** | Concurrency | ADR-ST-005 thread safety table: `incrementMeasCount()` — "Один atomic NVS write" після PRE-10 | 🟠 HIGH | Відкрита |
| **A-H2** | Concurrency | ADR-ST-006 conclusions: "один atomic write на вимір. Немає race condition." — аналогічний residue | 🟠 HIGH | Відкрита |
| **B-H1** | Boot | §17.2 [5]: LittleFSTransport ініціалізується безумовно, навіть якщо mountData() [4] повернув DEGRADED | 🟠 HIGH | Відкрита |
| **C-H1** | Hard Reset | §14.1: LittleFSTransport bg task не зупиняється перед `LittleFS_data.format()` → UB | 🟠 HIGH | Відкрита |
| **D-M1** | Write Model | §13.1: критичне попередження "139 діб до failure" використовує 72 writes/day (per-entry модель, до PRE-8) | 🟡 MEDIUM | Відкрита |
| **E-M1** | Concurrency | Lock ordering `lfs_data_mutex_` → `spi_vspi_mutex` не задокументована → майбутній deadlock ризик | 🟡 MEDIUM | Відкрита |
| **E-M2** | Concurrency | Watchdog free space check (`esp_littlefs_info()`) — не специфіковано, що виклик має бути всередині `lfs_data_mutex_` scope | 🟡 MEDIUM | Відкрита |
| **E-M3** | Concurrency | `LittleFSDataGuard::ok()=false` — контракт поведінки handler при тайм-ауті не специфікований | 🟡 MEDIUM | Відкрита |
| **F-L1** | Documentation | §16 Soft Reset row: "⚠️ Measurements optional" — суперечить PRE-5 (measurements не видаляються) | 🟢 LOW | Відкрита |
| **F-L2** | Documentation | §10 OQ-07 summary: "Вирішено: 3×300KB" — залишок до PRE-1; правильно "2×200KB" | 🟢 LOW | Відкрита |

---

## 3. Module A — Residual Concurrency Inconsistencies (PRE-10 sweep)

### A-H1 🟠 HIGH — ADR-ST-005 thread safety table: `incrementMeasCount()` "Один atomic NVS write"

**Метод аналізу:** cross-reference між §7.3 (PRE-10 fix) та ADR-ST-005 table

PRE-10 виправив §7.3 NVSManager API, прибравши помилковий коментар "Atomic: один NVS write" і додав THREADING constraint. Проте ADR-ST-005 thread safety table не була оновлена:

```
§11 ADR-ST-005, thread safety table (поточний стан після PRE-10):
| `incrementMeasCount()` | ✅ Так | Один atomic NVS write |   ← ❌ НЕ оновлено!
```

**Чому "atomic" неточно:** `incrementMeasCount()` виконує дві окремі NVS операції — read (`getUInt`) + write (`putUInt`) — де між ними може виникнути Context Switch. Операція безпечна тільки тому, що викликається виключно з одного task (MainLoop, prio=3), а не тому що вона атомарна.

**Ризик:** розробник читає ADR (авторитетний розділ) і бачить "atomic NVS write" → вважає, що incrementMeasCount() можна безпечно викликати з будь-якого task → додає виклик з WebServer task → NVS race condition.

**Симуляція race:**
```
MainLoop (prio=3)   WebServer (prio=1)
─────────────────────────────────────
  getUInt("meas_count") = 150
  [preempted by hypothetical higher prio]
                          getUInt("meas_count") = 150  ← читає те саме!
                          putUInt("meas_count", 151)
  putUInt("meas_count", 151)   ← дублює! реальний count = 151, записано двічі
```
Lost increment → один вимір ніколи не буде доступний через API.

**Виправлення:**
```
| `incrementMeasCount()` | ✅ Так | Безпечно: тільки MainLoop task; NOT atomic (NVS get+put) |
```

---

### A-H2 🟠 HIGH — ADR-ST-006 conclusions: "один atomic write на вимір"

**Те саме поле, інше місце.** ADR-ST-006 (Ring Buffer 300 вимірів, Наслідки):

```
Поточний текст:
"Тільки `meas_count` (UInt32) зберігається в NVS — один atomic write на вимір. Немає race condition."
```

**Проблема ідентична A-H1.** До того ж "один atomic write" суперечить "write-first invariant":

> ADR-ST-006 Порядок запису: `file write → lfs_file_close() → NVS putUInt("meas_count", n+1)`

Тобто виміри продукують не один NVS write, а два NVS operations (get+put), і вони стоять після `lfs_file_close()`. Твердження "atomic" є помилковим з обох точок зору.

**Виправлення:**
```
"Тільки `meas_count` (UInt32) зберігається в NVS. Write-first invariant: lfs_file_close() ПЕРЕД putUInt.
 Безпечно від race condition тому що викликається виключно з MainLoop task — не через атомарність NVS."
```

---

## 4. Module B — Boot Sequence State Machine Re-verification

### B-H1 🟠 HIGH — LittleFSTransport initialized unconditionally despite potential DEGRADED state

**Метод аналізу:** boot state machine trace, всі шляхи виконання через [4]→[5]

Поточний §17.2 boot sequence (скорочено):
```
[4] LittleFSManager::mountData()
    → якщо FAIL: DEGRADED MODE — без вимірів/логів, але alive

[5] Logger::init(SerialTransport,
                 RingBufferTransport,
                 LittleFSTransport)   ; ← БЕЗУМОВНО, незалежно від [4] результату!
```

**Що відбувається при DEGRADED ([4] fail)?**

`LittleFSTransport` bg task стартує і викликає `lfs_file_open(lfs_data_, "/logs/log.0.jsonl", LFS_O_APPEND)`. Якщо `lfs_data_` не змонтована → `lfs_file_open()` повертає `LFS_ERR_INVAL` або `LFS_ERR_CORRUPT`. LittleFS API очікує змонтований `lfs_t` context.

У кращому випадку — повернення помилки, яку bg task логує і зупиняється. У гіршому — запис у незалежний адресний простір (UB, можливий crash або SPI flash корупція на сусідній partition через розбалансовані write ops).

**Симуляція:
```
DEGRADED boot path:
  [4] mountData() → FAIL               ; lfs_data_ не ініціалізований
  [5] LittleFSTransport bg task start  ; stacks on core 0, prio=2
  ...boot continues...
  bg task: lfs_file_open(lfs_data_, ...) with lfs_data_ = uninitialized lfs_t
           → UB: reads from lfs_data_.cfg (random stack memory)
           → potential crash (assert in LittleFS) або bad SPI write
```

**Виправлення:** [5] має бути умовним:
```
[5] якщо mountData() успіх:
        Logger::init(SerialTransport, RingBufferTransport, LittleFSTransport)
    інакше (DEGRADED):
        Logger::init(SerialTransport, RingBufferTransport)  ; без LittleFSTransport
        LOG_WARN("LittleFS_data unavailable — LittleFSTransport disabled")
```

---

## 5. Module C — Hard Reset Safety Analysis

### C-H1 🟠 HIGH — LittleFSTransport bg task не зупиняється перед format()

**Метод аналізу:** sequence diagram Hard Reset + LittleFSTransport open-once pattern

Поточний §14.1 Hard Reset sequence:
```
2. Якщо підтверджено:
   a. NVS nvs_flash_erase()
   b. LittleFS_data.format()          ← ось проблема
   c. esp_ota_set_boot_partition(app0)
   d. LOG_WARN(...)
   e. esp_restart()
```

§12.1 та LOGGER_ARCHITECTURE §6.6 специфікують open-once pattern:
> LittleFSTransport bg task тримає `log.0.jsonl` відкритим між записами (`lfs_file_t currentFile_` відкритий постійно)

**Сценарій:**
```
MainLoop (prio=3)                  LittleFSTransport bg (prio=2)
────────────────────────────────────────────────────────────────
[User confirms Hard Reset]         [bg task blocked on queue, log.0.jsonl OPEN]
NVS erase...
LittleFS_data.format()             ← bg task ще живий!
   lfs_format() erases all blocks
   lfs_mount() reinitializes lfs_t
                                   [queue awakens, bg task continues processEntry()]
                                   lfs_file_write(currentFile_, ...)
                                   ← currentFile_ = dangling handle до форматованої FS!
                                   → LFS_ERR_CORRUPT або silent data overwrite
esp_restart()
```

`FUTURE-4` у backlog ідентифікує проблему, але §14.1 procedure не виправлена — вона досі не містить зупинки bg task.

**Виправлення:** додати крок `a0` перед erase:
```
2. Якщо підтверджено:
   a0. LittleFSTransport::stop()    → xTaskDelete bg task + lfs_file_close(currentFile_)
   a.  NVS nvs_flash_erase()
   b.  LittleFS_data.format()
   ...
```

**Додаткова примітка:** FUTURE-9 (coredump partition not cleared at Hard Reset) залишається актуальним — Hard Reset відновлює завод, але наступний [7.5] при reboot знайде stale coredump і збереже її в щойно відформатований LittleFS.

---

## 6. Module D — Write Frequency Model Consistency

### D-M1 🟡 MEDIUM — §13.1 SD lifetime warning uses pre-PRE-8 write model

**Метод аналізу:** трасування write frequency model через §12.1, §13.1, §17.3

Поточний §13.1 "Критичний висновок щодо SD":
```
288 KB logs/добу = 72 writes × 4 KB sectors → при 10K cycles → ~139 діб до failure!
```

Цей розрахунок базується на **per-entry write model** — написанні кожного log-запису безпосередньо в SD. Це саме та модель, яку PRE-8 замінив open-once+sync pattern та batch writes.

**Поточна модель (post PRE-1/PRE-8, §12.1 R-09):**
- SDTransport отримує дані тільки при ротації LittleFS (copy `log.1.jsonl` → SD)
- LittleFS rotation відбувається кожні ~200 KB логів = кожні ~2.8 год при 288 KB/добу
- SD writes на добу: ≈ 24 при batch (8.6 ротацій × ~2.8 SD writes per rotation incl. open/close/FAT)
- Але! Кожна ротація пише один 200 KB файл = 50 LittleFS blocks → ~400 SD sectors (512B)
- 24 SD writes/день → lifetime: 10 000 / 24 ≈ **417 діб** (3× краще ніж 139 діб!)

**Проте 417 діб на дешевому SD — все ще тривожно.** Рекомендація §13.1 (не писати потоком, а батчами при ротації) вже реалізована в архітектурі. Потрібно оновити розрахунок і перефокусувати попередження:

```
Виправлений розрахунок:
  Post-PRE-8 batch model: ~24 SD writes/dobу (ротація ~кожні 2-3 год)
  При 10K cycles: 10,000 / 24 ≈ 417 діб ≈ 1.1 роки
  При 100K cycles (якісні SD, наприклад Samsung Endurance): ~11 років
  Рекомендація: використовувати SD карти з high endurance (UHS-I U3, A2 class):
  Samsung PRO Endurance = 40,000 TBW, Sandisk MAX Endurance = 15,000 годин запису.
```

---

## 7. Module E — Lock Ordering & Mutex Scope Gaps

### E-M1 🟡 MEDIUM — Lock ordering contract не документована

**Метод аналізу:** аналіз lock ordering для lfs_data_mutex_ + spi_vspi_mutex у всіх task-контекстах

§12.1 rotation sequence (де обидва mutex):
```
rotate():
  1. xSemaphoreTake(lfs_data_mutex_)   ← first lock
  2. lfs_rename(log.0 → log.1)
  3. xSemaphoreTake(spi_vspi_mutex)    ← nested second lock (for SD copy)
  4. SD.open() → write log.1 → close()
  5. xSemaphoreGive(spi_vspi_mutex)
  6. lfs_remove(log.1)
  7. xSemaphoreGive(lfs_data_mutex_)
```

Lock ordering: `lfs_data_mutex_` → `spi_vspi_mutex`.

**Відсутній контракт у документі.** Якщо майбутній розробник (або PluginManager/FingerprintCache) напише код у зворотньому порядку:
```cpp
xSemaphoreTake(spi_vspi_mutex, portMAX_DELAY);   // SDCardManager
// ... SD operation ...
xSemaphoreTake(lfs_data_mutex_, portMAX_DELAY);  // потім LittleFS cache read
```
→ **Deadlock** при співпаданні з rotate():

```
LittleFSTransport bg:          FingerprintCache (hypothetical):
─────────────────────────────────────────────────────────────
Take lfs_data_mutex_ ✓
                               Take spi_vspi_mutex ✓
Take spi_vspi_mutex → BLOCKED  Take lfs_data_mutex_ → BLOCKED
[DEADLOCK — обидва чекають]
```

**Виправлення:** додати в ADR-ST-008 або §9.4 explicit lock ordering note:
```
⚠️ Lock ordering contract (MUST BE FOLLOWED):
   Якщо потрібні обидва mutex: ЗАВЖДИ отримувати в порядку:
   1. lfs_data_mutex_   (LittleFS_data context)
   2. spi_vspi_mutex    (VSPI bus context)
   НІКОЛИ не навпаки → deadlock.
   Поточні caller sites: rotate() (LittleFSTransport), MeasurementStore::save() (lfs only), WebServer handler (lfs only).
```

---

### E-M2 🟡 MEDIUM — Watchdog free space check (`esp_littlefs_info()`) — mutex scope не специфікована

**Метод аналізу:** LittleFS API thread safety + esp-idf source

§12.1 рекомендує:
> "`При LittleFSTransport` перевіряти вільне місце LittleFS_data після кожного запису логу"

`esp_littlefs_info()` (Arduino ESP32 wrapper для `lfs_fs_size()`) не є thread-safe між FreeRTOS task'ами. Внутрішній LittleFS виклик `lfs_fs_size()` обходить весь метадата-граф і не захищений жодним вбудованим mutex.

Якщо `esp_littlefs_info()` викликати за межами `lfs_data_mutex_` scope (наприклад пасивна перевірка в Main Loop або частина WebServer handler) одночасно із `lfs_file_write()` у LittleFSTransport bg task → **corrupted LittleFS metadata read** або assert (якщо LittleFS debug enabled).

**Правильна реалізація:**
```cpp
// Всередині LittleFSTransport bg task, після lfs_file_sync():
// mutex вже тримається від початку processEntry():
size_t used, total;
if (esp_littlefs_info("littlefs_data", &total, &used) == ESP_OK) {
    size_t free_kb = (total - used) / 1024;
    if (free_kb < 50) forceRotation();
    else if (free_kb < 20) disableLogging();
}
// mutex звільняється наприкінці processEntry()
```

**Виправлення:** додати explicit note в §12.1: watchdog threshold check **обов'язково** виконується всередині `lfs_data_mutex_` scope у bg task після `lfs_file_sync()`.

---

### E-M3 🟡 MEDIUM — `LittleFSDataGuard::ok()=false` — handler behavior contract unspecified

**Метод аналізу:** ревізія RAII guard API + виклики з WebServer handlers

`LittleFSDataGuard` при тайм-ауті (acquisitions=false) логує `LOG_ERROR` і продовжує. `ok()` повертає false. Але **жоден handler не показує, що робити при ok()=false**.

**Два небезпечних сценарії:**

**Сценарій 1 — Handler не перевіряє ok():**
```cpp
LittleFSDataGuard g(lfs_data_mutex_);
// Якщо ok()=false але code continues:
lfs_file_read(...)  // НЕ захищено mutex → filesystem corruption
```

**Сценарій 2 — Default timeout=100ms vs rotation ~150-350ms:**
```
rotation тримає lfs_data_mutex_ ≈ 150-350 ms:
  - lfs_rename(): ~5 ms
  - SD.write(200KB at 20MHz): ~150-300 ms (FAT32 overhead)
  - lfs_remove(): ~5 ms

WebServer handler із default 100ms guard:
  → xSemaphoreTake(100ms) TIMEOUT → ok()=false → LOG_ERROR
```
При кожній ротації всі WebServer reads завершаться з тайм-аутом і LOG_ERROR.

**Виправлення:**
1. Збільшити default timeout до `500` ms (відповідає AsyncWebServer window note в §12.1)
2. Задокументувати контракт у guard класі:
```cpp
// Contract: якщо ok()=false — caller MUST return error response (HTTP 503 або еквівалент).
//           НЕ продовжувати file operations без захисту mutex.
//           WebServer: 500ms timeout (достатньо для rotation cycle).
explicit LittleFSDataGuard(SemaphoreHandle_t& mtx, uint32_t timeout_ms = 500)
```

---

## 8. Module F — Minor Documentation Inconsistencies

### F-L1 🟢 LOW — §16 Soft Reset row: "⚠️ Measurements optional" суперечить PRE-5

§16 data survival matrix:
```
| Soft Factory Reset | ❌ WiFi + Settings | ✅ | ⚠️ Measurements optional | ✅ |
```

PRE-5 (v1.3.0) явно прибрав "optional file delete" крок:
> "Виміри НЕ видаляються ([PRE-5]) — вони залишаються в LittleFS_data і будуть природньо перезаписані через ring overflow semantics."

Слово "optional" тепер некоректне — виміри **завжди зберігаються** при Soft Reset.

**Виправлення:**
```
| Soft Factory Reset | ❌ WiFi + Settings | ✅ | ✅ Зберігаються (ring overflow = cleanup) | ✅ |
```

---

### F-L2 🟢 LOW — §10 OQ-07 summary: "3×300KB" — залишок до PRE-1

§10 "Відкриті питання — Summary" таблиця:
```
| OQ-07 | ... | ✅ Вирішено: 3×300KB LittleFS hot + SD cold archive при ротації (§12.1) |
```

PRE-1 (Variant A, v1.3.0) замінив 3×300KB → 2×200KB. OQ-07 closing text в §10 використовує стару формулу. §10 OQ-07 body (вище) правильно описує 2×200KB.

**Виправлення:**
```
✅ Вирішено: 2×200KB LittleFS hot (Variant A) + SD cold archive при ротації (§12.1)
```

---

## 9. Що архітектура зробила ПРАВИЛЬНО після v1.3.x

- ✅ **Open-once LittleFS pattern (PRE-8)** — правильно знижує wear: `lfs_file_sync()` ≠ `lfs_file_close()`, одна відкрита `lfs_file_t` між записами.
- ✅ **Write-first invariant (ADR-ST-006)** — `file close → NVS increment` в правильному порядку; crash safety коректна.
- ✅ **spi_vspi_mutex scope per-burst + SD open→close atomic (PRE-3, ADR-ST-008)** — FAT metadata захищено від LDC SPI collision; правильний scope для FAT integrity.
- ✅ **GPIO0 recovery без TWDT race (PRE-4)** — 50×100ms loop із `esp_task_wdt_reset()` є коректним рішенням.
- ✅ **log_gen removed from NVS (PRE-11)** — розмір NVS namespace відповідає дійсності; rotation via `lfs_file_size()` не потребує NVS counter.
- ✅ **coredump_mc<meas_count>.json (PRE-7)** — meas_count монотонний, не залежить від RTC; правильний унікальний ідентифікатор при boot.
- ✅ **3% free margin (PRE-1, 13 blocks)** — tight але достатньо для поточної схеми; block budget simulation correct.
- ✅ **Partition Option B** — дві LittleFS partitions правильно ізолюють `uploadfs` від user data.
- ✅ **DEGRADED vs FATAL taxonomy** — коректна класифікація; пристрій продовжує роботу без LittleFS_data (без вимірів, але alive).
- ✅ **ID range validation at HTTP layer (PRE-2)** — three-condition check правильний: `meas_count==0`, `id >= meas_count`, `meas_count > RING_SIZE && id < meas_count - RING_SIZE`.

---

## 10. Pre-implementation Checklist — що вирішити ДО P-3 (другий раунд)

| ID | Знахідка | Тип правки | Файл |
|---|---|---|---|
| **[SA2-1]** | A-H1: ADR-ST-005 table `incrementMeasCount()` "atomic" residue | Текстова правка | STORAGE_ARCHITECTURE.md §11 |
| **[SA2-2]** | A-H2: ADR-ST-006 "один atomic write" residue | Текстова правка | STORAGE_ARCHITECTURE.md §11 |
| **[SA2-3]** | B-H1: Boot [5] LittleFSTransport conditional на успіх [4] | Специфікаційна правка boot seq | STORAGE_ARCHITECTURE.md §17.2 |
| **[SA2-4]** | C-H1: Hard Reset [a0] зупинка LittleFSTransport bg task | Специфікаційна правка §14.1 | STORAGE_ARCHITECTURE.md §14.1 |
| **[SA2-5]** | D-M1: §13.1 SD warning — виправити 72 writes/day на batch model | Числова правка + рекомендація | STORAGE_ARCHITECTURE.md §13.1 |
| **[SA2-6]** | E-M1: Lock ordering contract документування | Нова note в ADR-ST-008 | STORAGE_ARCHITECTURE.md §11 |
| **[SA2-7]** | E-M2: Free space check — explicit mutex scope note | Текстова правка §12.1 | STORAGE_ARCHITECTURE.md §12.1 |
| **[SA2-8]** | E-M3: LittleFSDataGuard default 100ms→500ms + ok()=false contract | Код-специфікаційна правка | STORAGE_ARCHITECTURE.md §11 |
| **[SA2-9]** | F-L1: §16 Soft Reset "⚠️ optional" → ✅ | Текстова правка | STORAGE_ARCHITECTURE.md §16 |
| **[SA2-10]** | F-L2: §10 OQ-07 "3×300KB" → "2×200KB" | Текстова правка | STORAGE_ARCHITECTURE.md §10 |

---

## 11. Backlog — додаткові знахідки

Наступні знахідки не блокують P-3, але мають бути вирішені до production release:

| ID | Знахідка | Рекомендація |
|---|---|---|
| **SA2-FUTURE-1** | Hard Reset (§14.1) не очищає coredump partition (FUTURE-9 carryover) — після Hard Reset + reboot, [7.5] знайде stale coredump і збереже її в щойно відформатований LittleFS | Додати крок `b2. esp_partition_erase_range(coredump_partition, 0, size)` до Hard Reset sequence |
| **SA2-FUTURE-2** | TCA8418 DEGRADED + LittleFS_data corrupt = "stuck boot" без виходу (§17.2 [1.5] note) — GPIO0 recovery форматує тільки data, не є повним Hard Reset | Документувати secondary recovery path: GPIO0 утримання > 10s = повний Hard Reset без клавіатури |
| **SA2-FUTURE-3** | FAT32 SD write-to-temp-then-rename pattern відсутня — power fail під час rotation SD copy → corrupt archive file | При SD copy: писати в `tmp_log.txt`, потім rename → `2026-03-12.txt` |
| **SA2-FUTURE-4** | §15 P-4 `SDTransport.h/.cpp`: Logger SD async transport — per LOGGER_ARCHITECTURE, SDTransport отримує data тільки від LittleFSTransport at rotation (не є самостійним dispatch target в CoinTrace). §15 roadmap artifact — потребує clarification | Уточнити §15 P-4: SDTransport не є самостійним Logger transport у CoinTrace; SD writes відбуваються в LittleFSTransport::rotate() |

---

*Версія STORAGE_AUDIT_v1.3.0 — другий незалежний аудит після [PRE-1]..[PRE-11]. Документ STORAGE_ARCHITECTURE.md потребує [SA2-1]..[SA2-10] для production readiness.*
