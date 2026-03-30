# HW Quick Screen Test Guide — Wave 8 C-7b/e

**Версія:** 1.0.0  
**Дата:** 2026-03-28  
**Коміт:** `a41f7af` — `feat(C-7b,e): drawQuickScreen + gMatcher integration + R-key recalibrate`  
**Cross-ref:**
- `docs/architecture/QUICK_SCREEN_SPEC.md` — повна специфікація Quick Screen
- `docs/guides/HW_TESTING.md` — загальний HW testing pipeline (flash, REST тести)
- `docs/guides/UART_DEBUG_SETUP.md` — налаштування FT232RL для Serial Monitor
- `src/main.cpp` — `drawQuickScreen()`, `classifyQuick()`, Quick Screen IDLE branch

---

## Огляд

Цей гайд покриває верифікацію **HW-QS-1..5** — набір checklist-тестів для Quick Screen після
першого прошивки `a41f7af`.

**HW-QS-6 ЗАКРИТО** заздалегідь: пороги `QUICK_*_THRESH` є `static constexpr` — якщо
номінальні значення (SILVER=40%, COPPER=30%, ALUM=15%) не підходять, їх коригують у
`src/main.cpp` без зміни архітектури. Процедуру тюнінгу описано в §5.

```
Послідовність:
  0. SD-картка     — matcher.json + index.json (cardreader)
  1. Flash           — прошити девайс (pio run --target upload)
  2. Serial Monitor  — відкрити por FT232RL (не COM3!)
  3. HW-QS-1         — Quick Screen відображається
  4. HW-QS-2         — ENTER → STEP_BASE
  5. HW-QS-3         — R → recalibrate
  6. HW-QS-4         — threshold classification ≥3/4 монет
  7. HW-QS-5         — matchFull через HTTP POST
  8. (якщо потрібно) — тюнінг порогів → re-flash
```

---

## Необхідне обладнання

| Що | Деталі |
|---|---|
| M5Stack Cardputer-Adv | ESP32-S3FN8, прошитий `cointrace-dev` |
| FT232RL адаптер | COM4 — для Serial Monitor (COM3 скидається при відкритті) |
| USB-кабель | COM3 — для flash |
| WiFi | Device в STA mode, IP відомий (для HW-QS-5) |
| Тестові монети | Ag (срібна), Cu (мідна/бронзова), Al (алюмінієва кришка), Fe (стальна монета або будь-який феромагнетик) |
| Spacer 0.6mm | p3 протокол — для відтворення C-5 умов |

> **CLKIN:** `getLiveL()` та ΔL на дисплеї потребують CLKIN підключеного на GPIO4
> (mikroBUS Pin16). Без CLKIN ΔL рядок показує `-- (no CLKIN)` — це очікувана поведінка,
> ferro detection вимкнено. Тест HW-QS-1..4 можна пройти без CLKIN (тільки ΔRp%).

---

## §0 — Підготовка SD-картки

**Потрібно:** мікро-SD картка (FAT32) + card reader, підключений до ПК.

### Структура директорій на SD

```
SD:/
└── CoinTrace/
    ├── matcher.json        ← ваги MetalMatcher (обов'язковий для HW-QS-5)
    └── database/
        └── index.json      ← fingerprint DB (потрібна для matchFull())
```

### Крок 1 — скопіювати matcher.json

Файл вже є у репозиторії як seed: `data/sd_seed/CoinTrace/matcher.json`

```powershell
# Замінити E: на букву вашої SD-картки
$sd = "E:"

# Створити директорію якщо не існує
New-Item -ItemType Directory -Force "$sd\CoinTrace"

# Скопіювати matcher.json
Copy-Item "d:\GitHub\CoinTrace\data\sd_seed\CoinTrace\matcher.json" "$sd\CoinTrace\matcher.json"
```

**Вміст файлу** (версія seed, редагується без rebuild — перезавантаження застосовує зміни):

```json
{
  "_comment": "CoinTrace MetalMatcher v1. Weights order: [dRp1_n, k1, k2, slope, dL1_n].",
  "full_weights":  [1.0, 1.0, 1.0, 0.0, 1.0],
  "quick_weights": [1.5, 0.0, 0.0, 0.0, 2.5],
  "sigma": 0.35,
  "min_confidence": 0.3,
  "ferro_thresh_dL1_n": 99.0
}
```

