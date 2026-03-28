# CoinTrace — Матриця покриття тестами

**Тип документа:** Технічна довідка
**Версія:** 1.0.0
**Дата:** 28 березня 2026
**Статус:** Актуальний

---

## Зміст

1. [Загальна статистика](#1-загальна-статистика)
2. [Тест-сюїти та кейси](#2-тест-сюїти-та-кейси)
3. [Матриця покриття по модулях](#3-матриця-покриття-по-модулях)
4. [Що НЕ покрито тестами](#4-що-не-покрито-тестами)
5. [Тестова інфраструктура](#5-тестова-інфраструктура)
6. [Як запускати тести](#6-як-запускати-тести)

---

## 1. Загальна статистика

| Метрика | Значення |
|---------|----------|
| Тест-сюїтів | 10 |
| Тест-кейсів (total) | 108 |
| Середнє на сюїт | 10.8 |
| Моки (файлів) | 10 (в `test/mocks/`) |
| Середовище | `native-test` (PC, без апаратури) |
| Фреймворк | Unity |

---

## 2. Тест-сюїти та кейси

### test_config_manager — 24 кейси
**Модуль:** `ConfigManager` (in-memory key-value, max 64 записи)

| # | Назва тесту | Що перевіряє |
|---|-------------|-------------|
| 1 | `test_getInt_missing_key_returns_default` | Відсутній ключ → default значення |
| 2 | `test_getInt_negative_default` | Від'ємні default значення |
| 3 | `test_getUInt8_missing_key_returns_default` | UInt8 default при відсутності |
| 4 | `test_getUInt32_missing_key_returns_default` | UInt32 default при відсутності |
| 5 | `test_getFloat_missing_key_returns_default` | Float default при відсутності |
| 6 | `test_getBool_missing_key_returns_default_false` | Bool false default |
| 7 | `test_getBool_missing_key_returns_default_true` | Bool true default |
| 8 | `test_getString_missing_key_returns_default` | String default |
| 9 | `test_setInt_getInt_roundtrip` | Int set/get round-trip |
| 10 | `test_setInt_negative_roundtrip` | Від'ємні int round-trip |
| 11 | `test_setUInt32_getUInt32_roundtrip` | UInt32 round-trip |
| 12 | `test_setFloat_getFloat_roundtrip` | Float round-trip з допуском |
| 13 | `test_setFloat_release_threshold_roundtrip` | Float coin threshold round-trip |
| 14 | `test_setBool_true_stores_true` | Bool true зберігається |
| 15 | `test_setBool_false_stores_false` | Bool false зберігається |
| 16 | `test_setString_getString_roundtrip` | String round-trip |
| 17 | `test_getUInt8_value_255_accepted` | UInt8 max (255) приймається |
| 18 | `test_getUInt8_value_0_accepted` | UInt8 min (0) приймається |
| 19 | `test_getUInt8_value_256_returns_default` | Overflow (256) → default |
| 20 | `test_getBool_string_true_returns_true` | `"true"` → true |
| 21 | `test_getBool_string_1_returns_true` | `"1"` → true |
| 22 | `test_getBool_string_false_returns_false` | `"false"` → false |
| 23 | `test_getBool_string_0_returns_false` | `"0"` → false |
| 24–28 | *(upsert + capacity)* | Overwrite при дублікаті; 64 записи; 65-й відкидається |

---

### test_fingerprint_cache — 6 кейсів
**Модуль:** `FingerprintCache` (5D FP база, дистанційний запит)

| # | Назва тесту | Що перевіряє |
|---|-------------|-------------|
| 1 | `test_query_returns_zero_on_empty_cache` | Порожній кеш → 0 результатів, `!isReady()` |
| 2 | `test_exact_match_gives_full_confidence` | Точний збіг → distance=0, confidence=1.0 |
| 3 | `test_confidence_at_sigma_distance` | Формула exp(-d²/σ²) при d=σ → ≈0.3679 |
| 4 | `test_top_n_ordered_by_distance` | Top-N відсортовані за зростанням дистанції |
| 5 | `test_max_results_truncation` | Запит 3 результати з 10 → рівно 3 |
| 6 | `test_closer_entry_ranked_first` | Ближчий запис стоїть першим |

---

### test_log_entry — 11 кейсів
**Модуль:** `LogEntry` (структурований лог: timestamp + level + component + message)

| # | Назва тесту | Що перевіряє |
|---|-------------|-------------|
| 1 | `test_levelToString_all_levels` | Всі рівні → 5-char рядок (DEBUG, INFO , WARN , ERROR, FATAL) |
| 2 | `test_levelToChar_all_levels` | Всі рівні → 1-char код (D, I, W, E, F) |
| 3–7 | *(toText формат)* | Timestamp, level, component, message у тексті; повний шаблон |
| 8 | `test_toText_component_truncated_to_19_chars` | Component ≤ 19 символів |
| 9–10 | *(toJSON)* | Ключі t/l/c/m присутні; завершується `\n` |
| 11 | `test_toBLECompact_format` | BLE формат: levelChar\|ts\|component\|message |

---

### test_logger — 13 кейсів
**Модуль:** `Logger` (мульти-транспортний диспетчер)

| # | Назва тесту | Що перевіряє |
|---|-------------|-------------|
| 1 | `test_logger_begin_required_before_use` | `begin()` ініціалізує mutex |
| 2 | `test_logger_addTransport_calls_begin` | `addTransport()` викликає `begin()` транспорту |
| 3 | `test_logger_addTransport_max_four` | Максимум 4 транспорти; 5-й відхилено |
| 4 | `test_logger_addTransport_null_returns_false` | Null-транспорт відхилено |
| 5–6 | *(dispatch)* | info()/error() доставляються до транспорту; до всіх транспортів |
| 7 | `test_logger_timestamp_comes_from_millis` | Timestamp береться з `millis()` |
| 8–10 | *(level filtering)* | Global і per-transport min level фільтрація |
| 11 | `test_logger_truncates_long_message` | Повідомлення >256 байт truncate |
| 12 | `test_logger_removeTransport_stops_dispatch` | `removeTransport()` зупиняє доставку |
| 13 | `test_logger_ring_integration_getLastError` | Logger → RingBuffer: `getLastError()` повертає ERROR |

---

### test_measurement_store — 11 кейсів
**Модуль:** `MeasurementStore` (збереження вимірювань у FS)

| # | Назва тесту | Що перевіряє |
|---|-------------|-------------|
| 1–4 | *(rp[0] валідація)* | rp=0, від'ємний, <1.0 відхиляються; rp=1.0 приймається |
| 5 | `test_save_rejects_large_valid_rp_without_fs` | Без FS → false |
| 6–10 | *(sentinel)* | JSON без `"complete": true` відхиляється (null, false, string, відсутній) |
| 11 | `test_measurement_struct_sizes` | Розміри полів структури відповідають контракту |

---

### test_metal_matcher — 13 кейсів
**Модуль:** `MetalMatcher` (5D Euclidean match + confidence + alternatives)

| # | Назва тесту | Що перевіряє |
|---|-------------|-------------|
| 1–2 | `test_matchFull/Quick_invalid_on_empty_cache` | Порожній кеш → invalid результат |
| 3 | `test_matchFull_exact_match_confidence_one` | Точний збіг → confidence=1.0, valid=true |
| 4 | `test_matchFull_distant_match_low_confidence` | Далекий → confidence<0.3 → valid=false |
| 5 | `test_matchFull_selects_closer_entry` | Правильний top-1 з двох записів |
| 6 | `test_matchFull_alternatives_sorted_by_distance` | Alternatives відсортовані за дистанцією |
| 7 | `test_matchFull_dist_components_respect_weights` | dist_components[i]=0 при weight[i]=0 |
| 8 | `test_matchQuick_algo_and_weights` | `matchQuick()` використовує `ALGO_QUICK` + quick_weights |
| 9–10 | *(ferro flag)* | is_ferro вимкнено при threshold=99.0; вмикається при зниженні threshold |
| 11 | `test_confidence_formula_matches_expected` | Confidence спадає з дистанцією |
| 12 | `test_resetConfig_restores_defaults` | `resetConfig()` відновлює sigma=0.35 |
| 13 | `test_isReady_false_on_empty_cache` | `isReady()` відображає стан кешу |

---

### test_nvs_manager — 9 кейсів
**Модуль:** `NVSManager` (NVS storage: counters, WiFi, calibration)

| # | Назва тесту | Що перевіряє |
|---|-------------|-------------|
| 1 | `test_begin_sets_ready` | `begin()` → `isReady()=true` |
| 2–4 | *(meas counter)* | Початок з 0; increment; wrap = `count % NVS_RING_SIZE` |
| 5 | `test_increment_on_not_ready_is_noop` | Increment на !ready — safe no-op |
| 6 | `test_wifi_round_trip` | SSID/password/mode round-trip |
| 7 | `test_calibration_round_trip` | Калібровка сенсора round-trip |
| 8 | `test_soft_reset_preserves_sensor` | softReset зберігає калібровку сенсора |
| 9 | `test_hard_reset_clears_all` | hardReset очищує все включно з калібровкою |

---

### test_ota_nvs — 14 кейсів
**Модуль:** `NVSManager` OTA namespace (стан OTA-оновлення)

| Групи | Що перевіряє |
|-------|-------------|
| Fresh load | loadOtaMeta() → false; нульований struct |
| Save/load | save → load round-trip; pending=true, confirmed=false |
| Version | pre_version зберігається; довга версія truncate без overflow |
| Confirm/clear | setOtaConfirmed() → confirmed=true; clearOtaMeta() скидає флаги |
| Happy/rollback | Повний lifecycle: save → confirm → clear; rollback: save → clear |
| Error paths | !ready → save/load повертають false |

---

### test_ring_buffer — 16 кейсів
**Модуль:** `RingBufferTransport` (кільцевий лог-буфер)

| Групи | Що перевіряє |
|-------|-------------|
| Empty state | count=0; getEntries/getLastError на порожньому |
| Write | Increment count; правильний вміст |
| Capacity | Заповнення до capacity; wrap-around: старіші записи видаляються |
| Filtering | getEntries з minLevel; getLastError лише ERROR/FATAL |
| Maintenance | clear() → count=0; запис після clear; maxCount ліміт |

---

### test_vector_compute — 9 кейсів
**Модуль:** `VectorCompute` (нормалізація 5D вектора вимірювання)

| # | Назва тесту | Що перевіряє |
|---|-------------|-------------|
| 1 | `test_dRp1_positive_for_conductive` | dRp1 > 0 для провідних металів |
| 2 | `test_k1_k2_in_unit_interval` | k1, k2 ∈ (0, 1) |
| 3 | `test_k2_less_than_k1_for_conductors` | k1 > k2 (крутіший спад біля baseline) |
| 4 | `test_slope_negative` | slope < 0 для всіх металів |
| 5 | `test_dL1_discriminates_ferrous` | dL1 мале для Ag, велике для Fe |
| 6 | `test_guard_zero_rp0` | Division-by-zero guard: rp[0]=0 → всі 0 |
| 7 | `test_slope_ols_ground_truth` | slope = (k2-1)/2 (p3 протокол, ground truth) |
| 8 | `test_drift_ratio_zero_no_drift` | driftRatio=0 коли rp[3]=rp[0] |
| 9 | `test_drift_ratio_detects_drift` | driftRatio > DRIFT_THRESHOLD при відхиленні >5% |

---

## 3. Матриця покриття по модулях

| Модуль | Тест-сюїт | Кейсів | Покриття |
|--------|-----------|--------|---------|
| `ConfigManager` | test_config_manager | 24 | ✅ Добре |
| `FingerprintCache` | test_fingerprint_cache | 6 | ⚠️ Базове (weights, weighted query не тестуються окремо) |
| `LogEntry` | test_log_entry | 11 | ✅ Добре |
| `Logger` | test_logger | 13 | ✅ Добре |
| `MeasurementStore` | test_measurement_store | 11 | ✅ Добре |
| `MetalMatcher` | test_metal_matcher | 13 | ✅ Добре |
| `NVSManager` (загальний) | test_nvs_manager | 9 | ✅ Добре |
| `NVSManager` (OTA) | test_ota_nvs | 14 | ✅ Добре |
| `RingBufferTransport` | test_ring_buffer | 16 | ✅ Добре |
| `VectorCompute` | test_vector_compute | 9 | ✅ Добре |

---

## 4. Що НЕ покрито тестами

### 4.1 Компоненти без unit-тестів

| Компонент | Причина відсутності тестів | Ризик |
|-----------|---------------------------|-------|
| `LDC1101Plugin` | Header-only, залежить від SPI/FreeRTOS HW | Середній — тільки HW тест |
| `LDC1101Plugin::recalibrate()` | Wave 8 C-7c — нова функція; потребує HW тесту | Середній |
| `LDC1101Plugin::getLiveRp/L()` | Mutex + кешовані дані — потребує HW тесту | Низький |
| `StorageManager` (facade) | Orchestrates NVS+LFS+SD — інтеграційний рівень | Низький (компоненти покриті) |
| `SDCardManager` | SD mock доступний, тестів немає | Середній |
| `LittleFSManager` | LittleFS mock доступний, тестів немає | Середній |
| `WiFiManager` | Wi-Fi stack не mockується у native | Низький |
| `HttpServer` | Async HTTP + WebSocket — потребує інтеграційного тесту | Середній |
| `main.cpp` (UI, state machine) | Апаратний відображений дисплей | Низький |

### 4.2 Функції/шляхи без покриття у наявних тестах

| Модуль | Непокритий шлях |
|--------|----------------|
| `FingerprintCache::query()` | Weighted query з `weights != nullptr` окремо |
| `FingerprintCache::query()` | Граничні випадки при maxResults=1 |
| `MetalMatcher::loadConfig()` | SD-шлях (config read з SD — потребує SD mock або HW) |
| `MetalMatcher::logTopCandidates()` | Форматування виводу top candidates |
| `MeasurementStore` | Успішний шлях збереження з мок-FS |
| `NVSManager` | Concurrent access / FreeRTOS захист |

### 4.3 Відомі технічні борги по тестах

| ID | Опис | Пріоритет |
|----|------|-----------|
| W-QS1 | `doMatch()` у MetalMatcher перераховує confidence власним σ (ігноруючи FingerprintCache::CONFIDENCE_SIGMA) — 2× `expf` на запис при 50 Hz | Wave 9 |
| T-SD-1 | `SDCardManager` не має unit-тестів незважаючи на наявність SD mock | Wave 9 |
| T-LFS-1 | `LittleFSManager` не має unit-тестів незважаючи на наявність LittleFS mock | Wave 9 |
| T-MM-1 | `MetalMatcher::loadConfig()` тестується лише для порожнього SD (шлях помилки); happy path з валідним JSON не покритий | Wave 9 |

---

## 5. Тестова інфраструктура

### Директорія мок-файлів

```
test/mocks/
├── Arduino.h          — Serial, millis(), delay(), String, strlcpy/snprintf
├── LittleFS.h         — LittleFS.open/read/write/exists/mkdir mock
├── Preferences.h      — NVS key-value mock з resetAll()
├── SD.h               — SD.open/read/write/exists mock
├── Print.h            — serial output mock
├── esp32s3/           — ESP32-S3 system stubs
├── freertos/          — FreeRTOS semphr/task/queue stubs
└── mock_impl.cpp      — g_mock_millis: контроль часу в тестах
```

### Принципи мокування

| Принцип | Реалізація |
|---------|-----------|
| Всі тести native (без HW) | Всі ESP32-специфічні API замінені стабами |
| Ізоляція між тестами | `setUp()`/`tearDown()` скидає стан (Preferences::resetAll() тощо) |
| Детермінованість часу | `g_mock_millis` дозволяє контролювати `millis()` |
| `UNIT_TEST` макрос | FingerprintCache::loadTestEntry/beginTest/resetTest для test-only DB |

---

## 6. Як запускати тести

```bash
# Всі unit-тести (native, PC):
pio test -e native-test

# Один сюїт:
pio test -e native-test -f test_metal_matcher
pio test -e native-test -f test_fingerprint_cache
pio test -e native-test -f test_vector_compute

# Очікуваний результат:
# 108/108 PASS
```

**Важливо:** `cointrace-test` (device tests) потребує підключеного ESP32-S3 і LDC1101, і є окремим від native-test.

---

*Матриця актуальна станом на 28 березня 2026. Оновлювати при додаванні нових тест-кейсів або нових непокритих модулів.*
