# Wave 9 Roadmap — Measurement Science

**Статус:** 📋 Planned — очікує завершення Wave 8 C-7  
**Версія:** 1.0.0  
**Дата:** 2026-03-27  
**Попередня хвиля:** Wave 8 — Connectivity + Infrastructure + Sensor Integration (C-7 MetalMatcher + Quick Screen = final milestone)  
**Тригер:** C-5 Deep Analysis Audit (2026-03-27) — виявлено обмеження 2-dimensional effective vector, rp[2] saturation 80%, dL1_n ferro blindness  
**Cross-ref:** `WAVE8_COMPLETION_WAVE9_DISCOVERY_PLAN.md`, `DISCOVERY_MODE_SPEC.md`, `C5_DEEP_ANALYSIS_AUDIT.md`

---

## Контекст та головний constraint

Wave 8 доставила працюючу систему класифікації: 5 монет × 5 циклів = 25/25 accuracy, REST API, OTA, Web UI, configurable weights через matcher.json. Це proof-of-concept.

Wave 9 відповідає на питання: **як масштабувати від 5 монет до 50+, зберігаючи accuracy?**

C-5 аудит встановив три факти які визначають стратегію Wave 9:

| # | Факт | Наслідок |
|---|------|----------|
| 1 | Ефективна розмірність вектора = 2 (dRp1_n + k1), не 5 | k2/slope — артефакт насичення; dL1_n — blind spot |
| 2 | Найближча пара XCU↔XZNNIP має margin лише 0.51σ | При ΔT > 10°C або додаванні подібних металів — ризик misclassification |
| 3 | LDC1101 видає 3 raw параметри (RP, L_DATA, LHR_DATA), з яких використовується лише 1 | 24-bit LHR та fSENSOR не задіяні; multi-sample stats не збираються |

**Ключовий висновок:**

> Проблема не у firmware чи алгоритмі — а у **недостатньому використанні фізики сенсора**. LDC1101 здатний видати значно більше інформації ніж ми збираємо. Wave 9 — це дослідження того, яка інформація корисна для класифікації, і інженерне рішення на основі даних.

---

## Зміст