> `full_weights[3]` (slope) = 0.0 навмисно — slope є лінійною функцією k2 для p3 протоколу
> і не додає незалежної інформації (ADR-C5-003, VectorCompute.h).

### Крок 2 — скопіювати index.json (якщо є)

Якщо DB вже заповнена:

```powershell
New-Item -ItemType Directory -Force "$sd\CoinTrace\database"
Copy-Item "d:\GitHub\CoinTrace\data\sd_seed\CoinTrace\database\index.json" `
          "$sd\CoinTrace\database\index.json"
```

Якщо `index.json` порожній або відсутній — HW-QS-5 пройде з результатом
`[Meas] Matcher not ready — skipping match` (acceptable).

### Крок 3 — вставити SD у Cardputer

1. Вставити SD у слот Cardputer (мікро-SD, нижня частина корпусу)
2. Перезавантажити або прошити заново
3. Перевірити Serial Monitor при старті:
   ```
   [Matcher] Config loaded — sigma=0.35 weights=[1.0,1.0,1.0,0.0,1.0]
   [Cache]   FingerprintCache ready — N entries
   ```
   Якщо `[Matcher] Using default config` — SD не змонтована або файл не знайдено.
   Якщо `[SD]` помилки у Serial — перевірити форматування (FAT32, не exFAT).

---

## §1 — Flash

```powershell
cd d:\GitHub\CoinTrace

# Перевірити що тести pass (якщо ще не перевіряли після останнього коміту)
pio test -e native-test

# Зібрати + прошити
pio run -e cointrace-dev --target upload

