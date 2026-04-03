# ADR-VEC-002: df1_n як 6-й компонент production_vector (замість dk_n)

**ID:** ADR-VEC-002  
**Дата:** 2026-04-03  
**Статус:** 📋 Proposed — очікує реалізації (D-7 revised, D-7b)  
**Тригер:** C-8 HW Session (2026-04-03) + незалежний аудит  
**Передує:** ADR-VEC-001 (2026-04-02) — додав `df_n` до production_vector  
**Cross-ref:** `WAVE9_ROADMAP.md §D-7 (revised)`, `docs/guides/C8_HW_SESSION.md`,
`METAL_MATCHER_ARCHITECTURE.md §11`, `DISCOVERY_MODE_SPEC.md`

---

## Зміст

1. [Контекст](#1-контекст)
2. [Рішення](#2-рішення)
3. [Альтернативи відхилені](#3-альтернативи-відхилені)
4. [Наслідки](#4-наслідки)
5. [Відкрита залежність — XFE centroid](#5-відкрита-залежність--xfe-centroid)
6. [Приклад обчислення](#6-приклад-обчислення)

---

## 1. Контекст

### Поточний стан (gen-3, 5D)

gen-3 DB розгорнутий з 5D вектором `[dRp1_n, k1, k2, df_n, dL1_n]`:
- `df_n = (fSensor@0.6mm − f_empty) / f_empty` — LHR-derived, вже в production (D-6)
- `full_weights = [1.5, 0.0, 1.0, 3.5, 2.5]`
- Мінімальна pairwise дистанція = **0.672σ** між Eagle (`XAG999`) та Kennedy (`XKENNEDY`)

### Проблема: Eagle↔Kennedy overlap та XAG900 confusion

C-7 аналіз (A-1) встановив ефективну розмірність ≈ 2 (dRp1_n + df_n домінують). Kennedy_B та USSR_A потрапляють у критично близьку зону, ускладнену XFE centroid bug.

**План D-7 (оригінальний):** додати `dk_n = df_n_1 / df_n_0` — просторовий градієнт coupling.

### Результат C-8 (50 записів, 2026-04-03)

| Ознака | Kennedy_B vs USSR_A z-score | Коментар |
|--------|-----|---------|
| `dk_n = df_n_1 / df_n_0` | **0.9** | ❌ Гірший тест пари — не вирішує проблему |
| `df1_n = (fs1 − f_empty) / f_empty` | **7.5** | ✅ Розрізняє ту саму пару з великим запасом |
| `df_n` (baseline @ 0.6mm) | 34 | Context: Eagle_A vs Kennedy_A |

**Чому dk_n провалився:**

Kennedy_B: `df_n ≈ 0.782`, USSR_A: `df_n ≈ 0.779` — обидва `df_n_0` майже ідентичні.
При нормалізації `dk_n = df_n_1 / df_n_0` на подібні значення → `dk_n ≈ 1.0` для обох монет.
Ознака дублює вже наявну інформацію і не додає розпізнавальної сили.

**Чому df1_n ефективний:**

`df1_n` кодує абсолютний LHR coupling при 1.6mm spacer. При 1.6mm монета ближча:
- Срібло Ag900 з більшою масою (USSR 10 Rbl: 33.3g) → сильніший coupling → більший `df1_n`
- Срібло Ag400 Kennedy (11.5g, менша маса) → слабший coupling при 1.6mm → менший `df1_n`

Корелят цього — **маса монети × питома провідність**, а не тільки діаметр.

---

## 2. Рішення

**Замінити пропозицію D-7 (`dk_n`) на `df1_n` як 6-й компонент вектора.**

### Новий вектор v3 (6D)

```
[dRp1_n, k1, k2, df_n, dL1_n, df1_n]
```

де:
- `df_n`  = `(fs0 − f_empty) / f_empty` — частотний зсув @ 0.6mm (вже в production)
- `df1_n` = `(fs1 − f_empty) / f_empty` — частотний зсув @ 1.6mm (**НОВЕ**)
- `f_empty` = `fSensor_base − delta_f_base_hz` (конфіг, незмінний)
- `fs0` = `sDiscoverySteps[0].fSensorHz`
- `fs1` = `sDiscoverySteps[1].fSensorHz`

### Порядок у firmware та matcher.json

```json
"full_weights": [1.5, 0.0, 1.0, 3.5, 2.5, 2.0]
```

Індекси: `[dRp1_n=0, k1=1, k2=2, df_n=3, dL1_n=4, df1_n=5]`

> **Примітка:** `k2` (індекс 2) і `dL1_n` (індекс 4) зберігають позиції для сумісності з gen-3.
> `df1_n` додається на позицію 5 (новий останній елемент).

### Початкова оцінка ваги df1_n

Weight=2.0 (оцінка). Обґрунтування — z=7.5 для Kennedy_B/USSR_A (порівнянно з df_n z=34 для Eagle/Kennedy). Точна вага визначиться після A-6 optimization.

---

## 3. Альтернативи відхилені

| Альтернатива | Причина відхилення |
|-------------|-------------------|
| **dk_n** (D-7 оригінальний план) | C-8 empirical: z=0.9 для Kennedy_B/USSR_A (найгірший тест). Ділення `df_n_1/df_n_0` скасовує абсолютну величину сигналу. Теоретична мотивація (кодує діаметр) не підтвердилася |
| **Залишитися 5D** | min_dist=0.672σ → Kennedy_B/USSR_A вимагає 6D; XFE false positive також потребує рішення |
| **sigma_rp як 6-й** | σ(RP) доступна тільки в `DISCOVERY_MODE`. В production build потребує multi-sample capture → неприйнятне timing overhead |
| **df2_n (@ 2.6mm)** | `lhr_n=0` на Step 3 (відомий limitation: LHR conversion не завершується при 2.6mm spacer). Дані недоступні |

---

## 4. Наслідки

### 4.1 Firmware changes

| Файл | Зміна |
|------|-------|
| `lib/StorageManager/src/FingerprintCache.h` | `CacheEntry.df1_n` field; `query()` + 6th param `meas_df1_n`; NDJSON `"df1_n"` load |
| `lib/StorageManager/src/MetalMatcher.h` | `matchFull()` + 6th param `meas_df1_n`; compute `dd6 = (centroid.df1_n − meas_df1_n) * weights[5]` |
| `src/main.cpp` | `doMeasCompute()`: обчислити `meas_df1_n` з `sDiscoverySteps[1].fSensorHz - baseFS) / baseFS`; передати до `matchFull()` |
| `src/main.cpp` | `saveDiscoveryDump()`: додати `pv["df1_n"]` до production_vector у NDJSON |
| `data/sd_seed/CoinTrace/matcher.json` | `version: 3`; `full_weights` — 6 елементів; оновити `notes` |

> **D-7b залежність:** `meas_df1_n` обчислюється з `sDiscoverySteps[1]` — доступно тільки в `DISCOVERY_MODE`. D-7b має додати LHR read в production STEP_BASE handler для `meas_df_n`, і окремий read в STEP_ADDON_1 для `meas_df1_n`. Деталі специфікуються в D-7b.

### 4.2 Offline pipeline changes

| Файл | Зміна |
|------|-------|
| `scripts/a1_analysis.py` | Додати `df1_n` обчислення з `steps[1]["fSensor_hz"]`; оновити `FEATURES` list; оновити centroid output |
| `data/sd_seed/CoinTrace/database/index.json` | gen-4: додати `"df1_n"` field per entry; перерахувати centroids з C-7+C-8 NDJSON |
| `data/sd_seed/CoinTrace/matcher.json` | version 3, full_weights [6 елементів] |

### 4.3 NDJSON schema v3 (production_vector)

Нова схема `production_vector`:

```json
{
  "dRp1_n": -15.311,
  "k1":     1.2265,
  "k2":     1.3048,
  "dL1_n":  -2.418,
  "df_n":   0.7836,
  "df1_n":  0.5991
}
```

Код `saveDiscoveryDump()` — додати:
```cpp
if (steps[1].fSensorHz > 0 && baseFS > 0)
    pv["df1_n"] = round((steps[1].fSensorHz - baseFS) / baseFS * 10000) / 10000.0;
```

### 4.4 Backward compatibility

- `df1_n` вже присутній у `steps[1]["fSensor_hz"]` в C-7 та C-8 NDJSON файлах
- Для gen-4 DB centroids достатньо існуючих сесійних даних — нові HW вимірювання **не потрібні** (крім XFE re-seed, який є окремим блокером)
- gen-3 DB залишається функціональним до завершення C-9 (XFE re-seed)

### 4.5 Зміна unit tests

| Тест | Дія |
|------|-----|
| `test_metal_matcher` | Додати `meas_df1_n` до всіх `matchFull()` викликів (6-й аргумент) |
| `test_fingerprint_cache` | Додати `df1_n` до CacheEntry fixtures |

---

## 5. Відкрита залежність — XFE centroid

**XFE centroid (`XFERROUS`)** засіяний з Germany 1.5 Euro bimetal (Fe+Cu):
- `dRp1_n = -15.5818`, `dL1_n = -2.4221` — потрапляє в зону срібних монет
- Kennedy_B (`dRp1_n ≈ -15.65`) та USSR_A (`dRp1_n ≈ -15.73`) показують conf=64–97%
- `df1_n` **не вирішує** XFE false positive — Kennedy_B і XFE_bimetal мають схожий df1_n

**Рішення:** C-9 HW session з реальною феромагнітною монетою. Справжня сталь дає від'ємний `df_n` (феромагнетик ↓ fSENSOR при наближенні), що кардинально відрізняється від срібних монет.

**Блокує:** A-6 gen-4 DB (XFE centroid потрібен для повноти gen-4).

---

## 6. Приклад обчислення

### df1_n для Kennedy Half Dollar A-side (C-8, mid-session, бл. idx=10–14)

```
baseFS (fSensor_empty) = steps[0].fSensorHz - delta_f_base_hz
                       = 816400 - 31500 = 784900 Hz  (приблизно)

fs1 (fSensor @ 1.6mm)  = steps[1].fSensorHz ≈ 1255000 Hz (з C-8 NDJSON)

df1_n = (fs1 - baseFS) / baseFS
      = (1255000 - 784900) / 784900
      ≈ 0.599
```

### df1_n для USSR 10 Rbl A-side (idx=40–44)

```
fs1 ≈ 1340000 Hz (більша маса + Ag900)

df1_n = (1340000 - 784900) / 784900
      ≈ 0.707
```

**Різниця: 0.707 − 0.599 = 0.108** при σ_within ≈ 0.014 → **z ≈ 7.5** ✅

---

*ADR-VEC-002 v1.0.0 — 2026-04-03. Підписано C-8 empirical evidence (50 records, session_cf1edfcd.ndjson). dk_n REJECTED (z=0.9). df1_n ACCEPTED as 6th dimension (z=7.5). Залежить від D-7 (firmware) та D-7b (production LHR). XFE re-seed BLOCKED pending C-9.*
