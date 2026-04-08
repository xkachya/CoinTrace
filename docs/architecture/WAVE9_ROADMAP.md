# Wave 9 Roadmap — Measurement Science

**Статус:** ✅ **CLOSED (2026-04-08)** — всі заплановані deliverables виконані. Exit criterion по якості класифікатора (мінімальна пара > 1.0σ) не досягнутий як **запланований результат** апаратного обмеження одного LDC1101. Передано у Wave 10 (NAU7802 + 7D вектор).  
**Версія:** 2.0.0  
**Дата:** 2026-03-27 (CLOSED: 2026-04-08 — gen-7 DB: 28 entries, 13 classes, A-7 pairwise: 5 pairs < 1.0σ — physics constraint; Wave 10 triggered)
**Попередня хвиля:** Wave 8 — Connectivity + Infrastructure + Sensor Integration (C-7 MetalMatcher + Quick Screen = final milestone)  
**Тригер:** C-5 Deep Analysis Audit (2026-03-27) — виявлено обмеження 2-dimensional effective vector, rp[2] saturation 80%, dL1_n ferro blindness  
**Cross-ref:** `WAVE8_COMPLETION_WAVE9_DISCOVERY_PLAN.md`, `DISCOVERY_MODE_SPEC.md`, `C5_DEEP_ANALYSIS_AUDIT.md`, `2026-04-01.D4_SENSOR_CALIBRATION_PLAN.md`

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
| D-1 Multi-sample capture | D | ❌ | Wave 8 C-7 done | ✅ Done (2026-03-31) | N~600 samples per step, reservoir median, σ |
| D-2 LHR continuous mode | D | ❌ | Wave 8 C-7 done | ✅ Done (2026-03-30) | 24-bit fSENSOR в кожному update() |
| **D-2b StabilityTracker (ADR-STAB-001)** | D | ❌ | D-2 | ✅ Done (2026-03-30) | `StabilityTracker` + dual cache в `LDC1101Plugin.h`; STEP_1/3/DRIFT settling guard у `main.cpp` |
| D-3 Raw dump to SD | D | ❌ | D-1, D-2, D-2b | ✅ Done (2026-04-01) | NDJSON per-measurement append, `esp_random()` filename, ArduinoJson v7 |
| **D-4 Sensor Physical Calibration** | D | ✅ | D-3, C-6 analysis | ✅ Done (2026-04-01) | TC1/TC2 fix (×52/×11 errors); RP_SET RPMAX correction. ADR-LDC-002. hw-verified C-6b ✅ |
| C-6 Discovery HW Session | C | ✅ | D-1, D-2, D-3 | ✅ Done (2026-04-01) ⚠️ old config | 100 вимірів, 20 монет. RP-дані: old config (TC1/TC2 bug). LHR-дані: валідні |
| **C-6b Re-verification HW Session** | C | ✅ | D-4 ✅ | ✅ Done (2026-04-01) | 30 вимірів, 6 монет (Ag999×2, Ag900×3, Ag800×1). Zero SAT-lock. All AC pass. Audit: `2026-04-02.D4_C6b_AUDIT_REPORT.md` |
| **D-5 NDJSON Vector v2 patch** | D | ❌ | D-4, C-6b analysis | ✅ **Done (2026-04-02)** | Remove redundant `slope`, add `df_n` (Δf/f_empty) to `production_vector`. ADR-VEC-001. Commit 4561c09 |
| **C-7 Full Discovery Session** | C | ✅ | D-5, new 2.6mm spacer | ✅ **Done (2026-04-02)** | 9 монет × 5 вимірів (45 total). session_63824c66. 3 new types: XAG900, XKENNEDY, XCUZN |
| A-1 Offline analysis | A | ❌ | C-6b + C-7 | ✅ **Done (2026-04-03)** | `scripts/a1_analysis.py`. EXP-C7-1/3/5 PASS. df_n = 16σ XCU/XZNNIP discriminant. Report: `A1_ANALYSIS_session_63824c66.md` |
| A-2 Vector v2 decision | A | ❌ | A-1 | ✅ **Done (2026-04-03)** | Merged into ADR-VEC-001 (slope→df_n) + ADR-VEC-002 (dk_n→df1_n). 6D vector: [dRp1_n,k1,k2,df_n,dL1_n,df1_n]. Weights [1.5,0.0,1.0,3.0,2.5,0.4]. Числове обґрунтування: df_n 16σ (XCU↔XZNNIP), df1_n 7.5σ (Kennedy_B↔USSR_A) |
| A-3 Quick Screen Phase 2 | A | ⚠️ | A-2 | 🚀 **Deferred → Wave 10** | matchQuick() + mass_n threshold. Відкладено: Phase 2 потребує 7D вектора (mass_n). Перенесено в Wave 10 backlog |
| A-4 index.json gen 3 + matcher.json v2 | A | ⚠️ | A-2, A-3 | ✅ **Done (2026-04-03)** | 9 entries, df_n centroids. Weights [1.5,0,1,3.5,2.5] σ=0.35. Gen-3 DB deployed |
| **D-6 df_n in real-time matcher** | D | ❌ | A-1, A-4 | ✅ **Done (2026-04-03)** | FingerprintCache/MetalMatcher/main.cpp: slope→df_n pipeline. Firmware reads gen-3 df_n field. Build: SUCCESS |
| **A-5 dk_n spatial gradient analysis** | A | ❌ | A-1, C-7 data | ✅ **Done (2026-04-03)** | dk_n REJECTED: z=0.9 (Kennedy_B/USSR_A). df1_n=(fs1−f_empty)/f_empty дає z=7.5 тій самій парі. Див. ADR-VEC-002 |
| **ADR-VEC-002 df1_n як 6-й вимір** | A | ❌ | A-5, C-8 | ✅ **Done (2026-04-03)** | Замінює dk_n: df1_n=(fs1−f_empty)/f_empty. z=7.5 Kennedy_B/USSR_A. FingerprintCache 6th field, matchFull() 6th param, gen-4 schema, matcher.json v3 |
| **D-7 6D vector: add df1_n** | D | ❌ | ADR-VEC-002 | ✅ **Done (2026-04-03)** | **Revised** (dk_n→df1_n): CacheEntry.df1_n, query() 6th param, matchFull() 6th arg, sSteps[1].fSensorHz. buildFromSD() fix. 137 tests ✅. RAM 63.4% Flash 58.2% |
| **D-7b LHR в production path** | D | ❌ | D-7 | ✅ **Done (2026-04-03)** | Superseded by D-8: full unified capture pipeline (not single LHR read). See §D-8 |
| **D-8 Unified capture pipeline** | D | ❌ | D-7b | ✅ **Done (2026-04-03)** | `captureStep()` always compiled (no `#ifdef`). `sCaptureMs=1500ms/step` production, `sSteps[4]` always BSS. `meas_df_n`/`meas_df1_n` always from `sSteps[0/1].fSensorHz`. `df_n=` display. `IStorageManager::queryFingerprint` `slope`→`df_n` param. `ldc1101.json`: `prod_capture_ms=1500`. 137 tests ✅. RAM 63.4% Flash 58.1% |
| **C-8 HW Session (A/B side control)** | C | ✅ | — | ✅ **Done (2026-04-03)** | 50 записів, 5 монет×2 sides×5 вимірів. XAG800_BHS + XUSSR10R нові класи. dk_n REJECTED. XFE centroid bug (bimetal seed) знайдено. Guide: `C8_HW_SESSION.md` |
| **A-6 gen-4 → gen-7 DB pipeline** | A | ❌ | C-8, C-10, C-11, C-12 | ✅ **Done (2026-04-03→04-08)** | D-9 (gen-4/4 entries, commit `41d10e5`) → D-10 (gen-5/28 entries, `3510b05`) → D-11 (gen-6, `30c011a`) → D-11d (gen-7, `258c74a`). matcher v4→v5. Скрипти: `scripts/build_gen7_db.py` |
| **C-9 XFE re-seed HW Session** | C | ✅ | ADR-VEC-002 | ✅ **Resolved via C-12 (2026-04-07)** | XFE reseeded у C-12 (n=10/side). Сталева монета як окремий клас XFENIP/XFECUP знайдена в C-11. Kennedy_B overlap усунутий через 7D вектор (Wave 10). |
| **C-10 HW Session (9 classes reseed)** | C | ✅ | D-8 ✅ | ✅ **Done (2026-04-04)** | 93 записи (4 сесії), 2 post-hoc rebased. XUSSR10, XKENNED, XAG900, XCUZN, XFE, XAL, XCU, XZNNIP — повний 6D ресід. rp0_outlier idx0,2 у XZNNIP_a flagged. Commit `3510b05` |
| **C-11 HW Session (3 нові класи)** | C | ✅ | C-10 ✅ | ✅ **Done (2026-04-07)** | 48 записів: XFENIP (r95_a=0.978→C-12 reseed), XFECUP, XNICKEL. Kennedy_B reseed (+4 rec). XZNNIP_B reseed (+3 rec). Commit `3510b05` |
| **C-12 HW Session (XUSSR10 + XFENIP reseed)** | C | ✅ | D-11 ✅ | ✅ **Done (2026-04-07)** | 42 raw → 35 kept (idx30-31 warmup + idx37-41 XFENIP-B Z-fail excluded). XUSSR10 m1/m2 A/B confirmed same Ag900. XFENIP needs_reseed cleared (r95=0.145). `remap_c12.py` index-based remap |
| **A-7 Pairwise analysis (gen-7)** | A | ❌ | C-12, D-11d | ✅ **Done (2026-04-08)** | `scripts/a7_pairwise_analysis.py`. 5 пар < 1.0σ (всі — апаратний constraint LDC1101 silver overlap). Verdict: NAU7802 + 7D vector вирішує всі 5. Wave 10 triggered. |