# Перевірити розмір
pio run -e cointrace-dev 2>&1 | Select-String "RAM:|Flash:"
```

**Очікуваний розмір:**
```
RAM:   [======    ]  ~62% (≤ 200 KB з 327 KB — OK)
Flash: [======    ]  ~58% (≤ 1.52 MB з 2.62 MB — OK)
```

> ⚠️ Якщо `COM3: Access is denied` — закрити VS Code Serial Monitor / PlatformIO Monitor.

---

## §2 — Serial Monitor

**Відкрити через FT232RL (COM4), не через USB-CDC (COM3).**

```powershell
# FT232RL → COM4, 115200 baud
pio device monitor --port COM4 --baud 115200
```

або в PlatformIO: `Device Monitor → COM4 → 115200`.

Після старту пристрою очікувати в лозі:
```
[Matcher] Using default config (sigma=0.35)   ← або "Config loaded" якщо є SD
[Cache]   FingerprintCache ready — N entries  ← або "unavailable" якщо немає SD
[Storage] StorageManager ready — NVS:ok ...
```

> Якщо `[Matcher] Config loaded` — завантажено `/CoinTrace/matcher.json` з SD.  
> Якщо `[Matcher] Using default config` — SD відсутня або файл не знайдено, працює з компільованими дефолтами (sigma=0.35). Для HW-QS-1..4 це OK.

---

## §3 — HW-QS-1: Quick Screen відображається

**Мета:** перевірити що Quick Screen активується при підкладанні монети в IDLE.

### Кроки

1. Переконатись що пристрій у IDLE (відображає `CoinTrace / Place coin on coil to start`)
2. Покласти spacer 0.6mm на котушку, потім монету зверху
   - spacer запобігає КЗ між монетою та котушкою
   - **пороги класифікації скалібровані для d≈0.6mm** (C-5 hw-сесія, p3 протокол) —
     вимірювання без spacer дасть більший dRp% і зіб'є класифікацію
3. **Зняти руку** з монети відразу хвилину кладеть — очікувати ~500 ms
   - 0–100 ms: debouncing — екран не міняється
   - 100 ms: COIN_PRESENT — екран показує `Stabilizing...` (1–2 секунди помітно менше)
   - 500 ms: settling window закінчився — Quick Screen з'являється із стабільним dRp%

### Очікуваний результат на дисплеї
**Фаза 1** — ~100–500 ms після покладання (сетлінг):
```
┌────────────────────────────────────────┐
│                                         │
│                                         │
│   Stabilizing...                        │  ← сірий текст
│                                         │
└────────────────────────────────────────┘
```

**Фаза 2** — після 500 ms — Quick Screen зі стабільними значеннями:```
┌─────────────────────────────────────────┐
│ QUICK SCREEN             [ENTER=Full]   │  ← білий заголовок
│                                         │
│   dRp: +NN.N%      Rp:NNNNN           │  ← жовтий delta; сірий абсолют
│   dL:  +NN ct       L:NNNNN           │  ← жовтий delta (або -- без CLKIN)
│                                         │
│   Ferro: NO  v                         │  ← зелений
│                                         │
│   SILVER                               │  ← великий шрифт, зелений або сірий
│   (quick estimate)                     │
│                                         │
│ [ENTER] Full meas  [R] Recal           │  ← сіра підказка
└─────────────────────────────────────────┘
```

### Очікувані рядки у Serial

```
[LDC1101] Coin PRESENT (RP=NNNNN, ...)
[Meas] Coin detected — settling 400 ms
... (чекаємо 400 ms settling) ...
[Meas] QuickScreen ON: basRp=NNNNN liveRp=NNNNN dRp=+NN.N%
```

> До 2026-03-28 перший рядок відразу логував QuickScreen ON зі зниженим dRp% (рука ще на монеті).
> Тепер "Coin detected" виходить імедіатно, QuickScreen ON — після стабілізації.

### Критерій PASS

- [ ] Дісплей показує `Stabilizing...` перші ~400 ms, потім перемикається на Quick Screen
- [ ] `dRp:` рядок жовтий і не нуль для металевої монети
- [ ] Serial показує спочатку `Coin detected — settling 400 ms`, потім `QuickScreen ON:`

### Можлива проблема

| Симптом | Причина | Дія |
|---|---|---|
| Екран не змінюється | `gLDC == nullptr` або `isReady() == false` | Перевірити `[LDC1101]` рядки у Serial при старті |
| `dRp: +0.0%` | Монета не детектована або базелайн встановлено з монетою | Зняти монету, натиснути R (recalibrate), покласти знову |
| `dL: -- (no CLKIN)` | CLKIN не підключений | Очікувана поведінка — ΔL та ferro недоступні |

---

## §4 — HW-QS-2: ENTER у Quick Screen → STEP_BASE

**Мета:** перевірити що ENTER із монетою на котушці запускає повний цикл вимірювання.

### Кроки

1. Підкласти монету → Quick Screen активний (HW-QS-1 pass)
2. Натиснути **ENTER**

### Очікуваний результат

- Дисплей перемикається на `Step 1/4 — Base` (повний цикл вимірювання)
- Serial:
  ```
  [Meas] Meas session started (ENTER, STEP_BASE)
  ```

### Критерій PASS

- [ ] Перехід у STEP_BASE одразу після ENTER
- [ ] Дисплей показує step UI, не Quick Screen

---

## §5 — HW-QS-3: R-key → recalibrate

**Мета:** перевірити що `R` у IDLE оновлює baseline без монети.

### Кроки

1. **Spacer залишити на котушці, монету зняти** — recalibrate вимірює baseline в тій самій
   геометрії що й вимірювання з монетою (spacer на котушці, монети немає)
2. Натиснути **R** (або **r**)

### Очікуваний результат на дисплеї

- На ~250ms з'являється `  Recalibrating...` (cyan текст) в рядку Ferro
- Через ~250ms дисплей повертається у `drawMeasIdle()` (монети немає)

### Очікувані рядки у Serial

```
[LDC1101] recalibrate: rpBase NNNNN→NNNNN  lBase NNNN→NNNN  (ok=N/10)
[Meas] Recalibrate requested (R key)
```

### Критерій PASS

- [ ] Serial показує `recalibrate:` з новим baseline значенням
- [ ] `ok=N/10` — N ≥ 5 (інакше baseline не оновлюється)
- [ ] Після recalibrate: підкладення тієї ж монети показує ту саму `dRp%` з невеликим відхиленням (±1-2%)

### Перевірка захисту від recalibrate з монетою

**Кейс A — монета є, Quick Screen активний:**
1. Покласти монету → дочекатись Quick Screen (після settling)
2. Натиснути **R**
3. Serial повинен показати:
   ```
   [LDC1101] recalibrate() — coin present, remove coin first
   ```
4. Дисплей **не** показує `Recalibrating...`, Quick Screen залишається

**Кейс B — монета є, result screen показується (після повного вимірювання):**
1. Пройти повний 4-кроковий цикл → result screen показується, монету **не знімати**
2. Натиснути **R**
3. Result screen **залишається** — R повністю ігнорується (`!sResultPending` guard)
4. Serial `recalibrate()` **не** виводиться (код навіть не доходить до recalibrate())
5. Знизу екрану може з'явитись `Key: r` — це очікувана поведінка (fallback key display)

> **Чому два окремих guard:** `sResultPending` захищає від знищення result screen
> відображення; `isCoinPresent()` всередині `recalibrate()` захищає від запису
> baseline з монетою. Обидva guard незалежні.

---

## §6 — HW-QS-4: Phase 1 threshold classification (≥3/4 монет)

**Мета:** перевірити що класифікація правильна для ≥3 з 4 тестових монет.

### Підготовка

Записати baseline без монети:
```
Відкрити Serial Monitor → зняти всі монети → отримати з Serial:
[LDC1101] recalibrate: rpBase=NNNNN ...
```

Або просто покласти металевий предмет для першого орієнтиру.

### Таблиця тестів

результати вимірювань 2026-03-30 (5 вимірів / монета, basRp=57344, spacer 0.6mm):

| № | Монета | dRp% mean | dRp% range | dL mean ct | ferro= | Новий клас | PASS? |
|---|--------|-----------|-----------|-----------|--------|-----------|-------|
| 1 | XAG999 American Silver Eagle 1oz | **33.4%** | 32.1–34.4 | −12742 | NO | SILVER ✅ | ✅ |
| 2 | XCU Russian Empire 5 Kopecks | **43.4%** | 42.9–43.7 | −13696 | NO | COPPER ✅ | ✅ |
| 3 | XZNNIP Ukraine 10 UAH 2022 | **44.3%** | 42.5–45.2 | −12093 | NO | COPPER ⚠ | — |
| 4 | XFE Germany 1.5 Euro 1997 | **36.6%** | 36.2–37.1 | −12925 | NO | ? ⚠ | — |
| 5 | XAL Germany 50 Pfennig 1919 | **38.6%** | 37.6–39.9 | −12019 | NO | ALUM ✅ | ✅ |

**Нові пороги (після калібрування, `src/main.cpp`):**
```
COPPER_THRESH = 41.0%  → Cu (43.4%) ✅, ZnNi (44.3%) → COPPER ⚠ (близький метал)
ALUM_THRESH   = 37.3%  → Al (38.6%) ✅  ⚠ 0.5% gap до XFE (max 37.1%)
SILVER_THRESH = 35.0%  → Ag (33.4%) → SILVER ✅; Fe (36.6%) → ? (35–37.3% зона)
```

> **⚠️ Спостереження 1 — Інвертований порядок сигналів:**
> Оригінальне припущення (срібло=найвищий сигнал) виявилось ХИБНИМ.
> American Silver Eagle (∅38mm) більший за активну область котушки → нижча ефективність
> eddy coupling, ніж у менших монет CU/XAL. Порядок в реальних даних:
> `Ag(33.4%) < Fe(36.6%) < Al(38.6%) < Cu(43.4%) ≈ ZnNi(44.3%)`
>
> **⚠️ Спостереження 2 — XFE не показує ferro-відповідь:**
> Всі 5 монет мають НЕГАТИВНИЙ dL (−11912 до −13721 ct). Для справжнього феромагнетика
> dL має бути ПОЗИТИВНИМ (зростання індуктивності). Germany 1.5 Euro 1997 (XFE label в DB)
> — ймовірно Cu-Zn/Ni сплав, не сталь. Ferro flag перевірено: QUICK_FERRO_THRESH_L_RAW=100
> (позитивний поріг) коректний — для реальної стальної монети (S-5).
>
> **⚠️ Спостереження 3 — XZNNIP та XFE потребують уточнення:**
> XZNNIP (ZnNi, 44.3%) класифікується як COPPER — прийнятно для Phase 1 (близька родина металів).
> XFE (36.6%) класифікується як `?` — зона 35–37.3% між SILVER і ALUM з margin 0.5%.
> Точна класифікація цих двох металів — задача Phase 2 (matchQuick() via quick_centroid, Wave 9).

> **Spacer:** всі монети вимірювались **зі spacer 0.6mm** між монетою і котушкою.
> Пороги скалібровані для d≈0.6mm — без spacer dRp% буде вищим.

### Критерій PASS

- [x] XAG999 → SILVER (33.4% < 35%) ✅ 2026-03-30
- [x] XCU → COPPER (43.4% > 41%) ✅ 2026-03-30
- [x] XAL → ALUM (38.6% > 37.3%) ✅ 2026-03-30
- [ ] XFE → STEEL (потребує справжньої феромагнітної монети, S-5)
- [ ] XZNNIP → ? (Phase 2, matchQuick)
- [x] `(quick estimate)` видно на дисплеї — попереджає що це груба оцінка

**PASS критерій HW-QS-4 (≥3/4) — виконано:** AG ✅  CU ✅  AL ✅  = 3/4 = PASS.

---

## §7 — HW-QS-5: matchFull() через Serial + HTTP

**Мета:** перевірити що `matchFull()` у `doMeasCompute()` дає результат з DB.

**Потребує:** SD картка з `/CoinTrace/database/index.json` + `/CoinTrace/matcher.json` (обидва є у sd_seed) та WiFi з'єднання.

> **Передумови (Serial при старті):**
> ```
> [Cache]   FingerprintCache ready — 5 entries   ← SD змонтована, index.json завантажено
> [Matcher] Config loaded — sigma=0.35 ...        ← або "Using default config" якщо SD без matcher.json
> ```
> Якщо `FingerprintCache unavailable` — перевірити SD, re-flash або повторити §0.

### Крок 1 — повний цикл вимірювання

1. Підкласти срібну монету зі spacer 0.6mm → Quick Screen активний
2. **ENTER** → STEP_BASE → **ENTER** → STEP_1 → **ENTER** → STEP_3 → **ENTER** → STEP_DRIFT → **ENTER**
3. По завершенні COMPUTE — перевірити Serial:

```
[Meas] Vec: dRp1=NNNN  k1=N.NNN  k2=N.NNN  slope=N.NNNN  dL1=NNN
[Meas] #1: XAG999 "American Silver Eagle 1oz"  dist=N.NNN  conf=N.NN  algo=full
[Meas] #2: ...
[Meas] Saved #NN — 4pos [...]  conf=N.NN
```

> `[Meas] Matcher not ready — skipping match` при відсутній DB — acceptable.

### Крок 2 — читання збереженого виміру через HTTP

Endpoint `GET /api/v1/measure/{id}`. ID = meas_count − 1 (з `/api/v1/status`):

```powershell
$ip = "192.168.88.53"   # замінити на реальний IP (Serial → [WiFi])

