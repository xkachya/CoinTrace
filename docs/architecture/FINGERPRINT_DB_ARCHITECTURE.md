# Fingerprint Database Architecture — CoinTrace

**Статус:** 📐 Запроектовано, очікує імплементації  
**Версія:** 1.0.0  
**Дата:** 2026-03-11  
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

**Висновок:** Два CoinTrace-пристрої з різними `SINGLE_FREQUENCY` або spacers іншої товщини дадуть несумісні k1, k2 для тієї самої монети. Їхні записи в community database будуть виглядати ідентично структурно — і будуть nonsense при порівнянні.

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
    "freq_hz":        1000000,
    "steps_mm":       [0, 1, 3],
    "coil_model":     "MIKROE-3240",
    "coil_turns":     null,
    "resp_time_code": 6
  },

  "raw": {
    "rp0":  0.0,
    "rp1":  312.4,
    "rp2":  267.5,
    "rp3":  183.2,
    "l0":   0.0,
    "l1":   724.1
  },

  "vector": {
    "dRp1":               312.4,
    "k1":                 0.856,
    "k2":                 0.587,
    "slope_rp_per_mm_lr": -21.7,
    "dL1":                724.1
  },

  "metadata": {
    "device_id":     "CoinTrace-A1B2",
    "fw_version":    "1.0.0",
    "temp_c":        23.4,
    "measured_at":   "2026-03-21T14:23:45Z",
    "contributor":   "ykachmaryk",
    "notes":         ""
  }
}
```

### 3.2 Семантика кожного поля

#### Версійні поля

| Поле | Тип | Зміна при |
|------|-----|-----------|
| `schema_ver` | int | Зміна структури JSON (назви, типи, обов'язковість полів) |
| `algo_ver` | int | Зміна формули обчислення vector з raw |
| `protocol_ver` | int | Зміна кількості точок або відстаней вимірювання |

Firmware оголошує `MIN_SUPPORTED_*_VER` для кожної версії. Record несумісний якщо хоча б одна версія нижча за мінімум.

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

- `freq_hz` — integer Hz. Не "1MHz", не 1000, не 1000000.0 — тільки `1000000`.
- `steps_mm` — масив integer мм. `[0, 1, 3]` = три виміри: без спейсера, 1 мм, 3 мм.
- `coil_model` — string. `"MIKROE-3240"` для LDC1101 Click Board. Якщо саморобна котушка — `"custom-v1"` + `"coil_turns"`.
- `resp_time_code` — integer 0–7, відповідає RESP_TIME bits в LDC1101 config регістрі.

#### `raw` — source of truth

Значення безпосередньо з LDC1101, після RP_SET scaling, до будь-яких обчислень:
- `rp0..rp3` — Rp в Ohm на кожній відстані зі `steps_mm` + baseline (rp0 = baseline без монети)
- `l0, l1` — L в µH: baseline і при 0 мм

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
  slope_rp_per_mm_lr = linear_regression_slope([0,1,3], [0, dRp2/dRp1, dRp3/dRp1])
                       (нахил нормалізованої кривої в units 1/mm)
  dL1              = l1 - l0
```

**Зверніть увагу:** `slope_rp_per_mm_lr` тепер точно специфікований:
- вісь X: відстані зі `steps_mm` в мм
- вісь Y: нормалізований ΔRp/ΔRp1 (0 при 0 мм, k1 при 1 мм, k2 при 3 мм)
- метод: linear regression (least squares)
- одиниці: `1/мм` (безрозмірне на мм)

### 3.3 Метрика відстані для ідентифікації

Поточна в README: рівноважна Euclidean по всіх 5 координатах — математично некоректна (§2.4).

**Рекомендована для v1 — weighted Euclidean:**

```
dist = √(w₁·(dRp1_n - ref_dRp1_n)² + w₂·(k1-ref_k1)² + w₃·(k2-ref_k2)²
          + w₄·(slope-ref_slope)² + w₅·(dL1_n-ref_dL1_n)²)

де:
  dRp1_n  = dRp1 / dRp1_MAX   (нормалізований до [0..1], dRp1_MAX ≈ 500 для типових монет)
  dL1_n   = dL1 / dL1_MAX     (нормалізований аналогічно)
  w₁..w₅  = ваги, підбираються емпірично на validation set
```

**Альтернатива для v1.1 — Mahalanobis distance**, якщо з'явиться достатньо записів для оцінки covariance matrix per-class. Не для v1.

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
| Зміна відстаней `steps_mm` | `protocol_ver` + 1 | Нова папка в database |
| Зміна `freq_hz` по замовчуванню | `protocol_ver` + 1 | Окрема секція database |

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
BOUNDS = {
    "k1":   (0.3, 0.99),   # фізичні межі для будь-якого металу
    "k2":   (0.1, 0.98),
    "dRp1": (10.0, 800.0), # Ohm
    "dL1":  (0.0, 2000.0), # µH
    "slope_rp_per_mm_lr": (-2.0, 0.0),  # завжди негативний
}

