# C-7 Hardware Session — Full Discovery Session

**Версія:** 1.0.0  
**Дата:** 2026-04-02  
**Статус:** 📋 Planned — виконати після друку спейсера 2.6mm  
**Firmware:** `4561c09` — D-5 (NDJSON Vector v2, ADR-VEC-001)  
**Протокол:** `p3_MIKROE3240_b06_012mm`  
**Мета:** 7–10 монет × 5 циклів (3A + 2B) = 35–50 вимірів  
**Output:** NDJSON discovery session → A-1 offline analysis → index.json gen 3  
**Cross-ref:** `WAVE9_ROADMAP.md §3`, `DISCOVERY_MODE_SPEC.md`, `2026-04-02.C7_PREP.md`

---

## Зміст

1. [Обладнання та спейсер 2.6mm](#1-обладнання-та-спейсер-26mm)
2. [Монети C-7](#2-монети-c-7)
3. [Boot checklist](#3-boot-checklist)
4. [Заповнення Session7.txt](#4-заповнення-session7txt)
5. [Протокол одного циклу](#5-протокол-одного-циклу)
6. [Схема 5 циклів (3A + 2B)](#6-схема-5-циклів-3a--2b)
7. [Порядок вимірювань та спеціальні інструкції](#7-порядок-вимірювань-та-спеціальні-інструкції)
8. [Теплове управління](#8-теплове-управління)
9. [Ключові експерименти C-7](#9-ключові-експерименти-c-7)
10. [Після сесії — збереження даних](#10-після-сесії--збереження-даних)
11. [Чеклист](#11-чеклист)

---

## 1. Обладнання та спейсер 2.6mm

### Компоненти

| Компонент | Деталь |
|-----------|--------|
| Сенсор | MIKROE-3240 (LDC1101) |
| МК | M5Stack Cardputer (ESP32-S3) |
| Firmware | `4561c09` (D-5, Wave 9) |
| Base spacer | 0.6mm (старий — не змінюємо) |
| **Addon spacers** | **+1.0mm** (OLD) **+ новий +2.0mm** |
| SD карта | Вставлена, `CoinTrace/` структура готова |
| Serial monitor | COM4, 115200 baud |
| Логбук | `Session7.txt` (новий файл, почати перед сесією) |

### ⚠️ Друк нового спейсера 2.6mm — ОБОВ'ЯЗКОВО перед C-7

**Чому потрібен новий:**  
C-6b Bahamas без центрування → `rp_sigma=155–196` між репами (нестабільне позиціонування).  
Kennedy 1.6mm з центруванням → `rp_sigma=0 стабільно`. Центрувальний виступ усуває бічне ковзання монети.

**Параметри для CAD/слайсера:**

```
Зовнішній діаметр:          60.0 mm
Висота:                      2.60 mm  ← КРИТИЧНО: допуск ±0.05 mm
Центрувальний виступ:  ID = 40.0 mm,  висота виступу = 0.4 mm
Матеріал:                   PETG або PLA (жорсткий — не TPU)
Infill:                      100%  ← інакше висота під навантаженням нестабільна
Шар друку:                  0.10–0.12 mm для точності висоти
```

**Після друку — обов'язково виміряти штангенциркулем:**
- Допустимо: 2.55–2.65 mm
- Якщо < 2.55 або > 2.65 mm — передрукувати

> **Підтвердження перед сесією:** addon spacer 1.0mm + addon spacer 2.0mm повинні разом давати точно ті самі ефективні відстані що і в C-6b (0.6 / 1.6 / 2.6 mm).

---

## 2. Монети C-7

### Список за пріоритетом

| # | Монета | Metal code | Пріоритет | Ключова мета |
|---|--------|-----------|-----------|-------------|
| 1 | American Silver Eagle 1oz | XAG999 | 🔴 MUST | Baseline re-verify D-4; перша монета сесії |
| 2 | Australian Kangaroo 1oz Ag999 | XAG999 (kangaroo) | 🔴 MUST | df_n reference (C-6b: Δf=552,225 Hz) |
| 3 | Olympic 1984 Ag900 | XAG900 | 🔴 MUST | **Чиста сесія** — SAT-lock зник; Δdf=10,322 Hz тест |
| 4 | Ukraine 10 UAH 2022 | XZNNIP | 🔴 MUST | XCU↔XZNNIP pair separation |
| 5 | Russian Empire 5 Kopecks | XCU | 🔴 MUST | XCU↔XZNNIP pair separation |
| 6 | Germany 1.5 EUR 1997 | XFE | 🔴 MUST | EXP-1: ferro test через Cu плакування |
| 7 | Germany 50 Pfennig | XAL | 🔴 MUST | Lowest coupling reference |
| 8 | USSR 1 Ruble або Bahamas Ag800 | XAG800 | 🟡 WANT | Новий клас Ag800 — якщо є в колекції |
| 9 | Kennedy Half Dollar 1965 | XAG900b | 🟡 WANT | 2nd Ag900 — cross-verify Olympic |
| 10 | Au999 монета | XAU999 | 🟡 WANT | Новий клас золото; якщо доступна |

**Мінімум для успішної сесії:** монети 1–7 (7 × 5 = 35 вимірів)  
**Ціль:** 7–10 монет (50 вимірів)

### Характеристики монет для нотаток

| Монета | Склад | Діаметр | Маса | Примітки |
|--------|-------|---------|------|----------|
| Silver Eagle | Ag 99.9% | 40.6 mm | 31.1 g | Без капсули |
| Kangaroo 1oz | Ag 99.9% | 40.6 mm | 31.1 g | Без капсули — інший дизайн |
| Olympic 1984 | Ag 90.0% + Cu | 34.0 mm | 23.33 g | **30 сек прогрів перед першим репом** |
| Ukraine 10 UAH | Zn core + Ni plating | 23.5 mm | 6.4 g | Немагнітна |
| Russian 5 коп. | Cu 100% | 32.6 mm | ~16 g | Чиста мідь |
| Germany 1.5 EUR | Fe core + Cu plating | 31.0 mm | 11.95 g | ✅ Феромагнетик |
| Germany 50 Pfennig | Al 100% | 23.0 mm | 1.6 g | Найлегша |
| Kennedy 1965 | Ag 40% + Cu | 30.6 mm | 11.5 g | Ag 40% (не 90%) |

---

## 3. Boot checklist

Після увімкнення пристрою відкрити Serial Monitor (COM4, 115200 baud).  
**Всі пункти мають бути ✅ перед початком:**

```
✅ fSENSOR = 776–784 kHz         ← D-4 config активний (НЕ 909 kHz!); актуальний діапазон C-7 session
✅ isLDataValid() = true         ← CLKIN підключений та працює
✅ FingerprintCache ready — 9 entries (generation 3)
✅ SD mounted                    ← SD карта визначена
✅ Discovery mode: enabled        ← -D DISCOVERY_MODE активний
✅ Baseline RP: ~63,xxx           ← нормальне значення після D-4
```

**Якщо `fSENSOR = 909 kHz`** — `ldc1101.json` не завантажився з SD. Перевір:
1. SD карта правильно вставлена
2. `SD:\CoinTrace\plugins\ldc1101.json` містить `"tc1": 213` та `"tc2": 254`

**Якщо `FingerprintCache ready — 0 entries`** — `index.json` не знайдено, перевір SD.

---

## 4. Заповнення Session7.txt

Створити файл `Session7.txt` (локально на PC, не на SD). Заповнити **перед першим виміром**:

```
=== C-7 DISCOVERY SESSION ===
Date:          2026-04-xx
Start time:    HH:MM
Room temp:     __°C

=== BASELINE (з Serial boot log) ===
fSENSOR:       ___.___ kHz
rp_baseline:   _____
l_baseline:    _____
lhr_baseline:  _____
Session file:  session_xxxxxxxx.ndjson   ← з Serial log першого виміру

=== COIN ORDER ===
#1  Silver Eagle XAG999        start: HH:MM   temp: __°C
#2  Kangaroo XAG999             start: HH:MM   temp: __°C
#3  Olympic XAG900              start: HH:MM   temp: __°C   [прогрів: 30с]
#4  Ukraine XZNNIP              start: HH:MM   temp: __°C
#5  Russian 5coп XCU            start: HH:MM   temp: __°C
#6  Germany 1.5EUR XFE          start: HH:MM   temp: __°C
#7  Germany 50Pf XAL            start: HH:MM   temp: __°C
#8  _____________               start: HH:MM   temp: __°C
#9  _____________               start: HH:MM   temp: __°C

=== RECALIBRATIONS ===
After meas #__: R pressed, new baseline rp=_____
After meas #__: R pressed, new baseline rp=_____

=== ANOMALIES ===
(записувати будь-що незвичайне: SAT-lock, drift > 1%, помилки)

=== END ===
End time:       HH:MM
Total meas:     __
Max drift:      __%
```

---

## 5. Протокол одного циклу

Firmware Discovery Mode (p3 protocol, 4 steps per cycle):

```
Крок 1 — BASE (d=0.6mm)
  → Покласти монету на base spacer (0.6mm)
  → Натиснути ENTER (клавіша ↵ на міні-клавіатурі)
  → [capture ~2.3s] — дисплей показує progress bar
  → Чекати "done"

Крок 2 — ADDON 1.6mm
  → НЕ рухати монету
  → Покласти addon spacer +1.0mm ЗВЕРХУ монети
  → ENTER → [capture ~2.3s] → done

Крок 3 — ADDON 2.6mm
  → Замінити +1.0mm spacer на +2.0mm spacer (або додати ще 1.0mm)
  → ENTER → [capture ~2.3s] → done

Крок 4 — DRIFT CHECK (d=0.6mm)
  → Зняти всі addon spacers
  → Монета ЗАЛИШАЄТЬСЯ на base spacer 0.6mm
  → ENTER → [capture ~2.3s] → done

Крок 5 — COMPUTE
  → Зняти монету з котушки
  → Пристрій: COMPUTE → зберігає production match + NDJSON dump на SD → IDLE
  → Serial log: "Dump #N → session_xxxxxxxx.ndjson (NNN B JSON)"
```

> **Кнопка ENTER:** клавіша `↵` у правому нижньому куті міні-клавіатури Cardputer.  
> **Час одного циклу:** ~24 сек capture + ~20 сек transitions = ~45 секунд.

### Що НЕ робити

- ❌ Не рухати сенсор або монету під час capture (~2.3s вікно)
- ❌ Не забувати знімати addon spacers перед кроком 4 (drift check)
- ❌ Не пропускати крок 4 — drift % фіксується в NDJSON
- ❌ Не класти монету без base spacer (без 0.6mm — датчик може насититись)

---

## 6. Схема 5 циклів (3A + 2B)

Кожна монета вимірюється 5 разів: **3 рази сторона A**, потім **recalibrate**, потім **2 рази сторона B**.

```
Цикл 1: [Монета, сторона A] → протокол § 5
Цикл 2: [Монета, сторона A] → протокол § 5
Цикл 3: [Монета, сторона A] → протокол § 5
----------------------------------------
>>> Натиснути 'R' на клавіатурі — Recalibrate <<<
   Serial: "Calibrated. Baseline RP=XXXXX"
   Записати новий baseline у Session7.txt
----------------------------------------
Цикл 4: [Монета, сторона B] → протокол § 5
Цикл 5: [Монета, сторона B] → протокол § 5
```

> **Чому 3A + 2B:** Side A/B дає дані про вплив дизайну монети (рельєф реверсу vs аверсу).  
> **Recalibrate між A і B:** скидає thermal baseline — ізолює drift між підгрупами.

---

## 7. Порядок вимірювань та спеціальні інструкції

### Монета 1 — Silver Eagle XAG999 (перша завжди)

Eagle йде першою — це baseline re-verify після D-4 fix.

**Що очікувати:**
- `rp_base ≈ 44,655` (C-6b reference)
- `df_n ≈ 0.680` (Δf ≈ 532,000 Hz / fSENSOR≈780,000 Hz; actual C-7: 0.8022±0.010)
- `confidence > 0.85` (в DB є gen 2 Eagle centroid)

Якщо `rp_base` відрізняється від 44,655 більше ніж на 3% — зафіксувати у Session7.txt, це нормально (інша температура/зношеність котушки).

### Монета 2 — Kangaroo 1oz XAG999

Критична монета для df_n validation.

**Що очікувати:**
- `rp_base ≈ 44,655` (≈ Eagle, обидва Ag999)
- `df_n ≈ 0.680` (≈ Eagle — обидва Ag999, різниця мала)
- Різниця Eagle vs Kangaroo: переважно у `shape/relief` → дизайн-похідний rp_sigma

> C-7 вперше дасть нам два Ag999 зразки для cross-verify. Якщо `df_n` однакове — це підтверджує що df_n — матеріал-специфічний (не дизайн-специфічний).

### Монета 3 — Olympic 1984 XAG900 ⚠️ ОСОБЛИВА ПРОЦЕДУРА

**Ця монета — центральний тест C-7.** C-6b показав Δrp=15 (0.6σ, нерозрізнимо від Eagle) але Δdf=10,322 Hz (20σ, 688× чутливіше).

**Обов'язковий прогрів:**
```
1. Вийняти монету з холодного місця/капсули
2. Тримати в руці 30 секунд (тіло нагріє до ~35°C)
3. Покласти на стіл ще 30 секунд (охолоне до кімнатної ≈ 22°C)
4. ТОДІ починати перший цикл
```
Причина: конденсат водяної пари на холодній монеті змінює `fSENSOR` і спотворює `df_n`.

**Що очікувати:**
- `rp_base ≈ 44,640` (C-6b: ≈Eagle, нерозрізнимо)
- `df_n ≈ 0.667` (C-6b reference; Δdf_n ≈ 0.013 від Eagle/Kangaroo)
- `confidence` — може бути низька (немає в gen 2 DB, найближчий Eagle)

**Критерій успіху:** `|df_n(Eagle) - df_n(Olympic)| > 3 × σ(df_n within-metal)`

### Монета 4 — Ukraine 10 UAH XZNNiP + Монета 5 — Russian 5 Koр. XCU

Ця пара — найважча задача класифікатора (C-5: margin 0.51σ).

**Що очікувати:**
- XCU: `rp_base ≈ 57,xxx` (більше ніж Ag, Cu менш провідна при цій частоті)
- XZNNIP: `rp_base ≈ 55,xxx` (Zn+Ni схожий на Cu)
- `df_n` — очікуємо різний знак або величину: Zn (σ≈16 MS/m) vs Cu (σ≈59 MS/m)

Виміряти їх підряд щоб мінімізувати drift між парою.

### Монета 6 — Germany 1.5 EUR XFE (ferro)

**EXP-1: Ferro test через Cu плакування.**

**Що очікувати:**
- `dL1_n` — великий за абсолютним значенням (феромагнетик збільшує L)
- `df_n` — очікуємо від'ємний (Fe підвищує μr → fSENSOR знижується при наближенні монети)
- Порівняно з XCU (чиста Cu, схожий діаметр): `sign(df_n(XFE)) ≠ sign(df_n(XCU))`

Якщо SAT-lock (`confidence=0`, `rp=0x9999`) — це артефакт. Зафіксувати та продовжити — D-4 мав це виправити.

### Монета 7 — Germany 50 Pfennig XAL

Aluminum — найнижчий coupling, найменші `dRp1_n` та `df_n`.

**Що очікувати:**
- `rp_base` — близький до baseline (Al слабо реагує)
- `df_n` — мале позитивне значення
- `dL1_n` — мале (Al немагнітний)

### Монети 8–10 (якщо є час і монети)

Вимірювати у довільному порядку. Для кожної нової монети **спочатку записати у Session7.txt**:
```
Metal code: _______
Coin name:  _______
Composition: _______
Diameter:   __ mm
Notes:      _______
```

---

## 8. Теплове управління

LDC1101 — температурно-залежний сенсор. `fSENSOR` дрейфує ~0.5 kHz/°C.

### Правила

| Ситуація | Дія |
|----------|-----|
| Кожні 10 вимірів | Натиснути `R` (recalibrate), записати новий baseline у Session7.txt |
| `dRpPct_baseline` у Serial > 1% від попереднього | Пауза 2 хв + `R` |
| Кімнатна температура змінилась > 1°C | Записати у Session7.txt + `R` |
| Сесія > 30 хв без рекалібрації | `R` обов'язково |
| Будь-яка аномалія у match result | Записати у Session7.txt — не ігнорувати |

### Температура кімнати

Фіксувати кожні 15 хв у Session7.txt. Якщо `ΔT > 3°C` за сесію — це впливає на post-hoc аналіз. Дані залишаються валідними — `baseline_rp_session` у NDJSON дозволяє корекцію.

---

## 9. Ключові експерименти C-7

| # | Код | Що перевіряємо | Монети | Критерій успіху |
|---|-----|---------------|--------|-----------------|
| 1 | **EXP-C7-1** | df_n розрізняє Eagle vs Olympic | XAG999 vs XAG900 | `Δdf_n > 3 × σ(within-metal)` |
| 2 | **EXP-C7-2** | df_n sign для ferro | XFE vs XCU | `sign(df_n(XFE)) ≠ sign(df_n(XCU))` |
| 3 | **EXP-C7-3** | XCU↔XZNNIP margin з df_n | XCU vs XZNNIP | pairwise distance > C-6b (без df_n) |
| 4 | **EXP-C7-4** | Eagle vs Kangaroo — df_n матеріал-специфічний | XAG999 × 2 | `|df_n(Eagle) - df_n(Kangaroo)| < σ` — тобто вони ОДНАКОВІ |
| 5 | **EXP-C7-5** | rp_sigma стабільна між репами | всі монети | σ між репами однієї монети < σ між різними металами |

---

## 10. Після сесії — збереження даних

### Крок 1 — Скопіювати NDJSON з SD

SD карта → PC. Файл знаходиться в:
```
SD:\CoinTrace\discovery\session_xxxxxxxx.ndjson
```

Скопіювати до:
```
d:\GitHub\CoinTrace\data\sessions\session_xxxxxxxx.ndjson
```
> Директорія `data/sessions/` — в `.gitignore` (сирі дані не комітяться у repo).

Якщо директорії немає — створити:
```powershell
New-Item -ItemType Directory -Force d:\GitHub\CoinTrace\data\sessions
```

### Крок 2 — Зафіксувати Session7.txt

Заповнити кінцеві поля:
```
End time:    HH:MM
Total meas:  __
Max drift:   __%
NDJSON file: session_xxxxxxxx.ndjson  (__ рядків, __ KB)
```

Зберегти `Session7.txt` поряд з NDJSON або в `data/sessions/`.

### Крок 3 — Швидка перевірка даних

Запустити у PowerShell для підрахунку вимірів per метал:

```powershell
$file = "d:\GitHub\CoinTrace\data\sessions\session_xxxxxxxx.ndjson"
Get-Content $file | ForEach-Object {
    ($_ | ConvertFrom-Json).metal_code
} | Group-Object | Sort-Object Count -Descending | Format-Table Name, Count
```

Очікуваний output:
```
Name      Count
----      -----
XAG999    10     (Eagle × 5 + Kangaroo × 5)
XAG900    5      (Olympic × 5)
XZNNIP    5
XCU       5
XFE       5
XAL       5
...
```

Якщо якась монета має < 5 рядків — сесія неповна для цього металу.

### Крок 4 — Передати Copilot для A-1 аналізу

Повідомити:
> "C-7 сесія завершена. Файл: session_xxxxxxxx.ndjson. Монети: [список]. Запусти A-1 аналіз."

Надати файл `Session7.txt` та NDJSON шлях.

---

## 11. Чеклист

### Підготовка (до сесії)

- [ ] Спейсер 2.6mm надруковано
- [ ] Спейсер 2.6mm виміряно штангенциркулем → `2.55–2.65 mm` ✅
- [ ] SD карта: `CoinTrace/plugins/ldc1101.json` — `tc1=213, tc2=254, rp_set=54`
- [ ] Session7.txt відкрито та розпочато

### Boot

- [ ] Serial: `fSENSOR = 811.x kHz` ✅ (не 909 kHz)
- [ ] Serial: `isLDataValid() = true` ✅
- [ ] Serial: `FingerprintCache ready — 5 entries (generation 2)` ✅
- [ ] Serial: `SD mounted` ✅
- [ ] Serial: `Discovery mode: enabled` ✅
- [ ] Baseline RP/L/fSENSOR записані у Session7.txt

### Вимірювання

- [ ] Монета 1 — Silver Eagle XAG999: 5 циклів (3A + 2B) ✅
- [ ] Монета 2 — Kangaroo XAG999: 5 циклів ✅
- [ ] Монета 3 — Olympic XAG900: 5 циклів (30с прогрів!) ✅
- [ ] Монета 4 — Ukraine XZNNIP: 5 циклів ✅
- [ ] Монета 5 — Russian 5 коп. XCU: 5 циклів ✅
- [ ] Монета 6 — Germany 1.5 EUR XFE: 5 циклів ✅
- [ ] Монета 7 — Germany 50 Pfennig XAL: 5 циклів ✅
- [ ] Монета 8 (optional): _____________ 5 циклів
- [ ] Монета 9 (optional): _____________ 5 циклів
- [ ] Recalibration 'R' кожні ~10 вимірів — виконано

### Після сесії

- [ ] NDJSON скопійовано з SD → `data/sessions/session_xxxxxxxx.ndjson`
- [ ] Session7.txt заповнено повністю (end time, total meas, max drift)
- [ ] PowerShell count: всі монети мають по 5 рядків у NDJSON
- [ ] Copilot повідомлено для A-1 аналізу

---

*Cross-ref: [WAVE9_ROADMAP.md](../architecture/WAVE9_ROADMAP.md) | [DISCOVERY_MODE_SPEC.md](../architecture/DISCOVERY_MODE_SPEC.md) | [C5_HW_SESSION.md](C5_HW_SESSION.md)*