# 1. Отримати meas_count
$status = Invoke-RestMethod "http://$ip/api/v1/status"
$id = $status.meas_count - 1
Write-Host "Latest measurement ID: $id"

# 2. Завантажити вимір
Invoke-RestMethod "http://$ip/api/v1/measure/$id" | ConvertTo-Json
```

Очікуваний відповідь (фрагмент):
```json
{
  "id": 3,
  "metal_code": "XAG999",
  "coin_name": "American Silver Eagle 1oz",
  "conf": 0.87
}
```

> ⚠️ `GET /api/v1/measurements/latest` — не існує. Завжди через `meas_count − 1`.

### Крок 3 — пряме query вектора (опційно)

Для ручного тестування без повного циклу — POST з центроїдом XAG999 (C-5 hw-verified):

```powershell
# XAG999 centroid з index.json (C-5 hw-verified 2026-03-27)
$body = '{"algo_ver":1,"protocol_id":"p3_MIKROE3240_b06_012mm",
          "vector":{"dRp1_n":-10.675,"k1":1.22315,"k2":1.35472,"slope":0.17736,"dL1_n":-2.4176}}'
Invoke-RestMethod -Method POST -Uri "http://$ip/api/v1/database/match" `
    -ContentType "application/json" -Body $body
```

Очікуваний результат: `match.metal_code = "XAG999"`, `conf > 0.9`.

