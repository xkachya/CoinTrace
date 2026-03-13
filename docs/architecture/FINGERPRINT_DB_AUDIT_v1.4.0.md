# Fingerprint DB Architecture — Independent Audit v1.4.0

**Документ що аудитується:** `FINGERPRINT_DB_ARCHITECTURE.md` v1.4.0  
**Дата аудиту:** 2026-03-12  
**Метод:** Незалежний перегляд без читання попередніх рецензій, перехресна перевірка з реалізацією та суміжними документами

---

## Резюме

Документ v1.4.0 — якісна архітектурна специфікація. Чотири осі поламки в §2, три версійних поля (ADR-DB-001), `raw` як source of truth (ADR-DB-003), двофазний пошук RAM+SD, CI validation — все це добре продумано і специфіковано. Знайдено 6 знахідок. Одна критична (cross-doc стала посилання), одна висока (помилка у прикладному значенні), три середніх.

**Загальна оцінка:** 8.5 / 10

---

## Знахідки

---

### FDB-01 [CRITICAL] — Cross-doc: CONNECTIVITY §5.2 hardcodes RING_SIZE=300 (стало)

**Файл:** `CONNECTIVITY_ARCHITECTURE.md` §5.2 (через цей документ)  
**Рядок:** `id < meas_count − 300`

**Опис:**  
STORAGE_ARCHITECTURE.md v1.4.0 змінила `RING_SIZE 300 → 250` ([F-02] commit `4e13d32`).  
CONNECTIVITY_ARCHITECTURE.md §5.2 (`ID range validation`) досі містить жорстко закодоване `300`:

```
meas_count > RING_SIZE
  AND id < meas_count − 300  → вимір витіснено ring overflow (overwritten)
```

**Наслідок:** Firmware що реалізує цю логіку поверне HTTP 200 з даними для вимірів 251–300 яких вже немає в ring (були overwritten). Це саме та "silent wrong data" поведінка яку PRE-2 мав запобігти.

**Виправлення:** Замінити `300` на `RING_SIZE` константу (`250`). Деталі — дивись CO-01 (CONNECTIVITY_AUDIT).

---

### FDB-02 [HIGH] — dL1 в canonical §3.1 суперечить фізичному діапазону §3.3

**Файл:** `FINGERPRINT_DB_ARCHITECTURE.md` §3.1 vs §3.3

**§3.1 canonical example** (Ag925 Austrian Maria Theresa Thaler):
```json
"raw": {
  "l0":  1204.3,
  "l1":  1928.4
}
"vector": {
  "dL1": 724.1
}
```

**§3.3 фізичний діапазон:**
> «Кольорові метали (Ag, Cu, Au, Al): ΔL ≈ 0–50 µH. Магнітні (Fe, Ni): 200–2000 µH.»

**dL1 = 724.1 µH для срібла** суперечить заявленому діапазону "0–50 µH" у 14 разів. Контроль: STORAGE_ARCHITECTURE.md §8.3 приклад для аналогічного вимірювання дає `dL1 = 17.8 µH` — узгоджується з фізикою. Значення `l1 = 1928.4 µH` в §3.1 виглядає як типове значення для феромагнітного матеріалу, а не срібла.

**Наслідок (ланцюговий):**  
- CONNECTIVITY §5.4 BLE RESULT кодує `dl:2u → round(dL1 * 100); діапазон 0.00–20.00 µH`. Для коректного Ag dL1≈18 µH → 1800 < 65535, ✓. Але для магнітних металів (Fe, Ni): dL1_MAX=2000 µH → `round(2000*100) = 200000 > uint16_max (65535)` → **overflow**.
- Два окремих "under-spec" в одному знахідку: (a) помилковий приклад у §3.1, (b) BLE uint16 не покриває магнітні метали.

**Виправлення:**  
1. §3.1: виправити `l1` з `1928.4` на `~1186.5` (l1 ≈ l0 - 17–20 µH для Ag), `dL1 ≈ 18.0`.  
2. CONNECTIVITY §5.4: змінити `dl:2u → uint16` на `dl:2u → uint16` з позначкою про кліп: `min(round(dL1 * 100), 65535)` + note: full range у §5.5 JSON (float); BLE кодує тільки non-magnetic fraction (0–650 µH як upper bound) → або розширити до `dL1_n × 1000` (нормалізований).  
   **Рекомендація:** Кодувати `dL1_n = dL1 / dL1_MAX × 1000` → `uint16` 0–1000 (не µH), щоб охопити весь фізичний діапазон без overflow.

