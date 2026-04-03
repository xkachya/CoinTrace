# C-8 Hardware Session — A/B Side Control + gen-4 DB Expansion

**Версія:** 1.1.0 (completed)  
**Дата:** 2026-04-03  
**Статус:** ✅ Completed — 50 записів, 5 монет × 2 сторони × 5 вимірів  
**Firmware:** gen-3 DB активний (boot log підтверджено: `weights=[1.5,0.0,1.0,3.5,2.5]`, 9 entries)  
**Протокол:** `p3_MIKROE3240_b06_012mm` (незмінний з C-7)  
**Мета:** A/B side контроль для Kennedy + Kangaroo + нові типи XAG800/XAU999  
**Output:** `session_cf1edfcd.ndjson` (50 записів) → аналіз C-8 → план gen-4 DB  
**Cross-ref:** `C7_HW_SESSION.md`, `docs/external/2026-04-03.C8_DISCOVERY_ANALYSIS_REPORT.md`, `docs/external/2026-04-03.C8_INDEPENDENT_ANALYSIS_REPORT.md`

---

## Результати сесії (2026-04-03)

> Повний аналіз: [C8_DISCOVERY_ANALYSIS_REPORT.md](../external/2026-04-03.C8_DISCOVERY_ANALYSIS_REPORT.md) · [C8_INDEPENDENT_ANALYSIS_REPORT.md](../external/2026-04-03.C8_INDEPENDENT_ANALYSIS_REPORT.md)

### Монети виміряні (фактичні vs план)

| # | Монета | Склад | Ø mm | Маса | A/B | Записів | Статус |
|---|--------|-------|------|------|-----|---------|--------|
| 1 | American Silver Eagle 1oz | Ag 999 | 40.6 | 31.1g | A×5 + B×5 | 10 | ✅ (бонус B-side) |
| 2 | Kennedy Half Dollar 1964 | Ag 400 | 30.6 | 11.5g | A×5 + B×5 | 10 | ✅ |
| 3 | Australian Kangaroo 1oz | Ag 999 | 40.6 | 31.1g | A×5 + B×5 | 10 | ✅ |
| 4 | Bahamas $1 1966–70 | **Ag 800** | 34.0 | 18.14g | A×5 + B×5 | 10 | ✅ (замість XAU999) |
| 5 | USSR 10 Rubles 1977 | **Ag 900** | 39.0 | 33.3g | A×5 + B×5 | 10 | ✅ (замість XAG800 plan) |

**Загалом:** 50 записів. Session file: `session_cf1edfcd.ndjson`.

### Протокольні відхилення

| Відхилення | Записи | Вплив |
|-----------|--------|-------|
| Запис idx=0 (Eagle A) drift=9.34% — першпрогрів | idx=0 | Виключити з centroid (використати idx 1–4) |
| Kennedy A→B без рекалібрації (shared bln_rp=63999) | idx 10–19 | Низький — дрейф <0.25% у кожній підгрупі |
| USSR A→B без рекалібрації (shared bln_rp=63615) | idx 40–49 | Низький — стабільний сигнал |

### Результати ключових експериментів

| Код | Результат | z-score (ключова пара) |
|-----|-----------|------------------------|
| **EXP-C8-1** Eagle vs Kennedy | ✅ Розрізняємо через dRp1_n (z=22), але Eagle_A/Kennedy_B перетинаються в df_n (z=1.2) | Потрібна 6D |
| **EXP-C8-2** Kennedy A/B bimodal | ✅ Підтверджено: Δdf_n=0.037 (z=34), ΔdRp1_n=1.57 (z=22) | Два окремих класи |
| **EXP-C8-3** Kangaroo A/B | ✅ Підтверджено: Δdf_n=0.034 (z=43), ΔdRp1_n=1.29 (z=23) | Два окремих класи |
| **EXP-C8-4** Bahamas Ag800 (новий) | ✅ Тривіально унікальний: df_n>0.797, z>12 від найближчого сусіда | Новий клас XAG800_BHS |
| **EXP-C8-5** USSR Ag900 (новий) | ⚠️ B-side виразний (df_n=0.818), A-side перетинається з Kennedy_B і XFE | Новий клас, але A-side проблематичний |

### Критичні висновки

1. **Найнебезпечніший false-positive:** Kennedy_B та USSR_A класифікуються як XFE з conf=64–97%. Причина — XFE centroid засіяний з Germany 1.5 Euro (Fe+Cu bimetal), а не зі справжньої сталі. XFE centroid потрапляє в зону срібних монет.
2. **dk_n (D-7) неефективна ознака** для пари Kennedy_B/USSR_A (z=0.9). Натомість df1_n (frequency shift at 1.6mm) дає z=7.5 для тієї ж пари. Незалежний аудит рекомендує замінити dk_n → df1_n у production_vector.
3. **Kang B-side** (df_n=0.716) потрапляє в зону XAG900 Olympic → false conf=47–58%.

### Наступні кроки (пріоритет)

| Пріоритет | Дія | Блокер |
|-----------|-----|--------|
| 🔴 CRITICAL | Re-seed XFE centroid зі справжньої сталевої монети | Потрібна монета (old Soviet kopek, East German Pfennig, або євроцент) |
| 🔴 HIGH | ADR: замінити dk_n → df1_n у production_vector (6D) | Firmware change |
| 🟡 MEDIUM | Gen-4 DB: split всі A/B класи, додати XAG800_BHS | Після ADR |
| 🟡 MEDIUM | USSR_B ресесія з рекалібрацією | Можливо зараз |