### Критерій PASS

- [x] Serial показує `[Meas] Vec:` + conf=0.98 ✅ 2026-03-30
- [x] `GET /api/v1/measure/87` → `metal_code=XAG999`, `conf=0.984`, `pos_count=4` ✅ 2026-03-30
- [x] `GET /api/v1/measure/{meas_count-1}` повертає запис з `metal_code` і `conf > 0`

---

## §8 — Тюнінг порогів (якщо HW-QS-4 провалено)

Якщо класифікація неправильна — заміряти реальні `dRp%` для кожного металу і відкоригувати
константи у `src/main.cpp`:

```cpp
// src/main.cpp — Quick Screen constants (Wave 8 C-7b)
// Знайти блок приблизно на рядку 110-120:

static constexpr float QUICK_SILVER_THRESH_PCT  = 40.0f;   // ← коригувати
static constexpr float QUICK_COPPER_THRESH_PCT  = 30.0f;   // ← коригувати
static constexpr float QUICK_ALUM_THRESH_PCT    = 15.0f;   // ← коригувати
static constexpr float QUICK_FERRO_THRESH_L_RAW = 100.0f;  // ← коригувати якщо CLKIN є
```

### Процедура тюнінгу

1. Заміряти `dRp%` для кожного металу (з Serial `QuickScreen ON: ... dRp=+NN.N%`)
2. Побудувати таблицю:

   | Метал | Виміряний dRp% | Новий поріг |
   |-------|---------------|-------------|
   | Ag    | (записати)     | Ag_min + 5% |
   | Cu    | (записати)     | між Cu_min і Ag_min |  
   | Al    | (записати)     | між Al_min і Cu_min |
   | Fe    | dL_raw > ?    | FERRO_THRESH_L_RAW  |