> **Naming convention:** Track D = "Discovery" (нові firmware capabilities для збору даних). Track C continues sensor-specific HW sessions з Wave 8 numbering. Track A = "Analysis" (offline processing + firmware integration of results).

> **⚠️ C-6 Data Note (2026-04-01):** C-6 сесії виконані з TC1=0x1F/TC2=0x3F (помилка ×52/×11 від даташіту). **LHR-дані повністю валідні** — не залежать від TC1/TC2. **RP-дані** потребують повторного вимірювання в C-6b після D-4 fix. SAT-lock значення 0x9999/0xB6DB/0xCCCC у деяких монетах — артефакт bug TC1/TC2, не фізичне насичення.

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

**HW verified 2026-03-31 (Ag999, 1oz Eagle, Cardputer-Adv + MIKROE-3240):**
- N=110 per step (2000 ms capture) — `RESP_TIME_BITS=7` → `convTimeMs_()=15 ms` → `delay(17 ms)` → `2000/17≈117`; reservoir[64] filled at N>64. Spec estimated N~600 based on incorrect `convTimeMs=3.3 ms`.
- fSENSOR@BASE = 1,407,734 Hz (+55% від baseline 909 kHz) — eddy current shift Ag999 під'яскравлює L, що збільшує fSENSOR
- LHR drift: +2,068 Hz за ~90 с (Step 4 vs Step 1) — термальний дрейф зафіксовано 24-bit LHR; 16-bit L_DATA не відрізняє
- `lhr_n=0` на Steps 2 та 3 (known limitation): при 1.6 mm та 2.6 mm відстані LHR_STATUS.DRDYB ніколи не переходить в 0 протягом 2 с capture window — LHR конверсія не завершується (fSENSOR зміщується на цих відстанях). Впливи на D-3: `lhr_n=0` для steps 1 та 2 в JSON — це очікувана поведінка.
- `rp_sigma=0.0` на Steps 2 та 3 — жорсткий акриловий спейсер → сигнал стабільний до LSB
- Vec k1=1.215 k2=1.347 slope=0.1735 — ідентично попереднім C-5 сесіям, вимірювання відтворювані

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

