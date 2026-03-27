# Quick Screen Specification — CoinTrace

**Версія:** 1.3.0
**Дата:** 2026-03-27
**Статус:** Специфікація — очікує реалізації (Wave 8 C-7)
**Cross-ref:** `COLLECTOR_USE_CASE.md §9`, `MEASUREMENT_WORKFLOW.md`, `METAL_MATCHER_ARCHITECTURE.md`, `WAVE8_ROADMAP.md §C-7`

**Changelog:**
- 1.3.0 (2026-03-27) — C-5 hw-data sync: пороги класифікації оновлені для p3 d=0.6mm (SILVER 25→40%, COPPER 14→30%, ALUM 5→15%). Додано caveat про PENDING HW-QS-6 baseline calibration. Старі значення базувались на синтетичних p2 d=1.4mm даних.
- 1.2.0 (2026-03-26) — Pre-implementation sync: §9 пункт 1 (ENTER як шлях запуску) позначено ✅ — MEASUREMENT_WORKFLOW.md v1.0.0 вже містить цей пункт як path #2.
- 1.1.0 (2026-03-26) — Production-ready revision: (A) одиниці dL виправлено (raw L_DATA counts + ADR-CLKIN-002 guard: isLDataValid() + lDataValid в dL_raw/isFerro); (B) recalibrate() — N-sample avg (10 samples, ok≥5) + internal coin guard + note про blocking loop() ~250ms; (C) drawMeasStep_full(sMeas) сигнатуру виправлено; (D) gLFS.isDataMounted() видалено з ENTER (ADR-QS-5); (E) QUICK_NOISE_FLOOR_PCT + QUICK_L_NOISE_FLOOR_CT як constexpr; (F) sQuickScreenFresh — file-scope reset механізм уточнено; (G) §4.2 Phase 2 quick_centroid generation pipeline документовано; (H) §9 пункти 2–3 ✅; (I) main.cpp:150-152 STEP_BASE/STEP_1/STEP_3 display strings p1→p2.
- 1.0.0 (2026-03-25) — Початкова версія

---

## Зміст