def validate(record):
    errors = []
    for field, (lo, hi) in BOUNDS.items():
        val = record["vector"][field]
        if not (lo <= val <= hi):
            errors.append(f"{field}={val} out of range [{lo},{hi}]")
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

Firmware порівнює з centroid, використовує radius для confidence calibration.

### 6.3 Index staleness та merge conflicts

`index.json` що перераховує всі монети → при кожному PR оновлюється → merge conflicts між паралельними PR.

**Рішення: автогенерований index, не комітити вручну:**
```yaml
# CI: після merge → rebuild index
- name: Rebuild index
  run: python tools/build_index.py database/ > database/index.json
  
- name: Commit updated index
  run: |
    git config user.name "CoinTrace Bot"
    git add database/index.json
    git commit -m "chore: rebuild fingerprint index [skip ci]"
```

### 6.4 Структура директорій database

```
database/
  README.md               ← інструкції для contributors
  schema/
    fingerprint_v1.json   ← JSON Schema (jsonschema compatible)
  samples/
    XAG925/
      at_thaler_1780_001.json   ← іменування: {metal}_{country}_{name}_{serial}
      at_thaler_1780_002.json
      _aggregate.json           ← auto-generated, не редагувати
    XAG999/
    XAU585/
    XCU/
  index.json              ← auto-generated, не редагувати
  CHANGELOG.md            ← записи про schema_ver bumps
```

---

## 7. Frozen physical constants

Ці константи **після першого public release** стають частиною `protocol_ver`. Їх зміна = підняти `protocol_ver` + окрема папка в database.

| Константа | Поточне значення | де зараз | Ризик |
|-----------|-----------------|---------|-------|
| `SINGLE_FREQUENCY` | 1 000 000 Hz | `platformio.ini` | Будь-який розробник може змінити |
| Measurement distances | [0, 1, 3] мм | Тільки документація | Spacer буде 3D-printed ≠ точно 3.000 мм |
| `RESP_TIME` default | code 6 | `platformio.ini` | Впливає на noise/speed tradeoff |
| Котушка | MIKROE-3240 | BOM | Заміна на аналог → інша геометрія |

**Конкретний ризик `SINGLE_FREQUENCY`:** Якщо розробник-contributor змінює `SINGLE_FREQUENCY` на 500 000 Hz для кращої глибини проникнення, а потім робить PR з fingerprints — база отримує несумісні records без жодного попередження.

**Митигація:**
1. Перенести `SINGLE_FREQUENCY` і `DISTANCE_STEPS_MM` з `platformio.ini` в окремий файл `config/measurement_protocol_v1.h` з коментарем `// DO NOT CHANGE — part of protocol_ver 1`
2. `conditions.freq_hz` і `conditions.steps_mm` в кожному record — автоматична cross-check
3. CI validation: якщо `conditions.freq_hz ≠ CANONICAL_FREQ_HZ` → warning, не error (дозволяє experimental records в окремій папці)

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

**Точна формула:**
```
Y = [dRp_i / dRp1 for each step i ≠ 0]     // Y[0]=1 при distance=0mm, Y[1]=k1, Y[2]=k2
X = steps_mm[1:]                              // [1, 3]
slope = (N·Σ(XY) - ΣX·ΣY) / (N·Σ(X²) - (ΣX)²)  // standard linear regression
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

### 9.2 При підняті `protocol_ver`

**Тригер:** Зміна `steps_mm` або `freq_hz`.

**НЕ мігруємо** — нова папка:
```
database/samples_protocol_v1/  ← старі записи, тільки читання (frozen)
database/samples_protocol_v2/  ← нові записи
```

Firmware знає яку папку використовувати на основі `protocol_ver` прошивки.

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
- [ ] **DB-05** Перенести `SINGLE_FREQUENCY` і `DISTANCE_STEPS_MM` в `config/measurement_protocol_v1.h`
- [ ] **DB-06** Написати `tools/compute_vector.py` — canonical алгоритм (reference implementation)
- [ ] **DB-07** Написати перший запис-зразок `database/samples/XAG999/test_sample_001.json`
- [ ] **DB-08** Верифікувати що `compute_vector.py` відтворює вектор з raw для зразка
- [ ] **DB-09** Налаштувати автогенерацію `database/index.json` через CI
- [ ] **DB-10** Задокументувати `CONTRIBUTOR_GUIDE.md` — як вимірювати і подавати fingerprint

---

## Зв'язок з іншими документами

| Документ | Зв'язок |
|---------|--------|
| [CONNECTIVITY_ARCHITECTURE.md](./CONNECTIVITY_ARCHITECTURE.md) | HTTP API `/api/v1/database/match` використовує цей формат vector |
| [LDC1101_ARCHITECTURE.md](./LDC1101_ARCHITECTURE.md) | `raw.rp0..rp3` = вихід LDC1101Plugin |
| [PLUGIN_CONTRACT.md](./PLUGIN_CONTRACT.md) | `ISensorPlugin::getLastReading()` → джерело raw даних |
| [docs/concept/prior_art_disclosure.md](../concept/prior_art_disclosure.md) | Математичне обґрунтування k1/k2 size-independence |

---

*Версія документа: 1.0.0 — 2026-03-11*