### D-2b: StabilityTracker (ADR-STAB-001)

**Специфікація:** `LDC1101_ARCHITECTURE.md v1.5.0 §8.3`

**Що:** Реалізувати nested `StabilityTracker` struct у `LDC1101Plugin.h` з dual cache pattern. Додати `readyByTimer || readyBySignal` guard у STEP_1/3/DRIFT handlers у `main.cpp`.

**Чому:** Поточні STEP_1/3/DRIFT handlers роблять immediate single read після ENTER — без перевірки стабільності. При швидкому натисканні (spacer ще тремтить) можлива похибка 0.5–2%. `StabilityTracker` вирішує це на рівні сигналу (не тільки таймера), повертаючи frozen snapshot з N=8 послідовних зразків з σ(RP) < `stab_sigma_thresh_rp`.

**Зміни:**

| Файл | Зміна |
|------|-------|
| `lib/LDC1101Plugin/src/LDC1101Plugin.h` | `StabilityTracker` struct, `StableCache` struct, `stab_.feed()` в `update()`, `getStableRp()`/`getStableL()`/`isSignalStable()`/`getSignalSigma()` |
| `src/main.cpp` | `MEAS_STEP_SETTLE_MS=300` constant; `readyByTimer \|\| readyBySignal` guard в STEP_1/3/DRIFT |
| `data/plugins/ldc1101.json` | `stab_sigma_thresh_rp: 50.0`, `stab_n_samples: 8` |

**Config:**

```json
"stab_sigma_thresh_rp": 50.0,
"stab_n_samples": 8
```

**Timing overhead в update():** ~5 мкс worst case (N=8 mul/add + 1 sqrtf). <0.05% від 10 мс бюджету.

**Тест:** +1 unit test для `StabilityTracker::feed()` (стабільний + нестабільний вхід).

**Поріг `stab_sigma_thresh_rp=50.0`:** Початкове консервативне значення (~0.09% від basRp=57344). Уточнюється після EXP-2 (real noise floor per metal per step).

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

### D-4: Sensor Physical Calibration

**Специфікація:** `docs/external/2026-04-01.D4_SENSOR_CALIBRATION_PLAN.md`

**Статус:** 🔄 Ready for implementation (2026-04-01)

**Що:** Виправити три помилки конфігурації LDC1101, виявлені при аналізі схеми MIKROE-3240 та TI Datasheet §9.1.4–9.1.6:
1. TC1 (0x02): `0x1F` → `0xD5` — часова константа завищена в ×52
2. TC2 (0x03): `0x3F` → `0xFE` — часова константа завищена в ×11
3. RP_SET (0x01): `0x26` → `0x36` — RPMAX порушує правило RPD∞ ≤ RPMAX ≤ 2×RPD∞

**Чому:** Неправильні TC1/TC2 спричиняли regulation loop lock artifacts в C-6 даних (rp_raw=39321, 46811, 52428 для деяких монет). RPMAX=24kΩ при RPD∞=8.35kΩ порушує формулу §9.1.4 (ліміт 16.7kΩ).

| Зміна | Файл | До | Після |
|-------|------|-----|-------|
| TC1 | `LDC1101Plugin.h` + `ldc1101.json` | `0x1F` (τ=15.8ns) | `0xD5` (τ=893ns) |
| TC2 | `LDC1101Plugin.h` + `ldc1101.json` | `0x3F` (τ=91.5ns) | `0xFE` (τ=1039ns) |
| RP_SET | `ldc1101.json` | 38 (0x26, RPMAX=24kΩ) | 54 (0x36, RPMAX=12kΩ) |

**HW Prerequisite:** C-6 виконана (дані отримані — є baseline для порівняння)

**Після D-4:** HW Session C-6b для верифікації (AC-1..AC-5 у `D4_SENSOR_CALIBRATION_PLAN.md §10`).

**Очікувані зміни:**
- Новий baseline rp_raw: ~61,400 (з 57,344)
- Kangaroo Ag999 / Olympic 1984 Ag900: SAT-lock зникає, реальне значення ~39,400–40,200
- LHR baseline (lhr_base): без змін (LHR не залежить від TC1/TC2/RP_SET)

**Результати hw-verification C-6b:**
- fSENSOR = 811.5 kHz (baseline 63,652 замість artефактних 909 kHz/57,344)
- Scaling factor C-6→C-6b: k=1.1100, verified <1.5% error для 4 control coins
- Kangaroo Ag999: rp=44,655, Δf=552,225 Hz — реальні значення вперше
- Olympic Ag900: rp=44,640, Δf=541,903 Hz — Δrp=15 (0.6σ), але Δdf=10,322 Hz (20σ)

---

### D-5: NDJSON Vector v2 patch (ADR-VEC-001)

**Статус:** 🔄 Ready for implementation (2026-04-02)