1. [Матриця задач Wave 9](#1-матриця-задач-wave-9)
2. [Track D — Discovery Mode firmware](#2-track-d--discovery-mode-firmware)
3. [Track C continued — HW Session C-6](#3-track-c-continued--hw-session-c-6)
4. [Track A — Analysis pipeline](#4-track-a--analysis-pipeline)
5. [Рекомендована послідовність](#5-рекомендована-послідовність)
6. [RAM та Flash бюджет Wave 9](#6-ram-та-flash-бюджет-wave-9)
7. [Acceptance Criteria](#7-acceptance-criteria)

---

## 1. Матриця задач Wave 9

| Задача | Track | HW? | Залежить від | Статус | Опис |
|--------|-------|-----|-------------|--------|------|
| D-1 Multi-sample capture | D | ❌ | Wave 8 C-7 done | 📋 Planned | N~600 samples per step, reservoir median, σ |
| D-2 LHR continuous mode | D | ❌ | Wave 8 C-7 done | 📋 Planned | 24-bit fSENSOR в кожному update() |
| D-3 Raw dump to SD | D | ❌ | D-1, D-2 | 📋 Planned | JSON session file з повною статистикою |
| C-6 Discovery HW Session | C | ✅ | D-1, D-2, D-3 | 📋 Planned | 5 old + 2-4 new coins, raw dump collection |
| A-1 Offline analysis | A | ❌ | C-6 data | 📋 Planned | Python: Δf, σ, LHR precision, pairwise distances |
| A-2 Vector v2 decision | A | ❌ | A-1 | 📋 Planned | ADR: which dimensions, which weights |
| A-3 Quick Screen Phase 2 | A | ⚠️ | A-2 | 📋 Planned | matchQuick() + quick_centroid entries in DB |
| A-4 index.json gen 3 + matcher.json v2 | A | ⚠️ | A-2, A-3 | 📋 Planned | Updated DB + weights from analysis |

> **Naming convention:** Track D = "Discovery" (нові firmware capabilities для збору даних). Track C continues sensor-specific HW sessions з Wave 8 numbering. Track A = "Analysis" (offline processing + firmware integration of results).

---

## 2. Track D — Discovery Mode firmware

### D-1: Multi-sample capture

**Специфікація:** `DISCOVERY_MODE_SPEC.md §4`

**Що:** Після натискання ENTER на кожному measurement step — capture loop збирає N~600 пар {RP_DATA, L_DATA} протягом ~2 секунд. Статистична обробка: median (reservoir sampling, size=64), mean, σ, min, max.

**Чому:** Один read per step (поточний production) не дає інформації про noise floor, stability, та settling behavior. Multi-sample capture дає: надійнішу медіану (robust до outliers), σ як потенційний discriminator, settling curve analysis.

**Зміни:**

| Файл | Зміна |
|------|-------|
| `src/main.cpp` | `discoveryCaptureStep()` function; STEP_* handlers call capture when `discovery_enabled` |
| `data/plugins/ldc1101.json` | `discovery_enabled`, `discovery_capture_ms`, `discovery_settle_ms` |
| `platformio.ini` | `-D DISCOVERY_MODE` build flag |

**Config:**

```json
"discovery_enabled": true,
"discovery_capture_ms": 2000,
"discovery_settle_ms": 300
```

**RAM:** CaptureStats = 348 B stack; `sDiscoverySteps[4]` = 1,392 B BSS (static). Total: ~1.4 KB permanent BSS.

**Timing:** ~2.3 s per step (300 ms settle + 2000 ms capture). 4 steps × 2.3 s = 9.2 s capture overhead per coin.

**Production impact:** При `discovery_enabled=false` — zero overhead (if-guard at entry). Single-read production flow unchanged.

---

### D-2: LHR continuous mode

**Специфікація:** `DISCOVERY_MODE_SPEC.md §5`, `LDC1101_ARCHITECTURE.md §10 задача 9`

**Що:** Активувати існуючий `lhr_continuous` config path. При кожному `update()` перевіряти `LHR_STATUS.DRDYB`, якщо дані готові — read 3 bytes LHR_DATA (24-bit) в кеш.

**Чому:** LHR дає 256× вищу роздільність L-вимірювання ніж 16-bit L_DATA. Це дозволяє обчислити precise fSENSOR per step, і отримати Δf (зсув частоти) — потенційно найцінніший невикористаний параметр для ferro/non-ferro розділення.

**Prerequisite:** CLKIN wired (GPIO4 → mikroBUS Pin 16). Перевіряється `isLDataValid()` at boot.

**Зміни:**

| Файл | Зміна |
|------|-------|
| `lib/LDC1101Plugin/src/LDC1101Plugin.h` | LHR read block в `update()` (замість TODO); `getLiveLHR()` public; `diag.lhrErrorLogged` |
| `data/plugins/ldc1101.json` | `"lhr_continuous": true` |

**Timing overhead в update():** ~20 μs worst case (1 SPI status read + 3 SPI data reads). 0.1% від 20 ms бюджету.

**LHR_STATUS error handling:** Bits ERR_ZC, ERR_OR, ERR_UR, ERR_OF — log warning once, skip cache update, wait for next conversion. Деталі: DISCOVERY_MODE_SPEC.md §5.

---

### D-3: Raw dump to SD

**Специфікація:** `DISCOVERY_MODE_SPEC.md §6`

**Що:** Після кожного повного 4-step measurement (COMPUTE state), якщо `discovery_enabled` і SD mounted — зберегти розширений JSON з повною статистикою per step + production vector + discovery derived parameters (Δf, σ, dRpPct_baseline).

**Чому:** Raw dump — вхідні дані для офлайн аналізу (Track A). Без нього рішення про Vector v2 базуватимуться на здогадках, а не на даних.

**Зміни:**

| Файл | Зміна |
|------|-------|
| `src/main.cpp` | `saveDiscoveryDump()` function, called after `doMeasCompute()` when discoveryActive |

**File format:** One JSON session file per session: `SD:\CoinTrace\discovery\session_YYYYMMDD_HHMMSS.json`. Schema: DISCOVERY_MODE_SPEC.md §6.

**RAM:** ArduinoJson DynamicJsonDocument(3072) на heap — temporary, ~50 ms lifetime. Heap idle 30 KB → safe.

**SD space:** ~2 KB per measurement × 9 coins = ~18 KB per session. Negligible.

---

## 3. Track C continued — HW Session C-6

### C-6: Discovery HW Session

**Prerequisite:** D-1, D-2, D-3 implemented and verified. Firmware з `-D DISCOVERY_MODE` flashed.

**Мета:** Зібрати raw discovery data для всіх 5 оригінальних монет C-5 (baseline comparison) + 2-4 нові монети (DB expansion + нові класи металів).

### Checklist перед сесією

- [ ] `ldc1101.json`: `discovery_enabled: true`, `lhr_continuous: true`
- [ ] SD card mounted, `CoinTrace/discovery/` directory exists
- [ ] Firmware з `-D DISCOVERY_MODE` flashed
- [ ] Serial monitor: `isLDataValid() = true` (CLKIN wired)
- [ ] Serial monitor: `FingerprintCache ready — 5 entries (generation 2)` (DB loaded)
- [ ] Baseline RP, L, LHR — зафіксовано з boot log

### Coin set

| # | Монета | Metal code | Мета | C-5? |
|---|--------|-----------|------|------|
| 1 | American Silver Eagle 1oz | XAG999 | Baseline comparison; Δf reference (highest σ) | ✅ |
| 2 | Russian Empire 5 Kopecks | XCU | Baseline; XCU↔XZNNIP separation analysis | ✅ |
| 3 | Ukraine 10 UAH 2022 | XZNNIP | Baseline; XCU↔XZNNIP separation analysis | ✅ |
| 4 | Germany 1.5 EUR 1997 | XFE | **Key experiment: Δf ferro test through Cu plating** | ✅ |
| 5 | Germany 50 Pfennig | XAL | Baseline; lowest coupling reference | ✅ |
| 6 | Au999 coin (TBD) | XAU999 | New class: gold (σ=45 MS/m, μr=1) | ✅ NEW |
| 7 | Ag925 coin (TBD) | XAG925 | New class: sterling (σ ≈ 25-35 MS/m) — test Ag999/925 separation | ✅ NEW |
| 8 | CuNi coin (TBD) | XCUNI | New class: cupronickel (σ ≈ 3 MS/m) | Optional |
| 9 | Pure Ni coin (TBD) | XNI | New class: true ferromagnet (μr ≈ 600) — Δf should be strongly negative | Optional |

> Монети 6-9 залежать від наявності у колекції. Мінімум: 5 оригінальних + 1 нова. Ідеально: +Au999 і +Ag925 для тесту розділення сплавів.

### Процедура per coin

Стандартний p3 protocol (base 0.6mm + addon spacers 1mm, 2mm + drift), але кожний step з ~2.3s capture window:

```
1. Place coin on base (0.6mm) → ENTER → [2.3s capture] → done
2. Add +1mm spacer             → ENTER → [2.3s capture] → done  
3. Add +2mm spacer             → ENTER → [2.3s capture] → done
4. Remove spacers, return coin → ENTER → [2.3s capture] → drift check → done
5. Remove coin → COMPUTE → production save + discovery dump → IDLE
```

**Час на монету:** ~24 секунди capture + ~20 секунд transitions = ~45 секунд.  
**Total session (9 coins × 5 циклів):** ~45 хвилин. Realistic для однієї вечірньої сесії.

> ⚠️ **Thermal drift management:** При сесії > 30 хв drift може досягати 2-3% (C-5 data: 2.87% за ~20 хв). Рекомендація: re-calibrate ('R' key) кожні 10 вимірів. Discovery dump зберігає `baseline_rp_session` для post-hoc drift correction.

### Ключові експерименти C-6

| Код | Що перевіряємо | Критерій успіху |
|-----|---------------|----------------|
| **EXP-1** | Δf розрізняє XFE (Fe+Cu) від XCU (чиста Cu) | Δf(XFE) < 0 AND Δf(XCU) > 0 (протилежні знаки) |
| **EXP-2** | Multi-sample σ(RP) відрізняється per metal | σ(RP) for Al ≠ σ(RP) for Ag (statistically significant) |
| **EXP-3** | LHR 24-bit дає кращу fSENSOR роздільність ніж L_DATA 16-bit | LHR fSensor σ < L_DATA-derived fSensor σ |
| **EXP-4** | Ag999 vs Ag925 розділяються по dRp1_n або Δf | Pairwise distance > 2σ between XAG999 and XAG925 |
| **EXP-5** | Discovery медіана точніша за single read (C-5 comparison) | σ(centroid) для Discovery < σ(centroid) для C-5 single-read |
| **EXP-6** | Settling time 300ms достатній | First 50 samples vs last 50: mean difference < 0.5% |

---

## 4. Track A — Analysis pipeline

### A-1: Offline analysis

**Prerequisite:** C-6 raw dump JSON on SD.

**Tool:** `tools/discovery_analyze.py` (новий Python CLI script).

**Input:** `session_*.json` з SD card.

**Аналіз:**

| # | Що | Output |
|---|------|--------|
| 1 | Per-metal distributions | Boxplots: RP_median, L_median, fSensor per step per metal |
| 2 | Δf analysis | Scatter: Δf vs metal → sign consistency, ferro separation |
| 3 | σ(RP) correlation | Bar chart: σ(RP) per metal → is it a discriminator? |
| 4 | LHR vs L_DATA precision | Table: fSensor σ from LHR vs from Eq.6 |
| 5 | Pairwise distance matrix | Heatmap: new dimensions vs old 5D |
| 6 | Settling time validation | Time series: first 300ms vs steady-state |
| 7 | Quick Screen centroids | Table: dRpPct_baseline and dL_raw per metal |

**Output:** `C6_ANALYSIS_REPORT.md` — рекомендації для Vector v2.

---

### A-2: Vector v2 decision

**Prerequisite:** A-1 analysis report.

**Рішення формулюється як ADR (Architecture Decision Record)** з чіткими alternatives та обґрунтуванням.

**Можливі сценарії:**

| Сценарій | Новий вектор | Умова |
|----------|-------------|-------|
| **S1: Δf працює** | `[dRp1_n, k1, Δf_n, dL1_n]` 4D | EXP-1 passed: Δf розділяє ferro |
| **S2: σ працює** | `[dRp1_n, k1, σ_rp_n, dL1_n]` 4D | EXP-2 passed: σ характерний per metal |
| **S3: Δf + σ** | `[dRp1_n, k1, Δf_n, σ_rp_n, dL1_n]` 5D | EXP-1 + EXP-2 passed |
| **S4: Нічого нового** | `[dRp1_n, k1, dL1_n]` 3D | EXP-1..3 failed: дані не допомагають |
| **S5: Зменшити d_max** | `[dRp1_n, k1, k2_fixed, dL1_n]` 4D | k2 unsaturated при d_max ≈ 1.8mm |

> Рішення приймається на основі числових результатів A-1, не інтуїції. Мінімальний критерій для додавання нового компоненту: він повинен **збільшити мінімальну pairwise distance** (closest pair) на ≥ 20%.

**Наслідки для firmware:**

| Сценарій | VectorCompute зміни | LHR потрібен в production? | Capture потрібен в production? |
|----------|---------------------|---------------------------|-------------------------------|
| S1 | Додати `deltaF_n()` | ✅ Так (lhr_continuous=true) | ❌ Ні (single LHR read per step) |
| S2 | Додати `rpSigma_n()` | ❌ Ні | ✅ Так (multi-sample per step) |
| S3 | Обидва | ✅ Так | ✅ Так |
| S4 | Видалити slope() | ❌ Ні | ❌ Ні |
| S5 | Новий spacer, slope() рішення | ❌ Ні | ❌ Ні |

---

### A-3: Quick Screen Phase 2

**Prerequisite:** A-2 (vector decision) + discovery data for centroid computation.

**Що:** Замінити threshold-based `classifyQuick()` (Phase 1) на `gMatcher.matchQuick()` з real centroids.

**Зміни:**

| Файл | Зміна |
|------|-------|
| `data/sd_seed/CoinTrace/database/index.json` | Add `quick_centroid` per entry; bump generation → 3 |
| `data/sd_seed/CoinTrace/matcher.json` | Update `quick_weights` based on analysis |
| `src/main.cpp` | Inside `drawQuickScreen()`: Phase 1 → Phase 2 switch |

**Pipeline для quick_centroid:** DISCOVERY_MODE_SPEC.md §7 та QUICK_SCREEN_SPEC.md §4.2.

---

### A-4: index.json gen 3 + matcher.json v2

**Prerequisite:** A-2 (vector decision), A-3 (quick_centroid).

**Deliverables:**

1. **index.json generation 3** — включає:
   - Оновлені centroids (based on Discovery medians замість single-read means)
   - `quick_centroid` per entry (для Phase 2 Quick Screen)
   - Можливо нові dimension fields (якщо Vector v2 ≠ Vector v1)
   - Нові монети з C-6 (Au999, Ag925, тощо)

2. **matcher.json v2** — включає:
   - Оновлені `full_weights` (based on analysis)
   - Оновлені `quick_weights`
   - Можливо оновлений `sigma`
   - Можливо новий `ferro_thresh_dL1_n` (якщо Δf-based ferro detection замінить dL1_n-based)

---

## 5. Рекомендована послідовність

### Фаза 1: Discovery firmware (Sprint 2)

```
Prerequisites:
  Wave 8 C-7 MetalMatcher + Quick Screen Phase 1    ✅ (Wave 8 closure)
  DISCOVERY_MODE_SPEC.md                             ✅ Готово (2026-03-27)
  WAVE9_ROADMAP.md (цей документ)                    ✅ Готово (2026-03-27)

Sprint 2 sequence:
  D-2  LHR continuous mode        ~0.5 дні  (update() TODO → real, getLiveLHR())
  D-1  Multi-sample capture       ~1 день   (captureStep(), reservoir sampling, progress bar)
  D-3  Raw dump to SD             ~0.5 дні  (saveDiscoveryDump(), JSON serialization)
  ──── firmware ready ─────────────────────
  C-6  Discovery HW Session       ~1 день   (5+2-4 coins × 5 cycles, ~45 хвилин hw-time)
```

### Фаза 2: Analysis та рішення (Sprint 3)

```
  A-1  Offline analysis           ~1.5 дні  (Python script, plots, report)
  A-2  Vector v2 ADR              ~0.5 дні  (decision document based on data)
  A-3  Quick Screen Phase 2       ~0.5 дні  (matchQuick integration, quick_centroid)
  A-4  index.json gen 3           ~0.5 дні  (new centroids, new coins, new weights)
```

### Загальна оцінка

| Фаза | Зусилля | HW-час | Output |
|------|---------|--------|--------|
| Sprint 2 (firmware + HW) | ~3 робочі дні | ~1 день | Raw dump JSON, LHR data |
| Sprint 3 (analysis + integration) | ~3 робочі дні | ~0.5 дні (hw-verify) | Vector v2, gen 3 DB, Phase 2 Quick Screen |
| **Wave 9 total** | **~6 робочих днів** | **~1.5 дні** | **Data-driven classification architecture** |

---

## 6. RAM та Flash бюджет Wave 9

### Incremental RAM (over Wave 8 final)

| Компонент | Тип | Розмір | Постійний? | Умова |
|-----------|-----|--------|------------|-------|
| `sDiscoverySteps[4]` | BSS | 1,392 B | Так | `#ifdef DISCOVERY_MODE` |
| `diag.lhrErrorLogged` | BSS | 1 B | Так | Always (LHR continuous) |
| `DynamicJsonDocument(3072)` | Heap | 3,072 B | Ні (~50 ms) | Discovery dump write |
| `CaptureStats` local | Stack | 348 B | Ні (~2.3 s) | During capture loop |
| **Total permanent BSS** | | **~1,393 B** | | BSS headroom 118 KB → **1.2%** |
| **Peak heap** | | **~3 KB** | | Heap 30 KB → **10%** |

### Flash (firmware)

| Компонент | Розмір (estimate) | Деталі |
|-----------|------------------|--------|
| D-1 captureStep() | ~2 KB | Capture loop + reservoir + finalize |
| D-2 LHR continuous | ~0.5 KB | update() extension |
| D-3 raw dump | ~3 KB | JSON serialization + file write |
| D-x display progress | ~1 KB | drawCaptureProgress() |
| **Total** | **~6.5 KB** | Flash headroom 1.1 MB → **0.6%** |

### Висновок

Discovery Mode — **практично безкоштовний** з точки зору ресурсів. При `#ifdef DISCOVERY_MODE` в release builds — навіть BSS overhead відсутній.

---

## 7. Acceptance Criteria

### Wave 9 Phase 1 (Sprint 2) — Discovery firmware + HW Session:

**Discovery firmware:**
- [ ] `discovery_enabled=true` → capture loop runs 2s per step (Serial log: `Capture: N=600+ samples`)
- [ ] `lhr_continuous=true` → LHR data present in cache (Serial log: `LHR: mean=XXXXX, n=30+`)
- [ ] Discovery JSON saved to SD after each measurement (`session_*.json` exists)
- [ ] `discovery_enabled=false` → production behavior identical to Wave 8 (single read, no capture)
- [ ] All existing native tests pass (134+ with MetalMatcher from Wave 8 C-7)
- [ ] Display shows capture progress bar during each step

**C-6 HW Session:**
- [ ] Minimum 5 original C-5 coins measured with Discovery Mode
- [ ] Minimum 2 new coins measured (Au999 or Ag925 preferred)
- [ ] Raw dump JSON per coin contains: RP stats (median, σ, min, max, N), L stats, LHR stats (mean, N, fSensor)
- [ ] Production match result present in dump (metal_code, confidence)
- [ ] `delta_f_base_hz` computed and present for each measurement (Δf = fSensor_coin − fSensor_baseline)
- [ ] Baseline RP/L/LHR documented in session header
- [ ] Thermal drift: re-calibrate performed every 10 measurements if session > 30 min

### Wave 9 Phase 2 (Sprint 3) — Analysis + Vector v2:

**Analysis:**
- [ ] `C6_ANALYSIS_REPORT.md` created with plots and conclusions for EXP-1 through EXP-6
- [ ] Pairwise distance matrix computed for candidate new vectors
- [ ] ADR-V2 (Vector v2 decision) documented with numerical justification

**Integration:**
- [ ] index.json generation 3: updated centroids + `quick_centroid` + new coins
- [ ] matcher.json v2: updated weights based on analysis
- [ ] Quick Screen Phase 2: `matchQuick()` replaces `classifyQuick()` (if Phase 2 criteria met: QUICK_SCREEN_SPEC.md §4.2)
- [ ] Production accuracy: ≥ 95% on expanded coin set (5 original + new coins)
- [ ] Closest pair distance > 1.0σ (improvement over C-5 margin of 0.51σ)
- [ ] All native tests pass

### Wave 9 exit criteria (Wave 9 = COMPLETE when):

- [ ] Vector composition justified by experimental data (ADR-V2 documented)
- [ ] Classification accuracy ≥ 95% on ≥ 7 distinct metals
- [ ] All Discovery experiment conclusions documented (EXP-1..EXP-6)
- [ ] Quick Screen Phase 2 operational (or Phase 1 with justified threshold calibration)
- [ ] matcher.json v2 on SD with validated weights
- [ ] Decision documented: custom coil needed for v2? (based on Δf and skin depth analysis)
- [ ] `TECHNICAL_DEBT.md` оновлено: TD-03/TD-05/TD-06/TD-07/TD-08/TD-09 статуси відображають результати Wave 9

---

*Версія 1.1.0 (2026-03-30) — додано критерій закриття TECHNICAL_DEBT.md до Wave 9 exit criteria.*  
*Версія 1.0.0 — initial Wave 9 roadmap, created 2026-03-27 based on C-5 Deep Analysis Audit findings and WAVE8_COMPLETION_WAVE9_DISCOVERY_PLAN.md.*
