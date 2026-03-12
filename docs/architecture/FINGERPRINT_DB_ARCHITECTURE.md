# Fingerprint Database Architecture — CoinTrace

**Статус:** 📐 Запроектовано, очікує імплементації  
**Версія:** 1.5.1  
**Дата:** 2026-03-13  
**Автор:** Yuriy Kachmaryk

> ⚠️ **КРИТИЧНО:** Рішення в цьому документі не можна змінити після появи першого зовнішнього contributor (першого PR від сторонньої людини). Прочитати і погодити до початку будь-якого збору даних.

---

## Зміст

1. [Проблема: чому backward compatibility тут важча ніж для API](#1-проблема)
2. [Чотири незалежних виміри поламки](#2-чотири-незалежних-виміри-поламки)
3. [Специфікація схеми v1](#3-специфікація-схеми-v1)
4. [Еволюція схеми: safe vs breaking changes](#4-еволюція-схеми-safe-vs-breaking-changes)
5. [Raw Measurements as Source of Truth](#5-raw-measurements-as-source-of-truth)
6. [Git-based community database: приховані ризики](#6-git-based-community-database-приховані-ризики)
7. [Frozen physical constants](#7-frozen-physical-constants)
8. [Архітектурні рішення (ADR)](#8-архітектурні-рішення-adr)
9. [Міграційна стратегія](#9-міграційна-стратегія)
10. [Чек-лист: до першого public release](#10-чек-лист-до-першого-public-release)

---

## 1. Проблема

Backward compatibility для REST API — добре вирішена задача: `/api/v1/` prefix, deprecation headers, 6 місяців підтримки старих endpoints. Для fingerprint database ситуація принципово інша.

**Відмінності fingerprint records від API:**

| | REST API | Fingerprint records |
|--|---------|-------------------|
| Хто контролює | Ми (сервер) | Community (тисячі людей) |
| Де зберігається | Сервер, мігрується централізовано | SD карти по всьому світу |
| Виправити помилку | Deploy нової версії | Неможливо (дані на чужих пристроях) |
| Вплив поламки | Клієнт отримав помилку | Мовчазний неправильний результат |

**Головна небезпека — мовчазна несумісність.** Якщо community-запис з `k1 = 0.714` зроблено при частоті 1 MHz, а firmware обчислює відстань до нього при 1.5 MHz — match confidence буде 0.61 (сміття), але помилки не буде. Користувач побачить "Мідь 92%" замість "Срібло 925%" без жодного попередження.

---

## 2. Чотири незалежних виміри поламки

### 2.1 Три типи версій в одному полі

Поточна схема (README.md v0) має:
```json
{ "version": 1 }
```

Це поле одночасно претендує на три різні речі — і жодна не специфікована:

| Тип | Значення | Що ламається при зміні |
|-----|----------|----------------------|
| **Schema version** | Структура JSON, назви полів | Парсер падає з KeyError / ігнорує поле |
| **Algorithm version** | Формула обчислення k1, k2 | Однакові raw вимірювання → різні числа |
| **Protocol version** | Кількість точок, відстані `[0,1,3]` мм | Математично несумісні вектори |

**Приклад колізії:** Змінюємо відстані з `[0, 1, 3]` мм на `[0, 1, 2, 4]` мм (4 точки для кращої кривої). JSON schema не змінилась — поля ті самі, `"version": 1`. Але k1 тепер = ΔRp(2mm)/ΔRp(0mm) замість ΔRp(1mm)/ΔRp(0mm). Всі 1000 записів community database порівнюватимуться з новими вимірюваннями **і мовчки давати неправильні результати**.

### 2.2 Залежність k1/k2 від фізичних умов

Математичний доказ в Prior Art Disclosure вірний: k1 і k2 незалежні від **діаметру** монети. Але вони залежать від:

| Параметр | Вплив | Де зараз зберігається |
|---------|-------|----------------------|
| Частота `f` | **Критичний** — skin depth ∼1/√f → інша форма кривої | `platformio.ini` (змінюваний!) |
| Відстані `d₁, d₂, d₃` | **Критичний** — k1 = f(d₃/d₁ ratio) | Тільки документація |
| Геометрія котушки | Помірний | Тільки BOM |
| `RESP_TIME` LDC1101 | Менший (~0.1–0.5%) | `platformio.ini` |
| Температура | Мінімальний при ±10°C | Metadata (вже є) |

**Висновок:** Два CoinTrace-пристрої з різними `SINGLE_FREQUENCY` або spacers іншої товщини дадуть несумісні k1, k2 для тієї самої монети. Поле `conditions.protocol_id` (§3.2) вирішує цю проблему однозначно: записи порівнюються лише якщо `protocol_id` ідентичний.

### 2.3 Семантична невизначеність `slope`

Поточний формат (README.md):
```json
"slope": -89.2
```

Slope чого, в яких одиницях, яким методом? Не специфіковано нічого:
- Нахил Rp(distance)? ΔRp(distance)?
- Одиниці: Ом/мм? normalized? per-step?
- Метод: linear regression по 4 точках? різниця першої та останньої?

Якщо два contributors обчислюють slope по-різному — вони пишуть несумісні числа в те саме поле v1. **Неможливо виправити без ретроактивної міграції**, бо ми не знатимемо який метод використав кожен contributor.

### 2.4 Домінування `dRp1` у 5D Euclidean distance

Поточний вектор: `[dRp1, k1, k2, slope, dL1]`

`dRp1` **не** є нормалізованим — це абсолютне значення зміни Rp. Для великої монети (39 мм, Maria Theresa Thaler) воно буде в 3–4 рази більше ніж для маленької (18 мм, 10 копійок). При Euclidean distance `dRp1` домінуватиме над k1 і k2, фактично скасовуючи size-independence.

**Правильне рішення:** або нормалізувати `dRp1` (розділити на max відоме значення), або вилучити з distance calculation і використовувати лише як discriminator між бронзою і міддю, або перейти на weighted Euclidean.

---

## 3. Специфікація схеми v1

### 3.1 Повний формат fingerprint record (v1, canonical)

```json
{
  "_comment": "CoinTrace Fingerprint Record",

  "schema_ver":   1,
  "algo_ver":     1,
  "protocol_ver": 1,

  "coin": {
    "name":           "Austrian Maria Theresa Thaler",
    "year":           1780,
    "country":        "AT",
    "denomination":   "1 Thaler",
    "diameter_mm":    39.5,
    "weight_g":       28.06,
    "metal_code":     "XAG833",
    "metal_name":     "Silver 833"
  },

  "conditions": {
    "protocol_id":    "p1_1mhz_013mm",
    "freq_hz":        1000000,
    "steps_mm":       [0, 1, 3],
    "coil_model":     "MIKROE-3240",
    "coil_turns":     null,
    "resp_time_code": 6
  },

  "raw": {
    "rp0":  10247.3,
    "rp1":  10559.7,
    "rp2":  10514.8,
    "rp3":  10430.5,
    "l0":   1204.3,
    "l1":   1186.5   // [R-02 ВІДКРИТО] теоретичне значення; |l0-l1|≈18 µH для Ag
  },

  "vector": {
    "dRp1":               312.4,
    "k1":                 0.856,
    "k2":                 0.587,
    "slope_rp_per_mm_lr": -0.137,
    "dL1":                18.0    // [R-02 ВІДКРИТО] теоретичне значення (Ag: dL1 ≈ 0–50 µH)
  },

  "metadata": {
    "device_id":          "CoinTrace-A1B2",
    "fw_version":         "1.0.0",
    "temp_c":             23.4,
    "measured_at":        "2026-03-21T14:23:45Z",
    "contributor":        "ykachmaryk",
    "reference_quality":  "believed_authentic",
    "mint_certificate":   "",
    "status":             "published",
    "notes":              ""
  }
}
```

### 3.2 Семантика кожного поля

#### Версійні поля

| Поле | Тип | Зміна при |
|------|-----|-----------|
| `schema_ver` | int | Зміна структури JSON (назви, типи, обов'язковість полів) |
| `algo_ver` | int | Зміна формули обчислення vector з raw |
| `protocol_ver` | int | Зміна JSON-структури блоку `conditions` (назви чи типи полів) |

Firmware оголошує `MIN_SUPPORTED_*_VER` для кожної версії. Record несумісний якщо хоча б одна версія нижча за мінімум.

> **Важливо:** Додавання нової конфігурації вимірювання (нова частота або нові відстані) **не** підіймає `protocol_ver`. Натомість реєструється новий `protocol_id` в `protocols/registry.json`. `protocol_ver` зростає лише при зміні JSON-структури блоку `conditions`.

#### `coin.metal_code`

ISO-подібний код металу:

| Код | Метал |
|-----|-------|
| `XAG999` | Silver 999 (fine) |
| `XAG925` | Silver 925 (sterling) |
| `XAG900` | Silver 900 (coin silver) |
| `XAG833` | Silver 833 |
| `XAU999` | Gold 999 |
| `XAU585` | Gold 585 (14k) |
| `XCU` | Copper |
| `XAL` | Aluminum |
| `XSTL_MAG` | Steel (magnetic) |
| `XSTL_NM` | Steel (non-magnetic) |
| `XNI` | Nickel |
| `XZN` | Zinc / tombac |

**Чому код + name:** `metal_code` — стабільний ідентифікатор для коду. `metal_name` — людиночитаємий, може мати локалізацію. Код не змінюється, name — може.

#### `conditions` — ключ сумісності

**Два fingerprint records можна порівнювати тільки якщо `conditions` ідентичні.** Це не опціональні метадані.

- `protocol_id` — string. Immutable ідентифікатор конфігурації вимірювання. Формат: `p{N}_{freq}_{steps}mm`, наприклад `"p1_1mhz_013mm"`. Два записи порівнянні **тільки** якщо `protocol_id` ідентичний. Нову конфігурацію = новий `protocol_id` в `protocols/registry.json`; `protocol_ver` при цьому не зростає.
- `freq_hz` — integer Hz. Не "1MHz", не 1000, не 1000000.0 — тільки `1000000`.
- `steps_mm` — масив integer мм. `[0, 1, 3]` = три виміри: без спейсера, 1 мм, 3 мм.
- `coil_model` — string. `"MIKROE-3240"` для LDC1101 Click Board. Якщо саморобна котушка — `"custom-v1"` + `"coil_turns"`.
- `resp_time_code` — integer 0–7, відповідає RESP_TIME bits в LDC1101 config регістрі (таблиця `code → час конверсії`: `LDC1101_ARCHITECTURE.md §RESP_TIME`; значення за замовчуванням = 6 ≈ 4 мс, баланс шум/швидкість).

#### `raw` — source of truth

Значення безпосередньо з LDC1101, після RP_SET scaling, до будь-яких обчислень:
- `rp0..rp3` — Rp в Ohm на кожній відстані зі `steps_mm` + baseline (rp0 = baseline без монети)
- `l0, l1` — L в µH: baseline і при 0 мм

> **Семантика `rp0`/`l0` — абсолютні значення, не дельти.** `rp0` і `l0` — абсолютні показники LDC1101 RP_DATA / L_DATA, виміряні **без монети**. Для MIKROE-3240 типово: `rp0` ≈ 5000–15000 Ohm, `l0` ≈ 500–2000 µH. `rp1`/`rp2`/`rp3` і `l1` — абсолютні показники **з монетою**. Зберігання абсолютних значень дозволяє діагностику та крос-девайс порівняння.

**Всі значення float32 (JSON number з 4 знаками після коми).**

#### `vector` — derived, не редагувати вручну

Обчислюється з `raw` за алгоритмом версії `algo_ver`:

```
algo_ver = 1:
  dRp1             = rp1 - rp0
  dRp2             = rp2 - rp0
  dRp3             = rp3 - rp0
  k1               = dRp2 / dRp1        (якщо dRp1 ≠ 0)
  k2               = dRp3 / dRp1        (якщо dRp1 ≠ 0)
  slope_rp_per_mm_lr = linear_regression_slope([0, 1, 3], [1.0, k1, k2])
                       (нахил норм. кривої ΔRp/ΔRp1 по 3 точках, Y(0mm)=1.0)
  dL1              = l1 - l0
```

> **Поведінка firmware при `dRp1 ≤ 0` (FDB-06):** Якщо виміряний `dRp1 ≤ 0` (монета відсутня або непровідний матеріал), firmware:
> - Не обчислює k1/k2 (уникає ділення на нуль)
> - Повертає `confidence = 0.0`, `match = null`, error code `"NO_RESPONSE"`
> - Не записує `"complete": true` в `m_XXX.json` → вимір вважається failed та відкидається

**Зверніть увагу:** `slope_rp_per_mm_lr` — Variant C, 3 точки:
- вісь X: відстані зі `steps_mm` в мм: `[0, 1, 3]`
- вісь Y: нормалізований ΔRp/ΔRp1: `[1.0, k1, k2]` — Y(0mm) = **1.0**, не 0
- метод: linear regression (least squares) по всіх трьох точках
- одиниці: `1/мм` (безрозмірне на мм)
- фізичні межі: slope ∈ (−0.30, −0.01) для всіх реальних металів

#### `metadata` — поля якості та контексту

- `reference_quality` — рівень достовірності джерела монети:

  | Значення | Коли використовувати |
  |---|---|
  | `"certified"` | Державний монетний двір з документом (NBU, Royal Mint, US Mint) |
  | `"believed_authentic"` | Авторитетний нумізматичний дилер або особиста впевненість |
  | `"unknown"` | За замовчуванням, якщо не вказано |

- `mint_certificate` — ідентифікатор сертифіката монетного двору (якщо є). Порожній рядок якщо відсутній.

- `status` — lifecycle стан запису:

  | Значення | Де зберігається | CI дозволяє PR в `samples/`? |
  |---|---|---|
  | `"draft"` | Тільки локально на SD (невідома монета, незавершений запис) | ❌ Ні |
  | `"reviewed"` | Локально, перевірено власником, готовий до submission | ✅ Так |
  | `"published"` | Прийнятий в community database через merged PR | ✅ Так |

  За замовчуванням (якщо поле відсутнє): `"reviewed"`. Draft records зберігаються виключно локально — CI блокує будь-який PR де хоча б один record має `"status": "draft"`.

> **Важливо:** Перший запис кластера XAG999 ПОВИНЕН мати `reference_quality: "certified"`, бо він є точкою відліку для всього кластера. Зміщений anchor = зміщені всі порівняння.

### 3.3 Метрика відстані для ідентифікації

Поточна в README: рівноважна Euclidean по всіх 5 координатах — математично некоректна (§2.4).

**Рекомендована для v1 — weighted Euclidean:**

```
dist = √(w₁·(dRp1_n - ref_dRp1_n)² + w₂·(k1-ref_k1)² + w₃·(k2-ref_k2)²
          + w₄·(slope-ref_slope)² + w₅·(dL1_n-ref_dL1_n)²)

де:
  dRp1_n  = dRp1 / dRp1_MAX   (нормалізований до [0..1], dRp1_MAX = 800 Ohm — §3.3)
  dL1_n   = dL1 / dL1_MAX     (нормалізований аналогічно)
  w₁..w₅  = ваги, підбираються емпірично на validation set
```

**Альтернатива для v1.1 — Mahalanobis distance**, якщо з'явиться достатньо записів для оцінки covariance matrix per-class. Не для v1.

> **`dRp1_MAX` та `dL1_MAX` — звідки ці числа:**
> - `dRp1_MAX = 800 Ohm` — верхня межа BOUNDS для будь-якого металу (MIKROE-3240 при 1 MHz); узгоджено з `BOUNDS dRp1 (10.0, 800.0)` у §6.1. Уточнюється після validation set (**R-01**).
> - `dL1_MAX = 2000 µH` — верхня межа ΔL для феромагнітного матеріалу (сталь, нікель). Кольорові метали (Ag, Cu, Au, Al): ΔL ≈ 0–50 µH. Магнітні (Fe, Ni): 200–2000 µH. Уточнюється аналогічно.

---

### 3.4 Confidence transform: відстань → відсоток

**Функція для відображення на екрані та BLE RESULT характеристиці (§5.3 CONNECTIVITY_ARCHITECTURE.md):**

```
confidence = exp(−dist² / σ²)   [0.0 .. 1.0]

де:
  dist = weighted Euclidean з §3.3
  σ    = radius_95pct / √(−ln(0.05))  ≈  radius_95pct / 1.732
```

`radius_95pct` береться з `_aggregate.json` кластера. Якщо `_aggregate.json` відсутній (перший запис кластера) — σ = **0.15** як bootstrap значення.

| σ | confidence при dist=0 | confidence при dist=0.1 | confidence при dist=0.2 |
|---|---|---|---|
| 0.05 | 100% | 2% | ~0% |
| **0.15** | **100%** | **64%** | **17%** |
| 0.30 | 100% | 90% | 64% |

**Sigma = 0.15 — рекомендований старт** до накопичення validation set. Дозволяє розрізняти близькі метали (Ag999 vs Ag925) без надмірного розширення зони прийняття.

**Порогові значення для відображення:**

| confidence | Колір | Напис |
|---|---|---|
| ≥ 0.90 | 🟢 Зелений | "Висока схожість" |
| 0.70–0.89 | 🟡 Жовтий | "Середня схожість" |
| < 0.70 | 🔴 Червоний | "Слабка схожість / підозра" |

> Ці пороги — початкові. Уточнюються після validation set. Не кодувати як константи — виносити в конфігурацію firmware.

---

## 4. Еволюція схеми: safe vs breaking changes

### 4.1 Безпечні зміни (backward compatible)

Можна робити **не підіймаючи** `schema_ver`:

```jsonc
// ✅ Добавлення опціонального поля — старі readers ігнорують
"conditions": {
  "freq_hz": 1000000,
  "steps_mm": [0, 1, 3],
  "humidity_pct": 45         // ← нове опціональне поле
}

// ✅ Новий метал в довіднику metal_code
// ✅ Новий запис монети в database (незалежний файл)
// ✅ Зміна metadata.notes
```

### 4.2 Breaking changes — вимагають підняти версію

| Зміна | Який лічильник | Дія |
|-------|---------------|-----|
| Перейменування поля | `schema_ver` + 1 | Міграційний скрипт |
| Зміна типу поля | `schema_ver` + 1 | Міграційний скрипт |
| Нова обов'язкова поля | `schema_ver` + 1 | Дефолт для старих records |
| Зміна формули k1/k2 | `algo_ver` + 1 | Перерахунок з `raw` |
| Нова конфігурація вимірювання (нові freq або steps) | Новий `protocol_id` | Нова папка `samples/{protocol_id}/` |
| Зміна JSON-структури блоку `conditions` (поля, типи) | `protocol_ver` + 1 | Міграційний скрипт |

### 4.3 Особливо небезпечна зміна: реструктуризація вектора

```jsonc
// v1 — масив key-value:
"vector": { "dRp1": 312.4, "k1": 0.856, ... }

// Потенційна "оптимізація" — масив:
"vector": [312.4, 0.856, 0.587, -21.7, 724.1]   // ← НІКОЛИ НЕ РОБИТИ
```

Масив без імен — порядок елементів стає частиною формату. Одна транспозиція при генерації — мовчазні неправильні результати для всієї бази.

---

## 5. Raw Measurements as Source of Truth

### 5.1 Чому raw — ключ до будь-якої міграції

Маючи `raw` + `conditions` + нову версію алгоритму:

```python
# migrate_algo_v1_to_v2.py
def migrate(record):
    if record["algo_ver"] != 1:
        return record  # вже мігровано або несумісна версія
    
    raw = record["raw"]
    steps = record["conditions"]["steps_mm"]
    
    # Перерахунок за algo_ver=2
    dRp = [raw[f"rp{i}"] - raw["rp0"] for i in range(1, len(steps))]
    # ... новий алгоритм ...
    
    record["vector"] = new_vector
    record["algo_ver"] = 2
    return record
```

**Без `raw` такий скрипт неможливо написати.** Треба б просити всіх contributors перевиміряти монети.

### 5.2 Розмірний аналіз

100 записів у базі, `sizeof` кожного JSON:

| Секція | Розмір |
|--------|--------|
| `coin` metadata | ~200 байт |
| `conditions` | ~100 байт |
| `raw` (8 float) | ~120 байт |
| `vector` (5 float) | ~100 байт |
| `metadata` | ~150 байт |
| **Разом** | **~670 байт** |

100 монет = ~67 KB. microSD на Cardputer: 32 GB. Розмір не є обмеженням.

### 5.3 `raw` в BLE пакеті — не передавати

BLE характеристика `RESULT` (20 байт) містить тільки `vector` + match + confidence. `raw` не передається через BLE — залишається тільки в SD card записі. Це коректно: raw потрібне тільки для міграції та відтворюваності, не для live identification.

---

## 6. Git-based community database: приховані ризики

### 6.1 Відсутня валідація при merge

Contributor робить PR з `"k1": 7.14` (десятковий роздільник — кома в Excel перетворилась на відсутність крапки). Без автоматичної валідації — мовчки потрапляє в базу. Усі наступні вимірювання Silver 925 дають confidence 0.3 без очевидної причини.

**Рішення: GitHub Actions CI на кожен PR:**

```yaml
# .github/workflows/validate-fingerprints.yml
on:
  pull_request:
    paths: ['database/**/*.json']

jobs:
  validate:
    steps:
      - run: python tools/validate_fingerprint.py database/
```

```python
# tools/validate_fingerprint.py
# Примітка: BOUNDS перевіряє абсолютні значення `vector` (Ohm, µH) —
# не нормалізовані координати centroid з `index.json` (де dRp1_n = dRp1/600).
BOUNDS = {
    "k1":   (0.3, 0.99),   # фізичні межі для будь-якого металу
    "k2":   (0.1, 0.98),
    "dRp1": (10.0, 800.0), # Ohm — абсолютне значення з vector
    "dL1":  (0.0, 2000.0), # µH  — абсолютне значення з vector
    "slope_rp_per_mm_lr": (-0.35, -0.01),  # LR по Y=[1.0,k1,k2] vs X=[0,1,3]; фіз. межі ≈(−0.30,−0.02)
}

def validate(record):
    errors = []
    for field, (lo, hi) in BOUNDS.items():
        val = record["vector"][field]
        if not (lo <= val <= hi):
            errors.append(f"{field}={val} out of range [{lo},{hi}]")
    # [ADR-DB-003] Auto-recompute vector from raw та порівняти зі збереженим у vector:
    if "raw" in record and "conditions" in record:
        recomputed = compute_vector(record["raw"], record["conditions"])
        for field in ("dRp1", "k1", "k2", "slope_rp_per_mm_lr", "dL1"):
            delta = abs(recomputed.get(field, 0) - record["vector"].get(field, 0))
            if delta > 0.001:
                errors.append(f"{field}: stored={record['vector'][field]:.4f}, recomputed={recomputed[field]:.4f}")
    # + JSON schema validation via jsonschema lib
    return errors
```

### 6.2 Дублікати з дрейфом — статистична агрегація

Три contributors вимірюють Austrian Thaler → три записи:
```
record_1: k1=0.712, k2=0.385
record_2: k1=0.714, k2=0.388
record_3: k1=0.719, k2=0.383
```

**Варіанти обробки:**

| Стратегія | Плюс | Мінус |
|-----------|------|-------|
| Зберігати всі, порівнювати з кожним | Повна інформація | N×пошук, RAM |
| Canonical = перший запис | Простота | Перший міг бути гіршим |
| Mean vector | Стійкіший центроїд | Втрачаємо outlier-detection |
| Centroid + radius (statistical) | Detects outlier measurements | Складна імплементація |

**Рекомендація для v1:** Зберігати всі + генерувати `_aggregate.json` через CI:
```json
{
  "coin_name": "Austrian Maria Theresa Thaler",
  "records_count": 3,
  "centroid": {"dRp1": 308.5, "k1": 0.715, "k2": 0.385, ...},
  "std_dev":  {"dRp1": 2.1,   "k1": 0.003, "k2": 0.002, ...},
  "radius_95pct": 0.012
}
```

> **Одиниці в `_aggregate.json`:** `centroid.dRp1` і `std_dev.dRp1` — **абсолютні (Ohm)**; `centroid.dL1` — **абсолютні (µH)**. `k1`, `k2`, `slope` — безрозмірні. При генерації `index.json` CI нормалізує `dRp1` і `dL1` (→ `dRp1_n`, `dL1_n`). Два файли навмисно мають різні одиниці — `_aggregate.json` для CI-аналітики, `index.json` для firmware RAM-пошуку.
```

Firmware порівнює з centroid, використовує radius для confidence calibration.

**Алгоритм обчислення centroid (в CI `tools/build_aggregates.py`):**

```python
# Допоміжна функція відстані — рівноважна Euclidean на нормалізованих координатах
# (Діє до отримання validation set R-01 і підбору w₁–w₅; §3.3)
DRPL1_MAX = 800.0   # Ohm  — dRp1_MAX з §3.3
DL1_MAX   = 2000.0  # µH   — dL1_MAX з §3.3

def weighted_euclidean(v1, v2):
    """Нормалізована Euclidean по 5 компонентах. Замінити на weighted після R-01."""
    def nrm(v):
        return (v["dRp1"] / DRPL1_MAX, v["k1"], v["k2"],
                v["slope_rp_per_mm_lr"], v["dL1"] / DL1_MAX)
    return sum((a - b) ** 2 for a, b in zip(nrm(v1), nrm(v2))) ** 0.5


def build_aggregate(records):
    published = [r for r in records if r["metadata"].get("status") == "published"]
    vecs = [r["vector"] for r in published]

    # Зважений centroid: certified = вага 3.0, інші = 1.0
    weights = [3.0 if r["metadata"]["reference_quality"] == "certified" else 1.0
               for r in published]
    centroid = weighted_mean(vecs, weights)

    # Outlier trim (тільки якщо ≥ 5 записів): відкинути top/bottom 10%
    if len(vecs) >= 5:
        distances = [weighted_euclidean(v, centroid) for v in vecs]
        lo, hi = percentile(distances, 10), percentile(distances, 90)
        # Зберігаємо відповідність vecs↔weights після trim (FIX P-04: weights[:N] брав хибні ваги що вижили)
        pairs = [(v, w) for v, w, d in zip(vecs, weights, distances) if lo <= d <= hi]
        vecs, weights = zip(*pairs) if pairs else (list(vecs), list(weights))
        centroid = weighted_mean(vecs, weights)  # recompute after trim

    # radius_95pct = 95-й перцентиль відстаней від centroid
    distances = [weighted_euclidean(v, centroid) for v in vecs]
    return {"centroid": centroid, "std_dev": std_dev(vecs),
            "radius_95pct": percentile(distances, 95),
            "records_count": len(vecs)}
```

**Поведінка firmware при відсутньому `_aggregate.json`** (перший contributor в папці):
1. Firmware читає всі `*.json` у папці (до `MAX_RECORDS_PER_COIN = 10` — захист від OOM; константа оголошується в `src/config/fingerprint_config.h`)
2. Обчислює centroid in-memory без ваг (спрощений шлях, одноразово)
3. Використовує `σ = 0.15` bootstrap значення (§3.4) для confidence
4. Результат не зберігається на SD — CI генерує `_aggregate.json` після merge PR

### 6.3 Index staleness та merge conflicts

`index.json` що перераховує всі монети → при кожному PR оновлюється → merge conflicts між паралельними PR.

**Рішення: автогенерований index, не комітити вручну:**
```yaml
# CI: після merge → rebuild index
- name: Rebuild index
  run: python tools/build_index.py database/ > database/index.json  # інкрементує "generation" += 1
  
- name: Commit updated index
  run: |
    git config user.name "CoinTrace Bot"
    git add database/index.json
    git commit -m "chore: rebuild fingerprint index [skip ci]"
```

**Формат `index.json` — оптимізований для in-memory пошуку на ESP32-S3:**

```json
{
  "version": 1,
  "generated_at": "2026-03-11T14:00:00Z",
  "generation":   42,
  "protocols": ["p1_1mhz_013mm"],
  "entries": [
    {
      "id":             "xag925/at_thaler_1780",
      "protocol_id":    "p1_1mhz_013mm",
      "metal_code":     "XAG925",
      "coin_name":      "Austrian Maria Theresa Thaler",
      "year":           1780,
      "centroid":       {"dRp1_n": 0.390, "k1": 0.715, "k2": 0.385,
                         "slope": -0.128, "dL1_n": 0.005},
      "radius_95pct":   0.012,
      "records_count":  3
    }
  ]
}
```

> **`generation`** (uint32, optional, default=0) — монотонний лічильник, інкрементується `build_index.py` при кожному rebuild. Firmware зберігає в `/data/cache/sd_generation.bin` (4B LE uint32). Boot [7a]: порівняти `generation` SD index із кешованим → якщо відрізняється → кеш застарів → rebuild. `generated_at` залишається для human-readable аудиту, не для machine comparison. Поле backward-compatible: відсутнє = 0.

**RAM бюджет:** `centroid` (5 float) + `radius_95pct` (1 float) + metadata (~60 байт) ≈ **80 байт/запис**.
- 1000 монет × 80 байт = **80 KB** — безпечно для 337 KB RAM ESP32-S3.
- `aggregate_path` в index не зберігається — шлях будується динамічно: `samples/{protocol_id}/{metal_code}/{id}/_aggregate.json`.

> **Правило нормалізації при генерації `index.json` (P-03 — обов'язково для `build_index.py`):**
> CI-скрипт переводить абсолютні значення з `_aggregate.json` (де `centroid.dRp1` — в Ohm) в нормалізовані:
> - `dRp1_n = centroid.dRp1 / 800`
> - `dL1_n  = centroid.dL1  / 2000`
>
> де `800` і `2000` — константи `DRPL1_MAX` / `DL1_MAX` з `src/config/fingerprint_config.h` (відповідають `dRp1_MAX` і `dL1_MAX` з §3.3).
> Firmware нормалізує новий вимір аналогічно **перед** порівнянням з `index.json`. Порушення цього правила призведе до того, що `dRp1` (0–600 range) домінує над `k1`, `k2` (0–1 range) і пошук стане некоректним.

**Двофазний пошук:**
1. Фаза 1 (RAM): порівнення нового вектора з усіма centroid-ами → топ-10 кандидатів за weighted Euclidean
2. Фаза 2 (SD): читання `_aggregate.json` тільки для топ-10 → уточнення confidence з radius_95pct. **[SPI-2]** `FingerprintDB::loadAggregate()` захоплює `spi_vspi_mutex` (таймаут 50 мс) перед SD read. `portMAX_DELAY` заборонено. НЕ викликає `Logger::*` під час SD IO (запобігає deadlock з `SDTransport`).

### 6.4 Структура директорій database

```
database/
  README.md                    ← інструкції для contributors
  schema/
    fingerprint_v1.json        ← JSON Schema (jsonschema compatible)
  protocols/
    registry.json              ← реєстр protocol_id: параметри та статус (canonical/experimental)
  samples/
    p1_1mhz_013mm/             ← canonical; ключ = protocol_id
      XAG925/
        at_thaler_1780_001.json   ← іменування: {metal}_{country}_{name}_{serial}
        at_thaler_1780_002.json
        _aggregate.json           ← auto-generated, не редагувати
      XAG999/
        nbu_ag999_001.json        ← reference_quality: "certified"
      XAU585/
      XCU/
    p2_500khz_013mm/            ← паралельний protocol, не замінює p1
      XAG999/
    experimental/               ← нові protocol_id до підтвердження canonical
  index.json                   ← auto-generated, не редагувати
  CHANGELOG.md                 ← записи про schema_ver bumps
```

---

## 7. Frozen physical constants

Ці константи заморожуються **per `protocol_id`**: для `p1_1mhz_013mm` їх значення незмінні назавжди. Змінити або поекспериментувати = зареєструвати новий `protocol_id` + власна папка в `database/samples/`. `protocol_ver` при цьому **не зростає**.

| Константа | Поточне значення | де зараз | Ризик |
|-----------|-----------------|---------|-------|
| `SINGLE_FREQUENCY` | 1 000 000 Hz | `platformio.ini` | Будь-який розробник може змінити |
| Measurement distances | [0, 1, 3] мм | Тільки документація | Spacer буде 3D-printed ≠ точно 3.000 мм |
| `RESP_TIME` default | code 6 | `platformio.ini` | Впливає на noise/speed tradeoff |
| Котушка | MIKROE-3240 | BOM | Заміна на аналог → інша геометрія |

**Конкретний ризик `SINGLE_FREQUENCY`:** Якщо розробник-contributor змінює `SINGLE_FREQUENCY` на 500 000 Hz для кращої глибини проникнення, а потім робить PR з fingerprints — база отримує несумісні records без жодного попередження.

**Митигація:**
1. Перенести `SINGLE_FREQUENCY` і `DISTANCE_STEPS_MM` з `platformio.ini` в `config/measurement_protocol_v1.h` з коментарем `// DO NOT CHANGE — frozen for protocol_id p1_1mhz_013mm`
2. `conditions.protocol_id` в кожному record — пряма перевірка без порівняння числових полів поодинці
3. CI validation: якщо `protocol_id` відсутній у `protocols/registry.json` зі статусом `"canonical"` → record іде в `experimental/` (warning, не error)

---

## 8. Архітектурні рішення (ADR)

---

### ADR-DB-001: Три незалежних версійних поля

**Статус:** ✅ Прийнято  
**Рішення:** Замінити `"version": 1` на `"schema_ver": 1`, `"algo_ver": 1`, `"protocol_ver": 1`.  
**Причина:** Три різних типи змін з різними наслідками і різними міграційними стратегіями. Єдине поле не дозволяє відрізнити safe schema зміни від breaking protocol зміни.  
**Breaking:** Відносно поточного формату в README.md — так. Виконати до першого публічного release.

---

### ADR-DB-002: `conditions` — обов'язкове поле, не metadata

**Статус:** ✅ Прийнято  
**Рішення:** `conditions.freq_hz`, `conditions.steps_mm`, `conditions.coil_model` — обов'язкові поля, перевіряються при match. Два записи з різними `conditions` — несумісні.  
**Причина:** k1 і k2 математично залежать від цих параметрів. Без них сумісність гарантувати неможливо.

---

### ADR-DB-003: `raw` секція обов'язкова для community submissions

**Статус:** ✅ Прийнято  
**Рішення:** Кожен community record ПОВИНЕН містити `raw` (rp0..rp3, l0, l1). `vector` обчислюється автоматично — contributors не редагують вручну.  
**Причина:** Raw дані — єдина основа для майбутніх міграційних скриптів. `vector` без `raw` → dead end при зміні `algo_ver`.  
**Наслідок:** CI скрипт при PR: якщо є `raw` → автоматично перераховує і верифікує `vector`. Якщо відхилення > 0.1% → помилка CI.

---

### ADR-DB-004: `slope` → `slope_rp_per_mm_lr` з точною семантикою

**Статус:** ✅ Прийнято  
**Рішення:** Поле перейменовано. Нова назва кодує семантику:
- `rp` — нахил нормалізованої RP кривої (не абсолютної)
- `per_mm` — одиниці: 1/мм
- `lr` — метод: linear regression (least squares)  

**Точна формула (Variant C — 3 точки, фізично коректна):**
```
X = [0, 1, 3]              // steps_mm
Y = [1.0, k1, k2]          // норм. крива: Y(0mm)=1.0, Y(1mm)=k1, Y(3mm)=k2
N = 3
slope = (N·Σ(XY) - ΣX·ΣY) / (N·Σ(X²) - (ΣX)²)  // standard LR (least squares)

// Приклад Ag999: X=[0,1,3], Y=[1.0, 0.856, 0.587]
// → slope ≈ −0.137 (1/мм)
```

---

### ADR-DB-005: Weighted Euclidean distance замість рівноважної

**Статус:** ✅ Прийнято (формула підлягає уточненню на реальних даних)  
**Рішення:** `dRp1` і `dL1` нормалізуються до [0..1] перед distance calculation.  
**Причина:** `dRp1` залежить від розміру монети (абсолютний, не нормалізований) → домінує в Euclidean → скасовує size-independence яку дають k1/k2.  
**Уточнення:** Ваги w₁..w₅ підбираються емпірично після накопичення validation set з відомими монетами.

---

### ADR-DB-006: CI validation для всіх PR в database/

**Статус:** ✅ Прийнято  
**Рішення:** GitHub Actions на кожен PR що зачіпає `database/**` → `validate_fingerprint.py` → bounds check + JSON schema + auto-recompute vector з raw.  
**Причина:** Без автоматичної валідації людські помилки (неправильні одиниці, decimal separator) потрапляють в базу і тихо псують confidence для всіх користувачів.

---

### ADR-DB-007: `protocol_id` як immutable named variant конфігурації

**Статус:** ✅ Прийнято  
**Рішення:** Відокремити «яка фізична конфігурація вимірювання» (`protocol_id`) від «яка JSON-структура conditions» (`protocol_ver`). Кожна нова комбінація `freq_hz`+`steps_mm`+`coil_model` отримує унікальний `protocol_id`, зареєстрований у `protocols/registry.json`. `protocol_ver` зростає виключно при зміні JSON-структури блоку `conditions`.  
**Причина:** Лінійний `protocol_ver` змушує кожну нову конфігурацію «застарівати» попередню. Але дослідник хоче збирати дані при 1 MHz і 500 kHz **паралельно** для порівняння — без будь-якого breaking change. `protocol_id` дає це: нова конфігурація = новий рядок у `registry.json` + нова папка `samples/{protocol_id}/`. Всі попередні дані незмінні.  
**Формат:** `p{N}_{freq_abbr}_{steps}mm`, наприклад `p1_1mhz_013mm`, `p2_500khz_013mm`.  
**Breaking:** Нове обов'язкове поле `conditions.protocol_id` вимагає `schema_ver` + 1 при впровадженні (до першого public release — не breaking для зовнішніх contributors).

---

## 9. Міграційна стратегія

### 9.1 При підняті `algo_ver`

**Тригер:** Покращена формула обчислення k1/k2/slope.

```python
# tools/migrate_algo.py --from 1 --to 2
import glob, json

for path in glob.glob("database/samples/**/*.json", recursive=True):
    with open(path) as f:
        record = json.load(f)
    
    if record["algo_ver"] != 1:
        continue
    
    # Перерахунок: маємо record["raw"] + record["conditions"]
    raw = record["raw"]
    new_vector = compute_vector_v2(raw, record["conditions"])
    
    record["vector"] = new_vector
    record["algo_ver"] = 2
    
    with open(path, "w") as f:
        json.dump(record, f, indent=2)
```

**Процедура:**
1. `git checkout -b algo-v2-migration`
2. `python tools/migrate_algo.py --from 1 --to 2`
3. Верифікація: `python tools/validate_fingerprint.py database/ --strict`
4. PR + code review → merge

### 9.2 При новій конфігурації вимірювання (нові `freq_hz` або `steps_mm`)

**Тригер:** Бажання поекспериментувати з іншою частотою або відстанями.

**НЕ мігруємо і не підіймаємо `protocol_ver`** — реєструємо новий `protocol_id`:

1. Додати запис у `protocols/registry.json` зі статусом `"experimental"`.
2. Виміряти монети → зберегти у `database/samples/{new_protocol_id}/`.
3. Порівняти якість кластерів двох протоколів на тих самих монетах.
4. Якщо новий протокол кращий → змінити статус на `"canonical"`.
5. Старий `p1_*` залишається без змін — записи не видаляти.

Firmware вибирає папку за `conditions.protocol_id` запису.

**При підняті `protocol_ver`** (зміна JSON-структури блоку `conditions`):

Міграційний скрипт + backward compat читання старих `protocol_ver` у firmware. Структура папок не змінюється — `protocol_id` залишається незмінним.

### 9.3 При підняті `schema_ver`

**Тригер:** Зміна назв полів, нові обов'язкові поля.

Міграційний скрипт + backward compat читання старих schema_ver у firmware на N версій.

---

## 10. Чек-лист: до першого public release

Це **блокуючий чек-лист**. Виконати до моменту коли хоча б один чужий contributor може подати PR з fingerprint.

- [ ] **DB-01** Оновити схему в README.md: `"version": 1` → три поля
- [ ] **DB-02** Написати `database/schema/fingerprint_v1.json` (JSON Schema draft-07)
- [ ] **DB-03** Написати `tools/validate_fingerprint.py` з bounds + schema validation
- [ ] **DB-04** Написати `.github/workflows/validate-fingerprints.yml`
- [ ] **DB-05** Перенести `SINGLE_FREQUENCY` і `DISTANCE_STEPS_MM` в `config/measurement_protocol_v1.h` (коментар: `// DO NOT CHANGE — frozen for protocol_id p1_1mhz_013mm`)
- [ ] **DB-06** Написати `tools/compute_vector.py` — canonical алгоритм (reference implementation)
- [ ] **DB-07** Написати перший запис-зразок `database/samples/p1_1mhz_013mm/XAG999/nbu_ag999_001.json` — з certified монети (NBU/Royal Mint), `reference_quality: "certified"`
- [ ] **DB-08** Верифікувати що `compute_vector.py` відтворює вектор з raw для зразка
- [ ] **DB-09** Налаштувати автогенерацію `database/index.json` через CI
- [ ] **DB-10** Задокументувати `CONTRIBUTOR_GUIDE.md` — як вимірювати і подавати fingerprint
- [ ] **DB-11** Написати `database/protocols/registry.json` з першим записом `p1_1mhz_013mm`, статус `"canonical"`
- [ ] **DB-12** Додати `metadata.status` в JSON Schema (DB-02) + CI правило: `status == "draft"` → блокувати PR в `samples/`
- [ ] **DB-13** Специфікувати tolerance spacerів ≤ ±0.05 мм у `CONTRIBUTOR_GUIDE.md` (DB-10): метод верифікації — штангенциркуль + reference вимір відомої монети перед першим submission

---

## Зв'язок з іншими документами

| Документ | Зв'язок |
|---------|--------|
| [CONNECTIVITY_ARCHITECTURE.md](./CONNECTIVITY_ARCHITECTURE.md) | HTTP API `/api/v1/database/match` використовує цей формат vector |
| [LDC1101_ARCHITECTURE.md](./LDC1101_ARCHITECTURE.md) | `raw.rp0..rp3` = вихід LDC1101Plugin |
| [PLUGIN_CONTRACT.md](./PLUGIN_CONTRACT.md) | `ISensorPlugin::getLastReading()` → джерело raw даних |
| [docs/concept/prior_art_disclosure.md](../concept/prior_art_disclosure.md) | Математичне обґрунтування k1/k2 size-independence |

---

*Версія документа: 1.5.0 — 2026-03-12*  
*Версія 1.5.0 — [FDB-02] §3.1 canonical: l1=1186.5, dL1=18.0 µH [R-02 теоретичні значення]; [FDB-03] dRp1_MAX 600→800 Ohm (Variant M), DRPL1_MAX=800, норм. /800; [FDB-04] validate_fingerprint.py ADR-DB-003 auto-recompute block; [FDB-05] index.json "generation" counter (Variant G), note + CI; [FDB-06] dRp1≤0 firmware behavior spec.*