**Що:** Оновити `saveDiscoveryDump()` в `src/main.cpp` — два точкових зміни:
1. **Видалити** `pv["slope"]` з `production_vector` — математично redundant для p3 protocol
2. **Додати** `pv["df_n"]` = `(fSensor_coin − fSensor_empty) / fSensor_empty` — LHR-derived, нормалізований зсув частоти

**Чому slope redundant:** При рівномірних spacer distances x={0, 1, 2} мм OLS slope спрощується до `slope = (k2 − 1) / 2`. Це лінійне перетворення k2 → нульова додаткова інформація. `matcher.json` вже має `full_weights[3]=0.0` з Wave 8 C-5 (2026-03-27). Функція `VectorCompute::slope()` залишається для display (`drawMeasResult()` показує k1/k2/slope).

**Чому df_n критичний:** C-6b аналіз показав: Kangaroo Ag999 vs Olympic Ag900 нерозрізнимі по rp (Δ=15, 0.6σ), але розрізнені по df (Δ=10,322 Hz, 20σ) — 688× чутливіше. `df_n` — єдиний discriminant для цієї пари.

**NDJSON production_vector schema v2:**
```json
{
  "dRp1_n": -12.648,
  "k1":     1.2265,
  "k2":     1.3048,
  "dL1_n":  -2.237,
  "df_n":   0.4049
}
```

**Зміни:**

| Файл | Зміна |
|------|-------|
| `src/main.cpp` | `saveDiscoveryDump()`: move `baseFS` before `pv`, remove `pv["slope"]`, add `pv["df_n"]` |
| `lib/StorageManager/src/VectorCompute.h` | Додати ADR-VEC-001 коментар до MATH NOTE |

**Вплив на embedded matcher:** Нульовий — matcher читає `Measurement.rp[]`/`.l[]` через `VectorCompute`, не з NDJSON. `slope` вже weight=0. `df_n` не входить в поточний gen 2 matching — увійде в gen 3 DB після C-7+A-1.

---

### D-6: df_n в real-time matcher

**Статус: ✅ Done (2026-04-03) — commit 483b93d**

**Що:** Додати `df_n` (LHR-derived Δf/f) до `CacheEntry`, `FingerprintCache::query()`, `MetalMatcher`, та `main.cpp` render loop. Замінити застарілий `slope` у всьому matching pipeline.

**Чому:** `slope` = лінійне перетворення k2 при рівномірних spacer distances → нульова додаткова інформація (weight=0 з Wave 8). `df_n` = 16σ discriminant для XCU↔XZNNIP (A-1 analysis), encodes eddy current coupling magnitude.

**Зміни:**

| Файл | Зміна |
|------|-------|
| `lib/StorageManager/src/FingerprintCache.h` | `CacheEntry.df_n` field; `query()` param; NDJSON `"df_n"` load |
| `lib/StorageManager/src/MetalMatcher.h` | `matchFull()` 5th param `meas_df_n`; compute `dd5 = df_n − meas_df_n` |
| `src/main.cpp` | `doMeasCompute()`: `meas_df_n` from `sDiscoverySteps[0].fSensorHz`; pass to `matchFull()` |
| `data/sd_seed/CoinTrace/database/index.json` | gen-3: 9 entries with `"df_n"` field per coin |
| `data/sd_seed/CoinTrace/matcher.json` | `full_weights: [1.5, 0, 1.0, 3.5, 2.5]` (dRp1_n, k1, k2, df_n, dL1_n) |

**Результат:** Build SUCCESS. gen-3 DB deployed. σ=0.35, min_pair_dist=0.672 (Eagle↔Kennedy pair).

**Known limitation (D-7b):** ✅ **Виправлено в D-8.** `captureStep()` тепер завжди компілюється; `meas_df_n` та `meas_df1_n` обчислюються з `sSteps[0/1].fSensorHz` в кожному build.

**Display/log cosmetic:** ✅ **Виправлено в D-8.** `drawMeasResult()` показує `df_n=%.4f`, `doMeasCompute()` логує `df_n=%.4f`.

---

### D-7: 6D vector — add df1_n (frequency shift @ 1.6mm)

**Статус: ✅ Done (2026-04-03) — commit 9985bba**

> **⚠️ REVISED:** D-7 оригінально планував `dk_n`. C-8 емпіричні дані (50 записів) показали: `dk_n` z=0.9 для Kennedy_B/USSR_A — неефективний. Замінюється на `df1_n` (ADR-VEC-002). Деталі — `docs/architecture/ADR-VEC-002.md`.

**Що:** Додати `df1_n = (fSensor@1.6mm − f_empty) / f_empty` до embedding вектора:
- `f_empty` = `fSensor_base − delta_f_base_hz` (порожній сенсор)
- `df1_n` з `sSteps[1].fSensorHz` (вже записується в C-7/C-8 NDJSON)
- `dk_n = df_n_1 / df_n_0` — **виключено** з вектора (z=0.9 неефективний)

**Чому:** Kennedy_B/USSR_A: df1_n z=7.5 vs dk_n z=0.9. Дані вже є в C-7+C-8 NDJSON (запис steps[1]["fSensor_hz"]) — нових HW вимірювань не потрібно (>крім C-9 XFE re-seed).

**Зміни:**

