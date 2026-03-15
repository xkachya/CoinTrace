# Storage Architecture — Absorbing External Audit 2026-03-14

**Документ:** STORAGE_AUDIT_v1.4.0.md  
**Аудитована версія:** STORAGE_ARCHITECTURE.md v1.5.0 → v1.6.0  
**Джерело:** `docs/external/STORAGE_Architecture_Audit.2026-03-14.md`  
**Дата:** 2026-03-14  
**Метод:** Зовнішній незалежний аудит (embedded systems architecture review) + внутрішня верифікація знахідок, закриття та реєстрація в backlog  
**Контекст:** Аудит проведено після завершення STORAGE_AUDIT_v1.3.0 (SA2-1..SA2-10 закриті). Мета зовнішнього аудиту — незалежна верифікація перед початком Wave 7 (P-1 implementation).

---

## Зміст

1. [Загальна оцінка зовнішнього аудиту](#1-загальна-оцінка-зовнішнього-аудиту)
2. [Score-карта знахідок](#2-score-карта-знахідок)
3. [F-05 — Критична знахідка (закрита в v1.6.0)](#3-f-05--критична-знахідка-закрита-в-v160)
4. [Нові backlog знахідки (F-02, F-06, F-07, F-08)](#4-нові-backlog-знахідки)
5. [Маппінг до існуючого backlog з v1.3.0](#5-маппінг-до-існуючого-backlog-з-v130)
6. [Pre-P-1 Checklist — статус](#6-pre-p-1-checklist--статус)
7. [Що архітектура зробила ПРАВИЛЬНО (підтверджено зовнішнім аудитом)](#7-що-архітектура-зробила-правильно)
8. [Зведений backlog (актуальний стан після v1.4.0)](#8-зведений-backlog)
9. [Wave 7 P-3 External Audit — знахідки A-01..A-05](#9-wave-7-p-3-external-audit--знахідки-a-01a-05)

---

## 1. Загальна оцінка зовнішнього аудиту

| Критерій | Оцінка зовнішнього аудитора |
|---|---|
| Повнота | **9.5 / 10** — пропущено brownout handling, flash encryption migration |
| Коректність | **9 / 10** — одна помилка (F-05 protocol_id), решта верифіковано |
| Якість ADR | **10 / 10** — 9 ADR з обґрунтуванням, альтернативами та наслідками |
| Готовність до імплементації | **9 / 10** — можна починати негайно після F-05 |
| Якість vs ринок | **Топ-1%** embedded ESP32 документації |

**Вердикт:** 1 критична знахідка (F-05) + 3 середніх (F-06, F-07, F-08). F-05 закрита в v1.6.0 того ж дня. Середні знахідки — не блокують P-1, але повинні бути вирішені до P-4 (F-06, F-08) та post-v1 (F-07).

---

## 2. Score-карта знахідок

| ID | Знахідка | Severity | Статус |
|---|---|---|---|
| **F-01** | GAP 24 KB між otadata та app0 — задокументований, непридатний | ℹ️ INFO | ✅ Підтверджено коректність. Суміжне → FUTURE-6 (nvs_keys в gap) |
| **F-02** | LittleFS_data margin 14.1% — tight; measurement file >4 KB → overflow | ℹ️ INFO | 🔵 Backlog → **EXT-FUTURE-1** (P-3 runtime guard) |
| **F-03** | Coredump 128 KB — достатньо для ESP32-S3 | ✅ OK | ✅ Підтверджено |
| **F-04** | TWDT ризик під час buildCache() з великою SD | 🟡 INFO | ✅ Вже задокументовано в §17.2 [BOOT-2]; без змін |
| **F-05** | `protocol_id: "p1_1mhz_013mm"` — реальний fSENSOR ≈ 200–500 kHz, не 1 MHz | 🔴 CRITICAL | ✅ **Закрито в v1.6.0** — виправлено в 3 місцях |
| **F-06** | Brownout / low-voltage handling відсутнє | 🟡 MEDIUM | 🔵 Backlog → **EXT-FUTURE-2** (до P-4) |
| **F-07** | Flash encryption migration path не описаний в ADR-ST-005 | 🟡 MEDIUM | 🔵 Backlog → **EXT-FUTURE-3** (post-v1, до security review) |
| **F-08** | SD card hot-plug (insert/remove/replace) не описаний | 🟡 MEDIUM | 🔵 Backlog → **EXT-FUTURE-4** / розширення FUTURE-5 (до P-4) |
| **F-09** | Ring buffer slot overflow @ 4.3B count → 588,000 років | ℹ️ INFO | ✅ Не проблема |
| **F-10** | `"complete": true` write sentinel — правильний паттерн | ✅ POSITIVE | ✅ Підтверджено |
| **F-11** | 5+ ітерацій review (PRE-1..PRE-11, SA2-1..SA2-10, F-05..F-08) | ✅ POSITIVE | ✅ Задокументовано |

---

## 3. F-05 — Критична знахідка (закрита в v1.6.0)

### Опис проблеми

`protocol_id: "p1_1mhz_013mm"` використовувалося в трьох місцях STORAGE_ARCHITECTURE.md:
- §7.2 NVS layout (sensor namespace comment)
- §8.3 measurement format (JSON example)
- ADR-ST-007 (versioning scheme)

**Чому критично:** Реальний fSENSOR котушки MIKROE-3240 визначається індуктивністю котушки та ємністями LDC1101, **не конфігурацією**. Фактичне значення ≈ 200–500 kHz (навіть не 1 MHz, не 5 MHz). Якщо перший вимір зберігається з `"p1_1mhz_013mm"` — подальші порівняння та submissions до community DB є несумісними з реальними даними. **Необоротна помилка в перших виміряних монетах.**

**Рішення (v1.6.0):**
Замінено на `"p1_UNKNOWN_013mm"` — явний placeholder зі warning у всіх трьох місцях. Реальне значення fSENSOR буде виміряне під час R-01 (перший hardware milestone) та зафіксоване як офіційний `protocol_id` перед першим community DB submission.

**Cross-reference:** FINGERPRINT_DB_ARCHITECTURE.md §7 (protocol versioning).

### Місця виправлення

| Місце | Старе значення | Нове значення | Додано |
|---|---|---|---|
| §7.2 NVS layout, sensor.proto_id comment | `"p1_1mhz_013mm"` | `"p1_UNKNOWN_013mm"` | Пояснення реального fSENSOR діапазону |
| §8.3 JSON example, `protocol_id` field | `"p1_1mhz_013mm"` | `"p1_UNKNOWN_013mm"` | `[F-05]` warning block |
| ADR-ST-007, JSON snippet | `"p1_1mhz_013mm"` | `"p1_UNKNOWN_013mm"` | `[F-05]` warning note |

---

## 4. Нові backlog знахідки

### EXT-FUTURE-1 (з F-02)

**Знахідка:** LittleFS_data free margin = 14.1% (63 блоки = 252 KB). При вимірювальному файлі `m_XXX.json` > 4 KB — кожен файл займає 2 блоки замість 1, що перевищує ємність 448 блоків (250 файлів × 2 = 500 > 448).

**Поточний стан:** Поточний розмір `m_XXX.json` ≈ 700 B — безпечно (1 блок). Але якщо структура файлу розшириться (наприклад, додаткові поля fingerprint analysis, raw data samples) — перевищення 4 KB спричинить тихий overflow при ring buffer wrap-around.

**Рекомендація:** При P-3 (MeasurementStore implementation) додати:
```cpp
// Guard in MeasurementStore::save():
if (serialized_size > 3800) {
    LOG_ERROR("Measurement JSON exceeds 3800B (%d) — truncating alternatives", serialized_size);
    // truncate match.alternatives to stay within block limit
}
```
**Термін:** P-3 (до першого user testing)  
**Тип:** Реалізаційна нотатка (не документаційна правка)

---

### EXT-FUTURE-2 (з F-06)

**Знахідка:** Документ описує FAT32 power-fail risk (R-09) та мітигує batch writes через ротацію. Але відсутній аналіз:
- При якому рівні заряду батареї Cardputer вимикається (brownout threshold)?
- Чи є brownout warning interrupt для graceful shutdown (ESP32-S3 BOD)?
- Чи потрібно закривати SD файли перед brownout?
- `LittleFSTransport::lfs_file_sync()` — чи встигає виконатись до brownout reset?

**Impact:** Якщо brownout відбувається під час SD FAT32 write → corrupt archive. Якщо відбувається під час `lfs_file_sync()` → LittleFS atomic (safe). NVS has CRC → safe. Найбільший ризик: SD.

**Рекомендація:**
1. Досліджувати brownout threshold M5Stack Cardputer (USB vs battery)
2. Задокументувати BOD interrupt availability на ESP32-S3
3. Якщо BOD interrupt доступний — додати graceful SD close в ISR або task notification
4. Додати §19 "Power Fail Safety Analysis" або розширити §17 sequence

**Термін:** До P-4 (SDTransport implementation) — критично до першого live testing на батареї  
**Тип:** Документаційна + можлива архітектурна зміна

---

### EXT-FUTURE-3 (з F-07)

**Знахідка:** ADR-ST-005 правильно відкладає flash encryption на post-v1. Але відсутній **migration path** — процедура переходу існуючих пристроїв на encrypted flash:

- Flash encryption = eFuse burn (одноразовий, **необоротний**)
- NVS дані, записані до encryption, стають нечитаємими після ввімкнення encryption
- WiFi credentials, sensor calibration, `meas_count` — все потрібно migrate
- Потрібно: backup NVS → enable encryption → restore NVS

**Відмінність від FUTURE-6:** FUTURE-6 стосується фізичної наявності `nvs_keys` partition у flash layout. EXT-FUTURE-3 стосується процедури міграції **existing userdata** при ввімкненні encryption — навіть якщо `nvs_keys` partition зарезервована, потрібна migration tool/procedure.

**Рекомендація:** Додати в ADR-ST-005 секцію:
```
## Migration Path (post-v1)
Prerequisites: nvs_keys partition available (→ FUTURE-6)
Step 1: Export NVS config via /api/v1/config/export (WiFi, sensor cal)
Step 2: espsecure.py burn_key (eFuse — IRREVERSIBLE)
Step 3: Restore NVS via /api/v1/config/import
Step 4: Re-enter WiFi password (not exportable for security)
```

**Термін:** Post-v1, до першого security review  
**Тип:** Документаційна (ADR-ST-005 amendment)

---

### EXT-FUTURE-4 (з F-08, розширення FUTURE-5)

**Знахідка:** Існуючий FUTURE-5 покриває тільки hot-**insertion** (SD вставлена після boot). Зовнішній аудит F-08 ідентифікує повний hot-plug cycle:

| Сценарій | FUTURE-5 | EXT-FUTURE-4 |
|---|---|---|
| SD вставлена після boot | ✅ Покрито (checkHotplug polling) | Залишається |
| SD **витягнута** під час роботи | ❌ Не специфіковано | Graceful degrade + LOG_WARN |
| SD **замінена** під час роботи | ❌ Не специфіковано | Cache invalidation при remount |

**Сценарій "SD витягнута під час ротації":**
```
LittleFSTransport bg task: rotate()
  Step 1: lfs_rename(log.0 → log.1)  ← SD ще є
  Step 2: spi_vspi_mutex + SD.open() ← [USER REMOVES SD]
  SD.write() → SPI timeout or SD_ERR
  → без error handling → panic або silent data loss
```

**Рекомендація:**
1. Визначити `SDCardManager::onRemove()` callback або GPIO12 interrupt
2. Документувати в §9 поведінку при runtime SD removal: `isPresent_ = false`, graceful cancel active write
3. Документувати cache invalidation при SD replacement (CRC32 generation ID для порівняння)

**Термін:** До P-4 (SDTransport implementation)  
**Тип:** Документаційна §9 + архітектурна специфікація

---

## 5. Маппінг до існуючого backlog з v1.3.0

| Нова знахідка | Суміжний backlog item | Відношення |
|---|---|---|
| F-01 GAP 24KB | **FUTURE-6** (nvs_keys partition) | ✅ Повний перетин — F-01 підтверджує необхідність FUTURE-6 |
| F-02 measurement size guard | (немає) | ➕ Новий EXT-FUTURE-1 |
| F-04 TWDT buildCache | Вже в §17.2 [BOOT-2] | ✅ Покрито, не потребує запису |
| F-06 brownout | (немає) | ➕ Новий EXT-FUTURE-2 |
| F-07 flash encryption migration | **FUTURE-6** суміжно | ➕ Новий EXT-FUTURE-3 (різна суть) |
| F-08 SD hot-plug full cycle | **FUTURE-5** (тільки insertion) | ➕ Новий EXT-FUTURE-4 (розширює FUTURE-5) |

---

## 6. Pre-P-1 Checklist — статус

Pre-P-1 Acceptance Criteria специфіковані в STORAGE_ARCHITECTURE.md §15. Всі 13 пунктів референсні до реалізації (не до документації) — не блокують поточний документаційний коміт.

**Зовнішній аудит підтвердив:** partition table Option B коректна, адреси вивірені, gap 24KB є наслідком обов'язкового вирівнювання (не помилкою). Можна починати P-1 implementation.

---

## 7. Що архітектура зробила ПРАВИЛЬНО

*(Підтверджено незалежним зовнішнім аудитором — доповнює список з STORAGE_AUDIT_v1.3.0)*

| # | Аспект | Коментар аудитора |
|---|---|---|
| **E-01** | 3-tier architecture (NVS/LittleFS/SD) | "Класична і правильна для ESP32. Чітке розділення за lifecycle" |
| **E-02** | Dual LittleFS partitions | "Вирішує uploadfs problem елегантно. Рідко бачу в ESP32 проектах" |
| **E-03** | Write-first invariant (ADR-ST-006) | "mission-critical інваріант, правильно задокументований з power-fail аналізом" |
| **E-04** | Lock ordering contract (SA2-6) | "Explicit ordering rule з документацією caller sites. Промисловий стандарт" |
| **E-05** | Copy-before-overwrite (ADR-ST-009) | "SD archive при ring overflow — елегантне рішення. Power-fail таблиця для кожного кроку" |
| **E-06** | GPIO0 recovery (§17.2 [1.5]) | "Фізичний антидот для bricked device. З TWDT-safe loop. Рідко бачу в hobbyist проектах" |
| **E-07** | RAII LittleFSDataGuard | "Stack-based mutex guard з timeout і ok() check. Zero-copy, no heap alloc" |
| **E-08** | Wear leveling analysis (§13) | "Повний lifecycle розрахунок для NVS, LittleFS, SD. З конкретними числами" |
| **E-09** | Data survival matrix (§16) | "8 подій × 4 storage tiers = повна картина що виживає при чому" |
| **E-10** | Blocker analysis (§4.2) | "6 блокерів з severity, impact і fix. Рідко бачу перед початком кодування" |
| **E-11** | Якість vs ринок | "Топ-1%. Automotive Tier-1 рівень. Еквівалент $8k–$15k senior embedded architect work" |

---

## 8. Зведений backlog (актуальний стан після v1.4.0)

> Авторитетний backlog реєстр. Включає всі відкриті знахідки з STORAGE_AUDIT_v1.2.0, v1.3.0 та поточного v1.4.0.
> Закриті знахідки перенесені в розділ "Закриті" для довідки.

---

### До першого user testing / beta (блокують)

| ID | Джерело | Знахідка | Рекомендація |
|---|---|---|---|
| **FUTURE-1** | v1.2.0 M5-M1 | Rotation тримає `lfs_data_mutex_` ~150–350 ms; guard 500ms (SA2-8) зменшує errors, але затримує WebServer reads | Incremental rotation (release mutex між rename/copy/delete) або документувати max latency SLA |
| **FUTURE-2** | v1.2.0 M2-M1 | `LOG_ERROR` у `LittleFSDataGuard` ctor → повний dispatch під час contention (self-amplification) | Замінити на `Serial.println("[ERR] lfs_data lock timeout")` без Logger dispatch |
| **SA2-FUTURE-1** | v1.3.0 C-H1 | Hard Reset не очищає coredump partition → stale coredump зберігається після Hard Reset + reboot [7.5] | Додати крок `b2. esp_partition_erase_range(coredump_partition)` до §14.1 |
| **SA2-FUTURE-2** | v1.3.0 B-H1 | TCA8418 DEGRADED + LittleFS_data corrupt = "stuck boot" без виходу; GPIO0 recovery форматує тільки data | Документувати secondary recovery: GPIO0 утримання >10s = повний Hard Reset |
| **SA2-FUTURE-3** | v1.3.0 Module C | FAT32: power fail під час SD copy → corrupt archive (no temp+rename) | При SD copy: писати в `tmp_YYYYMMDD.txt`, потім rename; при reboot видаляти `tmp_*` |
| **EXT-FUTURE-1** | v1.4.0 F-02 | LittleFS_data margin 14.1%; measurement file >4KB → 2 blocks/file → ring-buffer overflow ємності | P-3 MeasurementStore: guard `if (json_size > 3800) LOG_ERROR + truncate alternatives` |
| **EXT-FUTURE-5** | P-3 A-02 | MeasurementStore: boundary validation відсутня — `computeVector()` може отримати `rp[0]=0` від будь-якого майбутнього плагіна → NaN | `save()`: перевірка `rp[i] > 1.0f` для ВСІХ позицій ДО `computeVector()` — незалежно від plugin-level guards (defense-in-depth; → §9) | ✅ **CLOSED P-3** — `MeasurementStore::save()` (P-3 2026-03-15): `if (m.rp[0] < 1.0f) return false` BEFORE `isDataMounted()`. Test: `test_measurement_store` A-02 покриття. |
| **EXT-FUTURE-6** | P-3 A-03 | MeasurementStore: `load()` не перевіряє sentinel `"complete": true` → partial/aborted записи завантажаться як валідні | `load()`: `deserializeJson()` success + `doc["complete"].is<bool>() && doc["complete"].as<bool>()` перед поверненням даних (→ §9) | ✅ **CLOSED P-3** — `MeasurementStore::load()` (P-3 2026-03-15): sentinel перевірка реалізована. Test: `test_measurement_store` A-03 покриття (4 edge-case). |

---

### До першого public release / community DB

| ID | Джерело | Знахідка | Рекомендація |
|---|---|---|---|
| **FUTURE-3** | v1.2.0 M7-L1 | `device_id` з 2 байтів MAC = 65 536 IDs → ~50% birthday collision при 300+ пристроях | `hex(mac[2:6])` → 8 hex chars = 16M IDs (breaking зміна до community DB submission) |
| **FUTURE-5** | v1.2.0 M7-L2 | SD hot-**insertion** post-boot → Matching (Deep) недоступний до reboot | Polling `SDCardManager::checkHotplug()` кожні 5 с або `POST /api/v1/storage/remount` |
| **EXT-FUTURE-4** | v1.4.0 F-08 | SD hot-plug повний цикл: insertion *(FUTURE-5)* + **removal during write** + **replacement cache invalidation** | §9: graceful cancel при SD removal (GPIO12 interrupt або SPI err detection); CRC32 generation ID при remount |
| **SA2-FUTURE-4** | v1.3.0 Module F | §15 P-4 `SDTransport.h/.cpp` roadmap artifact: SDTransport не є самостійним Logger dispatch target | Уточнити §15 P-4: видалити або перефразувати SDTransport entry |

---

### Перед production + security review

| ID | Джерело | Знахідка | Рекомендація |
|---|---|---|---|
| **EXT-FUTURE-2** | v1.4.0 F-06 | Brownout / low-voltage: ESP32-S3 BOD при падінні напруги; SD FAT32 write під brownout → corrupt | Дослідити M5Stack Cardputer brownout threshold + BOD interrupt; документувати §19 "Power Fail Safety"; graceful SD close в BOD handler якщо доступний |
| **FUTURE-6** | v1.2.0 M6-L1 | Flash повністю використаний; NVS encryption post-v1 = breaking change (потребує `nvs_keys` partition) | При наступній partition revision: зарезервувати 4 KB nvs_keys в GAP 0x1A000–0x20000 (підтверджено F-01) |
| **EXT-FUTURE-3** | v1.4.0 F-07 | ADR-ST-005 відкладає flash encryption, але migration path для existing devices не описаний (eFuse = незворотний burn, NVS incompatible after) | Додати в ADR-ST-005 "Migration Path": export NVS → burn eFuse → restore NVS; зв'язати з FUTURE-6 |

---

### Pre-v2 / Long-term

| ID | Джерело | Знахідка | Рекомендація |
|---|---|---|---|
| **FUTURE-7** | v1.2.0 M7-L3 | `RING_SIZE` зміна між версіями = out-of-bounds slot; не задокументовано як immutable | ADR-ST-006: `RING_SIZE = immutable post-v1`; зміна = major version bump з migration guide |
| **FUTURE-8** | v1.2.0 M7-L3 | OTA rollback + `schema_ver` compatibility matrix не специфікована | ADR-ST-007: rollback firmware MUST читати всі `schema_ver ≤ current`; підвищення = окремий compatibility ADR |

---

### Закриті backlog items (довідково)

| ID | Джерело | Знахідка | Закрито |
|---|---|---|---|
| ~~FUTURE-4~~ | v1.2.0 M7-M1 | Hard Reset: LittleFSTransport bg task не зупиняється перед format() | ✅ **SA2-4** (v1.3.2) — крок a0 `LittleFSTransport::stop()` |
| ~~FUTURE-9~~ | v1.2.0 M7-L4 | Hard Reset: stale coredump після format | → Перенесено як **SA2-FUTURE-1** (деталізовано) |
| ~~SA2-1~~ | v1.3.0 A-H1 | ADR-ST-005 `incrementMeasCount()` "atomic" residue | ✅ v1.3.2 |
| ~~SA2-2~~ | v1.3.0 A-H2 | ADR-ST-006 "один atomic write" residue | ✅ v1.3.2 |
| ~~SA2-3~~ | v1.3.0 B-H1 | Boot [5] LittleFSTransport не обумовлений успіхом [4] | ✅ v1.3.2 |
| ~~SA2-4~~ | v1.3.0 C-H1 | Hard Reset: bg task не зупиняється перед format() | ✅ v1.3.2 |
| ~~SA2-5~~ | v1.3.0 D-M1 | §13.1 SD warning використовує pre-PRE-8 write model | ✅ v1.3.2 |
| ~~SA2-6~~ | v1.3.0 E-M1 | Lock ordering contract не документована | ✅ v1.3.2 (ADR-ST-008) |
| ~~SA2-7~~ | v1.3.0 E-M2 | `esp_littlefs_info()` mutex scope не специфікована | ✅ v1.3.2 |
| ~~SA2-8~~ | v1.3.0 E-M3 | `LittleFSDataGuard` timeout 100ms→500ms + ok()=false contract | ✅ v1.3.2 |
| ~~SA2-9~~ | v1.3.0 F-L1 | §16 Soft Reset "optional" → виміри завжди зберігаються | ✅ v1.3.2 |
| ~~SA2-10~~ | v1.3.0 F-L2 | §10 OQ-07 "3×300KB" → "2×200KB" | ✅ v1.3.2 |
| ~~F-05~~ | v1.4.0 external | `protocol_id: "p1_1mhz_013mm"` — неправильний fSENSOR | ✅ **v1.6.0** (2026-03-14) — `"p1_UNKNOWN_013mm"` + §18 factory restore |
| ~~A-01~~ | P-3 external 2026-03-15 | STORAGE_ARCHITECTURE §8.3: `esp_efuse_get_custom_mac()` → повертає `ESP_ERR_INVALID_ARG` без custom eFuse MAC | ✅ **2026-03-15** — `esp_efuse_mac_get_default()` + ⚠️ [A-01] warning note в §8.3 (→ §9) |

---

*Наступний аудит: STORAGE_AUDIT_v1.5.0 — після завершення P-3 (MeasurementStore + LittleFSTransport реалізація). Верифікація: persistence round-trip тести, sentinel validation (EXT-FUTURE-6), boundary guard (EXT-FUTURE-5).*

---

## 9. Wave 7 P-3 External Audit — знахідки A-01..A-05

**Джерело:** `docs/external/Wave7_P3_Implementation_Audit.2026-03-15.md`  
**Дата:** 2026-03-15  
**Контекст:** Зовнішній аудит проведено перед початком P-3 (MeasurementStore + LittleFSTransport). Охоплює обидва модулі: Storage (A-01, A-02, A-03) та Logger/LittleFSTransport (A-04, A-05). Logger-знахідки A-04/A-05 поглинуті в LOGGER_AUDIT_v2.0.0.md § Backlog (LF-FUTURE-6, LF-FUTURE-7).

---

### Score-карта (Storage-релевантні знахідки)

| ID | Знахідка | Severity | Статус | Backlog |
|---|---|---|---|---|
| **A-01** | STORAGE_ARCHITECTURE §8.3: `esp_efuse_get_custom_mac()` → `ESP_ERR_INVALID_ARG` без custom eFuse MAC | 🟡 MEDIUM | ✅ CLOSED 2026-03-15 | ~~A-01~~ |
| **A-02** | MeasurementStore: відсутня boundary validation → `computeVector()` отримає `rp[0]=0` від будь-якого плагіна → NaN результат | 🟠 HIGH | ✅ CLOSED 2026-03-15 | **EXT-FUTURE-5** |
| **A-03** | MeasurementStore `load()`: відсутня перевірка sentinel `"complete": true` → partial/aborted запис вважається валідним | 🟡 MEDIUM | ✅ CLOSED 2026-03-15 | **EXT-FUTURE-6** |

*(A-04, A-05 → LOGGER_AUDIT_v2.0.0.md LF-FUTURE-6/7)*

---

### A-01 — esp_efuse_mac_get_default() (ЗАКРИТО)

**Знахідка:** STORAGE_ARCHITECTURE §8.3 pseudocode для `device_id` генерації використовував `esp_efuse_get_custom_mac()[4:6]`.

**Проблема:** `esp_efuse_get_custom_mac()` повертає `ESP_ERR_INVALID_ARG` якщо custom MAC не записаний в eFuse — типова ситуація для всіх пристроїв без заводського прошивання. Правильна функція: `esp_efuse_mac_get_default()` (повертає factory Ethernet MAC, завжди доступний).

**Правка (2026-03-15):** §8.3 оновлено: `esp_efuse_get_custom_mac` → `esp_efuse_mac_get_default` + ⚠️ [A-01] warning block з поясненням різниці.

---

### A-02 — MeasurementStore: Two-Layer Defense (EXT-FUTURE-5)

**Знахідка:** В специфікації `computeVector()` відсутній guard на `rp[0] == 0`. LDC1101Plugin має власний guard (`rpRaw == 0` at line 254 + `calibrationRpBaseline_ > 100.0f` at line 281), але MeasurementStore не може покладатися на це.

**Чому plugin-only guard недостатній:**

MeasurementStore — це **storage boundary**. Його `save()` може бути викликаним:
- `LDC1101Plugin` (наразі — з guards)
- Будь-яким майбутнім плагіном (PesoScalePlugin, CalibrationPlugin) — без гарантій
- Безпосередньо з тестів або діагностичного коду
- Після рефакторингу, де plugin guards будуть видалені чи змінені

**Правильна архітектура (defense-in-depth):**

```
Layer 1 — Plugin boundary (LDC1101Plugin):
  rpRaw == 0 || rpRaw == 0xFFFF → return early    ← hardware guard
  calibrationRpBaseline_ <= 100.0f → skip save    ← calibration guard

Layer 2 — Storage boundary (MeasurementStore::save()):
  for (int i = 0; i < 4; i++) {
      if (m.rp[i] < 1.0f) {                        ← boundary contract
          LOG_WARN("save() rejected: rp[%d]=%.2f", i, m.rp[i]);
          return false;
      }
  }
  computeVector(m.rp, m.l, vec);  // safe: no NaN
```

**Ключова відмінність:** Layer 1 захищає від bad hardware read. Layer 2 захищає від bad caller. Вони не дублюють одне одного — вони захищають від різних failure modes.

**Наслідок:** `computeVector()` може ВИМАГАТИ `rp[i] > 0` через precondition у doc-comment, але НЕ повинна самостійно ним управляти — відповідальність за валідацію belongs to `save()` як точка входу.

**Реалізація (P-3):** `save()` first validation block; `computeVector()` — додати `ASSERT(rp[i] > 0)` debug mode only.

---

### A-03 — "complete" sentinel validation (EXT-FUTURE-6)

**Знахідка:** `MeasurementStore::load()` у специфікації не перевіряє наявність `"complete": true` — файли, записані частково (power fail після `open()` але до запису sentinel) будуть завантажені як валідні вимірювання.

**Деталі:**

Файл `m_NNN.json` пишеться з sentinel-останнім патерном (ADR-ST-006 write-first invariant):
```json
{ "ts": ..., "rp": [...], ..., "complete": true }  ← sentinel last
```

Якщо power fail відбувається до запису `"complete"` → файл може містити valid JSON але без sentinel → MeasurementStore::load() має відхилити його.

**Правильна реалізація:**
```cpp
DeserializationError err = deserializeJson(doc, file);
if (err) { return false; }                            // bad JSON
if (!doc["complete"].is<bool>()) { return false; }   // missing sentinel
if (!doc["complete"].as<bool>()) { return false; }   // sentinel = false
// all fields validated → safe to use
```

**Відмінність від аудиторного рекомендації:** Аудитор пропонував зберігати `"complete"` першим для "швидкої перевірки". Це зайве (ArduinoJson не streaming parser) і суперечить ADR-ST-006 write-first invariant (sentinel MUST бути останнім записаним). Зберігати `"complete"` ОСТАННІМ — правильно. Перевіряти при `load()` — обов'язково.

---

### Агентська додаткова знахідка (не в оригінальному аудиті)

**A-04 мав друге входження в LOGGER_ARCHITECTURE.md.** Зовнішній аудит зафіксував `portMAX_DELAY` тільки в `rotate()` (§6.6). Агент виявив ідентичне порушення SA2-8 і в `processEntry()` (§6.6, ~12 рядків вище). Обидва виправлені одночасно:
- `processEntry()`: `portMAX_DELAY` → `pdMS_TO_TICKS(200)` з graceful drop
- `rotate()`: `portMAX_DELAY` → `pdMS_TO_TICKS(1000)` з graceful skip

---
