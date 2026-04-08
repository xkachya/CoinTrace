# Wave 10 Roadmap — Mass Dimension + Sensor Reproducibility

**Статус:** 🔄 Active — D-12a WIP написаний, під peer review (5 register bugs документовано → виправляються)  
**Версія:** 1.0.0  
**Дата:** 2026-04-08  
**Попередня хвиля:** Wave 9 — Measurement Science (CLOSED 2026-04-08; gen-7 DB: 28 entries, 13 classes, 5 пар < 1.0σ — physics constraint)  
**Тригер:** A-7 pairwise analysis (gen-7): 5 пар < 1.0σ — всі в silver-vs-silver zone. NAU7802 + 7D вектор: prooved → all 5 pairs > 2.0σ (W_mass=5.0 × mass_n). Wave 10 triggered.  
**HEAD:** `df626f2` — fix(db): D-11e XZNNIP_a outliers excluded (r95: 0.661→0.135)  
**Cross-ref:** `docs/external/2026-04-08.WAVE10_ARCHITECTURE_PLAN.md`, `docs/architecture/NAU7802_ARCHITECTURE.md v1.1.0`, `docs/architecture/WAVE9_ROADMAP.md v2.0.0`

---

## Контекст та головний constraint

Wave 9 довела, що 6D LDC1101-вектор вичерпаний для silver class separation. Всі 5 критичних пар мають фізичне обмеження (Ag-вміст, маса схожа):

| Пара | 6D σ | Проблема |
|------|------|----------|
| XKENNED ↔ XUSSR10 | 0.18 | Kennedy 12.5g vs USSR10 33.3g — _але_ схожий Ag-вміст |
| XKENNED ↔ XCUZN | 0.22 | Kennedy 12.5g vs CuZn ~12.0g |
| XKENNED ↔ XAG800 | 0.31 | Kennedy 12.5g vs Ag800 ~12.0g |
| XCUZN ↔ XAG800 | 0.45 | CuZn ~12.0g vs Ag800 ~12.0g |
| XKENNED ↔ XCU | 0.61 | Kennedy silver vs pure Cu ~16.0g |

**Рішення:** NAU7802 24-bit ADC + load cell як 7-й вимір `mass_n` з W_mass=5.0. Розрахунок:

| Пара | Δmass_g | Δmass_n | W_mass × |Δmass_n| | Прогноз σ |
|------|---------|---------|---------|---------|
| XKENNED ↔ XUSSR10 | 20.8g | 0.625 | **3.12σ** | > 2.0 ✅ |
| XKENNED ↔ XCUZN | ~0.5g | 0.015 | **0.075σ** | WARN (mass alone insufficient) |
| XKENNED ↔ XAG800 | ~0.5g | 0.015 | **0.075σ** | WARN (mass alone insufficient) |
| XCUZN ↔ XAG800 | ~0g | ~0 | ~0σ | WARN (LDC1101 must carry) |
| XKENNED ↔ XCU | ~3.5g | 0.105 | **0.525σ** | WARN + 6D contribution required |

> **Ключовий insight:** mass_n 100% вирішує Kennedy↔USSR10 (найкритичнішу пару). Для Kennedy↔CuZn/Ag800 — потрібна комбінація LDC1101 6D + mass_n. C-13 HW session надасть точні значення.

---

## Зміст