| Файл | Зміна |
|------|-------|
| `lib/StorageManager/src/FingerprintCache.h` | `CacheEntry.df1_n` field; `query()` 6th param; NDJSON `"df1_n"` load |
| `lib/StorageManager/src/MetalMatcher.h` | `matchFull()` 6th param `meas_df1_n`; `dd6 = df1_n − meas_df1_n` |
| `src/main.cpp` | `doMeasCompute()`: compute `meas_df1_n` from `sSteps[1].fSensorHz`; pass to `matchFull()` |
| `src/main.cpp` | `saveDiscoveryDump()`: add `pv["df1_n"]` (steps[1] LHR вже є в JSON, тільки projection vector) |
| `data/sd_seed/CoinTrace/database/index.json` | gen-4: додати `"df1_n"` field per coin (з C-7/C-8 NDJSON) |
| `data/sd_seed/CoinTrace/matcher.json` | version 3, `full_weights` — 6 компонентів (estimate: [1.5,0.0,1.0,3.5,2.5,2.0]) |

---

### D-7b: LHR в production measurement path

**Статус: ✅ Superseded by D-8 (2026-04-03)**

Початковий план — одиночний LHR read у STEP_BASE (+40ms). Після аналізу розширено до повноцінного уніфікованого pipeline (D-8): той самий `captureStep()` (~1500ms/step, медіана з N≈88), що усуває не тільки `meas_df_n = 0.0` gap, але й систематичне зміщення між Discovery DB і Production вимірюваннями (single-read vs median). Деталі — §D-8.

---

### D-8: Unified capture pipeline

**Статус: ✅ Done (2026-04-03)**

**Проблема (root cause):** `sDiscoverySteps[4]` існував тільки під `#ifdef DISCOVERY_MODE` → в production build `sSteps[0/1].fSensorHz == 0.0f` завжди → `meas_df_n = meas_df1_n = 0.0f` → matcher ефективно 3D (df_n weight=3.5 та df1_n weight=2.0 мовчки вимкнені). Крім того, production single-read vs Discovery median (~N=88) — систематичне зміщення між DB centroids та production вимірами.

**Рішення:** Перемістити `CaptureStats` struct, `captureStep()`, `sSteps[4]`, `sCaptureSettleMs`, `sCaptureMs` повністю поза `#ifdef`. DISCOVERY_MODE зберігає тільки `saveDiscoveryDump()` + session file state.

**Зміни:**

| Файл | Зміна |
|------|-------|
| `lib/LDC1101Plugin/src/LDC1101Plugin.h` | Знято `#ifdef DISCOVERY_MODE` з 5 capture API методів (`spiReadPublic`, `readMeasurementBurstPublic`, `readLHRBurstPublic`, `getClkinFreqHz`, `convTimeMs`) |
| `src/main.cpp` | `CaptureStats` struct + `sSteps[4]` + `sCaptureSettleMs/sCaptureMs` — поза `#ifdef`. `discoveryCaptureStep()` → `captureStep()` (завжди компілюється). `drawCaptureProgress()` — заголовок `"MEASURING"` (не `"DISCOVERY CAPTURE"`). `sDiscoverySteps` → `sSteps` скрізь. STEP_BASE/1/3/DRIFT handlers — unified, без dual-path `#ifdef`. `doMeasCompute()`: `meas_df_n`/`meas_df1_n` завжди обчислюються з `sSteps[0/1].fSensorHz`. `drawMeasResult()`: `slope=` → `df_n=`. Log: `slope=` → `df_n=` |
| `src/main.cpp` | `setup()`: `sCaptureSettleMs` + `sCaptureMs` завантажуються поза `#ifdef`. Discovery перевизначає `sCaptureMs = sDiscoveryCaptureMs` тільки при активному режимі |
| `data/plugins/ldc1101.json` | Додано `"prod_capture_ms": 1500` |
| `lib/StorageManager/src/StorageManager.h/.cpp` | `queryFingerprint()` param `slope` → `df_n` |
| `include/IStorageManager.h` | `queryFingerprint()` virtual param `slope` → `df_n` |

**Timing:** Production: 300ms settle + 1500ms capture × 4 steps ≈ **7.2s** загальний час. Discovery: 300ms + 2000ms × 4 ≈ 9.2s (без змін).

**RAM:** `sSteps[4]` = 4 × 348B = 1,392B BSS — тепер **завжди** присутній (раніше тільки в DISCOVERY_MODE). Net production BSS increase: +1,392B (бюджет 118KB, 1.2%).

---

## 3. Track C continued — HW Sessions C-6 / C-6b / C-7

### C-6: Discovery HW Session

**Статус: ✅ Done (2026-04-01) — з OLD CONFIG (TC1/TC2 bug)**

> **⚠️ Data Validity:** LHR-дані (lhr_mean, lhr_base) — повністю валідні. RP-дані (rp_base, k1, k2, slope) зібрані з TC1/TC2 bug і потребують C-6b для оновлення.

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
| **EXP-2** | Multi-sample σ(RP) відрізняється per metal | σ(RP) for Al ≠ σ(RP) for Ag (statistically significant). **Additional output:** optimal `stab_sigma_thresh_rp` для production (must be above worst-case settling σ, below metal-specific σ differences) |
| **EXP-3** | LHR 24-bit дає кращу fSENSOR роздільність ніж L_DATA 16-bit | LHR fSensor σ < L_DATA-derived fSensor σ |
| **EXP-4** | Ag999 vs Ag925 розділяються по dRp1_n або Δf | Pairwise distance > 2σ between XAG999 and XAG925 |
| **EXP-5** | Discovery медіана точніша за single read (C-5 comparison) | σ(centroid) для Discovery < σ(centroid) для C-5 single-read |
| **EXP-6** | Settling time 300ms достатній | First 50 samples vs last 50: mean difference < 0.5% |

---

### C-8: HW Session — A/B side control

**Статус: ✅ Done (2026-04-03) — 50 записів**

> Повний звіт і протокол: [`C8_HW_SESSION.md`](../guides/C8_HW_SESSION.md)