---

### FDB-03 [MEDIUM] — dRp1_MAX (600 Ohm) < BOUNDS upper (800 Ohm): dRp1_n може перевищити 1.0

**Файл:** `FINGERPRINT_DB_ARCHITECTURE.md` §3.3 vs §6.1

**§3.3:**
```
dRp1_MAX = 600 Ohm — фізичний максимум ΔRp для MIKROE-3240 при 1 MHz
```

**§6.1 `validate_fingerprint.py`:**
```python
BOUNDS = {
    "dRp1": (10.0, 800.0),   # Ohm
    ...
}
```

**Проблема:** `CI validation` дозволяє `dRp1` до 800 Ohm. Але нормалізація в CI `build_index.py` використовує `dRp1_n = dRp1 / 600`. Для запису з `dRp1 = 750 Ohm`:  
- Validation: ✅ (750 < 800, OK)
- Нормалізація: `dRp1_n = 750/600 = 1.25` — поза межами [0..1]

Weighted Euclidean formula в §3.3 передбачає що нормалізовані компоненти ∈ [0..1]. При `dRp1_n > 1.0` компонент `(dRp1_n − ref_dRp1_n)²` непропорційно домінує — відтворюється та сама проблема доменування з §2.4 яку нормалізація мала вирішити.

**Примітка:** R-01 (уточнення після validation set) позначений як open, тому `dRp1_MAX=600` та BOUNDS можуть бути тимчасові. Але поки constraint не виконаний: `BOUNDS["dRp1"][1]` повинен бути `≤ dRp1_MAX`.

**Виправлення:** Або підняти `dRp1_MAX` до 800 у §3.3 і `fingerprint_config.h`, або опустити BOUNDS upper до 600. Додати comment в BOUNDS: `# верхня межа ≤ dRp1_MAX = 600`.

---

### FDB-04 [MEDIUM] — ADR-DB-003: vector recompute відсутній в `validate_fingerprint.py` прикладному коді та чек-листі

**Файл:** `FINGERPRINT_DB_ARCHITECTURE.md` §6.1, §8 ADR-DB-003, §10 checklist

**ADR-DB-003 говорить:**
> «CI скрипт при PR: якщо є `raw` → автоматично перераховує і верифікує `vector`. Якщо відхилення > 0.1% → помилка CI.»

**validate_fingerprint.py у §6.1** реалізує тільки:
```python
for field, (lo, hi) in BOUNDS.items():
    val = record["vector"][field]
    if not (lo <= val <= hi):
        errors.append(...)
```

Ні `compute_vector_from_raw()`, ні порівняння computed vs stored вектора там немає.

**Чек-лист §10 DB-03:** «`validate_fingerprint.py` з bounds + schema validation» — не згадує recomputation.  
**Чек-лист §10 DB-06** додано `tools/compute_vector.py` — окремий інструмент. Але DB-03 і DB-06 не пов'язані явно: CI може запустити `validate_fingerprint.py` без `compute_vector.py` і пропустити знахідку FDB-02 type data (l0/l1 неправильні → dL1 неправильний але в BOUNDS → PR зімерджений).

**Виправлення:**  
1. `validate_fingerprint.py`: додати блок верифікації вектора:
```python
from compute_vector import compute_vector_v1
if "raw" in record:
    expected_vec = compute_vector_v1(record["raw"], record["conditions"])
    for field, expected in expected_vec.items():
        actual = record["vector"][field]
        if abs(actual - expected) / abs(expected) > 0.001:  # 0.1%
            errors.append(f"{field}: stored={actual:.4f}, computed={expected:.4f}")
```
2. Чек-лист DB-03: додати «+ автоматична верифікація vector з raw (ADR-DB-003)».

---

### FDB-05 [MEDIUM] — index_crc32.bin відстежує цілісність кешу, але не сталість SD database

**Файл:** `STORAGE_ARCHITECTURE.md` §8.2 (результат F-06 fix), §17.2 boot [7a]

**Специфіковано:**  
`/data/cache/index_crc32.bin` = CRC32 від `/data/cache/index.json` (самого кеш-файлу).  
Boot [7a]: «SD є, cache є → перевірка CRC32; якщо не збігається → видалити cache».

**Проблема:** Цей механізм виявляє **пошкодження кешу** (LittleFS запис оборвався, bit-flip), але **не виявляє оновлення SD бази**.  
Сценарій: Користувач вставив нову SD карту з 200 новими монетами. Firmware:
1. Читає `/data/cache/index.json` — CRC32 збігається ✓
2. Робить match по старому кешу з 50 монет ← **silent wrong result (recall miss)**
3. Нові 200 монет ніколи не будуть використані