1. [Матриця задач Wave 10](#1-матриця-задач-wave-10)
2. [Track A — NAU7802 firmware](#2-track-a--nau7802-firmware)
3. [Track B — 2nd LDC1101 reproducibility](#3-track-b--2nd-ldc1101-reproducibility)
4. [Track C — E1 Ø50mm flat coil experiment](#4-track-c--e1-ø50mm-flat-coil-experiment)
5. [Рекомендована послідовність](#5-рекомендована-послідовність)
6. [DB еволюція gen-7 → gen-8](#6-db-еволюція-gen-7--gen-8)
7. [RAM та Flash бюджет Wave 10](#7-ram-та-flash-бюджет-wave-10)
8. [Acceptance Criteria](#8-acceptance-criteria)

---

## 1. Матриця задач Wave 10

| Задача | Track | HW? | Залежить від | Статус | Опис |
|--------|-------|-----|-------------|--------|------|
| **D-12a Plugin skeleton** | A | ❌ | NAU7802_ARCHITECTURE v1.1.0 | 🔄 WIP | Driver: `_startupSequence()` + OTP + `update()` + state machine. Fix 5 bugs перед commit. |
| **D-12b NVS calibration** | A | ❌ | D-12a ✅ | ⬜ | `saveCalibration()` / `loadCalibration()` NVS namespace "nau7802", 5 keys. |
| **D-12c UX wizard** | A | ❌ | D-12b ✅ | ⬜ | Key 'K': tare → known weight prompt → verify. OLED + Serial feedback. |
| **D-12d 7D integration** | A | ❌ | D-12c ✅ | ⬜ | `mass_n` в `production_vector`, NDJSON schema v8, DB gen-8, matcher v6. |
| **D-12e Sequential workflow** | A | ❌ | D-12d ✅ | ⬜ | STEP_WEIGHT→STEP_QUICK→Full. matchQuick(7D+mass_n). ADR-NAU-008. A-3 unblocked! |
| **A-3 Quick Screen Phase 2** | A | ❌ | D-12e ✅ | ⬜ | matchQuick() з 7D (mass_n). Розблокований: STEP_QUICK = Phase 2 в production flow. |
| **C-13 HW session** | C→A | ✅ | D-12e ✅ | ⬜ | mass_g для всіх 13 класів + xznnip_a (n=3→5) + xkenned_b (n=4→5) reseed. |
| **A-8 7D pairwise analysis** | A | ❌ | C-13 ✅ | ⬜ | `scripts/a8_pairwise_7d.py`. Exit criterion: всі пари > 2.0σ. |
| **R-1 Reproducibility** | B | ✅ | HW в дорозі | ⬜ | 2nd LDC1101 Unit-2 vs gen-8 DB. 5 монет × 5 вимірів. PASS: Δwdist < 0.5σ. |
| **S-E1 Ø50mm coil winding** | C | ✅ | Дріт в дорозі | ⬜ | N=44 turns Ø50mm, L≈135.1µH. A/B symmetry vs flat coil comparison. |

---

## 2. Track A — NAU7802 firmware

### D-12a: Базовий драйвер (WIP → виправити bugs, тоді commit)

**Специфікація:** `docs/architecture/NAU7802_ARCHITECTURE.md v1.1.0`

**Що:** `NAU7802Plugin` як `ISensorPlugin` (PlatformIO lib). I²C 400kHz, OTP reload 14-крокова startup sequence, non-blocking acquisition state machine, mutex thread safety.

**⚠️ Bugs у WIP (виправити перед commit — всі 5):**

| # | Файл | WIP | Правильно | ADR |
|---|------|-----|-----------|-----|
| 1 | `.h` | `CTRL1_VAL = 0x27` | `0xBC` (`(0x05<<5)\|(0x07<<2)`) | ADR-NAU-007, B-05 |
| 2 | `.h` | `CTRL2_VAL = 0x30` | `0x60` (`(0x03<<5)`) | ADR-NAU-007, B-05 |
| 3 | `.h` | `SETTLE_MS = 200` | `500` | ADR-NAU-006 |
| 4 | `.cpp` | CTRL1/CTRL2 до OTP в `_startupSequence()` | CTRL1/CTRL2 після OTP reload | ADR-NAU-007 |
| 5 | `.cpp` | `tare()` mask `~0x70` | `~0xE0` (CRS at bits[7:5]) | B-05 |

**Файли:**

| Файл | Роль |
|------|------|
| `lib/NAU7802Plugin/src/NAU7802Plugin.h` | Class declaration, constants, AcqState enum |
| `lib/NAU7802Plugin/src/NAU7802Plugin.cpp` | Full implementation |
| `lib/NAU7802Plugin/library.json` | PlatformIO library manifest |

**Архітектурні константи (після fix):**

```cpp
static constexpr uint8_t  I2C_ADDR    = 0x2A;   // 7-bit
static constexpr uint8_t  CTRL1_VAL   = 0xBC;   // VLDO=3.0V + GAINS=128x
static constexpr uint8_t  CTRL2_VAL   = 0x60;   // CRS=80SPS, CH1
static constexpr uint32_t SETTLE_MS   = 500;    // ADR-NAU-006
static constexpr uint8_t  N_SAMPLES   = 20;     // median window
static constexpr float    MASS_REF_G  = 33.3f;  // XUSSR10 normalization
static constexpr float    SENTINEL    = -1.0f;  // ADR-NAU-004: not calibrated
```

**Acquisition state machine:**

```
IDLE → SETTLING (500ms) → SAMPLING (20 readings, 80SPS = 250ms) → READY
  ↑__________________________|  next measurement
```

**Acceptance criteria D-12a:**
- `initialize()` returns `true` on clean boot з NAU7802 підключеним
- `getWeight()` повертає `SENTINEL (-1.0f)` до завершення calibration
- `build` SUCCESS: 137 native tests pass, RAM ≤ 65%, Flash ≤ 65%
- Serial: `[NAU7802] startup OK, 80SPS, GAINS=128x` при успішному init

---

### D-12b: NVS calibration

**Що:** Persistent zero + scale factor через `nvs_flash`. Namespace: `"nau7802"`. 5 keys: `zero`, `scale`, `cal_ok`, `cal_ts`, `cal_mass`.

**Зміни:**

| Файл | Зміна |
|------|-------|
| `lib/NAU7802Plugin/src/NAU7802Plugin.h` | `saveCalibration()`, `loadCalibration()` |
| `lib/NAU7802Plugin/src/NAU7802Plugin.cpp` | NVS implementation, `_isCalibrated` flag |
| `data/config/nau7802.json` | `settle_ms: 500`, `n_samples: 20`, `fast_mode: false` |

**NVS schema:**

```
namespace: "nau7802"
  zero    : int32  (raw ADC count при нульовому навантаженні)
  scale   : float  (g/count = mass_ref_g / (raw_ref - zero))
  cal_ok  : uint8  (1 = valid calibration)
  cal_ts  : uint32 (Unix timestamp останньої калібровки)
  cal_mass: float  (reference mass, g — зазвичай XUSSR10 = 33.3g)
```

**Acceptance criteria D-12b:**
- Після power-cycle: попередня calibration завантажується автоматично
- NVS відсутній або corrupted → `getWeight()` = SENTINEL (graceful degradation)

---

### D-12c: UX calibration wizard

**Що:** Процес калібровки з OLED та Serial. Активується клавішею `'K'` на клавіатурі Cardputer.

**Уточнений flow:**

```
1. Press 'K' → "TARE: remove all load, press ENTER"
2. ENTER → capture N_SAMPLES raw → zero = median
3. "CAL: place XUSSR10 (33.3g), press ENTER"
4. ENTER (SETTLE_MS delay) → capture N_SAMPLES raw → scale = 33.3/(median - zero)
5. saveCalibration() → "CAL OK. mass=33.3g sigma=Xg"
6. Показати live weight display (5s)
```

**Acceptance criteria D-12c:**
- Весь wizard < 15s (SETTLE_MS=500, N=20, 80SPS → ~1700ms capture)
- sigma < 0.05g на reference coin
- OLED показує progress на кожному кроці

---

### D-12d: 7D vector integration

**Що:** `mass_n = mass_g / MASS_REF_G` як 7-й компонент вектора. NDJSON schema v8. DB gen-8. matcher v6.

**Зміни:**

| Файл | Зміна |
|------|-------|
| `include/IStorageManager.h` | `queryFingerprint()` + 7th param `mass_n` |
| `lib/StorageManager/src/StorageManager.cpp` | `buildFromSD()` parse `mass_n`, `FingerprintCache` 7D |
| `lib/StorageManager/src/FingerprintCache.h` | `CacheEntry.mass_n`, `query()` 7th param |
| `lib/StorageManager/src/MetalMatcher.cpp` | `matchFull()` 7th dimension W_mass |
| `data/sd_seed/CoinTrace/matcher.json` | version=6, `full_weights=[1.5,0.0,1.0,3.0,2.5,0.4,5.0]`, `keys=[...,"mass_n"]` |
| `data/sd_seed/CoinTrace/database/index.json` | version=8 |

**NDJSON schema v8 additions:**

```jsonc
{
  "schema_version": 8,
  "mass_g": 33.3,              // measured physical mass
  "production_vector": {
    "dRp1_n": ...,
    "k1": ...,
    "k2": ...,
    "df_n": ...,
    "dL1_n": ...,
    "df1_n": ...,
    "mass_n": 1.0              // mass_g / MASS_REF_G (33.3g)
  }
}
```

**FingerprintCache backward compatibility (ADR-NAU-004):**

```cpp
// При читанні DB gen-7 (без mass_n): entry.mass_n = SENTINEL (-1.0f)
// matchFull(): if (mass_n == SENTINEL) w_mass = 0.0f  // 6D mode
```

**Protocol ID:** `p3_MIKROE3240_b08_012mm` → `p4_MIKROE3240_b08_012mm_mass`

**Acceptance criteria D-12d:**
- gen-7 DB читається без помилок (mass_n = SENTINEL → 6D fallback)
- gen-8 DB: matchFull() використовує 7D вектор
- 137 native tests pass + мінімум 5 нових тестів: `test_mass_n_normalization`, `test_sentinel_fallback_6d`

---

### D-12e: Sequential Measurement Workflow (ADR-NAU-008)

**Що:** Три-кроковий sequential workflow: STEP_WEIGHT (ваги окремо) → STEP_QUICK (котушка без спейсерів + matchQuick 7D) → STEP_FULL (S1+S3, тільки при низькому confidence). Розблоковує A-3 Quick Screen Phase 2.

> **⚠️ Архітектурна зміна vs WIP:** Попередня ідея «паралельна acquisition в STEP_BASE» фізично неможлива — монета не може одночасно бути на вагах і на котушці. Правильна архітектура — послідовні кроки.

**State machine:**

```
IDLE
  ↓ ENTER
[NAU7802 present && calibrated?]
  ├─ YES: STEP_WEIGHT → "Поклади на ваги → ENTER"
  │         startAcquisition() → settle(500ms) → 20 samples → mass_g
  │         ↓ ENTER
  │       STEP_QUICK → "Перенеси на котушку → ENTER"  (= STEP_BASE)
  │         LDC1101 captureStep() → matchQuick(7D + mass_n)
  │         ├─ confidence ≥ 0.75 → PROPOSE RESULT → [C]onfirm [F]ull [N]ext
  │         └─ confidence < 0.75 → auto STEP_1 → STEP_3 → matchFull(7D)
  └─ NO:  6D fallback (Wave 9 flow: BASE→S1→S3→matchFull 6D)
```

**Ключова деталь:** STEP_QUICK і STEP_BASE — **один і той самий LDC1101 capture**. Дані не дублюються. При переході до STEP_FULL — STEP_BASE вже є, додаються тільки S1+S3 захоплення.

**Зміни:**

| Файл | Зміна |
|------|-------|
| `src/main.cpp` | Новий стан `STEP_WEIGHT` в `MeasState`; handler з NAU7802 acquisition |
| `src/main.cpp` | STEP_QUICK = STEP_BASE capture + `gMatcher.matchQuick()` 5 params |
| `src/main.cpp` | Confidence threshold check → propose/auto-proceed logic |
| `src/main.cpp` | 6D graceful fallback якщо `!gNAU->isCalibrated()` |
| `lib/StorageManager/src/MetalMatcher` | `matchQuick()`: 4→5 params, додати `mass_n` + `quick_weights[6]` |
| `data/sd_seed/CoinTrace/matcher.json` | v6: `quick_weights=[2.0,0.0,0.0,4.0,2.5,0.30,5.0]` (7 елементів) |

**Acceptance criteria D-12e:**
- STEP_WEIGHT → STEP_QUICK повний flow < 4s включно з settling
- 6D fallback: Wave 9 behavior незмінна якщо NAU7802 absent/uncalibrated
- JSON result contains `mass_g`, `mass_n`, `workflow` field
- matchQuick() 5-param: тест `test_matchQuick_7d_with_mass`

---

### C-13: HW session — mass calibration (13 класів)

**Завдання:** Виміряти `mass_g` для всіх 13 класів. Додати записи для xznnip_a (n=3→5) та xkenned_b (n=4→5).

**Цільова таблиця мас:**

| Class | mass_g (target) | mass_n | Нотатки |
|-------|----------------|--------|---------|
| XAL | 1.95 | 0.059 | Алюм. корпус |
| XFECUP | ~2.2 | 0.066 | Fe/Cu |
| XNICKEL | 4.0 | 0.120 | Nickel |
| XFENIP | ~4.5 | 0.135 | Fe/Ni |
| XFE | ~7.0 | 0.210 | Steel (bimetal) |
| XZNNIP | 7.1 | 0.213 | Zn/Ni |
| XKENNED | 11.5 | 0.345 | Kennedy half-US |
| XCUZN | ~12.0 | 0.360 | CuZn |
| XAG800 | ~12.0 | 0.360 | Ag800 |
| XCU | ~16.0 | 0.480 | Pure Cu |
| XAG900 | 26.7 | 0.802 | Ag900 |
| XAG999 | 31.1 | 0.934 | Ag999 |
| XUSSR10 | 33.3 | 1.000 | Reference (MASS_REF_G) |

**Reseed targets:**
- `xznnip_a`: поточний n=3 → мінімум n=5 (r95 0.135 після D-11e)
- `xkenned_b`: поточний n=4 → мінімум n=5 (Kennedy margin слабкий) 

**Acceptance criteria C-13:**
- Всі 13 класів мають `mass_g` в NDJSON
- xznnip_a: n ≥ 5, r95 < 0.20
- xkenned_b: n ≥ 5
- Calibration: sigma < 0.1g на reference XUSSR10
- DB gen-8 version=8 з mass_n field: `buildFromSD()` SUCCESS

---

### A-8: 7D pairwise analysis

**Що:** `scripts/a8_pairwise_7d.py` — pairwise distance matrix для всіх 13×13 класів у 7D просторі (matcher v6).

**Acceptance criteria A-8:**
- **EXIT CRITERION** Wave 10: всі пари з Δmass > 2g: > 2.0σ в 7D; пари з Δmass ≤ 2g (XCUZN↔XAG800, XKENNED↔XCUZN): > 1.5σ — LDC1101 несе навантаження
- 5 пар що були < 1.0σ у Wave 9: всі > 2.0σ
- WARN threshold: flag будь-яку пару < 2.0σ для Wave 11 planning
- Якщо будь-яка пара < 1.0σ після 7D → Wave 11 з додатковим discriminator (обов'язково)

---

## 3. Track B — 2nd LDC1101 reproducibility

### R-1: Reproducibility experiment

**Контекст:** У Wave 10 отримано другий LDC1101 unit (Unit-2). DB gen-8 побудована на Unit-1. Питання: **чи DB portable між units?**

**Мотивація:** LDC1101 має unit-to-unit variation у baseline RP, L, fSENSOR. Якщо variation > 0.5σ — потрібен per-unit seeding. Якщо < 0.5σ — одна DB для всіх units.

**Протокол:**

```
1. Підключити Unit-2 (другий MIKROE-3240) до тієї ж Cardputer
2. Виміряти 5 контрольних монет × 5 вимірів на Unit-2 (без рекалібровки DB)
3. Порівняти wdist з gen-8 DB (побудована на Unit-1)
4. Розрахувати Δwdist для кожної монети
```

**Монети для тестування (R-1 selection):**
- XAG999 (найчутливіший до RP variation)
- XKENNED (слабка пара в 6D)
- XUSSR10 (reference mass)
- XFE (ferro — перевірка dRp1_n)
- XAL (легка монета — перевірка mass_n)

**Pass/Fail criteria:**

| Результат | Δwdist | Наслідок |
|-----------|--------|---------|
| **PASS** | < 0.5σ на всіх 5 монетах | DB portable → один gen-8 для всіх units |
| **WARN** | 0.5σ – 1.0σ | Вивчити окремі параметри (temperature, spacer fit) |
| **FAIL** | > 1.0σ | Per-unit seeding needed → Wave 11 planning |

**Dependency:** HW в дорозі (Unit-2). Non-blocking: Track A не залежить від R-1.

---

## 4. Track C — E1 Ø50mm flat coil experiment

### S-E1: Намотка та вимір E1 котушки

**Контекст:** Поточна MIKROE-3240 flat coil має A/B side asymmetry. E1 — альтернатива: encircling circular coil Ø50mm.

**Специфікація котушки:**

| Параметр | Значення | Розрахунок |
|----------|---------|-----------|
| Геометрія | Ø50mm circular, flat | Wheeler formula |
| Кількість витків | N = 44 | L = (N² × A) / (9A + 10B) |
| Індуктивність | L ≈ 135.1 µH | Target: resonance ~1.2 MHz з 10nF |
| Матеріал | Монозродовий Cu, d=0.3mm | В дорозі |
| Target freq | ~1.2 MHz base | Порівнянна з MIKROE-3240 |

**Завдання S-E1:**

```
1. Намотати N=44 витків Ø50mm (або скорегувати N для L≈135µH)
2. Виміряти фактичне L (LCR meter або resonance)
3. Підключити замість MIKROE-3240 (SMA або solder)
4. Baseline measurement (порожня платформа): записати f_empty, RP_empty
5. 5 контрольних монет × 5 вимірів → порівняти з MIKROE-3240 gen-8 DB
6. A/B symmetry test: flip coin, compare wdist
```

**Pass criteria S-E1:**

| Метрика | PASS | FAIL |
|---------|------|------|
| A/B symmetry | Δwdist < 0.3σ для всіх 5 монет | > 0.5σ |
| SNR vs MIKROE-3240 | RP signal ≥ 70% від MIKROE | < 50% |
| L measurement | ±10% від 135.1 µH | > ±20% |

**Dependency:** Дріт в дорозі. Non-blocking: Track A не залежить від S-E1.

---

## 5. Рекомендована послідовність

```
ТИЖДЕНЬ 1 — D-12a fix + D-12b + D-12c:

Day 1:
  D-12a: виправити 5 bugs (CTRL1_VAL, CTRL2_VAL, startup order, tare mask, SETTLE_MS)
  D-12a: commit після hw-test (boot, init OK message)

Day 2-3:
  D-12b: NVS calibration (saveCalibration / loadCalibration)
  D-12c: UX calibration wizard ('K' key flow)
  HW verify: XUSSR10 calibration, sigma < 0.05g

ТИЖДЕНЬ 2 — D-12d + D-12e:

Day 4-5:
  D-12d: 7D integration (FingerprintCache, MetalMatcher, NDJSON schema v8, gen-8 DB)
  Tests: test_mass_n_normalization, test_sentinel_fallback_6d
  
Day 6:
  D-12e: sequential workflow (STEP_WEIGHT→STEP_QUICK→Full, ADR-NAU-008)
  A-3: matchQuick() 5-param (7D + mass_n), quick_weights v6
  Integration test: STEP_WEIGHT→STEP_QUICK→PROPOSE confirm flow

ТИЖДЕНЬ 3 — C-13 + A-8:

Day 7:
  C-13 HW session: виміряти mass_g для 13 класів, xznnip_a/xkenned_b reseed
  Build gen-8 DB

Day 8:
  A-8: scripts/a8_pairwise_7d.py
  Verify exit criterion: all pairs > 2.0σ

ПАРАЛЕЛЬНО (non-blocking, залежать від HW доставки):
  R-1: після прибуття Unit-2 → reproducibility test
  S-E1: після прибуття дроту → coil winding + A/B test
```

---

## 6. DB еволюція gen-7 → gen-8

| Параметр | gen-7 (Wave 9 HEAD) | gen-8 (Wave 10) |
|----------|---------------------|-----------------|
| `version` в index.json | `7` | `8` |
| Protocol ID | `p3_MIKROE3240_b06_012mm` | `p4_MIKROE3240_b06_012mm_mass` |
| Розмірність вектора | 6D | 7D |
| Нові поля NDJSON | — | `mass_g`, `mass_n` |
| matcher.json версія | v5 | v6 |
| `full_weights` | `[1.5,0.0,1.0,3.0,2.5,0.4]` | `[1.5,0.0,1.0,3.0,2.5,0.4,5.0]` |
| `keys` | `[...,df1_n]` | `[...,df1_n,mass_n]` |
| Кількість записів | 28 | 28 + C-13 reseed (≥ 32) |
| Класів | 13 | 13 (ті самі + уточнені центроїди) |

**Backward compatibility:**
- gen-7 DB читається без помилок (`mass_n = SENTINEL → 6D mode`)
- gen-8 NDJSON файли з `mass_n=-1.0` обробляються коректно (SENTINEL flow)

---

## 7. RAM та Flash бюджет Wave 10

**Baseline Wave 9 HEAD (`df626f2`):**

```
RAM:   63.4%  (поточний — зафіксовано D-8)
Flash: 58.1%  (поточний — зафіксовано D-8)
```

**Оцінка overhead від NAU7802Plugin:**

| Компонент | RAM | Flash |
|-----------|-----|-------|
| `NAU7802Plugin` object | ~200 B BSS | — |
| `_samples[20]` (float buf) | 80 B BSS | — |
| NVS zero/scale/flags | ~50 B stack | — |
| `_mutex` (FreeRTOS) | 88 B BSS | — |
| Code (init + state machine) | — | ~8 KB |
| **Estimated total** | **~420 B BSS** | **~8 KB** |
| **New % estimate** | **~64.1%** | **~59.2%** |

**Бюджет limits (HARD):** RAM ≤ 70%, Flash ≤ 75%. Wave 10 добре в межах.

---

## 8. Acceptance Criteria

### Wave 10 Phase 1 — NAU7802 driver (D-12a..12c):

**D-12a — Driver:**
- [x] ~~5 register bugs~~  → усі 5 виправлені до commit
- [ ] `initialize()` returns `true` з NAU7802 на I²C bus
- [ ] `getWeight()` = SENTINEL до calibration
- [ ] `getWeight()` = ±0.1g на reference coin після calibration
- [ ] Serial log: `[NAU7802] startup OK, 80SPS, GAINS=128x`
- [ ] 137 native tests pass (нові не потрібні для D-12a)

**D-12b — NVS:**
- [ ] Calibration persist через power-cycle
- [ ] Missing/corrupted NVS → SENTINEL (graceful)
- [ ] `cal_ok = 0` після factory reset

**D-12c — UX wizard:**
- [ ] Wizard < 15s від 'K' до completion
- [ ] sigma < 0.05g на XUSSR10 reference
- [ ] OLED shows progress кожного кроку

### Wave 10 Phase 2 — 7D integration (D-12d..12e + C-13):

**D-12d — Vector:**
- [ ] gen-7 DB reads without errors (6D fallback)
- [ ] gen-8 DB: matchFull() 7D з mass_n
- [ ] Мінімум 5 нових тестів: mass_n normalization, sentinel fallback
- [ ] 137+5 native tests pass

**D-12e — Workflow:**
- [ ] STEP_WEIGHT стан у MeasState, handler коректно захоплює mass_g
- [ ] STEP_QUICK = STEP_BASE + matchQuick(7D+mass_n)
- [ ] 6D fallback: якщо NAU7802 absent/uncalibrated → Wave 9 production flow незмінений
- [ ] JSON result contains `mass_g`, `mass_n`, `workflow`

**A-3 — Quick Screen Phase 2:**
- [ ] matchQuick() розширений до 5 params (mass_n)
- [ ] matcher.json v6: `quick_weights` має 7 елементів
- [ ] STEP_QUICK confidence ≥ 0.75 → propose result
- [ ] Тест: `test_matchQuick_7d_with_mass`

**C-13 — HW Session:**
- [ ] Всі 13 класів з `mass_g` в DB gen-8
- [ ] xznnip_a: n ≥ 5, r95 < 0.20
- [ ] xkenned_b: n ≥ 5
- [ ] Calibration sigma < 0.1g

**A-8 — 7D pairwise:**
- [ ] `scripts/a8_pairwise_7d.py` runs без помилок
- [ ] Всі 5 пар що були < 1.0σ → тепер > 2.0σ

### 🏁 Wave 10 EXIT CRITERION:
> **Мінімальна пара в 7D > 2.0σ для всіх 13×13 комбінацій класів.**  
> Очікується: мінімальна пара XCUZN↔XAG800 (~1.5σ) — якщо < 2.0σ після A-8 → Wave 11 planning з додатковим discriminator.

### Track B (non-blocking):
- [ ] R-1: Δwdist < 0.5σ для 5 монет на Unit-2 → DB portable

### Track C (non-blocking):
- [ ] S-E1: A/B symmetry Δwdist < 0.3σ для Ø50mm coil
- [ ] S-E1: L = 135.1µH ± 10%

---

## 9. Coil Profile System — Multi-coil без змін коду

### Концепція

При прототипуванні потрібно тестувати різні котушки (MIKROE-3240 flat, E1 Ø50mm, можливо інші частоти/геометрії) **не втрачаючи накопичені дані по кожній**. Мета: zero code changes при зміні котушки.

### Механізм: `protocol_id` + coil profile на SD

Архітектура **вже частково підтримує** це через `protocol_id` в DB (кожен DB entry містить `protocol_id`; FingerprintCache фільтрує по ньому). Потрібно лише розширити на рівні конфігурації:

**SD file layout (розширений):**

```
SD:\CoinTrace\
  active_coil.txt              ← одна стрічка: "mikroe3240" або "e1_50mm" тощо
  coils\
    mikroe3240\                ← поточна виробнича котушка
      ldc1101.json             ← hw params (tc1, tc2, rp_set, f_empty_hz)
      matcher.json             ← weights оптимізовані для цієї котушки
      database\
        index.json             ← DB entries з protocol_id="p4_MIKROE3240_b06_012mm_mass"
        samples\ ← .ndjson файли
    e1_50mm\                   ← E1 Ø50mm котушка (після S-E1)
      ldc1101.json             ← інші tc1/tc2/rp_set (інша L, C)
      matcher.json             ← weights — потрібна re-calibration
      database\
        index.json             ← окрема DB, protocol_id="p1_E1_50mm_b06_012mm"
        samples\
```

**`active_coil.txt` → boot sequence:**

```cpp
// На старті в setup():
String activeCoil = readActiveCoil("/active_coil.txt");  // default: "mikroe3240"
String base = "/coils/" + activeCoil + "/";
loadConfig(base + "ldc1101.json");
loadDB(base + "database/");
loadMatcher(base + "matcher.json");
strlcpy(sProtocolId, buildProtocolId(activeCoil), sizeof(sProtocolId));
// Serial: "[Config] Active coil: mikroe3240"
```

**Зміна котушки — три кроки (без рефлешинга):**

```
1. Фізично підключити іншу котушку
2. Записати ім'я на SD: SD:\CoinTrace\active_coil.txt → "e1_50mm"
3. Перезавантаження (або OTA reset)
   Firmware автоматично завантажує e1_50mm/ldc1101.json + e1_50mm/database/
```

**Ізоляція DB по котушці:**

```
Котушка MIKROE-3240: накопичує за protocol_id="p4_MIKROE3240_b06_012_mass"
Котушка E1 Ø50mm:   накопичує за protocol_id="p1_E1_50mm_b06_012mm"

FingerprintCache::buildFromSD() завантажує тільки записи відповідного protocol_id.
Дані різних котушок НІКОЛИ не змішуються в одному DB query.
```

### Per-coil параметри (ldc1101.json дефіцит)

Кожна котушка має власний `ldc1101.json` з ключовими відмінностями:

| Параметр | MIKROE-3240 | E1 Ø50mm (est.) | Зміст |
|---|---|---|---|
| `tc1_val` | 213 | TBD (hw-verify S-E1) | PCB compensation |
| `tc2_val` | 254 | TBD | PCB compensation |
| `rp_set` | 54 | TBD | RP threshold |
| `f_empty_hz` | ~909200 | ~TBD (L≈135µH) | Baseline frequency |
| `min_freq_nibble` | 4 | TBD | Watchdog |
| `coin_detect_threshold` | 0.90 | TBD | Coin presence |

> **f_empty_hz** — новий параметр, який варто додати в ldc1101.json замість хардкоду у `delta_f_base_hz` computation. Дозволяє df_n/df1_n бути коректними без перекомпіляції при зміні котушки.

### Порівняльний A/B тест (S-E1 methodology)

```python
# scripts/compare_coils.py — post-hoc порівняння двох котушок
# Input: два NDJSON файли одної монети, різні protocol_id
# Output: wdist comparison, sigma comparison, A/B symmetry analysis
compare_coils(
    "sessions/mikroe3240/session_abc.ndjson",
    "sessions/e1_50mm/session_def.ndjson",
    metric="wdist_vs_db"
)
# Порівнює: чи DB gen-8 (mikroe3240) дає кращий wdist ніж proto E1 DB?
```

### Implementation plan (Wave 10 backlog → Wave 11 якщо потрібен E1)

| Задача | Складність | Блокер для |
|---|---|---|
| `active_coil.txt` reader в setup() | Low | S-E1 |
| Per-coil config path в loadConfig() | Low | S-E1 |
| Per-coil DB path в buildFromSD() | Low | S-E1 |
| `f_empty_hz` в ldc1101.json | Low | Точність df_n |
| `compare_coils.py` script | Medium | A/B analysis |

> Якщо S-E1 buys significant SNR / A/B symmetry improvement → Wave 11 може мігрувати основну DB на E1 як primary coil, зберігши MIKROE-3240 як reference.

---

*Документ: `docs/architecture/WAVE10_ROADMAP.md`*  
*Версія: 1.1.0 | Дата: 2026-04-08 (оновлено: 2026-04-08 — D-12e redesign sequential, A-3 unblocked, coil profile system)*  
*Базується на: WAVE9_ROADMAP.md v2.0.0, NAU7802_ARCHITECTURE.md v1.1.0, docs/external/2026-04-08.WAVE10_ARCHITECTURE_PLAN.md*  
*Наступне оновлення: після D-12a hw-verify (register bugs confirmed fixed)*