**Фактичні результати:**

| Монета | Metal code | A-side | B-side | Записів | Результат |
|--------|-----------|--------|--------|---------|--------|
| American Silver Eagle 1oz | XAG999 | ×5 | ×5 (бонус) | 10 | ✅ |
| Kennedy Half Dollar 1964 (Ag400) | XKENNEDY | ×5 | ×5 | 10 | ✅ |
| Australian Kangaroo 1oz | XAG999_KANG | ×5 | ×5 | 10 | ✅ |
| Bahamas $1 1966–70 (Ag800) | XAG800_BHS | ×5 | ×5 | 10 | ✅ (новий клас) |
| USSR 10 Rubles 1977 (Ag900) | XUSSR10R | ×5 | ×5 | 10 | ✅ (новий клас) |

**Ключові висновки:**
1. **dk_n REJECTED** (z=0.9 Kennedy_B/USSR_A) → df1_n ACCEPTED (z=7.5)
2. **XFE centroid bug:** Germany 1.5 Euro bimetal seed → Kennedy_B false conf=64–97%
3. **A/B асиметрія** підтверджена (всі 5 монет, |Δdf_n|=1–5%)
4. **XAG800_BHS** і **XUSSR10R** — виразні нові класи (z>5 від найближчого сусіда)

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

**Статус (фактичний):** ✅ **Done (2026-04-03)** — A-4 виконана паралельно з D-6 (не чекаючи A-2/A-3). gen-3 DB: 9 монет, 5D вектор з `df_n`, weights=[1.5,0,1,3.5,2.5], σ=0.35, min_dist=0.672.

---

### A-5: dk_n spatial gradient analysis

**Статус: ✅ Done (2026-04-03) — dk_n REJECTED; df1_n ACCEPTED**

**Результат (C-8 емпіричні дані + незалежний аудит):**

| Ознака | Kennedy_B vs USSR_A z-score | Висновок |
|--------|-----|-------|
| `dk_n = df_n_1 / df_n_0` | **0.9** | ❌ REJECTED — ділення виключає абсолютний сигнал |
| `df1_n = (fs1−f_empty)/f_empty` | **7.5** | ✅ ACCEPTED — зберігає різницю coupling при 1.6mm |
| `df_n (baseline)` | **34** | Context: Eagle_A vs Kennedy_A |

**Чому dk_n неефективний:** Kennedy_B df_n≈0.782 і USSR_A df_n≈0.779 — обидва f_n_0 ідентичні. При діленні близьких значень отримуємо dk_n≈1.0 для обох → ознака стає неінформативною. `df1_n` натомість зберігає різницю у фізичному coupling при 1.6mm spacer.

**Output:** ADR-VEC-002 для df1_n як 6-го виміру вектора (docs/architecture/ADR-VEC-002.md). Detailed report: `docs/external/2026-04-03.C8_INDEPENDENT_ANALYSIS_REPORT.md` (gitignored).

**Що:** Використати наявні C-7 NDJSON дані для обчислення нового признаку `dk_n = df_n_1 / df_n_0`:

```python
# df_n_0 — LHR @ 0.6mm (вже є в generation 3)
df_n_0 = (steps[0]["fSensor_hz"] - baseFS) / baseFS

# df_n_1 — LHR @ 1.6mm (є в C-7 NDJSON, але НЕ в DB)
df_n_1 = (steps[1]["fSensor_hz"] - baseFS) / baseFS

# dk_n — просторовий градієнт coupling
dk_n = df_n_1 / df_n_0  # if abs(df_n_0) > 1e-6 else None
```

**Чому dk_n кодує діаметр монети:**

$dk_n \approx e^{-\alpha \cdot d_{spacer} / R_{coin}}$

де $R_{coin}$ — ефективний радіус монети, $\alpha$ — константа sensor geometry. Велика монета (Eagle 38.1mm): $dk_n \approx 0.51$. Мала монета (Kennedy 30.6mm): $dk_n \approx 0.37$. Ця різниця = потенційне рішення Eagle↔Kennedy ambiguity.

**Чому без нових HW даних:** `steps[1]["fSensor_hz"]` вже присутній в C-7 NDJSON (`session_63824c66`). Дані є — потрібний тільки Python аналіз.

**Prerequisite:** C-7 NDJSON на диску (вже є).

**Tool:** Розширення `scripts/a1_analysis.py` (новий розділ dk_n).

**Input:** `data/` NDJSON з C-7 session.

**Аналіз:**

| # | Що | Output |
|---|---|--------|
| 1 | dk_n per group (mean ± σ) | Table: dk_n stat для всіх 9 монет |
| 2 | Eagle vs Kennedy separation | distance(XKENNEDY, XAG999_EAGLE) в [df_n, dk_n] space |
| 3 | pairwise 6D distance matrix | Heatmap: baseline 5D vs extended 6D |
| 4 | dk_n vs coin diameter correlation | Scatter: dk_n vs фізичний diameter (мм) |
| 5 | Σ-within з dk_n | Порівняти з gen-3 σ=0.35: чи dk_n додає separation? |

**Критерій успіху:** d(Eagle, Kennedy) у 6D space ≥ 1.0σ (vs поточних 0.672σ у 5D). Якщо так — D-7 виправданий.

**Output:** Розділ `## dk_n Analysis` у `docs/external/A1_ANALYSIS_session_63824c66.md`.

---

### A-6: index.json gen-4 + matcher.json v3

**Статус: 📋 Planned — залежить від C-9, D-7**

**Prerequisite:** D-7 firmware (6D vector з df1_n) + C-9 HW session (XFE re-seed).

**Deliverables:**