**Що пропущено:** Механізм детекції оновлення SD не специфікований. Кандидати:
- Порівняння `version` або `generated_at` поля SD's `index.json` з кешованим
- CRC32 SD's `database/index.json` (окремо від LittleFS cache CRC32)
- Монотонний лічильник у SD `protocols/registry.json`

**Рекомендація:** Додати до boot [7a]:  
```
читаємо SD:/database/index.json header → поле "generated_at"
якщо cache["generated_at"] != sd_index["generated_at"] → кеш застарів → rebuild
```
Потребує одноразового читання SD header (~100 байт) без завантаження всього index.json в RAM.

---

### FDB-06 [LOW] — dRp1 = 0: нормалізація не захищена (тільки k1, k2 мають guard)

**Файл:** `FINGERPRINT_DB_ARCHITECTURE.md` §3.2 (algo_ver = 1)

**Специфіковано:**
```
k1 = dRp2 / dRp1   (якщо dRp1 ≠ 0)
k2 = dRp3 / dRp1   (якщо dRp1 ≠ 0)
```

Але нормалізація в §3.3:
```
dRp1_n = dRp1 / dRp1_MAX
```
...не має guard для `dRp1 = 0`. При `dRp1_MAX = 600`, якщо `dRp1 = 0` → `dRp1_n = 0` (не NaN) — тут математично безпечно. Але CI validation в §6.1 має `BOUNDS["dRp1"] = (10.0, 800.0)` — тобто dRp1=0 буде відхилено validation. Це неявний guard.

**Питання:** Якщо k1/k2 мають явний guard `(якщо dRp1 ≠ 0)` — специфікатор має сказати що відбувається в firmware при `dRp1 = 0`: повернути `{"error": "no_deflection"}`, LOG_WARN, чи пропустити вектор? Наразі не визначено.

**Виправлення:** Додати рядок в §3.2 після формул: «Якщо `dRp1 = 0` (монета не виявлена або sensor fault) → firmware повертає `{"error":"no_deflection","code":"SENSOR_ZERO_DEFLECTION"}`, вимір не зберігається.»

---

## Таблиця знахідок

| ID | Серйозність | Секція | Суть | Блокує | Статус |
|----|-------------|--------|------|--------|--------|
| FDB-01 | 🔴 CRITICAL | cross-doc CONNECTIVITY §5.2 | RING_SIZE жорстко = 300 (має бути 250) | silent wrong data в ID range check | ✅ Виправлено v1.4.0 |
| FDB-02 | 🟠 HIGH | §3.1, §3.3, CONNECTIVITY §5.4 | dL1 = 724.1 µH для Ag925 (фізично неможливо); BLE dl:2u overflow для магнітних металів | некоректний canonical приклад; BLE encoding для Fe/Ni | ✅ Виправлено v1.5.0 |
| FDB-03 | 🟡 MEDIUM | §3.3 vs §6.1 BOUNDS | dRp1_MAX=600 < BOUNDS upper=800 → dRp1_n > 1.0 | нормалізаційний інваріант [0..1] порушено | ✅ Виправлено v1.5.0 |
| FDB-04 | 🟡 MEDIUM | §6.1, ADR-DB-003, §10 | validate_fingerprint.py не переобчислює vector з raw | ADR-DB-003 не виконано в коді | 🔓 Відкрита |
| FDB-05 | 🟡 MEDIUM | §8.2 (STORAGE), boot [7a] | index_crc32.bin = integrity check, не staleness check | оновлення SD бази непомітне | 🔓 Відкрита |
| FDB-06 | 🔵 LOW | §3.2 | dRp1=0 firmware behavior не специфіковано | невизначена поведінка | 🔓 Відкрита |

---

## Рекомендації до §14 Черги

З цього аудиту пріоритетна черга на наступні документи:
- **LDC1101_ARCHITECTURE.md** — безпосереднє джерело `raw.rp0..rp3`, `l0`, `l1` значень; валідація FDB-02 потребує перевірки типових діапазонів від сенсора
- **PLUGIN_CONTRACT.md** — `ISensorPlugin::getLastReading()` → джерело raw; перевірка що dRp1=0 contract відображений в return type

---

*Аудит: незалежний перегляд, без доступу до попередніх рецензій*  
*Версія документа що аудитувалась: FINGERPRINT_DB_ARCHITECTURE.md v1.4.0 (2026-03-11)*