1. [Мета і scope](#1-мета-і-scope)
2. [Інтеграція зі state machine](#2-інтеграція-зі-state-machine)
3. [Джерела даних — що доступно в IDLE](#3-джерела-даних--що-доступно-в-idle)
4. [Алгоритм класифікації — фазова модель](#4-алгоритм-класифікації--фазова-модель)
5. [Display layout](#5-display-layout)
6. [Keyboard handling](#6-keyboard-handling)
7. [UART logging](#7-uart-logging)
8. [Зміни у LDC1101Plugin](#8-зміни-у-ldc1101plugin)
9. [Зміни у MEASUREMENT_WORKFLOW.md](#9-зміни-у-measurement_workflowmd)
10. [Архітектурні рішення (ADR)](#10-архітектурні-рішення-adr)
11. [Пов'язані файли](#11-повязані-файли)

---

## 1. Мета і scope

### Що таке Quick Screen

Quick Screen — **автоматичний live-дисплей** у стані IDLE, який активується щойно монета кладеться на котушку. Без spacerів, без натискань. За ~2 секунди (час стабілізації RP після coin-detect) екран показує:

- **ΔRp%** — відносна зміна паразитного опору (провідність металу)
- **ΔL ct** — абсолютна зміна L_DATA в raw counts (пропорційна магнітній проникності)
- **is_ferro** — прапор феромагнітного core
- **Клас металу** — груба ідентифікація з вбудованих порогів (Phase 1) або з DB (Phase 2)

Це реалізація сценарію `COLLECTOR_USE_CASE.md §9`:
> _"Монета на платформу, без spacerів. Результат за ~5 секунд."_

### Що Quick Screen НЕ робить

| Обмеження | Причина |
|---|---|
| Не визначає пробу (Ag999 vs Ag925) | Потрібні k1/k2 — вимагають мінімум 2 spacer-позиції |
| Не запускає повний цикл автоматично | Зроблено навмисно: автостарт прибрано (2026-03-25) |
| Не зберігає запис у LittleFS | Quick Screen — live-дисплей, не сесія |
| Не конвертує ΔL уµH | L_DATA raw → µH потребує параметрів котушки (Phase 2 enhancement) |

### Scope цього документа — Phase 1

Phase 1 реалізує live-дисплей із **threshold-based** класифікацією (без DB query). Phase 2 (після C-5 hw-даних) додає `matchQuick()` з MetalMatcher. Межа між фазами описана в §4.

---

## 2. Інтеграція зі state machine

### Поточна state machine (MEASUREMENT_WORKFLOW.md)

```
IDLE → (HTTP /measure/start + COIN_PRESENT) → STEP_BASE → ... → IDLE
```

### Нова поведінка Quick Screen

Quick Screen **не є новим станом** у `MeasState` enum. Це візуальний режим всередині IDLE.

```
IDLE (no coin)      →  coin placed  →  IDLE (COIN_PRESENT) = Quick Screen
IDLE (Quick Screen) →  ENTER        →  STEP_BASE (повний цикл)
IDLE (Quick Screen) →  монету знято →  IDLE (no coin)
IDLE (Quick Screen) →  'R'          →  recalibrate baseline → IDLE (Quick Screen)
```

**Рефактор drawMeasIdle():**

```
Було:  drawMeasIdle() — один варіант (немає монети)
Стане: drawMeasIdle()        — монети немає  (без змін)
       drawQuickScreen()     — монета є, live дані
```

Викликається з loop() при `sMeas.state == IDLE`:
```cpp
if (gLDC->isCoinPresent()) {
    const float liveRp  = gLDC->getLiveRp();
    const float liveL   = gLDC->getLiveL();
    const float basRp   = gLDC->getBaseline();
    const float basL    = gLDC->getLBaseline();
    drawQuickScreen(liveRp, liveL, basRp, basL);
} else {
    drawMeasIdle();
}
```

### Timing update в loop()

Quick Screen оновлюється разом із стандартним polling RP у IDLE — тобто при кожному виклику loop(). Окремий таймер не потрібен: LDC1101 вже оновлює cache_ кожні ~20 ms через `update()`.

Часткові оновлення (без full redraw) — при кожному тіку IDLE, якщо COIN_PRESENT:
- рядок ΔRp%: `fillRect` + новий відсоток
- рядок ΔL ct: `fillRect` + нове значення
- Заголовок "QUICK SCREEN" та is_ferro рядок — лише при зміні is_ferro або першому відображенні

---

## 3. Джерела даних — що доступно в IDLE

### Дані сенсора

| Значення | Джерело | Наявність | Одиниці | CLKIN |
|---|---|---|---|---|
| `liveRp` | `gLDC->getLiveRp()` | **ПОТРЕБУЄ ДОДАВАННЯ** (§8) | raw RP_DATA (uint16 counts) | незалежний |
| `liveL` | `gLDC->getLiveL()` | **ПОТРЕБУЄ ДОДАВАННЯ** (§8) | raw L_DATA (uint16 counts) | **⚠️ потребує** |
| `baseline` | `gLDC->getBaseline()` | ✅ вже є | raw RP_DATA (float avg) | незалежний |
| `lBaseline` | `gLDC->getLBaseline()` | ✅ вже є | raw L_DATA (float avg) | **⚠️ потребує** |
| `fSensor` | `gLDC->getFSensor()` | ✅ вже є | Hz — опційний для UART debug | need CLKIN |
| `isLDataValid` | `gLDC->isLDataValid()` | **ПОТРЕБУЄ ДОДАВАННЯ** (§8) | bool | — |

> ⚠️ **ADR-CLKIN-002 — L_DATA validity:** `getLiveL()` та `getLBaseline()` повертають **raw L_DATA register codes** тільки якщо CLKIN підключено (GPIO4 → mikroBUS Pin16, hw-verified S-3). Без CLKIN L_DATA = 0 або garbage — будь-яка ΔL логіка, включаючи ferro detection, буде некоректна. Перевіряти через `isLDataValid()` перед використанням. Всі ΔL пороги та відображення використовують raw counts (позначаються `ct`).

### Обчислювані значення (в loop(), на стеку)

```cpp
// ── Quick Screen порогові константи ──────────────────────────────────────────
// Розміщення: src/main.cpp (static constexpr, поруч з drawQuickScreen)
// Значення попередні — верифікувати після hw-сесій S-4 + S-5.
// ⚠️ C-5 audit: при d≈0.6mm (p3 protocol) ВСІ 5 металів мають dRpPct > 25% →
//   старі пороги (SILVER>25%) класифікують ВСЕ як SILVER. Оновлено нижче.
//   Значення — best-estimates з centroid k1. PENDING HW-QS-6: виміряти реальний
//   baseline без монети та dRpPct для кожного металу; скоригувати якщо потрібно.
static constexpr float QUICK_NOISE_FLOOR_PCT   =   2.0f;  // dRpPct нижче — сигнал у шумі (%)
static constexpr float QUICK_L_NOISE_FLOOR_CT  =   2.0f;  // dL_raw нижче — у шумі (raw counts)
static constexpr float QUICK_SILVER_THRESH_PCT =  40.0f;  // dRpPct > 40% → SILVER  (було 25, C-5 p3)
static constexpr float QUICK_COPPER_THRESH_PCT =  30.0f;  // dRpPct > 30% → COPPER  (було 14, C-5 p3)
static constexpr float QUICK_ALUM_THRESH_PCT   =  15.0f;  // dRpPct > 15% → ALUMINIUM (було 5, C-5 p3)
static constexpr float QUICK_FERRO_THRESH_L_RAW = 100.0f; // dL_raw > 100 ct → ferro
                                                            // ⚠️ верифікувати S-5

// ── Обчислення (на стеку у loop()) ───────────────────────────────────────────

// ΔRp% — відносна зміна провідності
// baseline > liveRp для провідних металів (Rp знижується з монетою ближче)
// Від'ємне значення = монета підвищує Rp (нетипово; відобразиться як "?" у класифікації)
const float dRpPct = (baseline > 1.0f)
    ? (baseline - liveRp) / baseline * 100.0f
    : 0.0f;

// ΔL ct — абсолютна зміна L_DATA у raw counts
// ADR-CLKIN-002: валідне тільки якщо CLKIN підключено. Без CLKIN — liveL = garbage.
// isLDataValid() перевіряє clkinGpio_ >= 0 всередині плагіна (§8).
// Знак: позитивний = L зросла (феромагнетик: ефект підсилення індуктивності)
//        від'ємний = L знизилась (типово для Ag/Cu/Al — діамагнетики, вихрові струми)
// ⚠️ Напрямок знаку підлягає hw-верифікації в сесії S-5 (ferro behavior at d≥1.4mm)
const bool  lDataValid = gLDC->isLDataValid();
const float dL_raw     = lDataValid ? (liveL - lBaseline) : 0.0f;

// is_ferro — феромагнітний core
// Guard: false якщо L_DATA invalid — запобігає false ferro positives без CLKIN
// ⚠️ Поріг та знак підлягають верифікації S-5 (сталева монета, 0.6/0.8mm spacer test)
const bool isFerro = lDataValid && (dL_raw > QUICK_FERRO_THRESH_L_RAW);
```

### ⚠️ Важлива математична різниця з fingerprint vector

`dRpPct` у Quick Screen ≠ `dRp1_n` у fingerprint vector:

| | Quick Screen | Fingerprint dRp1 |
|---|---|---|
| Формула | `(baseline_no_coin − liveRp) / baseline` | `(rp[0] − rp[1]) / 800` |
| Фізика | повний сигнал від "нема монети" до "є монета на d≈0.6mm (bare coin, p3 base spacer)" | диференціальний сигнал між d≈0.6mm та d≈1.6mm |
| Типовий Ag999 | **⚠️ pending C-5** (p2 естимат ~18–22% при d≈1.4mm; p3 d≈0.6mm дасть значно більший сигнал) | dRp1 значення pending hw-сесії C-5 |
| Залежність | `dRpPct ≈ 5 × dRp1_n × (800/baseline)` (емпірично) | стандартизовано по металу |

Через цю різницю **Phase 1 використовує threshold-based класифікацію**, а не query до FingerprintCache. Пряме порівняння з fingerprint centroids потребує окремих "quick_centroid" записів у DB (Phase 2, §4.2).

---

## 4. Алгоритм класифікації — фазова модель

### Phase 1: Threshold-based (реалізується зараз)

Проста таблиця порогів на `dRpPct` та `isFerro`. Не потребує DB, не потребує MetalMatcher.

```cpp
// QUICK_SCREEN_SPEC.md Phase 1 — вбудовані класифікаційні пороги
// Константи QUICK_*_THRESH визначені у §3.

struct QuickClass {
    const char* label;      // "SILVER", "COPPER", "STEEL", "ALUMINIUM", "?"
    const char* label_ua;   // "СРІБЛО", "МІДЬ", "СТАЛЬ", "АЛЮМІНІЙ", "?"
};

QuickClass classifyQuick(float dRpPct, bool isFerro) {
    if (isFerro)                             return {"STEEL",     "СТАЛЬ ⚠"};
    if (dRpPct > QUICK_SILVER_THRESH_PCT)    return {"SILVER",    "СРІБЛО"};
    if (dRpPct > QUICK_COPPER_THRESH_PCT)    return {"COPPER",    "МІДЬ"  };
    if (dRpPct > QUICK_ALUM_THRESH_PCT)      return {"ALUMINIUM", "АЛЮМІН"};
    return                                          {"?",          "?"     };
}
```

**Пороги Phase 1 (попередні, на основі синтетичних даних):**

| Метал | ΔRp% орієнтовно | is_ferro | Примітка |
|---|---|---|---|
| Ag999 | **⚠️ pending C-5** (est. >25% при d≈0.6mm) | false | Eagle bare coin, p3 протокол |
| Ag925/Ag833 | ~16–20% | false | трохи нижче Ag999 |
| Cu | ~10–15% | false | значно нижче срібла |
| Al | ~5–10% | false | алюміній — слабкий сигнал |
| Fe/Ni (сталь) | будь-який | **true** | ferro визначається по dL_raw |
| Вольфрам | ~30–40%? | false | висока провідність → класифікується як SILVER _(очікувана поведінка Phase 1; `(quick estimate)` на дисплеї попереджає)_ |

> ⚠️ Пороги **підлягають калібровці** після першого hw-тесту з реальними монетами (S-4).
> До S-4 — синтетичні оцінки. Відсоток може суттєво відрізнятись від реального.

### Phase 2: matchQuick() через MetalMatcher (після C-5)

Після того як C-5 накопичить реальні hw-виміри, до кожного запису в `index.json` додається:

```json
"quick_centroid": {
    "dRpBaseline_n": 0.195,
    "dL_n": 0.004
}
```

Де:
- `dRpBaseline_n = (baseline − rp[0]) / baseline_ref` — нормалізовано по власному baseline
- `dL_n = dL_raw / dL_norm_factor` — та сама нормалізація що й у fingerprint dL1_n

`MetalMatcher::matchQuick(dRpBaseline_n, dL_n)` отримає ці централи і дасть % confidence.

**Умова переходу Phase 1 → Phase 2:**
- Мінімум 3 реальних записи з `quick_centroid` для кожного класу металу в DB
- hw_test підтверджує, що quick_centroid query дає accuracy > threshold-based

**Pipeline генерації `quick_centroid` (C-5 hw-сесія):**

Для кожної монети в DB — 3 кроки:

1. **Повне вимірювання** (стандартний C-2 цикл) → отримати `rp[0]` та поточний `getBaseline()`:
   ```
   dRpBaseline_n = (baseline - rp[0]) / baseline_ref
   ```
   де `baseline_ref` — глобальне значення без монети того ж сеансу (не per-coin!).

2. **L_DATA** (при CLKIN): `dL_n = dL_raw / dL_norm_factor`
   де `dL_norm_factor` — визначити після S-5 (типовий max dL_raw для Fe монети).

3. **Запис у index.json** — вручну або через `tools/quick_centroid_gen.py` (новий скрипт):
   ```python
   # tools/quick_centroid_gen.py — зчитує measurements/*.json →
   # для кожного measurement: обчислює dRpBaseline_n, dL_n →
   # додає "quick_centroid" до відповідного запису в index.json
   ```

> **Примітка:** `baseline_ref` — baseline того ж сеансу вимірювань. Якщо монети вимірюються в різні дні, baseline може дрейфувати (~0.1–0.5% між сесіями). Phase 2 accuracy залежить від стабільності baseline між training та inference. Фіксувати `baseline` при кожному measurement session та зберігати в metadata.

**Нульова зміна для main.cpp при переході:** Phase 2 змінює лише MetalMatcher.matchQuick() internals та index.json — виклик з main.cpp залишається незмінним (`drawQuickScreen` отримує той самий `QuickClass` struct).

---

## 5. Display layout

### Cardputer: 240×135 px, шрифт FreeMonoBold9pt7b (~9px)

```
┌────────────────────────────────────────┐  y=0
│ QUICK SCREEN          [ENTER=Full]     │  ← заголовок, білий
├────────────────────────────────────────┤  y=16
│                                        │
│  ΔRp:  +18.4%                         │  y=30  — жовтий (рухомий)
│  ΔL:    +9 ct                         │  y=46  — жовтий (рухомий; raw L_DATA counts)
│                                        │
│  Ferro: НІ ✔                          │  y=62  — зелений / червоний
│                                        │
│  ┌──────────────────────────────────┐  │
│  │       СРІБЛО                     │  y=82  — великий шрифт, зелений
│  │       (quick estimate)           │  y=100 — сірий дрібний
│  └──────────────────────────────────┘  │
│                                        │
│  [ENTER] Повний вимір  [R] Recal      │  y=120 — підказка, сірий
└────────────────────────────────────────┘
```

**Колірна логіка:**

| Елемент | Колір | Умова |
|---|---|---|
| ΔRp% | Жовтий | `dRpPct > QUICK_NOISE_FLOOR_PCT` (>2%) |
| ΔRp% | Сірий | `dRpPct ≤ QUICK_NOISE_FLOOR_PCT` |
| ΔL ct | Жовтий | `fabsf(dL_raw) > QUICK_L_NOISE_FLOOR_CT` (>2 ct) |
| ΔL ct | Сірий | `fabsf(dL_raw) ≤ QUICK_L_NOISE_FLOOR_CT` |
| Ferro: ТАК ❌ | Червоний | `isFerro == true` |
| Ferro: НІ ✔ | Зелений | `isFerro == false` |
| Клас (СРІБЛО) | Зелений | не ferro, провідний |
| Клас (СТАЛЬ ⚠) | Червоний | `isFerro` |
| Клас (?) | Сірий | `dRpPct ≤ QUICK_ALUM_THRESH_PCT` |

### Часткові оновлення (без мерехтіння)

```cpp
// Щотік (кожен loop() при COIN_PRESENT):
// Лише рядки ΔRp і ΔL — fillRect + новий текст
display.fillRect(0, 22, 240, 14, BLACK);
display.setCursor(4, 30);
const uint16_t rpColor = (dRpPct > QUICK_NOISE_FLOOR_PCT) ? YELLOW : DARKGREY;
display.setTextColor(rpColor);
display.printf("  dRp: %+.1f%%", dRpPct);

display.fillRect(0, 38, 240, 14, BLACK);
display.setCursor(4, 46);
const uint16_t lColor = (fabsf(dL_raw) > QUICK_L_NOISE_FLOOR_CT) ? YELLOW : DARKGREY;
display.setTextColor(lColor);
display.printf("  dL:  %+.0f ct", dL_raw);

// Рядки ferro + клас — лише при зміні is_ferro або першому відображенні
static bool sLastFerro        = false;
static bool sQuickScreenFresh = true;   // Скидається у true з loop() при COIN_REMOVED → IDLE
if (sQuickScreenFresh || (isFerro != sLastFerro)) {
    // ... full redraw ferro + class blocks
    sLastFerro        = isFerro;
    sQuickScreenFresh = false;
}
```

> **Механізм reset `sQuickScreenFresh`:** Змінна оголошена в `src/main.cpp` на file scope (не як static всередині функції), щоб loop() міг її скидати. При виявленні `CoinState::COIN_REMOVED` або при переході у `drawMeasIdle()` loop() виставляє `sQuickScreenFresh = true`. Це гарантує повний redraw при наступній появі монети і запобігає "залипанню" ferro-статусу від попередньої монети при швидкій заміні (зняв-поклав).
>
> **Реалізація у loop():**
> ```cpp
> // При виявленні COIN_REMOVED або переході до IDLE no-coin:
> sQuickScreenFresh = true;   // наступний drawQuickScreen() буде з full redraw
> drawMeasIdle();
> ```

---

## 6. Keyboard handling

| Клавіша | Дія | Умова |
|---|---|---|
| `ENTER` (`\r`/`\n`) | Запустити повний вимір → STEP_BASE | IDLE + COIN_PRESENT |
| `R` / `r` | Recalibrate baseline → `gLDC->recalibrate()` | IDLE (монети немає — перевіряється всередині recalibrate()) |
| Будь-яка інша | Ігнорується у Quick Screen | — |

### ENTER — запуск повного циклу

```cpp
// В keyboard handler loop() при IDLE + COIN_PRESENT:
if (key == '\r' || key == '\n') {
    if (gLDC->isCoinPresent()) {
        // Запустити STEP_BASE — той самий шлях що й HTTP start
        // Примітка: збереження перевіряється в doMeasCompute() при COMPUTE step,
        //           не тут — симетрія з HTTP /measure/start path (C-4).
        sMeas.state  = MeasState::STEP_BASE;
        sMeas.stepMs = millis();
        drawMeasStep_full(sMeas);  // ← передати MeasSession sMeas
        gLogger.info("Meas", "Quick Screen → Full cycle (ENTER)");
    }
}
```

### 'R' — Recalibrate baseline

```cpp
if (key == 'R' || key == 'r') {
    // Показати "Recal..." перед блокуючим recalibrate() (~250 ms)
    M5Cardputer.Display.fillRect(0, 54, 240, 20, BLACK);
    M5Cardputer.Display.setTextColor(CYAN);
    M5Cardputer.Display.setCursor(4, 66);
    M5Cardputer.Display.print("  Recalibrating...");

    if (gLDC->recalibrate()) {
        gLogger.info("Meas", "Baseline recalibrated (R key)");
    }
    // recalibrate() внутрішньо перевіряє isCoinPresent() та логує warning якщо монета є.
    // Full redraw після повернення — показати оновлені значення.
    drawMeasIdle();  // ← coin_present стан буде перехоплений у наступному loop() тіку
}
```

> **Чому recalibrate() лише без монети:** baseline — це no-coin RP/L. Якщо монета є, baseline запише значення з монетою як нульову точку → всі подальші вимірювання зміщені. Перевірку зроблено **всередині** `recalibrate()` (§8), а не лише в keyboard handler — щоб захистити від майбутніх call sites (наприклад, HTTP `/calibrate`).

---

## 7. UART logging

### При активації Quick Screen (перший тік з COIN_PRESENT)

```
[Meas] QuickScreen: basRp=57344 liveRp=47130 basL=3198 liveL=3207
[Meas] QuickScreen: dRp=+18.4% dL=+9ct ferro=NO class=SILVER
```

### При кожній зміні класу (ferro або label)

```
[Meas] QuickScreen: class changed → SILVER (dRp=+18.4%, dL=+9ct)
```

### При ENTER → повний цикл

```
[Meas] QuickScreen → Full cycle (ENTER)
```

### При recalibrate()

```
[LDC1101] recalibrate: rpBase 57344→57201  lBase 3198→3196  (ok=9/10)
```

### При recalibrate() з монетою на котушці

```
[LDC1101] recalibrate() — coin present, ignored
```

### При знятті монети

```
[Meas] QuickScreen: coin removed (dRp was +18.4%, dL=+9ct, class=SILVER)
```

---

## 8. Зміни у LDC1101Plugin

### Два нових публічних методи + один new inline getter

```cpp
// lib/LDC1101Plugin/src/LDC1101Plugin.h — в публічній секції

// Поточне живе значення RP (з кешу, під dataMutex_).
// Повертає raw RP_DATA register code (uint16 cast до float).
// Повертає 0.0f якщо кеш не valid або mutex timeout.
// RP_DATA valid незалежно від CLKIN — amplitude-based measurement.
// THREAD-SAFE: використовує dataMutex_ (contract §2.2).
float getLiveRp() const;

// Поточне живе значення L_DATA (з кешу, під dataMutex_).
// Повертає raw L_DATA register code (uint16 cast до float).
// НЕ мікрогенрі — raw counts. Конвертація → µH відкладена до Phase 2.
// ⚠️ ADR-CLKIN-002: повертає garbage якщо CLKIN не підключено (clkinGpio_ < 0).
//    Завжди перевіряти isLDataValid() перед використанням.
float getLiveL()  const;

// Повертає true якщо CLKIN сконфігуровано → L_DATA вимірювання валідні.
// False → getLiveL() / getLBaseline() повертають garbage (0 або довільні counts).
// Cross-ref: ADR-CLKIN-002, LDC1101Plugin.h initialize() clkinGpio_ field.
bool isLDataValid() const { return clkinGpio_ >= 0; }
```

### Реалізація getLiveRp / getLiveL (LDC1101Plugin.cpp або inline в .h)

```cpp
float LDC1101Plugin::getLiveRp() const {
    if (xSemaphoreTake(dataMutex_, pdMS_TO_TICKS(50)) != pdTRUE) return 0.0f;
    const float rp = cache_.valid ? (float)cache_.rpRaw : 0.0f;
    xSemaphoreGive(dataMutex_);
    return rp;
}

float LDC1101Plugin::getLiveL() const {
    if (xSemaphoreTake(dataMutex_, pdMS_TO_TICKS(50)) != pdTRUE) return 0.0f;
    const float l = cache_.valid ? (float)cache_.lRaw : 0.0f;
    xSemaphoreGive(dataMutex_);
    return l;
}
```

### Новий метод recalibrate()

```cpp
// Перезнімає baseline RP та L — N-sample average (mirrors calibrate() quality).
// ОБОВ'ЯЗКОВО: монети на котушці бути не повинно — метод перевіряє isCoinPresent()
//             і повертає false із warning log якщо монета є.
// Blocking: до ~250 ms (10 readings × ~25 ms each). Викликати лише з loop() task.
// Повертає true якщо мінімум 5 з 10 readings valid та baseline оновлено.
bool recalibrate();
```

```cpp
bool LDC1101Plugin::recalibrate() {
    // ── Internal guard: не можна калібрувати з монетою ─────────────────────
    if (isCoinPresent()) {
        ctx_->log->warning(getName(), "recalibrate() — coin present, ignored");
        return false;
    }

    // ── N-sample average (mirrors calibrate() quality, 10 samples) ─────────
    float    rpSum = 0.0f, lSum = 0.0f;
    uint32_t ok    = 0;
    for (int i = 0; i < 10; i++) {
        delay(convTimeMs_() + 5);  // wait for fresh conversion (~20–25 ms)
        uint16_t rp, l;
        if (readBurst_(rp, l) && rp > 0 && rp < 65535) {
            rpSum += rp;
            lSum  += l;
            ++ok;
        }
    }

    if (ok < 5) {
        ctx_->log->error(getName(),
            "recalibrate() failed: only %u/10 valid readings", ok);
        return false;
    }

    const float oldRp = calibrationRpBaseline_;
    const float oldL  = calibrationLBaseline_;
    calibrationRpBaseline_ = rpSum / ok;
    calibrationLBaseline_  = lSum  / ok;
    lastCalibrationTime_   = millis();

    ctx_->log->info(getName(),
        "recalibrate: rpBase %.0f\u2192%.0f  lBase %.0f\u2192%.0f  (ok=%u/10)",
        oldRp, calibrationRpBaseline_, oldL, calibrationLBaseline_, ok);
    return true;
}
```

> **Чому N=10, ok≥5 (не N=20 як у calibrate()):**
> `calibrate()` викликається при старті (немає тайм-пресингу, `delay(2000)` acceptable).
> `recalibrate()` — відповідь на дію користувача. ~250 ms — прийнятна затримка.
> `calibrate()` вимагає ok≥10 з 20 (50%). `recalibrate()` ok≥5 з 10 (50%) — та сама якість по відношенню до пропущених readings.

> **⚠️ recalibrate() блокує loop() на ~250 ms:**
> У цей час: `update()` не викликається (~12 циклів пропускається), coin state machine не
> просувається, WebSocket frames не надсилаються. Наслідок: якщо монету зняти під час
> recalibrate() — transient `COIN_REMOVED` (1 цикл, ADR-COIN-001) буде пропущений.
> CoinState залишиться COIN_PRESENT до наступного `update()` після повернення з recalibrate().
> Монету буде виявлено знятою з затримкою ~0–250 ms — **прийнятно для v1** (keyboard action,
> не автоматичний процес). Підказка "Recalibrating..." (§6) повідомляє користувача про паузу.

---

## 9. Зміни у MEASUREMENT_WORKFLOW.md

| Пункт | Статус |
|---|---|
| §1 "Два шляхи запуску" — `ENTER у Quick Screen (IDLE + COIN_PRESENT)` вже є як path #2 | ✅ **Вже виконано** (MEASUREMENT_WORKFLOW.md v1.0.0, рядок 16) |
| §3 "Автозапуск" — видалити або позначити `[REMOVED 2026-03-25]` | ✅ **Вже виконано** (MEASUREMENT_WORKFLOW.md v3.x) |
| §10 "Display Rendering Summary" — додати рядок `drawQuickScreen()` | **ПОТРЕБУЄ РЕАЛІЗАЦІЇ** |
| Новий §N "Quick Screen" — посилання на цей документ | **ПОТРЕБУЄ РЕАЛІЗАЦІЇ** |

---

## 10. Архітектурні рішення (ADR)

### ADR-QS-1: Quick Screen — субрежим IDLE, не новий стан MeasState

**Рішення:** Не додавати `QUICK_SCREEN` до `MeasState` enum.

**Причина:** Quick Screen — це лише спосіб відображення IDLE стану при COIN_PRESENT. Додання нового enum value: змінює HTTP endpoint `/sensor/state` (нові клієнти ламають backward compat), додає ще один варіант у кожен `switch(sMeas.state)`. Нульова архітектурна вигода.

**Trade-off:** `drawMeasIdle()` розгалужується на два варіанти. Прийнятно.

---

### ADR-QS-2: Phase 1 — threshold-based, без DB query

**Рішення:** Phase 1 не викликає `MetalMatcher::matchQuick()`.

**Причина:** `dRpPct` (no-coin baseline vs live reading) математично відрізняється від `dRp1_n` у fingerprint centroids (§3). Пряме порівняння дасть некоректні confidence значення. Threshold-based — чесно і одразу корисно.

**Phase 2 умова:** реальні hw-виміри в C-5 + окремий `quick_centroid` у DB.

---

### ADR-QS-3: recalibrate() — internal guard + N-sample average

**Рішення:** `recalibrate()` самостійно перевіряє `isCoinPresent()` та збирає 10 readings.

**Причина (guard):** Precondition "немає монети" — критична для baseline якості. Якщо перевірку залишити лише з боку caller (keyboard handler), майбутній HTTP `/calibrate` endpoint або тест-код забудуть її — і baseline буде зіпсовано мовчки. Hard guard всередині методу захищає від всіх call sites.

**Причина (N-sample):** Single snapshot з `cache_` має noise ~10–30 raw counts (RP_DATA при RESP_TIME 6144 cycles). Помилка baseline на 20 counts при baseline ~57000 = 0.035% — незначна для dRpPct, але помітна при малих монетах (Al, ~6% сигнал). N=10 avg → noise ≈ 3–10 counts → похибка < 0.02%.

**Trade-off:** ~250 ms blocking. За умовами використання (manual 'R' key) — прийнятно.

---

### ADR-QS-4: getLiveRp/getLiveL — thread-safe через dataMutex_

**Рішення:** Нові методи використовують той самий `dataMutex_` що захищає `cache_`.

**Причина:** `cache_` заповнюється в `update()` (main task) і читається з Quick Screen (також main task). Mutex вже є і при відсутності contention acquire ~0 µs — краще ніж порушення existing mutex protocol.

---

### ADR-QS-5: isDataMounted() — не перевіряється при старті ENTER

**Рішення:** ENTER handler не перевіряє `gLFS.isDataMounted()`.

**Причина:** Симетрія з HTTP `/measure/start` path (C-4). Обидва шляхи ведуть до того ж `doMeasCompute()`, який перевіряє збереження на LittleFS при COMPUTE step. Якщо LFS відключено — вимір відбудеться, але `save()` поверне помилку і залогує її. Перевіряти mount state при старті (а не при save) — це раннє відхилення неповної інформації, яка ламає UX без реальної причини.

---

## 11. Пов'язані файли

| Файл | Зміни | Статус |
|---|---|---|
| `src/main.cpp` | `drawQuickScreen()`, `sQuickScreenFresh` (file-scope), keyboard ENTER/R в IDLE, `static constexpr` пороги | **РЕАЛІЗАЦІЯ** |
| `lib/LDC1101Plugin/src/LDC1101Plugin.h` | `getLiveRp()`, `getLiveL()`, `isLDataValid()` (inline), `recalibrate()` — оголошення | **РЕАЛІЗАЦІЯ** |
| `lib/LDC1101Plugin/src/LDC1101Plugin.cpp` | `getLiveRp()`, `getLiveL()`, `recalibrate()` — реалізація (`isLDataValid()` — inline в .h) | **РЕАЛІЗАЦІЯ** |
| `docs/concept/COLLECTOR_USE_CASE.md §9` | джерело use case | source |
| `docs/architecture/MEASUREMENT_WORKFLOW.md` | §1 третій шлях ENTER, §10 drawQuickScreen — §3 видалення авто-старту вже виконано | **ЧАСТКОВЕ ОНОВЛЕННЯ** |
| `docs/architecture/METAL_MATCHER_ARCHITECTURE.md` | Phase 2 matchQuick() + `quick_centroid` у matcher.json | reference |
| `docs/architecture/WAVE8_ROADMAP.md §C-7` | task статус | reference |

### Pending fix (поза scope цього документа)

| Файл | Знахідка | Пріоритет |
|---|---|---|
| `src/main.cpp:150-152` | `STEP_BASE/STEP_1/STEP_3` display strings оновлено: p3 відстані 0.6/1.6/2.6mm | ✅ Виправлено 2026-03-26 |