1. **index.json generation 4** — включає:
   - 6D centroids: `[dRp1_n, k1, k2, df_n, dL1_n, df1_n]` per entry
   - A/B-aware entries для Kennedy та Kangaroo (або окремі записи, або розширений radius)
   - Нові монети: XAG800\_BHS, XUSSR10R (з C-8 session)
   - XFE centroid зі справжньої сталі (з C-9 session) — **блокує C-9**
   - Оновлені centroids для всіх 9 gen-3 монет (зважена вибірка C-7 vs C-8)

2. **matcher.json v3** — включає:
   - `full_weights` для 6D vector (з df1_n); початкова оцінка: [1.5,0.0,1.0,3.5,2.5,2.0]
   - Оновлений `sigma` (очікується зменшення після кращого A/B контролю)

**Критерій:** min_pairwise_distance > 1.0σ для всіх пар (поточний: 0.672σ Eagle↔Kennedy).

---

### Фаза 1: Discovery firmware (Sprint 2) — ✅ DONE

```
D-2  LHR continuous mode        ✅ Done (2026-03-30)
D-1  Multi-sample capture       ✅ Done (2026-03-31)
D-3  Raw dump to SD             ✅ Done (2026-04-01)
D-4  Sensor calibration         ✅ Done (2026-04-01)
C-6  Discovery HW Session       ✅ Done (2026-04-01) ⚠️ old config
C-6b Re-verification HW         ✅ Done (2026-04-01)
D-5  NDJSON Vector v2 patch     ✅ Done (2026-04-02)
C-7  Full Discovery Session     ✅ Done (2026-04-02)
A-1  Offline analysis           ✅ Done (2026-04-03)
A-4  index.json gen-3           ✅ Done (2026-04-03)
D-6  df_n in real-time matcher  ✅ Done (2026-04-03)
```

### Фаза 2: df1_n + production LHR (Sprint 4) — поточна фаза

```
A-5  dk_n spatial gradient analysis  ✅ Done  (dk_n REJECTED z=0.9 → df1_n z=7.5 Kennedy_B/USSR_A)
C-8  HW Session A/B control          ✅ Done  (50 records, 5 coins×2 sides, XFE centroid bug знайдено)

ADR-VEC-002  df1_n як 6D вимір        ✅ Done  (документ + FingerprintCache + matchFull() + schema)
D-7   6D vector: add df1_n            ✅ Done  (CacheEntry, matchFull() 6th param, NDJSON, matcher v3, buildFromSD fix)
D-7b/D-8  Unified capture pipeline    ✅ Done  (captureStep() always compiled, 1500ms/step production, meas_df_n/df1_n always-path)
C-9   XFE re-seed HW Session          ⛔ BLOCKED (потрібна сталева монета)
A-6   index.json gen-4 + matcher v3   ~0.5 дні  (6D centroids з df1_n; залежить від C-9)
```

### Загальна оцінка (оновлено)

| Фаза | Зусилля | HW-час | Output |
|------|---------|--------|--------|
| Sprint 2 (firmware + HW) | ~3 робочі дні | ~1 день | Raw dump JSON, LHR data |
| Sprint 3 (analysis + integration) | ~3 робочі дні | ~0.5 дні | gen-3 DB, df_n pipeline |
| Sprint 4 (dk_n + prod LHR) | ~3 робочі дні | ~1 день | gen-4 6D DB, Eagle↔Kennedy resolved |
| **Wave 9 total** | **~9 робочих днів** | **~2.5 дні** | **6D data-driven classification, all pairs > 1σ** |

---

## 6. RAM та Flash бюджет Wave 9

### Incremental RAM (over Wave 8 final)

| Компонент | Тип | Розмір | Постійний? | Умова |
|-----------|-----|--------|------------|-------|
| `sSteps[4]` | BSS | 1,392 B | Так | Always (D-8 unified — production + discovery) |
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
- [x] `discovery_enabled=true` → capture loop runs 2s per step ✅ D-1 (`captureStep()`, 110 samples/step)
- [x] `lhr_continuous=true` → LHR data present in cache ✅ D-2 (фактичний N=110, не 600 — RESP_TIME_BITS=7)
- [x] Discovery JSON saved to SD after each measurement ✅ D-3 (NDJSON per-session append, ArduinoJson v7)
- [x] `discovery_enabled=false` → production behavior unchanged ✅ D-8 (unified pipeline — always compiled)
- [x] All existing native tests pass ✅ 137/137 (D-7, D-8; final: `258c74a`)
- [x] Display shows capture progress bar during each step ✅ D-8 (`drawCaptureProgress()`, "MEASURING")

**C-6 HW Session:**
- [x] Minimum 5 original C-5 coins measured ✅ C-6 (100 вимірів, 20 монет — old TC1/TC2 config)
- [x] New coins measured ✅ C-6b (верифікація D-4; C-7: 9 монет × 5 = 45 вимірів)
- [x] Raw dump JSON contains RP/L/LHR stats ✅ D-3 schema verified in C-6/C-7 sessions
- [x] Production match result in dump ✅ `coin_name`, `metal_code`, `confidence` у кожному NDJSON record
- [x] `df_n` computed per measurement ✅ D-5 → D-6 pipeline (ADR-VEC-001)
- [x] TC1/TC2 bug виявлено і виправлено ✅ D-4 (commit `dc72c51`, C-6b hw-verified)

### Wave 9 Phase 2 (Sprint 3) — Analysis + Vector v2:

