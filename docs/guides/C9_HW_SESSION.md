# C-9 Hardware Session — XFE Re-seed + gen-4 DB

**Версія:** 1.0.0  
**Дата:** TBD (заблоковано)  
**Статус:** ⛔ Blocked — потрібна феромагнітна монета (стара радянська копійка, East German Pfennig, або магнітний євроцент)  
**Firmware:** gen-3 DB активний; очікує ADR-VEC-002 + D-7 firmware patch (df1_n в `matchFull()`) перед сесією  
**Протокол:** `p3_MIKROE3240_b06_012mm` (незмінний з C-7/C-8)  
**Мета:** Re-seed XFE centroid зі справжньої сталі; зібрати df1_n дані для gen-4 DB  
**Output:** `session_XXXXXXXX.ndjson` → `a1_analysis.py` → `index.json gen-4` → `matcher.json v3`  
**Cross-ref:** `C8_HW_SESSION.md`, `ADR-VEC-002.md`, `WAVE9_ROADMAP.md §C-9/A-6`, `METAL_MATCHER_ARCHITECTURE.md §11`

---

## Зміст

1. [Передумови для розблокування](#1-передумови-для-розблокування)
2. [Чому XFE re-seed критично](#2-чому-xfe-re-seed-критично)
3. [Очікуваний план вимірювань](#3-очікуваний-план-вимірювань)
4. [Boot checklist](#4-boot-checklist)
5. [Протокол одного виміру](#5-протокол-одного-виміру)
6. [Ключові експерименти C-9](#6-ключові-експерименти-c-9)
7. [Після сесії — gen-4 DB](#7-після-сесії--gen-4-db)
8. [Чеклист](#8-чеклист)

---

## 1. Передумови для розблокування

### Критичний блокер: немає феромагнітної монети

XFE centroid поточного gen-3 DB засіяний з Germany 1.5 Euro bimetal (Fe+Cu):
> `coin_name: "Germany 1.5 Euro 1997 European Week Berlin"`, `dRp1_n=-15.5818, dL1_n=-2.4221`

Ця точка **перебуває в зоні срібних монет**, а не феромагнітних матеріалів, і спричиняє критичні false positive:
- Kennedy_B: conf=97% vs XFE
- USSR_A: conf=64% vs XFE

**Підходять монети (феромагнітні — тест магнітом):**

| Монета | Матеріал | Характерний μr | Примітка |
|--------|----------|----------------|---------|
| Радянська копійка (до 1991) — 1, 2, 3, 5 коп | Сталь + Cu покриття | ~200–600 | Дешева, легко доступна |
| East German Pfennig (до 1990) | Сталь + Al/Zn покриття | ~200–400 | Доступна у колекціонерів |
| Євроцент 1c/2c/5c (поточний випуск) | Сталь + Cu покриття | ~200–500 | Перевірити магнітом |
| Канадський цент після 2001 (магнітний) | Cu-plated сталь | ~300 | Підходить якщо магнітний |
| Будь-яка монета, що притягується до магніту | Феромагнетик | >50 | Перевірка: звичайний холодильниковий магніт |

> **⚠️ Перевірка:** Піднести монету до будь-якого магніту. Якщо притягується → феромагнітна → підходить для XFE re-seed.

> **❌ НЕ підходять:** Lincoln cent (Cu-плакований цинк, немагнітний), Germany 1.5 Euro (bimetal Fe+Cu з Cu оболонкою → частково магнітний, вже є як помилковий seed).

### Технічні передумови (виконати заздалегідь)

- [ ] ADR-VEC-002 реалізований у firmware (df1_n в `matchFull()`, `saveDiscoveryDump()`)
- [ ] D-7b реалізований (production build LHR reads для `meas_df_n` і `meas_df1_n`)
- [ ] `scripts/a1_analysis.py` оновлений: df1_n обчислення з `steps[1]["fSensor_hz"]`
- [ ] gen-3 DB залишається активним під час C-9
- [ ] Firmware прошитий, boot log підтверджує: `FingerprintCache ready — 9 entries (generation 3)`

---

## 2. Чому XFE re-seed критично

### Поточна ситуація (gen-3)

```
XFE centroid (Germany 1.5 Euro bimetal):
  dRp1_n = -15.5818   (потрапляє в зону Ag монет)
  dL1_n  = -2.4221
  df_n   = ??? (невідоме — bimetal не є справжнім феромагнетиком)

Kennedy_B centroid (C-8 data):
  dRp1_n = -15.648    ← майже ідентично XFE!
  dL1_n  = -2.45
```

Kennedy_B дає conf=97% vs XFE через майже ідентичний dRp1_n і dL1_n.

### Очікувана ситуація після re-seed зі справжньої сталі

Справжня феромагнітна монета (μr >> 1):
- **fSENSOR при наближенні зменшується** (феромагнетик знижує ефективну індуктивність)
- `df_n` = **від'ємне** значення (на відміну від +0.7–0.8 для срібних монет)
- `dRp1_n` також кардинально відрізняється (RP regulation реагує на μr)

Реалістична оцінка XFE_real:
- `df_n` ≈ -0.2 ... -0.5 (від'ємний, велика відстань від Ag)
- `dRp1_n` ≈ -5 ... -8 (менший модуль через різну регуляцію)

Після re-seed: d(Kennedy_B, XFE_real) >> 3σ → false positive зникає.

---

## 3. Очікуваний план вимірювань

| Монета | Metal code | Сторони | Виміри | Пріоритет | Примітка |
|--------|-----------|---------|--------|-----------|---------|
| XFE_REAL (феромагнітна монета) | XFE | A-side | 5 | 🔴 CRITICAL | Головна ціль C-9. Власний baseline перед сесією |
| Kennedy Half Dollar 1964 | XKENNEDY | A-side | 3 | 🔴 CRITICAL | Верифікація: d(Kennedy_B, XFE_new) > 1.0σ |
| USSR 10 Rubles 1977 | XUSSR10R | A-side + B-side | 3+3 | 🟡 MEDIUM | B-side з власним baseline (не shared з A-side) |
| American Eagle 1oz | XAG999 | A-side | 3 | 🟡 MEDIUM | Control reference, df1_n центроїд |
| Australian Kangaroo 1oz | XAG999_KANG | A-side | 3 | 🟢 LOW | Опціонально, control |

**Total:** ~17–20 вимірів. Коротка сесія (~15–20 хв).

> **Калібрування:** Окремий baseline перед XFE_REAL. Після XFE_REAL — новий baseline перед Kennedy (щоб не ділити baseline між феромагнетиком і сріблом).

---

## 4. Boot checklist

```
Перед C-9:
[ ] Феромагнітна монета перевірена магнітом → відповідає
[ ] Firmware прошитий з D-7 (df1_n в matchFull + saveDiscoveryDump)
[ ] boot log: "FingerprintCache ready — 9 entries (generation 3)"
[ ] boot log: fSENSOR = ~784.9 kHz (підтверджує baseline)
[ ] discovery_enabled: true, lhr_continuous: true в ldc1101.json
[ ] Новий файл сесії: перевірити що session_XXXXXXXX.ndjson почав записуватись
[ ] delta_f_base_hz записано з boot log
```

---

## 5. Протокол одного виміру

Стандартний p3 protocol (незмінний з C-7/C-8):

```
1. Розмістити монету на base spacer (0.6mm) → ENTER → [capture ~2.3s]
2. Додати +1mm spacer (1.6mm total)         → ENTER → [capture ~2.3s]
3. Додати +2mm spacer (2.6mm total)         → ENTER → [capture ~2.3s]
4. Зняти spacers, повернути монету           → ENTER → [drift check]
5. Зняти монету → COMPUTE → production save + discovery dump → IDLE
```

**Для XFE_REAL:**
- Виконати 5 вимірів підряд без перекалібрування
- Перший вимір: якщо drift > 5% → виключити з centroid (прогрів)
- Записати в сесійному логбуку: назву монети, матеріал, номінал, рік, country

**Важливо для XFE:** Спостерігати за `match_result` в NDJSON. Очікується `metal_code: "XFE"` після re-seed (до re-seed може показувати XAG999/XKENNEDY через bug). Записати raw результати і не перейматись неправильним match під час сесії.

---

## 6. Ключові експерименти C-9

| Код | Що перевіряємо | Критерій успіху |
|-----|---------------|----------------|
| **EXP-C9-1** | XFE_real дає від'ємний df_n | `df_n(XFE_real) < 0` — феромагнетик знижує fSENSOR при наближенні |
| **EXP-C9-2** | XFE_real відрізняється від Kennedy_B | `d(Kennedy_B, XFE_new) > 2.0σ` (vs поточних ~0.05σ) |
| **EXP-C9-3** | XFE_real відрізняється від USSR_A | `d(USSR_A, XFE_new) > 2.0σ` (vs поточних ~0.3σ) |
| **EXP-C9-4** | USSR_B з власним baseline стабільний | `σ_within(USSR_B) < 0.3` (C-8 USSR_B: shared baseline bln=63615) |
| **EXP-C9-5** | df1_n розрізняє Kennedy_A vs Kennedy_B | `|df1_n(Kennedy_A) − df1_n(Kennedy_B)| > 2σ_within` |

---

## 7. Після сесії — gen-4 DB

### Pipeline

```
1. Скопіювати session_XXXXXXXX.ndjson з SD на ПК
2. Оновити scripts/a1_analysis.py:
   - NDJSON_IN = REPO / "docs/external/2026-XX-XX.C9.session_XXXXXXXX.ndjson"
   - Додати XFE_REAL до GROUPS
   - Використати df1_n = (steps[1]["fSensor_hz"] - f_empty) / f_empty
3. Запустити a1_analysis.py → отримати нові centroids
4. Оновити database/index.json до gen-4:
   - 11+ entries (9 gen-3 + XFE_REAL + нові з C-8: XAG800_BHS + XUSSR10R)
   - Додати "df1_n" field до кожного запису
5. Оновити matcher.json до v3:
   - version: 3
   - full_weights: [1.5, 0.0, 1.0, 3.5, 2.5, 2.0]  (початкова оцінка)
   - notes: "Gen 4 — 6D vector (dRp1_n, k1, k2, df_n, dL1_n, df1_n)"
6. Оновити C9_HW_SESSION.md з результатами
7. Оновити WAVE9_ROADMAP.md: C-9 Done, A-6 Done
```

### Acceptance criteria для gen-4

- [ ] XFE centroid: `df_n < 0` (від'ємний — фізично обґрунтовано)
- [ ] min_pairwise_distance(Kennedy_B, XFE_new) > 2.0σ
- [ ] min_pairwise_distance(всі пари) > 1.0σ
- [ ] Build SUCCESS з gen-4 DB

---

## 8. Чеклист

### До C-9 (передумови)
- [ ] Феромагнітна монета знайдена та пройшла тест магнітом
- [ ] D-7 реалізований (df1_n в FingerprintCache + matchFull)
- [ ] D-7b реалізований (LHR reads в production path)
- [ ] a1_analysis.py оновлений для df1_n
- [ ] Session logbook готовий (новий файл Session9.txt або Session_C9.txt)

### Після C-9 (результати)
- [ ] **EXP-C9-1:** `df_n(XFE_real) < 0` → ✅/❌
- [ ] **EXP-C9-2:** `d(Kennedy_B, XFE_new) > 2.0σ` → ✅/❌
- [ ] **EXP-C9-3:** `d(USSR_A, XFE_new) > 2.0σ` → ✅/❌
- [ ] **EXP-C9-4:** USSR_B σ_within < 0.3 → ✅/❌
- [ ] **EXP-C9-5:** df1_n Kennedy_A vs Kennedy_B → ✅/❌
- [ ] gen-4 DB задеплоєний на SD
- [ ] C9_HW_SESSION.md оновлений з результатами
- [ ] WAVE9_ROADMAP.md оновлений (C-9 Done, A-6 Done або In Progress)
- [ ] Commit з результатами (аналогічно до C-8 commit 913f3f7)

---

*v1.0.0 — 2026-04-03. Created based on C-8 critical findings: XFE centroid bug (bimetal seed) + dk_n rejection → ADR-VEC-002. Blocked pending steel coin acquisition. Predecessor: C8_HW_SESSION.md.*
