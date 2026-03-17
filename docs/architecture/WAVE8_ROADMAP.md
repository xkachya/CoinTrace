# Wave 8 Roadmap — Connectivity + Infrastructure + Sensor Integration

**Статус:** 📋 Planning  
**Версія:** 1.1.0  
**Дата:** 2026-03-17 (оновлено після незалежного аудиту)  
**Попередня хвиля:** Wave 7 — Storage Foundation (`d53a440`, 84/84 native tests, hardware verified)

---

## Контекст та головний constraint

Wave 8 стартує з унікальним constraint: **LDC1101 Click Board (MIKROE-3240) ще в дорозі.** Це визначає всю стратегію: максимальна кількість роботи виконується до прибуття сенсора, щоб після прибуття залишився тільки hardware-gated мінімум.

**Ключовий висновок аналізу залежностей:**

> Весь connectivity stack, web UI, HTTP API, testing infrastructure, та vector math — **не залежать від фізичного сенсора.** Лише 6 з 16 задач є hardware-gated.

Cross-ref: [STORAGE_ARCHITECTURE.md §15](./STORAGE_ARCHITECTURE.md), [CONNECTIVITY_ARCHITECTURE.md](./CONNECTIVITY_ARCHITECTURE.md), [FINGERPRINT_DB_ARCHITECTURE.md](./FINGERPRINT_DB_ARCHITECTURE.md)

---

## Зміст

