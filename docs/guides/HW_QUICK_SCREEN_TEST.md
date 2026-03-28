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
2. Підкласти срібну монету (або будь-яку монету) на котушку без spacer'ів
3. Чекати ~500 ms — час coin-detect + стабілізація RP

### Очікуваний результат на дисплеї

```
┌─────────────────────────────────────────┐
│ QUICK SCREEN             [ENTER=Full]   │  ← білий заголовок
│                                         │
│   dRp: +NN.N%                          │  ← жовтий (або сірий якщо < 2%)
│   dL:  +NN ct                          │  ← жовтий (або -- якщо немає CLKIN)
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
[Meas] QuickScreen ON: basRp=NNNNN liveRp=NNNNN dRp=+NN.N%
```

### Критерій PASS

- [ ] Екран переключається з `Place coin on coil` на Quick Screen після появи монети
- [ ] `dRp:` рядок жовтий і не нуль для металевої монети
- [ ] Serial показує `QuickScreen ON:` рядок

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

1. **Зняти монету** з котушки (якщо лежала) — чекати `QuickScreen OFF` у Serial
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

1. Покласти монету → Quick Screen
2. Натиснути **R**
3. Serial повинен показати:
   ```
   [LDC1101] recalibrate() — coin present, ignored
   ```
4. Дисплей **не** показує `Recalibrating...`, Quick Screen залишається

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

Для кожної монети — підкласти на котушку, записати значення з Serial та дисплею:

| № | Монета | Очікув. клас | dRp% (виміряний) | dL ct | Ferro | Клас на екрані | PASS? |
|---|--------|-------------|------------------|-------|-------|---------------|-------|
| 1 | Ag монета (срібна) | SILVER | > 40% | — | NO | SILVER | |
| 2 | Cu/бронза | COPPER | 30–40% | — | NO | COPPER | |
| 3 | Al (кришка) | ALUM | 15–30% | — | NO | ALUM | |
| 4 | Fe (стальна монета або скоба) | STEEL | будь-який | > 100 ct | YES | STEEL ! | |

> **Примітка:** значення dRp% залежать від товщини монети, spacer, та поточного baseline.
> p3 протокол (d≈0.6mm) дає значно більший сигнал ніж p2 (d≈1.4mm).
> Якщо всі монети показують один клас — потрібен тюнінг порогів (§7).

### Критерій PASS

- [ ] Мінімум 3 з 4 класифікацій правильні
- [ ] STEEL завжди правильний якщо CLKIN підключений (ferro via dL_raw)
- [ ] `(quick estimate)` видно на дисплеї — попереджає що це груба оцінка

---

## §7 — HW-QS-5: MetalMatcher через HTTP POST /api/v1/database/match

**Мета:** перевірити що `matchFull()` у `doMeasCompute()` дає результат з DB.

**Потребує:** SD картка з `/CoinTrace/index.json` (fingerprint DB) та WiFi з'єднання.

### Кроки

1. Підкласти срібну монету → Quick Screen → ENTER → пройти повний 4-кроковий цикл
2. По завершенні перевірити Serial:
   ```
   [Meas] Vec: dRp1=NNNN  k1=N.NNN  k2=N.NNN  slope=N.NNNN  dL1=NNN
   [Meas] Match: <назва монети>  conf=N.NN
   ```

### HTTP перевірка (якщо DB завантажена)

```powershell
# Замінити IP на реальний
$ip = "192.168.88.53"

# Запит до бази — пряме query по вектору
$body = '{"dRp1_n": 0.085, "k1": 0.72, "k2": 0.61, "slope": -0.0041, "dL1_n": 0.12}'
Invoke-RestMethod -Method POST -Uri "http://$ip/api/v1/database/match" `
    -ContentType "application/json" -Body $body

# Або переглянути результат останнього вимірювання
Invoke-RestMethod "http://$ip/api/v1/measurements/latest"
```

### Критерій PASS

- [ ] Serial показує `[Meas] Match:` рядок з conf > 0 (якщо DB не порожня)
- [ ] Або `[Meas] Matcher not ready — skipping match` якщо DB відсутня (acceptable)
- [ ] HTTP GET `/api/v1/measurements/latest` повертає запис з `metal_code` та `coin_name`

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

Заповнити після тестування:

| ID | Тест | Результат | Примітки | Дата |
|----|------|-----------|----------|------|
| HW-QS-1 | Quick Screen відображається при COIN_PRESENT | | | |
| HW-QS-2 | ENTER → STEP_BASE | | | |
| HW-QS-3 | R → recalibrate (без монети) | | | |
| HW-QS-3b | R → ignored (з монетою) | | | |
| HW-QS-4 | Threshold classification ≥3/4 монет | /4 правильних | | |
| HW-QS-5 | matchFull() result у Serial або HTTP | | | |
| Threshold | SILVER thresh реальний dRp% | | | |
| Threshold | COPPER thresh реальний dRp% | | | |
| Threshold | ALUM thresh реальний dRp% | | | |
| Threshold | FERRO thresh реальний dL ct | | | |

**Після проходження всіх HW-QS-1..5:**
- Оновити `WAVE8_ROADMAP.md` — позначити HW-QS-1..5 як `[x]`
- Оновити `QUICK_SCREEN_SPEC.md` — статус `PARTIALLY_IMPLEMENTED → IMPLEMENTED`
- Якщо пороги скориговані — додати `fix:` коміт з реальними значеннями