---

## Чому C-8 потрібна — контекст (original plan)

A-5 аналіз (2026-04-03) показав:

| Проблема | Деталь |
|---------|--------|
| Eagle↔Kennedy separation = **1.61σ** | Тільки 0.672σ pairwise distance — найближча пара в gen-3 DB |
| Kennedy dk_n = **bimodal** | A-side `[0.622, 0.625, 0.627]` vs B-side `[0.613, 0.614]` ≈ Eagle |
| 6D improvement = **+0.1%** | dk_n не допомагає поки centroid забруднений A/B mix |
| **Root cause** | C-7 Kennedy вимірювався без контролю сторони → B-side тягне centroid до Eagle |

**C-8 вирішує:** Виміряти Kennedy A-side і B-side **окремо** → чисті centroids → re-run A-5 → очікуємо Eagle↔Kennedy(A) відстань значно зросте.

---

## Зміст
- [Результати сесії (2026-04-03)](#результати-сесії-2026-04-03)
- [Чому C-8 потрібна — контекст](#чому-c-8-потрібна--контекст-original-plan)0. [Прошивка firmware та JSON-конфігів](#0-прошивка-firmware-та-json-конфігів)
1. [Головні відмінності від C-7](#1-головні-відмінності-від-c-7)
2. [Обладнання](#2-обладнання)
3. [Монети C-8](#3-монети-c-8)
4. [Boot checklist](#4-boot-checklist)
5. [Заповнення Session8.txt](#5-заповнення-session8txt)
6. [Протокол одного циклу](#6-протокол-одного-циклу)
7. [Схема A×5 + Recal + B×5 (КЛЮЧОВА ВІДМІННІСТЬ)](#7-схема-a5--recal--b5-ключова-відмінність)
8. [Порядок вимірювань та спеціальні інструкції](#8-порядок-вимірювань-та-спеціальні-інструкції)
9. [Теплове управління](#9-теплове-управління)
10. [Ключові експерименти C-8](#10-ключові-експерименти-c-8)
11. [Після сесії — збереження даних](#11-після-сесії--збереження-даних)
12. [Чеклист](#12-чеклист)

---

## 0. Прошивка firmware та JSON-конфігів

> **Cross-ref:** `docs/guides/UPLOADFS_GUIDE.md` — детальна документація по двох LittleFS розділах.

### Що і куди заливається

Flash ESP32-S3 поділено на **незалежні регіони**. Кожна команда торкається тільки свого:

```
Регіон            Команда PlatformIO                      Вміст
──────────────    ────────────────────────────────────    ──────────────────────────────────
app0 (firmware)   pio run -e cointrace-dev -t upload      Скомпільований .elf (C++ код)
littlefs_sys      pio run -e uploadfs-sys  -t uploadfs    Web UI + JSONи з папки data/
littlefs_data     (немає команди — тільки firmware)       Виміри, логи, кеш відбитків
```

> ⚠️ **`littlefs_data` НІКОЛИ не стирається командами вище.** Всі попередні виміри та кеш — в безпеці після будь-якого перепрошивання firmware або системних JSON-ів.

---

### Сценарій A — перший старт / змінився тільки код

Якщо JSON-конфіги і `data/` **не змінювались** — потрібна тільки прошивка firmware:

```powershell
cd d:\GitHub\CoinTrace

# 1. Збудувати та прошити firmware
pio run -e cointrace-dev -t upload
```

**Переконатись що прошивка успішна:**
```
...
Writing at 0x00010000... (100 %)
Hash of data verified.
==== [SUCCESS] ====
```

---

### Сценарій B — оновились JSON-конфіги або index.json

Якщо змінились файли LittleFS (`data/config/`, `data/web/`) — потрібно залити `littlefs_sys` **окремою командою**:

```powershell
cd d:\GitHub\CoinTrace

# 1. Прошити firmware (якщо теж оновився)
pio run -e cointrace-dev -t upload

# 2. Залити sys-розділ (web UI + JSON-конфіги)
pio run -e uploadfs-sys -t uploadfs
```

**Переконатись що uploadfs успішна:**
```
...
Writing at offset 0x00510000...
==== [SUCCESS] ====
```

> ⚠️ **Порядок важливий:** спочатку upload (firmware), потім uploadfs. Зворотний порядок не ламає нічого, але firmware при старті може не знайти оновлений `index.json` до перезавантаження.

---

### Сценарій C — повне стирання (новий пристрій або corrupted flash)

Тільки при першому старті нового пристрою або якщо `littlefs_data` corrupted:

```powershell
cd d:\GitHub\CoinTrace

# 1. Стерти весь flash
pio run -e cointrace-dev -t erase

# 2. Прошити firmware
pio run -e cointrace-dev -t upload

# 3. Залити sys-розділ
pio run -e uploadfs-sys -t uploadfs

# littlefs_data — firmware форматує сам при першому старті
```

> ⚠️ Сценарій C стирає `littlefs_data` → **всі виміри та кеш втрачаються**. Для C-8 цього не потрібно — тільки A або B.

---

### Для C-8: що робити перед сесією

**Якщо firmware не змінювався з C-7 (commit `e9e78c1`):** нічого не робити — пристрій готовий.

**Якщо D-7b реалізовано (production LHR fix):** виконати **Сценарій A** (тільки firmware upload).

**Якщо index.json або matcher.json оновились:** виконати **Сценарій B** (firmware + uploadfs-sys).

Перевірка після будь-якого оновлення — Serial Monitor:
```
✅ FingerprintCache ready — 9 entries (generation 3)
✅ fSENSOR = 776–784 kHz
```

---

### Окрема заміна тільки JSON-файлів (без uploadfs)

Якщо потрібно оновити тільки один JSON (наприклад `matcher.json`) — є швидший спосіб через Web UI:

```
1. Підключити пристрій до WiFi (або прямий AP режим)
2. Відкрити http://<device-ip>/
3. Розділ "Config" → Upload file
4. Завантажити оновлений JSON
5. Перезавантажити пристрій (кнопка Reset або через Web UI)
```

> Але для C-8 підготовки рекомендується `uploadfs-sys` — він синхронізує ВСЮ папку `data/` і виключає розходження між файлами.

---

### SD карта — index.json та matcher.json (через кардрідер)

> **`uploadfs-sys` НЕ записує на SD карту.** LittleFS і SD — два різних носії.  
> `index.json` та `matcher.json` живуть на SD карті і копіюються **фізично через кардрідер**.

#### Структура SD карти (що має бути)

```
SD:\
└── CoinTrace\
    ├── matcher.json                  ← ваги matcher (full_weights, sigma)
    ├── database\
    │   └── index.json                ← fingerprint DB (9 entries, gen-3)
    ├── plugins\
    │   └── ldc1101.json              ← конфіг сенсора (tc1, tc2, rp_set)
    └── discovery\
        └── session_xxxxxxxx.ndjson  ← сюди firmware пише виміри
```

#### Коли потрібно оновлювати SD

| Ситуація | Що оновлювати |
|---------|--------------|
| gen-3 DB вже на SD з C-7 | ✅ Нічого — не чіпати |
| Новий `index.json` або `matcher.json` після A-6 | Оновити обидва |
| Нова SD карта / повне форматування | Скопіювати все з `data/sd_seed/` |

**Для C-8 сесії:** якщо SD карта використовувалась у C-7 і gen-3 DB вже на ній — **нічого копіювати не треба**. Продовжуй з boot checklist §4.

#### Оновити index.json та matcher.json на існуючій SD

```powershell
# Вставити SD карту через кардрідер
# Замінити E: на букву вашої SD-карти (перевірити у Explorer)
$sd = "E:"
$repo = "d:\GitHub\CoinTrace"

# Переконатись що директорії існують
New-Item -ItemType Directory -Force "$sd\CoinTrace\database"

# Скопіювати оновлені файли
Copy-Item "$repo\data\sd_seed\CoinTrace\database\index.json" `
          "$sd\CoinTrace\database\index.json" -Force

Copy-Item "$repo\data\sd_seed\CoinTrace\matcher.json" `
          "$sd\CoinTrace\matcher.json" -Force

Write-Host "Done. Verify:"
Write-Host "  $sd\CoinTrace\database\index.json"
Write-Host "  $sd\CoinTrace\matcher.json"
```

#### Повна ініціалізація нової SD карти

```powershell
$sd = "E:"
$repo = "d:\GitHub\CoinTrace"

# Створити структуру
New-Item -ItemType Directory -Force "$sd\CoinTrace\database"
New-Item -ItemType Directory -Force "$sd\CoinTrace\plugins"
New-Item -ItemType Directory -Force "$sd\CoinTrace\discovery"

# Скопіювати весь sd_seed
Copy-Item "$repo\data\sd_seed\CoinTrace\*" "$sd\CoinTrace\" -Recurse -Force

Write-Host "SD initialized. Contents:"
Get-ChildItem "$sd\CoinTrace" -Recurse | Select-Object FullName
```

#### Перевірка після копіювання

Вставити SD у Cardputer → Serial Monitor при завантаженні:
```
✅ FingerprintCache ready — 9 entries (generation 3)
```
Якщо `0 entries` або `generation 2` — SD не оновилась, перевірити букву диску і повторити.

---

## 1. Головні відмінності від C-7

| Параметр | C-7 | **C-8** |
|----------|-----|---------|
| Мета | Генерація gen-3 DB (9 типів) | A/B side контроль + gen-4 expansion |
| Схема циклів | 3A + 2B per coin (mixed) | **A×5 окремо → Recal → B×5 окремо** |
| Kennedy | 5 циклів mixed → bimodal centroid | **A×5 + B×5 = 10 вимірів controlled** |
| Kangaroo | 5 циклів mixed | **A×5 + B×5 = 10 вимірів controlled** |
| Eagle | 5 циклів (baseline) | 5 циклів (control, без A/B split) |
| Нові типи | Немає | XAG800×5 + XAU999×5 (якщо є) |
| Загальна к-сть вимірів | 45 (9×5) | 35–45 (залежно від доступних монет) |
| Session файл | `Session7.txt` | **`Session8.txt`** (новий) |
| Firmware | `4561c09` (D-5) | **`e9e78c1`** або пізніший |

> ⚠️ **Критично:** Схема A×5 + B×5 — не те саме що C-7 (3A+2B). Потрібно ЗАВЕРШИТИ всі 5 A-side циклів перед будь-яким B-side виміром. Змішування анулює мету C-8.

---

## 2. Обладнання

| Компонент | Деталь | Примітка |
|-----------|--------|---------|
| Сенсор | MIKROE-3240 (LDC1101) | Без змін з C-7 |
| МК | M5Stack Cardputer (ESP32-S3) | |
| Firmware | `e9e78c1` (A-5 commit) або пізніший | ✅ gen-3 DB активний |
| Base spacer | 0.6mm | Без змін |
| Addon spacer 1 | +1.0mm | Без змін |
| Addon spacer 2 | +2.0mm | **Той самий що надруковано для C-7 (2.60mm)** |
| SD карта | Вставлена, `CoinTrace/` структура готова | |
| Serial monitor | COM4, 115200 baud | |
| Логбук | `Session8.txt` — новий файл | Почати до першого виміру |

### Перевірка спейсера 2.6mm

Якщо спейсером не користувались з C-7 — перевірити перед сесією:
- Виміряти штангенциркулем: допустимо `2.55–2.65 mm`
- Перевірити центрувальний виступ — цілий, не відламаний

---

## 3. Монети C-8

### Список за пріоритетом

| # | Монета | Metal code | Пріоритет | A/B? | Вимірів | Ключова мета |
|---|--------|-----------|-----------|------|---------|-------------|
| 1 | American Silver Eagle 1oz Ag999 | XAG999_EAGLE | 🔴 MUST | ❌ No split | 5 | Control baseline; A-5 root reference |
| 2 | Kennedy Half Dollar 1967 Ag400 | XKENNEDY | 🔴 MUST | ✅ A×5 + B×5 | **10** | EXP-C8-1/2: dk_n bimodal fix |
| 3 | Australian Kangaroo 1oz Ag999 | XAG999_KANG | 🔴 MUST | ✅ A×5 + B×5 | **10** | EXP-C8-3: A/B quantification |
| 4 | Ag800 монета (TBD) | XAG800 | 🟡 WANT | ❌ No split | 5 | EXP-C8-4: новий клас Ag800 |
| 5 | Au999 монета (TBD) | XAU999 | 🟡 WANT | ❌ No split | 5 | EXP-C8-5: золото окремий cluster |

**Мінімум (монети 1–3):** 25 вимірів, ~30 хв  
**Повна C-8 (монети 1–5):** 35 вимірів, ~45 хв

### Характеристики монет

| Монета | Склад | Діаметр | Маса | gen-3 dk_n mean | Примітки |
|--------|-------|---------|------|----------------|---------|
| Silver Eagle | Ag 99.9% | 40.6 mm | 31.1 g | 0.61323 (σ=0.00255) | Без капсули |
| Kennedy 1965 | Ag 40% + Cu 60% | 30.6 mm | 11.5 g | 0.62049 (**σ=0.00587**) | ⚠️ Bimodal в C-7 |
| Kangaroo 1oz | Ag 99.9% | 40.6 mm | 31.1 g | 0.62736 (σ=0.00475) | Без капсули |
| Ag800 (TBD) | Ag 80% + Cu | ~varies | varies | — | новий тип |
| Au999 (TBD) | Au 99.9% | ~varies | varies | — | новий тип |

### A vs B side визначення

| Монета | **Side A (Avers)** | **Side B (Revers)** |
|--------|------------------|-------------------|
| Kennedy | Портрет Kennedy (голова) | Орел (Presidential coat of arms) |
| Kangaroo | Портрет Queen Elizabeth II | Кенгуру (стрибучий) |
| Eagle | Ходячий орел (obverse) | Щит + фраза E PLURIBUS UNUM |

> **Правило запам'ятовування:** Side A = **обличчя/портрет** (завжди). Side B = **реверс/тематика**.  
> Тримати монету так, щоб портрет дивився ВГОРУ = Side A. Перевернути = Side B.

---

## 4. Boot checklist

Після увімкнення відкрити Serial Monitor (COM4, 115200 baud).  
**Всі пункти ✅ перед першим виміром:**

```
✅ fSENSOR = 776–784 kHz          ← D-4 config (НЕ 909 kHz!)
✅ isLDataValid() = true           ← CLKIN підключений
✅ FingerprintCache ready — 9 entries (generation 3)   ← gen-3 DB
✅ SD mounted                      ← SD карта визначена
✅ Discovery mode: enabled          ← -D DISCOVERY_MODE активний
✅ Baseline RP: ~63,xxx             ← нормальне після D-4
```

**Якщо `fSENSOR = 909 kHz`:**
1. SD карта правильно вставлена?
2. `SD:\CoinTrace\plugins\ldc1101.json` містить `"tc1": 213` та `"tc2": 254`?
3. Перезавантажити → перевірити знову

**Якщо `FingerprintCache ready — 0 entries`:**  
`index.json` не знайдено. Перевір `SD:\CoinTrace\database\index.json`.

**Якщо `generation 2` замість `generation 3`:**  
Старий `index.json`. Завантажити актуальний з `e9e78c1` commit → записати на SD → перезавантажити.

---

## 5. Заповнення Session8.txt

Створити файл `Session8.txt` (на PC, не SD). Заповнити **перед першим виміром**:

```
=== C-8 DISCOVERY SESSION ===
Date:          2026-0x-xx
Start time:    HH:MM
Room temp:     __°C
Firmware:      e9e78c1 (або пізніший — вказати commit)

=== BASELINE (з Serial boot log) ===
fSENSOR:       ___.___ kHz
rp_baseline:   _____
l_baseline:    _____
lhr_baseline:  _____
Session file:  session_xxxxxxxx.ndjson   ← з Serial log першого виміру

=== МОНЕТИ — A-SIDE (ПЕРША ПОЛОВИНА СЕСІЇ) ===
#1  Silver Eagle XAG999_EAGLE  [5 циклів, no A/B split]
    start: HH:MM   temp: __°C
    Recal before: rp=_____

#2  Kennedy XKENNEDY — SIDE A (портрет Kennedy ВГОРУ)
    start: HH:MM   temp: __°C
    Recal after A×5: rp=_____

#3  Kangaroo XAG999_KANG — SIDE A (Queen Elizabeth ВГОРУ)
    start: HH:MM   temp: __°C
    Recal after A×5: rp=_____

=== МОНЕТИ — B-SIDE (ДРУГА ПОЛОВИНА СЕСІЇ) ===
#4  Kennedy XKENNEDY — SIDE B (орел / CoA ВГОРУ)
    start: HH:MM   temp: __°C
    Recal before B×5: rp=_____

#5  Kangaroo XAG999_KANG — SIDE B (кенгуру ВГОРУ)
    start: HH:MM   temp: __°C
    Recal before B×5: rp=_____

=== НОВІ ТИПИ (якщо є) ===
#6  XAG800 ____________: 5 циклів
    start: HH:MM   temp: __°C   composition: ______
    diameter: __ mm   mass: __ g

#7  XAU999 ____________: 5 циклів
    start: HH:MM   temp: __°C   composition: ______
    diameter: __ mm   mass: __ g

=== RECALIBRATIONS ===
After #__:  R pressed  → new rp=_____
After #__:  R pressed  → new rp=_____
After #__:  R pressed  → new rp=_____

=== ANOMALIES ===
(записувати: SAT-lock, drift > 1%, будь-що незвичайне)

=== END ===
End time:       HH:MM
Total meas:     __ (expected: 25–35)
Max drift:      __%
NDJSON file:    session_xxxxxxxx.ndjson  (__ рядків)
```

---

## 6. Протокол одного циклу

Ідентичний C-7. Firmware Discovery Mode, p3 protocol:

```
Крок 1 — BASE (d=0.6mm)
  → Покласти монету ПОТРІБНОЮ СТОРОНОЮ ВНИЗ на base spacer
  → ENTER (↵ на Cardputer клавіатурі)
  → [capture ~2.3s] — progress bar на дисплеї
  → Чекати "done"

Крок 2 — ADDON 1.6mm
  → НЕ рухати монету
  → Покласти addon spacer +1.0mm ЗВЕРХУ монети
  → ENTER → [capture ~2.3s] → done

Крок 3 — ADDON 2.6mm
  → Замінити +1.0mm на +2.0mm spacer
  → (або додати ще один +1.0mm якщо є два однакові)
  → ENTER → [capture ~2.3s] → done

Крок 4 — DRIFT CHECK (d=0.6mm)
  → Зняти ВСІ addon spacers
  → Монета ЗАЛИШАЄТЬСЯ на base spacer
  → ENTER → [capture ~2.3s] → done

Крок 5 — COMPUTE
  → Зняти монету з котушки
  → Пристрій: COMPUTE → NDJSON dump на SD → IDLE
  → Serial: "Dump #N → session_xxxxxxxx.ndjson (NNN B JSON)"
  → Записати у Session8.txt якщо match result несподіваний
```

> **Час одного циклу:** ~24 сек capture + ~20 сек transitions = **~45 секунд**.

### Що НЕ робити

- ❌ Не рухати монету або сенсор під час capture вікна (~2.3s)
- ❌ Не забувати знімати addon spacers перед кроком 4
- ❌ Не перевертати монету під час A-side блоку (всі 5 циклів — одна сторона)
- ❌ Не пропускати Recalibrate між A та B блоками

---

## 7. Схема A×5 + Recal + B×5 (КЛЮЧОВА ВІДМІННІСТЬ)

### Для Kennedy та Kangaroo (монети з A/B split)

```
┌─────────────────────────────────────────────────────────┐
│  БЛОК A — Side A (портрет/обличчя ВГОРУ, тобто ДОНИЗУ) │
├─────────────────────────────────────────────────────────┤
│  Цикл A-1: [монета, Side A] → протокол §6              │
│  Цикл A-2: [монета, Side A] → протокол §6              │
│  Цикл A-3: [монета, Side A] → протокол §6              │
│  Цикл A-4: [монета, Side A] → протокол §6              │
│  Цикл A-5: [монета, Side A] → протокол §6              │
├─────────────────────────────────────────────────────────┤
│  ► ОБОВ'ЯЗКОВО натиснути 'R' — Recalibrate ◄           │
│    Serial: "Calibrated. Baseline RP=XXXXX"              │
│    Записати новий baseline у Session8.txt               │
│    Пауза 60 секунд (термальне вирівнювання)             │
├─────────────────────────────────────────────────────────┤
│  БЛОК B — Side B (реверс ВГОРУ, тобто ДОНИЗУ)          │
├─────────────────────────────────────────────────────────┤
│  Цикл B-1: [монета, Side B] → протокол §6              │
│  Цикл B-2: [монета, Side B] → протокол §6              │
│  Цикл B-3: [монета, Side B] → протокол §6              │
│  Цикл B-4: [монета, Side B] → протокол §6              │
│  Цикл B-5: [монета, Side B] → протокол §6              │
└─────────────────────────────────────────────────────────┘
```

> **Орієнтація монети при вимірюванні:**  
> Монета кладеться на base spacer **лицем вниз** (сторона дивиться на котушку).  
> Side A вимірювання = портрет Kennedy/Queen **дивиться вниз** на сенсор.  
> Side B вимірювання = реверс (орел/кенгуру) **дивиться вниз** на сенсор.

### Для Eagle та нових типів (без A/B split)

```
  Цикл 1–5: будь-яка сторона, однакова для всіх 5
  Recalibrate після циклу 3 (середина серії)
```

---

## 8. Порядок вимірювань та спеціальні інструкції

### Рекомендований порядок сесії

```
[1] Eagle ×5      → RECAL
[2] Kennedy A×5   → RECAL (60с пауза)
[3] Kennedy B×5   → RECAL
[4] Kangaroo A×5  → RECAL (60с пауза)
[5] Kangaroo B×5  → RECAL
[6] XAG800 ×5     → RECAL (якщо є)
[7] XAU999 ×5               (якщо є)
```

**Загальний час:** ~35 вимірів × 45с + рекалібрації + паузи = **~40–50 хв**

---

### Монета 1 — Silver Eagle XAG999_EAGLE (Control)

Eagle йде першою як reference anchor для A-5 re-analysis.

**Схема:** 5 циклів, без A/B split. Довільна сторона, але **однакова для всіх 5**.  
Рекомендація: Side A (Walking Liberty obverse) для consistency з C-7.

**Що очікувати (gen-3 reference):**
```
rp_base    ≈ 63,xxx  (fSENSOR ≈ 780 kHz)
df_n       ≈ 0.8022 ± 0.0099
dk_n       ≈ 0.6132 ± 0.0026  (з C-7 — найстабільніша монета)
confidence > 0.8 (Eagle є в gen-3 DB)
```

**Критерій якості:** `rp_sigma < 0.5` у кожному циклі (жорсткий spacer = стабільний сигнал).  
Якщо `rp_sigma > 2.0` — монета рухалась під час capture, повторити цикл.

Recalibrate після Eagle перед Kennedy.

---

### Монета 2 — Kennedy Half Dollar XKENNEDY ⚠️ КЛЮЧОВА МОНЕТА

> **Ця монета — головна мета C-8.** A-5 показав dk_n bimodal: A-side `[0.622–0.627]` vs B-side `[0.613–0.614]`.  
> C-8 має роздільно підтвердити кожну групу для чистих centroids.

#### Kennedy — БЛОК A (портрет ВНИЗ)

```
Підготовка:
  1. Recalibrate перед першим A-side циклом
  2. Записати baseline rp у Session8.txt: "Kennedy A start: rp=_____"
  3. Переконатись що Kennedy завжди Side A (обличчя Kennedy на котушці)
```

**Що очікувати (A-side, з A-5):**
```
df_n     ≈ 0.765–0.780   (A-side має нижчий df_n ніж B-side)
dk_n     ≈ 0.622–0.627   (A-side centroid)
```

> ⚠️ Зіставлення з gen-3 Kennedy centroid (mixed): `df_n=0.789±0.017`. A-side може видавати нижчий df_n — це очікувано.

#### Kennedy — БЛОК B (реверс Presidential CoA ВНИЗ)

```
Між блоками:
  1. Натиснути 'R' — Recalibrate
  2. Записати новий baseline: "Kennedy Recal A→B: rp=_____"
  3. Пауза 60 секунд
  4. ПЕРЕВЕРНУТИ монету — Presidential coat of arms дивиться вниз
```

**Що очікувати (B-side, з A-5):**
```
df_n     ≈ 0.800–0.820   (B-side має вищий df_n — реверс рельєф більш масивний)
dk_n     ≈ 0.613–0.614   ← УВАГА: майже ідентично Eagle (0.613!)
```

> **Якщо B-side Kennedy dk_n ≈ Eagle dk_n** — це підтверджує A-5 gіпотезу і проблему bimodal centroid.

---

### Монета 3 — Kangaroo 1oz XAG999_KANG

Kangaroo має менший A/B ефект ніж Kennedy (gen-3 σ_within=0.00475 vs Kennedy 0.00587), але все одно bimodal в C-7.

#### Kangaroo — БЛОК A (Queen Elizabeth ВНИЗ)

```
Підготовка: Recalibrate перед блоком A
```

**Що очікувати:**
```
rp_base  ≈ 63,xxx  (обидва Ag999, схоже на Eagle)
df_n     ≈ 0.748–0.762   (A-side Kangaroo — дизайн відрізняється від Eagle)
dk_n     ≈ 0.622–0.631   (A-side)
```

#### Kangaroo — БЛОК B (кенгуру ВНИЗ)

```
Між блоками: Recalibrate + 60с пауза
```

**Що очікувати:**
```
df_n     ≈ 0.762–0.780   (B-side)
dk_n     ≈ 0.618–0.628   (B-side)
```

---

### Монета 4 — XAG800 (якщо є) 🟡 OPTIONAL

Новий тип. Фізика: Ag 80% → σ ≈ 22 MS/m (між Ag900 і Cu/Zn сплавами).

```
Підготовка:
  - Записати у Session8.txt: назву, склад, діаметр, масу
  - 5 циклів, без A/B split, довільна сторона
```

**Що очікувати:**
```
df_n   — між XAG900 (0.739) та XAG999 (0.802): очікуємо ~0.750–0.770
dk_n   — невідомо, але між Ag900 та Ag999
dRp1_n — між XAG900 та XAG999
```

**Metal code для NDJSON:** `XAG800`

---

### Монета 5 — XAU999 (якщо є) 🟡 OPTIONAL

Gold: σ_Au = 45 MS/m (вище за Ag = 62, нижче за Cu = 59). μr = 1.0 (немагнітний). **Унікальний density = 19.3 g/cm³**.

```
Підготовка:
  - Зафіксувати в Session8.txt: назву, діаметр, масу
  - 5 циклів без A/B split
```

**Що очікувати:**
```
df_n   — близьке до Ag999 (σ схожий) але НЕ ідентичне через різний k1/k2
dRp1_n — може відрізнятись через density (RF penetration depth)
dk_n   — невідомо: маленькі Au монети можуть мати низький dk_n
```

**Metal code для NDJSON:** `XAU999`  
**Важливо:** Якщо монета в капсулі — виміряти з капсулою І без. Зафіксувати обидва набори окремо.

---

## 9. Теплове управління

| Ситуація | Дія |
|----------|-----|
| Перед кожним монетним блоком | `R` (recalibrate), записати rp у Session8.txt |
| Між A та B блоком (обов'язково) | `R` + **60с пауза** (дати термально вирівнятись) |
| `dRpPct_baseline` > 1% у Serial | Пауза 2 хв + `R` + продовжити |
| Сесія > 30 хв без рекалібрації | `R` обов'язково |
| Кімнатна температура змінилась > 1°C | Записати + `R` |

### Теплова пауза між A та B (60 секунд)

**Чому 60 секунд, а не 300ms як в C-7?**  
C-7 рекалібрація між підгрупами відбувалась раз на ~10 вимірів. В C-8 між A та B блоком — навмисна пауза для термальної стабілізації котушки після серії вимірів. 60 секунд достатньо для LDC1101 (`fSENSOR` settling rate ≈ 0.5 kHz/°C/хв).

---

## 10. Ключові експерименти C-8

| # | Код | Що перевіряємо | Монети | Критерій успіху |
|---|-----|---------------|--------|----------------|
| 1 | **EXP-C8-1** | dk_n розрізняє Eagle vs Kennedy(A only) | XAG999_EAGLE vs XKENNEDY(A) | d(Eagle, Kennedy_A) у 6D **≥ 1.0σ** (vs 0.672σ poточний) |
| 2 | **EXP-C8-2** | Kennedy A/B bimodal підтверджено | XKENNEDY A×5 vs B×5 | σ_within(A only) **< 0.30** AND σ_within(B only) **< 0.30** (vs mixed 0.663) |
| 3 | **EXP-C8-3** | Kangaroo A/B quantification | XAG999_KANG A×5 vs B×5 | σ_within(A only) **< 0.20** AND σ_within(B only) **< 0.20** (vs mixed 0.475) |
| 4 | **EXP-C8-4** | XAG800 відділяється від XAG999 | XAG800 vs XAG999_EAGLE | pairwise distance **> 1.0σ** у 6D |
| 5 | **EXP-C8-5** | XAU999 відділяється від усіх Ag | XAU999 vs усі XAGxxx | pairwise distance до ближчого Ag **> 2.0σ** |

### D-7 Decision Matrix (після A-5 re-run на C-8 даних)

| Сценарій | Результат | Рекомендація |
|----------|-----------|-------------|
| Eagle↔Kennedy(A) sep ≥ 1.0σ AND improvement ≥ 20% | dk_n працює | ✅ D-7 GO — додати dk_n до 6D вектора |
| Eagle↔Kennedy(A) sep 0.7–1.0σ | Marginal | ⚠️ D-7 CONDITIONAL — розглянути split в gen-4 DB |
| Eagle↔Kennedy(A) sep < 0.7σ | Не допомагає | ❌ D-7 HOLD — шукати інший discriminant |
| Kennedy A та B абсолютно ідентичні (sep < 0.3σ) | Немає A/B ефекту | ❌ Гіпотеза bimodal хибна — переглянути root cause |

---

## 11. Після сесії — збереження даних

### Крок 1 — Скопіювати NDJSON з SD

```
SD:\CoinTrace\discovery\session_xxxxxxxx.ndjson
  →  d:\GitHub\CoinTrace\data\sessions\session_xxxxxxxx.ndjson
```

Якщо директорія не існує:
```powershell
New-Item -ItemType Directory -Force d:\GitHub\CoinTrace\data\sessions
```

### Крок 2 — Швидка перевірка даних

```powershell
$file = "d:\GitHub\CoinTrace\data\sessions\session_xxxxxxxx.ndjson"

# Кількість вимірів per metal_code
Get-Content $file | ForEach-Object {
    ($_ | ConvertFrom-Json).metal_code
} | Group-Object | Sort-Object Count -Descending | Format-Table Name, Count
```

**Очікуваний output:**
```
Name             Count
----             -----
XKENNEDY         10     ← A×5 + B×5
XAG999_KANG      10     ← A×5 + B×5
XAG999_EAGLE     5      ← control
XAG800           5      (якщо вимірювалась)
XAU999           5      (якщо вимірювалась)
```

> Якщо XKENNEDY або XAG999_KANG показують `5` замість `10` — A/B блок не завершено.

### Крок 3 — Перевірка steps наявності

A-5 re-run потребує `steps[1]["fSensor_hz"]` для кожного запису:

```powershell
$file = "d:\GitHub\CoinTrace\data\sessions\session_xxxxxxxx.ndjson"
Get-Content $file | ForEach-Object {
    $r = $_ | ConvertFrom-Json
    $ok = ($r.steps -ne $null) -and ($r.steps.Count -ge 2) -and ($r.steps[1].fSensor_hz -gt 0)
    [PSCustomObject]@{
        metal = $r.metal_code
        steps_count = if ($r.steps) { $r.steps.Count } else { 0 }
        fs1_valid = $ok
    }
} | Format-Table
```

Всі записи мають мати `fs1_valid = True` і `steps_count = 4`.

### Крок 4 — Оновити конфіг a1_analysis.py перед re-run

У `scripts/a1_analysis.py` потрібно оновити:

```python
# 1. NDJSON_IN — вказати C-8 session file
NDJSON_IN = REPO / "docs/external/2026-0x-xx.C8.session_xxxxxxxx.ndjson"

# 2. GROUPS — додати Kennedy_A, Kennedy_B як окремі групи
GROUPS = {
    ...
    "XKENNEDY_A": {"indices": [...], ...},
    "XKENNEDY_B": {"indices": [...], ...},
    ...
}
```

> Copilot виконає ці зміни — надати `session_xxxxxxxx.ndjson` та `Session8.txt`.

### Крок 5 — Передати Copilot для A-5 re-analysis

Повідомити:
> "C-8 сесія завершена. Файл: `session_xxxxxxxx.ndjson`. Kennedy A записи: [індекси A-side]. Kennedy B записи: [індекси B-side]. Запусти A-5 re-analysis з A/B split."

---

## 12. Чеклист

### Прошивка (до підготовки обладнання)

- [ ] Визначено потрібний сценарій (A / B / C) з §0 ✅
- [ ] `pio run -e cointrace-dev -t upload` — SUCCESS ✅ (якщо firmware оновлювався)
- [ ] `pio run -e uploadfs-sys -t uploadfs` — SUCCESS ✅ (якщо JSON оновлювались в LittleFS)
- [ ] SD карта: `index.json` та `matcher.json` скопійовано через кардрідер (§0 SD секція) ✅
- [ ] Serial після прошивки: `FingerprintCache ready — 9 entries (generation 3)` ✅

### Підготовка (до сесії)

- [ ] Спейсер 2.6mm перевірено штангенциркулем → `2.55–2.65 mm` ✅
- [ ] SD карта: `ldc1101.json` містить `"tc1": 213, "tc2": 254, "rp_set": 54`
- [ ] SD карта: `index.json` — generation **3** (9 entries з df_n) ✅
- [ ] Firmware: `e9e78c1` або пізніший ✅
- [ ] `Session8.txt` відкрито та заповнено header ✅
- [ ] Монети підготовлено: Eagle, Kennedy, Kangaroo (+ XAG800/XAU999 якщо є) ✅
- [ ] Відомо яка сторона є A vs B для Kennedy та Kangaroo (§3) ✅

### Boot

- [ ] Serial: `fSENSOR = 776–784 kHz` ✅ (НЕ 909 kHz)
- [ ] Serial: `isLDataValid() = true` ✅
- [ ] Serial: `FingerprintCache ready — 9 entries (generation 3)` ✅
- [ ] Serial: `SD mounted` ✅
- [ ] Serial: `Discovery mode: enabled` ✅
- [ ] Baseline RP/L/fSENSOR записані у Session8.txt ✅

### Вимірювання

- [ ] Eagle: 5 циклів ✅ (Side A або B, однакова для всіх 5)
- [ ] **RECAL** → записати rp у Session8.txt ✅
- [ ] Kennedy **A-SIDE**: 5 циклів (портрет вниз) ✅
- [ ] **RECAL + 60с пауза** між Kennedy A та B ✅
- [ ] Kennedy **B-SIDE**: 5 циклів (реверс вниз) ✅
- [ ] **RECAL** після Kennedy ✅
- [ ] Kangaroo **A-SIDE**: 5 циклів (Queen вниз) ✅
- [ ] **RECAL + 60с пауза** між Kangaroo A та B ✅
- [ ] Kangaroo **B-SIDE**: 5 циклів (кенгуру вниз) ✅
- [ ] XAG800: 5 циклів ✅ (якщо є)
- [ ] XAU999: 5 циклів ✅ (якщо є)

### Збереження

- [ ] NDJSON скопійовано до `data/sessions/` ✅
- [ ] PowerShell verify: XKENNEDY Count=10, XAG999_KANG Count=10 ✅
- [ ] PowerShell verify: всі записи `fs1_valid = True` ✅
- [ ] Session8.txt заповнено (End time, Total meas, Max drift, NDJSON file) ✅
- [ ] Повідомити Copilot для A-5 re-analysis ✅