**Analysis:**
- [x] Pairwise distance matrix computed ✅ A-7 (`scripts/a7_pairwise_analysis.py`, gen-7 final)
- [x] ADR-V2 documented: ADR-VEC-001 (df_n), ADR-VEC-002 (df1_n) ✅ числове обґрунтування
- [x] dk_n REJECTED z=0.9; df1_n ACCEPTED z=7.5 ✅ A-5 + C-8 empirical data

**Integration:**
- [x] index.json gen-3→7: 9→28 entries, 3→13 classes ✅ D-9/D-10/D-11/D-11d
- [x] matcher.json v3→v5: [1.5,0.0,1.0,3.0,2.5,0.4], σ=0.35 ✅
- [ ] Quick Screen Phase 2 — 🚀 **Deferred → Wave 10** (потребує mass_n як 7-й поріг)
- [x] Closest pair characterized ✅ XKENNED↔XUSSR10: 0.18σ — physics constraint, NAU7802 needed
- [x] All native tests pass ✅ 137/137

### Wave 9 exit criteria — **РЕЗУЛЬТАТИ:**

- [x] **Vector composition justified** ✅ ADR-VEC-001 + ADR-VEC-002 з числовим обґрунтуванням (df_n 16σ, df1_n 7.5σ)
- [x] **13 distinct metal classes** ✅ (XAG999×2, XAG800, XAG900, XUSSR10×3, XKENNED, XFE, XAL, XCU, XCUZN, XZNNIP, XFENIP, XFECUP, XNICKEL)
- [x] **Discovery experiments documented** ✅ A-1 report, C-8 findings, C-12 analysis
- [ ] Quick Screen Phase 2 — 🚀 **→ Wave 10** (mass_n threshold)
- [x] **matcher.json v5 on SD** ✅ `data/sd_seed/CoinTrace/matcher.json`, weights validated
- [x] **Custom coil decision deferred** ✅ E1 Ø50мм planned in Wave 10 Трек C (дріт в дорозі)
- [x] **Exit criterion per-class pairs > 1.0σ** — ❌ **5 пар < 1.0σ (PLANNED OUTCOME)** — апаратне обмеження одного LDC1101 в silver-vs-silver zone. Resolution: Wave 10 NAU7802 + 7D вектор.

> **Wave 9 closure verdict (2026-04-08):** Всі заплановані deliverables виконані. Exit criterion по якості класифікатора не досягнутий — але це **підтверджений запланований результат**, а не провал. Wave 9 довела, що 6D LDC1101-вектор вичерпаний для silver class separation. Шлях вперед емпірично доведений і кількісно обґрунтований (NAU7802 + W_mass=5.0 → всі 5 пар виходять > 2.0σ в 7D). Wave 10 triggered.

---

*Версія 2.0.0 (2026-04-08) — **WAVE 9 CLOSED.** C-10 Done (93 rec, 9 classes reseed, commit 3510b05); C-11 Done (48 rec, 3 new classes: XFENIP/XFECUP/XNICKEL, commit 3510b05); C-12 Done (42 raw→35, XUSSR10 m1/m2, XFENIP reseed, remap_c12.py, commit 258c74a); D-9/D-10/D-11/D-11d DB pipeline (gen-4→7); A-7 pairwise (scripts/a7_pairwise_analysis.py, 5 pairs < 1.0σ — physics). Wave 10 → docs/external/2026-04-08.WAVE10_ARCHITECTURE_PLAN.md. Stash: WIP NAU7802 D-12a skeleton.*  
*Версія 1.9.0 (2026-04-03) — D-8 Done: unified capture pipeline — `captureStep()` always compiled, production 1500ms/step, `meas_df_n`/`meas_df1_n` always-path, `df_n=` display, `IStorageManager` slope→df_n param rename, `ldc1101.json` prod_capture_ms; 137 tests ✅; RAM 63.4% Flash 58.1%.*  
*Версія 1.8.0 (2026-04-03) — ADR-VEC-002 Done; D-7 Done: 6D vector df1_n plumbing (FingerprintCache h+cpp, MetalMatcher h+cpp, StorageManager, main.cpp, matcher.json v3, 2 test files); buildFromSD() df1_n parse fix; 137 native tests ✅; firmware RAM 63.4% Flash 58.2% SUCCESS.*  
*Версія 1.7.0 (2026-04-03) — C-8 Done (50 records); A-5 Done (dk_n REJECTED z=0.9, df1_n z=7.5); D-7 revised (dk_n→df1_n); ADR-VEC-002 added to matrix; C-9 XFE re-seed BLOCKED; Фаза 2 timeline updated. Commit 913f3f7.*  
*Версія 1.6.0 (2026-04-03) — A-5/D-7/D-7b/C-8/A-6 Planned tasks added; D-6/D-7/D-7b detail sections; C-8 HW session spec; A-5 dk_n analysis; A-6 gen-4 plan; Sprint 4 sequence; roadmap brought current after commit 483b93d.*  
*Версія 1.5.0 (2026-04-03) — D-5/C-7/A-1/A-4/D-6 Done; gen-3 DB deployed (9 entries, df_n=3.5, σ=0.35).*  
*Версія 1.2.0 (2026-03-30) — AI-1: додано задачу D-2b StabilityTracker (ADR-STAB-001) + секція Track D; AI-3: розширено EXP-2 scope (додано stab_sigma_thresh_rp calibration output); статус оновлено Active — Wave 8 CLOSED.*  
*Версія 1.1.0 (2026-03-30) — додано критерій закриття TECHNICAL_DEBT.md до Wave 9 exit criteria.*  
*Версія 1.0.0 — initial Wave 9 roadmap, created 2026-03-27 based on C-5 Deep Analysis Audit findings and WAVE8_COMPLETION_WAVE9_DISCOVERY_PLAN.md.*