1. [Матриця залежностей від LDC1101](#1-матриця-залежностей-від-ldc1101)
2. [Track A — Connectivity](#2-track-a--connectivity)
3. [Track B — Infrastructure](#3-track-b--infrastructure)
4. [Track C — Sensor Integration (hardware-gated)](#4-track-c--sensor-integration-hardware-gated)
5. [Рекомендована послідовність](#5-рекомендована-послідовність)
6. [RAM бюджет Wave 8](#6-ram-бюджет-wave-8)
7. [Acceptance Criteria](#7-acceptance-criteria)

---

## 1. Матриця залежностей від LDC1101

| Компонент | Залежить від LDC1101? | Track | Примітка |
|---|---|---|---|
| WiFiManager AP/STA | ❌ | A-1 | Чистий networking |
| AsyncWebServer + mDNS | ❌ | A-2 | Port 80, CORS header |
| `GET /api/v1/status` | ❌ | A-3 | heap/uptime/storage stats |
| `GET /api/v1/measure/{id}` | ❌ | A-3 | Читає Wave 7 MeasurementStore |
| `GET /api/v1/log` | ❌ | A-3 | RingBufferTransport |
| `GET /api/v1/database` | ❌ | A-3 | FingerprintCache::entryCount() |
| **`POST /api/v1/database/match`** | ❌ | A-3 | **Vector у тілі запиту — повністю тестується без сенсора** |
| `GET /api/v1/sensor/state` | ❌ | A-3 | Proxy до getCoinState(); повертає IDLE_NO_COIN до C-2 |
| OTA mechanism | ❌ | A-4 | ADR-007 фізична клавіша 'O' |
| Web UI (HTML/CSS/JS) | ❌ | A-5 | Match screen тестується через manual POST |
| WebSocket (status+log frames) | ❌ | A-6 | Sensor frames = stub |
| BLE GATT service | ❌ | A-7 | Опційно, Wave 8 backlog |
| `test_nvs_manager/` | ❌ | B-1 | Native, Preferences mock готовий |
| `test_fingerprint_cache/` | ❌ | B-2 | Потребує loadTestEntry() accessor |
| GPIO0 boot recovery | ❌ | B-3 | Тривіальний setup() fix (§17.2 STORAGE_ARCH) |
| **Vector computation math** | ❌ | **C-3\*** | **Чиста математика над float[4] — unit-testable зараз!** |
| `POST /api/v1/measure/start` | ⚠️ partial | A-3 | Stub: `503 sensor_not_ready` до C-2 |
| WebSocket sensor frames | ⚠️ partial | A-6 | Stub → real після C-2 |
| R-01 → real protocol_id | ✅ БЛОК | C-1 | Визначає fSENSOR MIKROE-3240 |
| Multi-position state machine | ✅ БЛОК | C-2 | Фізичне переміщення по 4 дистанціях |
| queryFingerprint() wiring | ✅ БЛОК | C-4 | Залежить від C-2 (vector є) + C-3 (math є) |
| FingerprintCache σ tuning | ✅ БЛОК | C-5 | Потребує реальних монет |
| Real FP DB seeding | ✅ БЛОК | C-6 | Замінити synthetic entries |

> **C-3\*** — єдиний виняток з Track C: vector math (dRp1/k1/k2/slope/dL1) є чистою математикою над `float[4]`, незалежною від SPI. Реалізується та тестується до прибуття сенсора.

---

## 2. Track A — Connectivity

### A-1: WiFiManager

**Нові артефакти:** `lib/WiFiManager/src/WiFiManager.h/.cpp`

```cpp
class WiFiManager {
public:
    // Стартує AP або STA залежно від NVS "wifi" namespace.
    // AP mode: SSID="CoinTrace-XXXX" (suffix=last 4 MAC hex), pass="cointrace", IP=192.168.4.1
    // STA mode: NVS ssid/pass → WiFi.begin() + mDNS "cointrace.local"
    bool begin(NVSManager& nvs);

    // TCA8418 keyboard → readLine(ssid) + readLine(pass) → NVS saveWifi() → reconnect
    // Викликати через клавішу 'W' або через Web UI provisioning page
    bool promptSTA(NVSManager& nvs);

    bool        isConnected() const;
    const char* getIP()       const;  // "192.168.4.1" або DHCP IP
    const char* getSSID()     const;
};
```

**Залежності:** `NVSManager::loadWifi()` / `saveWifi()` — Wave 7 ✅  
**ADR:** ADR-005 (WiFi Provisioning via keyboard), ADR-006 (WiFi + BLE coexistence)  
**Дисплей:** показати IP + SSID. QR-код `http://192.168.4.1` для AP mode.

> **W-01 (audit):** QR generation на ESP32 потребує бібліотеку (~10 KB RAM). Альтернативи для v1: (a) pre-rendered PNG у `LittleFS_sys/web/qr-ap.png` (статичний, завжди `192.168.4.1`), або (b) `qrcode.js` (15 KB) на клієнті — генерує QR з актуального IP динамічно. Рекомендую варіант (b) для STA mode де IP динамічний.

**main.cpp hook:**
```cpp
// ── 6. WiFiManager (Wave 8 A-1) ─────────────────────────────
gWifi.begin(gNVS);
gCtx.wifi = &gWifi;  // inject into PluginContext (Wave 8 extension)
```

---

### A-2: AsyncWebServer + mDNS

**Нові артефакти:** `lib/HttpServer/src/HttpServer.h/.cpp`

```cpp
class HttpServer {
public:
    // Реєструє всі /api/v1/ routes та статику /sys/web/.
    // CORS header: Access-Control-Allow-Origin: *
    // ADR-002: всі endpoints тільки через /api/v1/ prefix.
    void begin(AsyncWebServer& srv, IStorageManager& storage, NVSManager& nvs);

private:
    void registerRoutes();      // реєструє всі handler-и
    void serveStatic();         // /sys/web/index.html → LittleFS_sys
    void handle404(AsyncWebServerRequest*);
};
```

**Library:** `ESP Async WebServer` (вже в platformio.ini залежностях)  
**Port:** 80  
**Static files:** `LittleFSManager::sys().open("/web/index.html")` → `AsyncWebServer::serveStatic()`

---

### A-3: HTTP REST Endpoints

#### Повністю функціональні без сенсора:

```
GET  /api/v1/status
     ← heap, heap_min, uptime, wifi, ble, storage{lfs_free_kb, sd_free_mb, meas_count}

GET  /api/v1/measure/{id}
     ← ID range validation: 404 якщо id >= meas_count або id < meas_count - RING_SIZE
     ← Читає LittleFS /data/measurements/m_XXX.json (Wave 7 виміри з metal_code="UNKN")
     Cross-ref: STORAGE_ARCHITECTURE.md §12.3 [PRE-2]

GET  /api/v1/log?n=50&level=DEBUG&since_ms=0
     ← RingBufferTransport::getEntries()

GET  /api/v1/database
     ← FingerprintCache::entryCount() + список metal_code груп

POST /api/v1/database/match            ← КЛЮЧОВИЙ для pre-sensor розробки UI
     body: {"algo_ver":1,"protocol_id":"p1_UNKNOWN_013mm","vector":{...}}
     ← ctx->storage->queryFingerprint() → top-N FPMatch
     ← 400 якщо відсутній vector або значення поза BOUNDS
     ← 503 якщо FingerprintCache не завантажений
     ← 200 {"match":null} якщо protocol_id не знайдено (не 404)

GET  /api/v1/sensor/state
     ← {"state":"IDLE_NO_COIN"|"COIN_PRESENT"|"COIN_REMOVED"|"MEASURING_STEP_1"|"MEASURING_STEP_3"|"MEASURING_DRIFT"|"CALIBRATING"}
     ← До C-2: повертає лише IDLE_NO_COIN/COIN_PRESENT/COIN_REMOVED (базові стани LDC1101Plugin::getCoinState())
     ← Після C-2: повні стани multi-position state machine
     ← Критично для Web UI: клієнт polls цей endpoint під час calibrate() (2.5 сек) замість timeout

GET  /api/v1/ota/status
     ← NVS "ota": version, latest, update_available

POST /api/v1/ota/update
     ← 403 якщо не активоване фізичне OTA вікно (ADR-007)
     ← Content-Type: application/octet-stream, Content-Length обов'язковий
```

#### Stub endpoints (503 до прибуття сенсора):

```
POST /api/v1/measure/start
     ← 503 {"error":"sensor_not_ready"} — до C-2
     ← 409 {"error":"already_measuring"} — після C-2

POST /api/v1/calibrate
     ← 503 {"error":"sensor_not_ready"} — до C-2
```

> **`POST /api/v1/database/match`** — найважливіший для pre-sensor тестування. Дозволяє тестувати весь matching pipeline: вручну зібрати vector → POST → отримати match result → перевірити Web UI відображення. Весь flow верифікується ще до першого вимірювання.

---

### A-4: OTA Firmware Update

**Механіка (ADR-007):**
1. Клавіша `'O'` → 30-секундне вікно + зворотний відлік на дисплеї
2. `POST /api/v1/ota/update` (Content-Type: application/octet-stream) → ESP-IDF OTA partition swap
3. Reboot → auto-rollback якщо не підтвердив `'O'` знову протягом 60с

**NVS integration:**
- `saveOtaMetadata(preVersion, confirmed=false)` перед `Update.apply()`
- `markOtaConfirmed()` після успішного boot + фізичного підтвердження

**Backlog v2:** ECDSA підпис firmware binary (в v1 ризик прийнято — фізичний доступ = авторизація).

---

### A-5: Web UI (data/web/)

**Стек:** Vanilla HTML/CSS/JS без build step або Preact (~3 KB). Цільовий розмір < 200 KB gzip. Розміщення: `data/web/` → `pio run -e uploadfs-sys -t uploadfs` (Wave 7 pipeline вже готовий).

> **W-03 (audit):** Web UI завжди займає більше часу ніж очікується. Розбити на **A-5a (MVP)** і **A-5b (повна версія)** — це зменшує critical path фази 1 на ~2-3 дні.

#### A-5a: MVP (достатньо для Phase 1 acceptance criteria) — ~2 дні

| Екран | Endpoint(s) | Доступний без сенсора |
|---|---|---|
| Status | `GET /status` | ✅ |
| **Match** | `POST /database/match` | ✅ ← ключовий pre-sensor test |

**Match screen (pre-sensor режим):** форма для ручного введення 5 компонентів вектора → POST → відображення match result + alternatives. Після прибуття сенсора — замінюється автозаповненням з останнього виміру.

#### A-5b: Повна версія — ~3 дні (не в critical path фази 1)

| Екран | Endpoint(s) | Доступний без сенсора |
|---|---|---|
| Measurements | `GET /measure/{id}` | ✅ (Wave 7 UNKN виміри) |
| Log stream | WebSocket `"t":"log"` | ✅ |
| Settings | `GET/POST` NVS fields | ✅ |
| Sensor stream | WebSocket `"t":"sensor"` | ⏳ після C-2 |

---

### A-6: WebSocket Streaming

**Endpoint:** `ws://cointrace.local/api/v1/stream`  
**Library:** `AsyncWebSocket` (входить в `ESP Async WebServer`)

**Frames реалізовані без сенсора:**
```json
{"v":1,"t":"status","heap":320000,"heap_min":295040,"uptime":1234}
{"v":1,"t":"log","level":"INFO","comp":"Cache","msg":"...","ms":1234}
{"v":1,"t":"heartbeat"}
```

**Frames-стаби (активуються в Track C):**
```json
{"v":1,"t":"sensor","rp":1250.5,"l":18.2,"pos":0,"ts":1234567}
{"v":1,"t":"result","match":"Ag925","conf":0.94,"vector":{...}}
{"v":1,"t":"shutdown_pending","hold_ms_remaining":1500}
{"v":1,"t":"shutdown_complete"}
```

**Heartbeat:** Server → `{"v":1,"t":"ping"}` кожні 10 с → client відповідає `{"v":1,"t":"pong"}`.  
**Reconnect:** client exponential backoff 1s → 2s → 4s → max 30s.  
**Max clients:** 4 (ADR-006, OOM prevention).

---

### A-7: BLE GATT Service (опційно, Wave 8 backlog)

Визначено в CONNECTIVITY_ARCHITECTURE.md §5.4. UUIDs зафіксовані назавжди (ADR-004):

```
Service:  D589804A-228E-4171-BE8B-872534A652C6
MEASURE:  5AF690DC-5EA5-4166-80B4-2138AC7CF491  Write
RESULT:   9AF88885-31CE-412B-A567-94052E9598BC  Notify
STATUS:   E7FAB8DE-E68B-44B3-A851-4CC777486DF3  Read+Notify
LOG:      CAB81902-EF36-410E-A8AE-3891C00375CD  Notify
RAW:      CFF322C3-6E5C-4008-996C-7A08BDAD4C53  Notify
```

**Пріоритет:** Низький для Wave 8 фази 1 (A-1..A-6). BLE не потрібен поки WiFi AP покриває всі use cases. Реалізувати після верифікації WiFi stack + heap budget.

---

## 3. Track B — Infrastructure

### B-1: test_nvs_manager/

**Передумова:** `test/mocks/Preferences.h` існує (TODO wave8 з NVSManager.h:35).

**Тест-кейси:**

| # | Що тестуємо |
|---|---|
| 1 | `begin()` → `isReady()` = true |
| 2 | `incrementMeasCount()` × 3 → `getMeasCount()` = 3 |
| 3 | `getMeasSlot()` = `getMeasCount() % RING_SIZE` |
| 4 | `incrementMeasCount()` overflow (uint32 wraparound) |
| 5 | `loadWifi()` / `saveWifi()` round trip |
| 6 | `softReset()` → "wifi"/"system" очищені, "sensor" збережений |
| 7 | `hardReset()` → всі namespaces очищені |
| 8 | `loadCalibration()` → `saveCalibration()` → `loadCalibration()` round trip |

**Розташування:** `test/test_nvs_manager/test_nvs_manager.cpp`

---

### B-2: test_fingerprint_cache/

**Передумова:** потрібен `loadTestEntry()` accessor в `FingerprintCache.h`:

```cpp
#ifdef UNIT_TEST
// Inject a pre-built cache entry for testing. Not available in production build.
void loadTestEntry(const CacheEntry& e) {
    if (count_ < MAX_ENTRIES) entries_[count_++] = e;
}
#endif
```

**Тест-кейси** (зафіксовані в FingerprintCache.h:86 TODO):

| # | Що тестуємо |
|---|---|
| 1 | `query()` повертає 0 при порожньому кеші (count_==0) |
| 2 | Weighted Euclidean distance + insertion sort (top-N ordering) |
| 3 | `confidence = exp(-d²/σ²)`: d=0 → 1.0, d=σ → ~0.368 |
| 4 | maxResults truncation: запит 3 з кешу 10 → повертає 3 |
| 5 | `queryFingerprint()` через MockStorageManager → делегує до FingerprintCache |

**Розташування:** `test/test_fingerprint_cache/test_fingerprint_cache.cpp`

---

### B-3: GPIO0 Boot Recovery

**Проблема:** P-1 Acceptance Criteria містить чекбокс «GPIO0 held at boot → LittleFS_data formatted» — не реалізовано (STORAGE_ARCHITECTURE.md §17.2 [1.4]).

**Fix у `setup()` — додати після `gLogger.begin()`:**

```cpp
// ── 1.4. Deep sleep wakeup / GPIO0 boot recovery ─────────────────────
// Per STORAGE_ARCHITECTURE §17.2: GPIO0 = dual-role
//   Boot-time (here):    held LOW → format LittleFS_data → restart (factory data clear)
//   Runtime (loop()):    long-press 2s → soft shutdown fallback if TCA8418 fails
//
// Skip if waking from deep sleep (Soft Shutdown Fn+Q — §14.3).
if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED) {
    pinMode(0, INPUT_PULLUP);
    if (digitalRead(0) == LOW) {
        Serial.println("[BOOT] GPIO0 held — formatting LittleFS_data...");
        LittleFSManager tempLfs;
        if (tempLfs.mountData()) tempLfs.formatData();
        esp_restart();
    }
}
```

---

## 4. Track C — Sensor Integration (hardware-gated)

### C-1: R-01 — Визначення реального protocol_id  ⚠️ ПЕРШОЧЕРГОВО після прибуття

**Що:** Виміряти реальну операційну частоту fSENSOR котушки MIKROE-3240.

**Очікування:** документація передбачає ~200–500 kHz (НЕ 1 MHz як у placeholder).

**Процедура:**
1. Підключити LDC1101, запустити пристрій
2. Прочитати boot log: `initialize()` і `calibrate()` обчислюють fSENSOR за Eq.6 (`fSENSOR = fCLKIN × RESP_TIME / (3 × L_DATA)`) та Eq.11 (`fSENSOR = LHR_DATA × 2 × fCLKIN / 2²⁴`); обидва значення логуються в `[LDC1101]` рядках. `REG_DIG_CONFIG` містить RESP_TIME/MIN_FREQ — не fSENSOR напряму.
3. Зафіксувати у вигляді `protocol_id` за форматом FINGERPRINT_DB §3.2
4. Оновити NVS "sensor"."proto_id" (в коді та seed data)

**Оновити після R-01:**
- `data/plugins/ldc1101.json` — замінити "p1_UNKNOWN_013mm"
- `FINGERPRINT_DB_ARCHITECTURE.md §7` — "Frozen physical constants"
- Всі 5 synthetic seed entries в `data/plugins/ldc1101.json`

> ⚠️ До R-01 — жоден запис **не публікувати** в community DB. `protocol_id = "p1_UNKNOWN_013mm"` робить їх community-incompatible (мовчазна несумісність при зміні protocol_id).

---

### C-2: Multi-position measurement state machine

**Поточний стан loop():** зберігає вимір при `COIN_REMOVED` з 1 позицією (`pos_count=1`).

**Цільова state machine (4 позиції + drift check):**

```
IDLE ──── coin placed ────►  STEP_0   rp[0],l[0]
                                │
                 keyboard 'ENTER': "Place on 1mm spacer, press ENTER"
                           (timeout 120s → abort → IDLE)
                                ▼
                             STEP_1   rp[1],l[1]
                                │
                 keyboard 'ENTER': "Place on 3mm spacer, press ENTER"
                           (timeout 120s → abort → IDLE)
                                ▼
                             STEP_3   rp[2],l[2]    ← назва STEP_3 = дистанція
                                │
                 keyboard 'ENTER': "Return to 0mm (no spacer), press ENTER"
                           (timeout 120s → abort → IDLE)
                                ▼
                          STEP_DRIFT  rp[3],l[3]    ← drift check: rp[3] ≈ rp[0]
                                │
                          STEP_COMPUTE → computeVector() → queryFingerprint() → save()
                                │
                              IDLE
```

**Семантика `rp[3]` (W-07 ADR):** `rp[3]` = reading at 0mm після повернення монети (перевірка дрейфу). Спрощення: якщо `|rp[3] - rp[0]| / rp[0] > 0.05` (5%) → лог WARNING "Sensor drift detected" + зберегти вимір з `conf = 0.0` (не входить у matching). `rp[3]` **не входить** у VectorCompute — тільки rp[0..2].

> **W-08 (audit):** Timeout 120 с (не 60 с) — фізична дія + пошук spacer займає більше часу ніж здається. `esp_timer` або `millis()` від входу в стан.
>
> **W-09 (audit):** Step detection v1 = клавіатурне підтвердження 'ENTER' ("Press when ready"). Автоматична детекція через RP threshold (RP при 1mm < RP при 0mm) — v1.5 enhancement.

**Дисплей під час вимірювання:**
```
[CoinTrace] Step 2 / 4
────────────────────
   Raise coin 1mm
   ■□□□ ...
   Place on 1mm spacer →
```

**Timeout:** якщо наступний крок не виконаний за **120 с** → abort → `IDLE`.

---

### C-3: Vector computation  ★ Розблокований — реалізувати ЗАРАЗ

**Нові артефакти:** `lib/StorageManager/src/VectorCompute.h` (header-only, чиста математика)

```cpp
// VectorCompute.h — Fingerprint Vector Computation (Wave 8 C-3)
// Pure math over Measurement::rp[4] / l[4]. No hardware dependency.
// Cross-ref: FINGERPRINT_DB_ARCHITECTURE.md §3.3

#pragma once
#include "Measurement.h"

namespace VectorCompute {

// dRp1 = Rp(0mm) − Rp(1mm)  [Ohm]
// Represents total conductivity response at contact distance.
// FINGERPRINT_DB §3.3: must be positive (Rp decreases with coin closer)
inline float dRp1(const Measurement& m) {
    return m.rp[0] - m.rp[1];
}

// k1 = Rp(1mm) / Rp(0mm)  [dimensionless, 0..1]
// Size-normalized: larger coin → same k1 as smaller coin of same metal.
// proof: FINGERPRINT_DB §2.1 Prior Art Disclosure math.
inline float k1(const Measurement& m) {
    if (m.rp[0] < 1.0f) return 0.0f;  // guard: avoid div-by-zero
    return m.rp[1] / m.rp[0];
}

// k2 = Rp(3mm) / Rp(0mm)  [dimensionless, 0..1]
// Second spatial ratio; combined with k1 gives 2D metal signature.
inline float k2(const Measurement& m) {
    if (m.rp[0] < 1.0f) return 0.0f;
    return m.rp[2] / m.rp[0];
}

// slope_rp_per_mm_lr = linear regression coefficient of k-ratios vs distance [1/mm]
// Steps: d0=0, d1=1, d3=3 mm. Typically negative (−0.05..−0.20).
// Method: least-squares over 3 points {(0, rp[0]/rp[0]), (1, rp[1]/rp[0]), (3, rp[2]/rp[0])}
// = OLS slope = (Σxy − n·x̄·ȳ) / (Σx² − n·x̄²)
// Cross-ref: STORAGE_ARCHITECTURE §8.3 field "slope_rp_per_mm_lr"
float slope(const Measurement& m);   // implemented in VectorCompute.cpp (non-trivial)

// dL1 = L(0mm) − L(1mm)  [µH]
// Permeability component. Near-zero for Ag/Au, large for Fe/Ni.
inline float dL1(const Measurement& m) {
    return m.l[0] - m.l[1];
}

} // namespace VectorCompute
```

**Unit tests** — `test/test_vector_compute/`:

| # | Тест |
|---|---|
| 1 | dRp1 > 0 для всіх типових металів (rp[0] > rp[1]) |
| 2 | k1 + k2 ∈ (0, 1) для всіх типових металів |
| 3 | slope < 0 (завжди від'ємний) |
| 4 | dL1 ≈ 0 для Ag/Au, dL1 >> 0 для Fe |
| 5 | k1 = 0 при rp[0] = 0 (guard) |
| 6 | slope обчислений методом OLS відповідає known ground truth |

---

### C-4: queryFingerprint() wiring у loop()

**Передумова:** C-2 (state machine) + C-3 (VectorCompute) завершені.

```cpp
// loop() — після STEP_COMPUTE state:
Measurement m = buildMeasurement(rawData);   // C-2 заповнює rp[0..3], l[0..1]

// Compute vector
float vDRp1  = VectorCompute::dRp1(m);
float vK1    = VectorCompute::k1(m);
float vK2    = VectorCompute::k2(m);
float vSlope = VectorCompute::slope(m);
float vDL1   = VectorCompute::dL1(m);

// Query cache (< 1ms — RAM lookup)
IStorageManager::FPMatch matches[3];
uint8_t n = gCtx.storage->queryFingerprint(vDRp1, vK1, vK2, vSlope, vDL1, matches, 3);

if (n > 0) {
    strlcpy(m.metal_code, matches[0].metal_code, sizeof(m.metal_code));
    strlcpy(m.coin_name,  matches[0].coin_name,  sizeof(m.coin_name));
    m.conf = matches[0].confidence;
}

gMeasStore.save(m);

// Broadcast via WebSocket (A-6)
gWebSocket.broadcastResult(m, matches, n);
```

---

### C-5: FingerprintCache σ (sigma) tuning

**Поточний стан:** `CONFIDENCE_SIGMA` = константа-оцінка в `FingerprintCache.h`.

**Процедура після C-1 + C-4:**
1. Виміряти 3–5 монет відомого складу
2. Зафіксувати відстань d до найближчого правильного match у кеші
3. Підібрати σ так щоб `confidence = exp(-d²/σ²) ∈ [0.7, 0.95]` для правильних matches
4. Оновити `CONFIDENCE_SIGMA` в `FingerprintCache.h` + `test_fingerprint_cache/` тест #3

---

### C-6: Real FP DB seeding

**Поточний стан:** 5 synthetic entries у `data/plugins/ldc1101.json`.

**Процедура:**
1. Виміри 3–5 монет різних металів (Ag925, Au999, Cu, Fe) з реальним MIKROE-3240
2. Обчислити вектори через `VectorCompute` (C-3)
3. Згенерувати `index.json` через `tools/batch_analyze.py` (Wave 8 Python CLI)
4. Скопіювати на SD → перевірити `FingerprintCache::init()` → `entryCount()`
5. Запустити `POST /api/v1/database/match` з типовими векторами → верифікувати confidence

---

## 5. Рекомендована послідовність

### Фаза 1: LDC1101 в дорозі

```
Паралельно:
  B-1  test_nvs_manager/          ~1 день    (нативні тести, Preferences mock є)
  B-2  test_fingerprint_cache/    ~1 день    (loadTestEntry() accessor + 5 тестів)
  B-3  GPIO0 boot recovery        ~2 год     (тривіальний setup() fix)
  C-3  VectorCompute              ~1 день    (header-only + unit tests)

Послідовно:
  A-1  WiFiManager AP/STA         ~2 дні
  A-2  AsyncWebServer + mDNS      ~1 день
  A-3  HTTP REST endpoints        ~2 дні    ← database/match тестується одразу
  A-4  OTA mechanism              ~2 дні    ← partition swap + rollback потребує тестування
  A-5a Web UI MVP (Status+Match)  ~2 дні   ← достатньо для фази 1 acceptance
  A-5b Web UI повна версія        ~3 дні   ← не в critical path
  A-6  WebSocket streaming        ~1 день   (status + log frames; sensor = stub)
```

### Фаза 2: Після прибуття LDC1101

```
C-1  R-01 → real protocol_id          ПЕРШОЧЕРГОВО → ~0.5 дні
C-2  Multi-position state machine     ~2-3 дні (hardware iteration на реальних spacers)
C-4  queryFingerprint() wiring        ~0.5 дні (C-2 + C-3 вже готові)
C-5  σ tuning                         ~1 день (реальні монети)
C-6  Real FP DB seeding               ~2 дні (Python CLI tool)
A-x  WebSocket sensor frames (live)   ~0.5 дні (стаби → real)
A-x  POST /measure/start (full)       ~0.5 дні (C-2 готово)
A-7  BLE GATT (опційно)              ~3-4 дні
```

---

## 6. RAM бюджет Wave 8

Детальний аналіз в CONNECTIVITY_ARCHITECTURE.md §8.7. Стислий summary:

| Стан | Зайнято | Вільно (з 337 KB) |
|---|---|---|
| Wave 7 baseline (1 plugin, no WiFi) | ~171 KB | ~166 KB |
| + WiFi stack | ~241 KB | ~96 KB |
| + AsyncWebServer (2 conn.) | ~266 KB | ~71 KB |
| + index.json в RAM (1000 монет) | ~346 KB | **~⚠️ −9 KB** |
| + index.json (200 монет, ~16 KB) | ~282 KB | ~55 KB ✅ |
| WiFi + BLE + index.json 1000 | ~→ OOM | ❌ не відкривати |

> **W-10 (audit):** Числа базуються на CONNECTIVITY_ARCHITECTURE §8.7 (консервативні оцінки). Детальний перерахунок дає **~113-138 KB вільно** після WiFi+WebServer+WebSocket+200 entries. Різниця через те, що числа §8.7 включали PSRAM overhead відсутній на Cardputer-Adv. **Рекомендація: не обмежувати FP DB до 200 entries завчасно.** Виміряти реальний `heap_min` при Phase 1 acceptance test (після першого WebSocket клієнта). Якщо `heap_min` > 100 KB — 1000 entries без BLE реально.

**Висновок для Wave 8 фази 1:** обмежити FP DB до ~200 монет в RAM або вимикати BLE під час Deep Scan (ADR-006).

**Моніторинг:** `GET /api/v1/status` → поле `heap_min` (`ESP.getMinFreeHeap()`). Перевіряти після кожного підключення першого клієнта та після завантаження index.json.

---

## 7. Acceptance Criteria

### Wave 8 фаза 1 (до LDC1101):

- [ ] WiFi AP mode: підключитись телефоном → `http://192.168.4.1` відкриває Web UI (A-5a: Status + Match screens)
- [ ] WiFi STA mode: ввести SSID/pass на клавіатурі → підключитись → `cointrace.local` резолвиться
- [ ] `GET /api/v1/status` → heap, uptime, wifi state, meas_count — коректні
- [ ] `GET /api/v1/sensor/state` → `{"state":"IDLE_NO_COIN"}` без монети
- [ ] `GET /api/v1/measure/{id}` → Wave 7 виміри (UNKN) відображаються в UI
- [ ] `POST /api/v1/database/match` → вручну введений vector → match result з synthetic DB
- [ ] WebSocket: Live log stream відображається у Web UI при подіях
- [ ] OTA: `POST /api/v1/ota/update` без натискання 'O' → 403 Forbidden
- [ ] OTA: `POST /api/v1/ota/update` після 'O' → успішний flash → reboot → auto-rollback test
- [ ] `pio run -e uploadfs-sys -t uploadfs` → Web UI оновлюється, LittleFS_data не торкається
- [ ] Native tests: всі (B-1 + B-2 + C-3 tests) → 100% pass
- [ ] GPIO0 held at boot → Serial: "formatting LittleFS_data..." → restart
- [ ] heap_min > 50 KB після 5 хв роботи з 1 WebSocket клієнтом (зафіксувати фактичне значення для W-10 calibration)

### Wave 8 фаза 2 (після LDC1101):

- [ ] R-01: реальний fSENSOR зафіксований з boot log (Eq.6/Eq.11), protocol_id оновлений, synthetic entries видалені
- [ ] Multi-position: coin placed → 4 кроки + STEP_DRIFT → result на дисплеї та в WebSocket result frame
- [ ] Drift check: `|rp[3] - rp[0]| / rp[0] < 5%` для стабільного сенсора; WARNING лог при перевищенні
- [ ] `queryFingerprint()`: confidence > 0.7 для правильного металу (3 тестові монети)
- [ ] WebSocket sensor frame: real-time rp/l/pos stream при COIN_PRESENT
- [ ] FP DB: мінімум 3 реальні монети різних металів, entryCount() > 0

---

*Версія 1.0.0 — Wave 8 initial planning. Constraint: LDC1101 MIKROE-3240 in transit (2026-03-17).*  
*Версія 1.1.0 — [Wave8-Audit-v1] Впроваджено 9 знахідок зовнішнього аудиту: W-01 QR альтернативи (A-1); W-02 GET /api/v1/sensor/state (A-3, матриця, acceptance); W-03 A-5 split A-5a/A-5b + timeline revision; W-04 WebSocket sensor frame pos field (A-6); W-06 C-1 процедура Eq.6/Eq.11 замість DIG_CONFIG; W-07 rp[3] ADR — STEP_DRIFT + drift validation 5% threshold (C-2); W-08 timeout 120s (C-2); W-09 keyboard advance v1 (C-2); W-10 RAM budget audit note. W-11/W-12 false positive — STORAGE_ARCHITECTURE v1.7.1 вже виправлено.*