3. Оновити `static constexpr` значення — логіка: SILVER > COPPER > ALUM > noise floor
4. Re-build: `pio run -e cointrace-dev --target upload`
5. Повторити HW-QS-4

### Коміт після тюнінгу

```
fix(C-7b): tune Quick Screen thresholds after first HW verification

HW-QS-4 measurement results (p3 d≈0.6mm, DATE):
  Ag XXX: dRp=+NN.N% → SILVER thresh: XX%
  Cu YYY: dRp=+NN.N% → COPPER thresh: XX%
  Al ZZZ: dRp=+NN.N% → ALUM thresh: XX%
  Fe/Ni:  dL=+NNN ct → FERRO thresh: NNN ct
```

---

## §9 — Checklist зведення

| ID | Тест | Результат | Примітки | Дата |
|----|------|-----------|----------|------|
| HW-QS-1 | Quick Screen відображається при COIN_PRESENT | **PASS** | Stabilizing→QS flow OK | 2026-03-28 |
| HW-QS-2 | ENTER → STEP_BASE | **PASS** | | 2026-03-28 |
| HW-QS-3 | R → recalibrate (без монети) | **PASS** | ok=N/10 коректний | 2026-03-28 |
| HW-QS-3b | R → ignored (Quick Screen активний, монета є) | **PASS** | `isCoinPresent()` guard | 2026-03-28 |
| HW-QS-3c | R → ignored (result screen, `sResultPending=true`) | **PASS** | `!sResultPending` guard | 2026-03-28 |
| **HW-QS-4** | Threshold classification ≥3/4 монет | **PASS** | AG✅ CU✅ AL✅ = 3/4 | 2026-03-30 |
| **HW-QS-5** | matchFull() result у Serial або HTTP | **PASS** | XAG999 conf=0.984, id=87 ✅ | 2026-03-30 |
| Threshold | SILVER thresh | **35.0%** | Ag≈33.4% (mean, 5 вимірів) | 2026-03-30 |
| Threshold | COPPER thresh | **41.0%** | Cu≈43.4%, ZnNi≈44.3% | 2026-03-30 |
| Threshold | ALUM thresh | **37.3%** | Al≈38.6%; ⚠ 0.5% gap до XFE | 2026-03-30 |
| Threshold | FERRO thresh | 100 ct (не перевірено) | XFE coin non-ferro; потребує S-5 | — |

> **HW-QS-1..5 всі PASS** (2026-03-30). Наступний крок: закрити Wave 8 — оновити `WAVE8_ROADMAP.md` та `QUICK_SCREEN_SPEC.md`.

**Після проходження всіх HW-QS-1..5:**
- Оновити `WAVE8_ROADMAP.md` — позначити HW-QS-1..5 як `[x]`
- Оновити `QUICK_SCREEN_SPEC.md` — статус `PARTIALLY_IMPLEMENTED → IMPLEMENTED`
- Якщо пороги скориговані — додати `fix:` коміт з реальними значеннями
