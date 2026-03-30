# Technical Debt Registry — CoinTrace

**Версія:** 1.0.0  
**Дата:** 2026-03-30  
**Статус:** Активний реєстр  
**Область:** Увесь проєкт (cross-wave)  
**Cross-ref:** `WAVE8_ROADMAP.md`, `WAVE9_ROADMAP.md`, `LOGGER_AUDIT.md`, `LDC1101_ARCHITECTURE.md`, `METAL_MATCHER_ARCHITECTURE.md`

---

## Призначення

Цей документ — єдиний реєстр усього задокументованого технічного боргу. Кожен новий борг додається сюди при виявленні, кожен закритий — позначається `✅ Closed` з датою та посиланням на коміт.

**Правило:** При закритті Wave N — оновити статус відповідних TD-### тут, потім оновити реєстр у наступному ROADMAP.

---

## Зміст

1. [Матриця боргів](#1-матриця-боргів)
2. [Детальний опис](#2-детальний-опис)
   - [Категорія 1 — Прошивка / hardcoded](#категорія-1--прошивка--hardcoded)
   - [Категорія 2 — Сенсор / апаратура](#категорія-2--сенсор--апаратура)
   - [Категорія 3 — Connectivity](#категорія-3--connectivity)
   - [Категорія 4 — Logger audit backlog](#категорія-4--logger-audit-backlog)
   - [Категорія 5 — Документація (stale)](#категорія-5--документація-stale)
3. [Закриті борги](#3-закриті-борги)
4. [Changelog](#4-changelog)

---

## 1. Матриця боргів

| ID | Категорія | Опис (коротко) | Файл:рядок | Вплив | Цільова хвиля | Статус |
|----|-----------|----------------|-----------|-------|---------------|--------|
| TD-01 | Firmware | `save()` hardcoded `"p1_MIKROE3240_024mm"` (повинно бути `p3_`) | `MeasurementStore.cpp:84` | Низький (DB не фільтрує по protocol_id) | Wave 9 cleanup | 🔵 Open |
| TD-02 | Firmware | `load()` fallback hardcoded `"p1_MIKROE3240_024mm"` | `MeasurementStore.cpp:199` | Низький | Wave 9 cleanup | 🔵 Open |
| TD-03 | Firmware | `lhrContinuous=true` шлях не реалізований в `update()` | `LDC1101Plugin.h:880` | **Блокує Wave 9 D-2** | Wave 9 D-2 | 🔴 Blocks |
| TD-04 | Firmware | `TCA8418::begin()` закоментовано — WiFi provisioning по M-key не реалізовано | `main.cpp:592`, `WiFiManager.h:14,45`, `WiFiManager.cpp:114` | Середній (UX) | Wave 9+ | 🔵 Open |
| TD-05 | Сенсор | XFE зона (35–37.3%) → `?` — мала відстань 0.5% до XAL | `main.cpp` — `classifyQuick()` | Середній (misclassify XFE) | Wave 9 A-3 | 🔵 Open |
| TD-06 | Сенсор | FERRO поріг (100 ct raw dL) не верифіковано на реальній сталевій монеті | `main.cpp` — `QUICK_FERRO_THRESH_L_RAW` | Середній (steel blind) | Wave 9 C-6 (S-5) | 🔵 Open |
| TD-07 | Сенсор | `high_q_sensor=false` — Q-фактор не виміряно, 24-bit LHR точність невідома | `LDC1101Plugin.h:4` | Середній | Wave 9 C-6 (R-01) | 🔵 Open |
| TD-08 | Апаратура | CLKIN не підключено (GPIO4 → mikroBUS Pin16) — L_DATA некоректний, LHR недоступний | Schematic / hardware | **Передумова для D-2 HW-verify** | Wave 9 (до C-6) | 🔴 Blocks |
| TD-09 | Сенсор | Авто-рекалібровка Strategy B — placeholder, не активна. Дрейф ~2.87%/20хв | `LDC1101_ARCHITECTURE.md §7.2` | Середній (довгі сесії) | Wave 9+ | 🔵 Open |
| TD-10 | Connectivity | WebSocket A-6 не реалізовано — sensor frames stub | `HttpServer.cpp`, `WAVE8_ROADMAP.md A-6` | Низький (UI real-time) | Wave 9 post-C-6 | 🔵 Open |
| TD-11 | Connectivity | mDNS вимкнено (Bug B-03 OOM) — `cointrace.local` не працює | `WiFiManager.cpp` / B-03 | Низький (UX) | Wave 9 (A-6 heap fix) | 🔵 Open |
| TD-12 | Connectivity | AP mode `http://192.168.4.1` — не hw-верифіковано | `WiFiManager` AP mode | Низький | Wave 9 quick check | 🔵 Open |
| TD-13 | Connectivity | `POST /api/v1/calibrate` → stub 503 | `HttpServer.cpp:516` | Низький (API неповний) | Wave 9+ | 🔵 Open |
| TD-14 | Connectivity | BLE GATT service — відкладено до v2 (PSRAM) | `HttpServer.cpp:101` | Низький (v2 feature) | v2 hardware | ⚪ Deferred |
| TD-15 | Logger | LF-FUTURE-1..5 (LOGGER_AUDIT §13) — doc/thread-safety backlog | `LOGGER_AUDIT.md §13` | Низький | Wave 9+ | 🔵 Open |
| TD-16 | Docs | `METAL_MATCHER_ARCHITECTURE.md` — C-7b/e показані як `🔲 Pending` (фактично реалізовано) | `METAL_MATCHER_ARCHITECTURE.md:13,14` | None (stale doc) | Wave 8 closure | ✅ Closed |
| TD-17 | Docs | `LDC1101_ARCHITECTURE.md §10` — 10 задач без статусу завершення | `LDC1101_ARCHITECTURE.md §10` | None (stale doc) | Wave 9 | 🔵 Open |

**Умовні позначення:**
- 🔴 Blocks — блокує подальший розвиток
- 🔵 Open — відкритий, не блокує
- ⚪ Deferred — свідомо відкладено на майбутню версію
- ✅ Closed — закрито (дата + коміт у §3)

---

## 2. Детальний опис

### Категорія 1 — Прошивка / hardcoded

#### TD-01 — `MeasurementStore::save()` hardcoded protocol_id
**Файл:** `lib/StorageManager/src/MeasurementStore.cpp:84`  
**Код:**
```cpp
doc["protocol_id"] = "p1_MIKROE3240_024mm";  // WRONG — actual: "p3_MIKROE3240_b06_012mm"
```
**Контекст:** При збереженні вимірювання, `protocol_id` у JSON жорстко закодований як `p1_` (початкова версія протоколу), тоді як використовується `p3_` (spacer 0.6mm з add-on 0.6/1.2/1.8mm).  
**Вплив:** DB запис має некоректний `protocol_id`. Зараз `query()` не фільтрує по цьому полю, тому runtime-вплив нульовий. Але заважатиме майбутньому multi-protocol режиму.  
**Рішення:** Передати актуальний `protocol_id` як параметр у `MeasurementStore::save()` або зчитати з `ConfigManager`.

---

#### TD-02 — `MeasurementStore::load()` fallback hardcoded protocol_id
**Файл:** `lib/StorageManager/src/MeasurementStore.cpp:199`  
**Контекст:** Fallback при відсутності `protocol_id` у JSON — той самий `"p1_MIKROE3240_024mm"`.  
**Вплив:** Аналогічно TD-01, наразі без runtime-впливу.  
**Рішення:** Виправити разом з TD-01.

---

#### TD-03 — LHR continuous mode не реалізовано
**Файл:** `lib/LDC1101Plugin/src/LDC1101Plugin.h:880`  
**Код:**
```cpp
// TODO (v1.5): lhrContinuous=true path (ADR-LHR-001, §10 задача 9)
```
**Контекст:** `update()` не читає 24-bit LHR_DATA регістр при `lhrContinuous=true`. Метод `getLiveLHR()` або аналог не існує.  
**Вплив:** 🔴 **Блокує Wave 9 D-2** — без LHR continuous неможливо зібрати Δf/fSensor статистику в Discovery Mode.  
**Рішення:** Реалізувати блок у `update()`: перевірити `LHR_STATUS.DRDYB`, якщо ready — прочитати 3 байти LHR_DATA (24-bit), зберегти в кеш поряд з RP/L. Додати `getLiveLHR()` public getter.

---

#### TD-04 — TCA8418 keyboard controller не ініціалізовано
**Файли:** `src/main.cpp:592`, `lib/WiFiManager/src/WiFiManager.h:14,45`, `lib/WiFiManager/src/WiFiManager.cpp:114`  
**Код (main.cpp):**
```cpp
// TCA8418::begin(0x34, GPIO_NUM_11);  // TODO: keyboard init
```
**Контекст:** WiFi provisioning через M-key (фізична клавіша Cardputer) потребує TCA8418 I²C keyboard controller, але `begin()` закоментоване. Провіжн через AP mode можливий (TD-12), але M-key shortcut не працює.  
**Вплив:** Середній — відсутність зручного WiFi provisioning UX.  
**Рішення:** Видалити коментар та реалізувати provisioning flow.

---

### Категорія 2 — Сенсор / апаратура

#### TD-05 — XFE зона Quick Screen → `?` (недостатня відстань до XAL)
**Файл:** `src/main.cpp` — `classifyQuick()`  
**Контекст:** Стальна монета (XFE) очікується у діапазоні 35–37.3% dRp. Але XAL (алюміній) — 37.3–41.0%. Запас між XFE upper та XAL lower = 0.5%, що нижче температурного шуму.  
**Поточна поведінка:** `classifyQuick()` ніколи не повертає `"STEEL"` у Quick Screen (тільки FERRO-bit спрацьовує), натомість діапазон 35–37.3% позначений як `?`.  
**Вплив:** Середній — стальна монета не визначається в Quick Screen Phase 1.  
**Рішення:** Wave 9 A-3 — `matchQuick()` з ML-вектором (Phase 2) замінить threshold cascade. S-5 HW-сесія необхідна для калібровки FERRO sign.

---

#### TD-06 — FERRO поріг не верифіковано (S-5 pending)
**Файл:** `src/main.cpp` — `QUICK_FERRO_THRESH_L_RAW = 100.0f`  
**Контекст:** `isFerro` визначається як `dL_raw > 100 ct` (позитивний зсув індуктивності). Порогове значення 100 обрано теоретично; реальний знак і magnitude dL для сталевої монети невідомі.  
**Перехрестне посилання:** `METAL_MATCHER_ARCHITECTURE.md §4` — `ferro_thresh_dL1_n = 99.0` (DISABLED through Wave 9), `// ⚠️ ЗНАК НЕВИЗНАЧЕНИЙ — pending hw-сесії S-5`  
**Вплив:** Середній — steel blindspot у Quick Screen та matchFull.  
**Рішення:** S-5 hw-сесія (Wave 9 C-6) з реальною сталевою монетою.

---

#### TD-07 — Q-фактор сенсора не виміряно (`high_q_sensor=false`)
**Файл:** `lib/LDC1101Plugin/src/LDC1101Plugin.h:4`  
**Контекст:** `high_q_sensor=false` — початкове значення. R-01 hw-сесія (LHR characterization) ще не виконана. Без Q-measurement невідомо наскільки точними є 24-bit LHR показники.  
**Вплив:** Середній — блокує вибір між RP+LHR vs RP-only векторами у Wave 9 A-2.  
**Рішення:** R-01 hw-сесія (паралельно або раніше C-6).

---

#### TD-08 — CLKIN не підключено (GPIO4 → mikroBUS Pin16)
**Тип:** Апаратна проблема  
**Контекст:** LDC1101 очікує зовнішній reference clock на CLKIN pin (mikroBUS Pin16). Без нього IC використовує внутрішній RC-осцилятор, що призводить до:
1. `L_DATA` — некоректний (залежить від CLKIN частоти)
2. LHR mode — технічно недоступний без стабільного reference  
**Вплив:** 🔴 **Передумова для TD-03 HW-verify** — код D-2 можна написати без CLKIN, але перевірити тільки після підключення.  
**Рішення:** Підключити GPIO4 → mikroBUS Pin16. Налаштувати ESP32-S3 LEDC на вихід опорної частоти.  
**Примітка:** `LDC1101_ARCHITECTURE.md §10, задача 10` — задокументовано там.

---

#### TD-09 — Авто-рекалібровка (Strategy B) не активна
**Файл:** `docs/architecture/LDC1101_ARCHITECTURE.md §7.2`  
**Контекст:** Strategy B (periodic background re-calibration) описана в архітектурі як placeholder. Поточна реалізація: manual `'R'` key або `POST /api/v1/calibrate` (stub, TD-13). Вимірювання дрейфу Wave 8: `2.87%/20хв` при кімнатній температурі.  
**Вплив:** Середній — тривалі hw-сесії (C-6, ~45хв) потребуватимуть ручного рекалібрування.  
**Рішення:** Реалізувати auto-recal таймер (~10хв interval) або threshold-based re-cal при відсутності монети.

---

### Категорія 3 — Connectivity

#### TD-10 — WebSocket sensor frames не реалізовано (A-6)
**Файл:** `lib/HttpServer/src/HttpServer.cpp`  
**Контекст:** Wave 8 A-6 (WebSocket real-time sensor stream) позначено в WAVE8_ROADMAP.md як `❌ відкладено`. Поточне рішення: REST polling. Для Web UI live display — stub.  
**Вплив:** Низький — функціональність є через polling.  
**Рішення:** Wave 9 post-C-6 — AsyncWebSocket handler + sensor frame push при кожному `update()`.

---

#### TD-11 — mDNS вимкнено (Bug B-03 OOM)
**Файл:** `lib/WiFiManager/src/WiFiManager.cpp`  
**Контекст:** `cointrace.local` не відповідає. Bug B-03: mDNS + AsyncWebSocket одночасно → OOM. Зафіксовано в WAVE8_ROADMAP.md. mDNS вимкнено як обхідний захід.  
**Вплив:** Низький — `http://192.168.88.53` (IP) працює, але `cointrace.local` зручніший.  
**Рішення:** Після реалізації WebSocket (TD-10) — профілювати heap, усунути OOM, re-enable mDNS.

---

#### TD-12 — AP mode не hw-верифіковано
**Файл:** `lib/WiFiManager/src/WiFiManager.cpp` — AP fallback  
**Контекст:** При недоступності відомої мережі — fallback на AP SoftAP `192.168.4.1`. Реалізовано, але не тестувалось на реальному пристрої.  
**Вплив:** Низький — ймовірно працює, але без гарантії.  
**Рішення:** Швидка перевірка Wave 9 — вимкнути відому мережу, перевірити AP mode.

---

#### TD-13 — `POST /api/v1/calibrate` stub 503
**Файл:** `lib/HttpServer/src/HttpServer.cpp:516`  
**Код:**
```cpp
// POST /api/v1/calibrate — stub (sensor not ready until C-2)
sendError(req, 503, "sensor_not_ready");
```
**Контекст:** RESTful calibrate endpoint повертає 503. Ручний recal через `'R'` key є. HTTP API для автоматизованого тестування відсутній.  
**Вплив:** Низький — blocking тільки для scripted test automation.  
**Рішення:** Wave 9+ — реалізувати trigger recal через HTTP POST.

---

#### TD-14 — BLE GATT service відкладено (v2 hardware)
**Файл:** `lib/HttpServer/src/HttpServer.cpp:101`  
**Код:**
```cpp
doc["ble"] = "off";  // BLE not implemented yet (Wave 8 future)
```
**Контекст:** BLE потребує PSRAM (heap > 300 KB для BLE stack). ESP32-S3FN8 без PSRAM — недостатньо ресурсів.  
**Вплив:** Низький — v2 feature, не блокує поточну роботу.  
**Рішення:** v2 hardware з PSRAM (ESP32-S3R8 або аналог).  
**Статус:** ⚪ Свідомо відкладено.

---

### Категорія 4 — Logger audit backlog

#### TD-15 — LOGGER_AUDIT §13 LF-FUTURE-1..5 відкриті
**Файл:** `docs/audit/LOGGER_AUDIT.md §13`  
**Контекст:** 5 відкритих позицій Logger backlog:
- `LF-FUTURE-1`: Додати `File` поле до `LogEntry` (MEDIUM)
- `LF-FUTURE-2`: Thread-safety для multi-core (MEDIUM) — esp. при Wave 9 async capture
- `LF-FUTURE-3`: Log rotation policy при повному SD (LOW)
- `LF-FUTURE-4`: Structured logging (JSON) — опціонально (LOW)
- `LF-FUTURE-5`: Remote syslog sink — опціонально (LOW)

Закриті (для довідки): `LF-FUTURE-6` (`LogLevel::NONE`), `LF-FUTURE-7` (UTF-8 encoding).  
**Вплив:** Низький — поточна система працює. LF-FUTURE-2 стає актуальнішим при Wave 9 D-1 (concurrent capture loop).  
**Рішення:** Розглянути LF-FUTURE-2 при Wave 9 D-1 design.

---

### Категорія 5 — Документація (stale)

#### TD-16 — `METAL_MATCHER_ARCHITECTURE.md` C-7b/e показані як Pending ~~(закрито 2026-03-30)~~
**Файл:** `docs/architecture/METAL_MATCHER_ARCHITECTURE.md:13,14`  
**Контекст:** Таблиця компонентів у §1 показує:
```
| drawQuickScreen() / classifyQuick() | src/main.cpp  | 🔲 Pending (C-7b) |
| gMatcher integration в main.cpp     | src/main.cpp  | 🔲 Pending (C-7e) |
```
Обидва реалізовані та hw-verified у Wave 8 C-7b/e.  
**Вплив:** None — лише документаційна невідповідність.  
**Рішення:** Оновити таблицю → `✅ Реалізовано (C-7b/e, hw-verified 2026-03-30)`.  
**Статус:** ✅ **Closed** — виправлено у цьому ж коміті (2026-03-30).

---

#### TD-17 — `LDC1101_ARCHITECTURE.md §10` задачі без статусу
**Файл:** `docs/architecture/LDC1101_ARCHITECTURE.md §10`  
**Контекст:** §10 містить таблицю 10 задач (задача 1..10) включаючи:
- Задача 9: `lhrContinuous` convTimeMs розрахунок (пов'язано з TD-03)
- Задача 10: CLKIN/LEDC hardware wiring (пов'язано з TD-08)

Жодна задача не має маркера `✅` або `❌`.  
**Вплив:** None — лише незручність при перегляді.  
**Рішення:** Wave 9 — при закритті кожної задачі позначати статус у §10.

---

## 3. Закриті борги

| ID | Опис | Дата закриття | Коміт | Хвиля |
|----|------|---------------|-------|-------|
| TD-16 | METAL_MATCHER_ARCHITECTURE.md C-7b/e стale Pending status | 2026-03-30 | (цей коміт) | Wave 8 closure |

---

## 4. Changelog

- **1.0.0** (2026-03-30) — Початкове створення після Wave 8 closure. Повний аудит усього проєкту: 17 позицій виявлено (з них TD-16 вже закрито в тому ж коміті). Методологія: `grep` TODOs/FIXMEs/stubs по src/ та lib/, огляд docs/architecture/, docs/audit/ LOGGER_AUDIT §13 backlog, WAVE8_ROADMAP.md відомі відкриті проблеми.
