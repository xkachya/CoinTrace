# Wave 8 Roadmap — Connectivity + Infrastructure + Sensor Integration

**Статус:** 🔄 In Progress — Фаза 2 — Sensor Integration (C-1 ✅, C-2 ✅, C-4 ✅, C-5 ✅ hw-2026-03-27, C-6 ✅ hw-2026-03-27 → **C-7 MetalMatcher + Quick Screen НАСТУПНИЙ**)
**Версія:** 2.5.0
**Дата:** 2026-03-27 (C-7 pre-coding prep: sub-tasks C-7a..C-7f, HW checklist HW-QS-1..6, C-5 audit sync: ferro_thresh DISABLED 99.0, dL1 weight 1.0, Quick Screen пороги p3)
**Попередня хвиля:** Wave 7 — Storage Foundation (`d53a440`, 84/84 native tests, hardware verified)
**Cross-ref:** `docs/architecture/MEMORY_MAP.md` — детальна карта Flash/SRAM/Heap (hw-verified 2026-03-18)

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
| WiFiManager AP/STA | ✅ | A-1 | ✅ hw-verified 2026-03-17 — AP `CoinTrace-F974`, STA+NVS, promptSTA() |
| AsyncWebServer + mDNS | ✅ | A-2 | ✅ hw-verified 2026-03-18 — 9/9 hw tests PASSED, STA 192.168.88.53, heap idle 30 KB |
| `GET /api/v1/status` | ❌ | A-3 | ✅ hw-verified 2026-03-18 — реалізовано в HttpServer.cpp (A-2 сесія) |
| `GET /api/v1/measure/{id}` | ❌ | A-3 | ✅ реалізовано — MeasurementStore ring-buffer read, 404 для out-of-range |
| `GET /api/v1/log` | ❌ | A-3 | ✅ реалізовано — RingBufferTransport, params: n/level/since_ms |
| `GET /api/v1/database` | ❌ | A-3 | ✅ реалізовано — FingerprintCache count + ready flag |
| **`POST /api/v1/database/match`** | ❌ | A-3 | ✅ hw-verified 2026-03-18 — conf=0.919 (xcu/synthetic), top-5 alternatives |
| `GET /api/v1/sensor/state` | ❌ | A-3 | ✅ реалізовано — повертає IDLE_NO_COIN (pre-C-2 stub) |
| OTA mechanism | ❌ | A-4 | ✅ hw-verified 2026-03-18 — flash ✅ confirm ✅ auto-rollback 60s ✅ (`1530deb`) |
| Web UI (HTML/CSS/JS) | ❌ | A-5 | Match screen тестується через manual POST — **НАСТУПНИЙ** |
| WebSocket (status+log frames) | ❌ | A-6 | Sensor frames = stub |
| BLE GATT service | ❌ | A-7 | ⛔ Відкладено до v2 (PSRAM) — ~30 KB вільно, NimBLE потребує ~50 KB |
| `test_nvs_manager/` | ✅ | B-1 | 9/9 native tests PASSED (`bde3c03`) |
| `test_fingerprint_cache/` | ✅ | B-2 | 6/6 native tests PASSED, `loadTestEntry()` додано |
| GPIO0 boot recovery | ✅ | B-3 | Реалізовано в `src/main.cpp`; hw-verified: пристрій перезавантажується при утриманні G0 під час 3s вікна |
| **Vector computation math** | ✅ | **C-3\*** | **9/9 native tests PASSED, OLS slope верифіковано** |
| `POST /api/v1/measure/start` | ⚠️ partial | A-3 | ✅ Stub реалізовано: `503 sensor_not_ready` до C-2 |
| WebSocket sensor frames | ⚠️ partial | A-6 | Stub → real після C-2 |
| R-01 → real protocol_id | ✅ done | C-1 | `"p2_MIKROE3240_024mm"` hw-verified (`6628487`) → **updated `p3_MIKROE3240_b06_012mm` 2026-03-26** (0.6mm base spacer, bare coin) |
| Multi-position state machine | ✅ done | C-2 | hw-verified: session trigger (`313b179`) |
| queryFingerprint() wiring | ✅ done | C-4 | hw-verified 2026-03-24 — sensor/state real MeasState + POST measure/start |
| FingerprintCache σ tuning | ✅ done | C-5 | hw-verified 2026-03-27: σ=0.35, 25/25 correct, slope() x={0,1,2} fixed |
| Real FP DB seeding | ✅ done | C-6 | hw-verified 2026-03-27: 5 real centroids, generation=2, index.json replaced |
| MetalMatcher + Quick Screen | ⚡ НАСТУПНИЙ | C-7 | Специфікація: METAL_MATCHER_ARCHITECTURE.md, QUICK_SCREEN_SPEC.md |

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

**Статус:** ✅ hw-verified 2026-03-18 — 9/9 hw tests PASSED (STA mode, IP 192.168.88.53)  
**Build:** RAM 61.5% (201676/327680 B) · Flash 56.0% (1468377/2621440 B) · `cointrace-dev`  
**Бут-лог (STA mode, FT232RL COM3):**
```
[   835ms] INFO  System  | CoinTrace 1.0.0-dev starting
[   835ms] INFO  System  | CPU: 240 MHz | Heap: 165216 B | PSRAM: 0 MB
[  1073ms] INFO  NVS     | Ready — meas_count=0 slot=0
[  1095ms] INFO  LFS     | sys mounted — free: 976 KB
[  1484ms] INFO  Cache   | FingerprintCache ready — 5 entries
[  1484ms] DEBUG Mem     | sizeof(LogEntry)=220 sizeof(CacheEntry)=140 (MAX=1000 entries)
[  1642ms] DEBUG Heap    | before WiFi: 107408 B free
[  2059ms] DEBUG Heap    | after WiFi:  54488 B free
[  2059ms] INFO  WiFi    | STA mode — SSID: YuKa  IP: 192.168.88.53
[  2092ms] DEBUG Heap    | after HTTP:  34844 B free
[  2092ms] INFO  HTTP    | REST API ready — http://192.168.88.53/api/v1/status
[ 10005ms] DEBUG Stack  | LFS task watermark: 1332 B free (of 4096 B stack)
```
**Фінальні виміри (hw_test.py — 9/9 PASSED):**
| Метрика | Значення |
|---------|----------|
| heap idle | 30052 B |
| heap_min (boot) | 30044 B |
| heap_max_block idle | 21492 B = 72% → no fragmentation |
| heap after POST /database/match | 29104 B |
| heap_max_block after POST | 18420 B = 63% → healthy |
| heap drift (9 tests) | 948 B < 1 KB → no memory leak |
| sizeof(CacheEntry) | **140 B** confirmed (1000 entries = 140 KB SRAM) |
| LFS task stack used | 2764 B of 4096 → reduced to **3072 B** (saves 1 KB heap) |
| Boot → HTTP ready | 2092 ms (STA mode) |
| Fingerprint match confidence | 0.919 (xcu/synthetic) |

**Реалізовані рекомендації External Review (2026-03-17):**
- `heap_max_block` (ESP.getMaxAllocHeap()) додано до GET /status — детектує фрагментацію
- heap diagnostic logs INFO → LOG_DEBUG (не засмічують INFO-лог)
- `sizeof(LogEntry/CacheEntry/MAX_ENTRIES)` — one-time boot diagnostic
- LFS task `stackWatermarkBytes()` — one-time loop() log post 10 s

**Відхилені рекомендації (з обґрунтуванням):**
- `load()` retry з `delay()` → `delay()` блокує lwIP thread → відкладено до A-3 (MainLoop queue)
- snprintf flat JSON → ризик injection для user strings → відкладено до A-3
- Regex engine removal → ризик > 2 KB gain → відхилено

**UART debug:** FT232RL на EXT 2.54-14P G15/G13 → COM3 (docs/guides/UART_DEBUG_SETUP.md)  

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

**Статус:** ✅ Реалізовано в рамках A-2 hw-сесії (2026-03-18) — `lib/HttpServer/src/HttpServer.cpp`  
**Всі 9 ендпоінтів hw-verified:** GET /status, /sensor/state, /database, /log, /measure/{id}, /ota/status; POST /database/match, /measure/start (503), /ota/update (403)

> ℹ️ A-2 і A-3 реалізовані разом як єдиний `HttpServer`. Поділ залишається в роадмапі для архівних цілей.

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
     body: {"algo_ver":1,"protocol_id":"p3_MIKROE3240_b06_012mm","vector":{...}}
     ← ctx->storage->queryFingerprint() → top-N FPMatch
     ← 400 якщо відсутній vector або значення поза BOUNDS
     ← 503 якщо FingerprintCache не завантажений
     ← 200 {"match":null} якщо protocol_id не знайдено (не 404)

GET  /api/v1/sensor/state
     ← {"state":"IDLE_NO_COIN"|"IDLE_COIN_PRESENT"|"MEASURING_STEP_BASE"|"MEASURING_STEP_1"|"MEASURING_STEP_3"|"MEASURING_STEP_DRIFT"|"MEASURING_COMPUTE"}
     ← hw-verified 2026-03-24 (C-4): всі 7 станів пройдено в T1–T6
     ← Критично для Web UI: клієнт polls цей endpoint під час вимірювання замість timeout

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

**Статус:** ✅ hw-verified 2026-03-18 — commit `1530deb` (6 файлів, 488 insertions)

**Build:** RAM 61.6% (201900/327680 B) · Flash 56.3% (1474605/2621440 B)

**Реалізовано:**
- `NVSManager`: `OtaMeta` struct + `saveOtaMeta()`, `loadOtaMeta()`, `setOtaConfirmed()`, `clearOtaMeta()`
- `HttpServer`: `GET /ota/status` (ota_window, seconds_left, pending, confirmed, pre_version) + `POST /ota/update` (chunked flash via `Update.h`)
- `main.cpp`: OTA state machine — window (30s), ORANGE countdown display, confirm `'O'`, rollback timer (60s), `esp_ota_set_boot_partition(app0)` + `esp_restart()`
- `test/test_ota_nvs/`: 14/14 native tests PASSED (136/136 total no regressions)
- `scripts/test_ota_flash.py`: `--env` arg, dynamic firmware path, version change check, Windows URLError/timeout handled
- `platformio.ini`: `cointrace-ota-test` env (version `1.0.1-ota-test`) для верифікації version bump

**HW-verified acceptance criteria:**
| Тест | Результат |
|------|-----------|
| POST без O → 403 | ✅ |
| O → window відкривається, countdown display | ✅ |
| OTA flash → device reboots into new firmware | ✅ (×2) |
| 60s без O → auto-rollback до попередньої прошивки | ✅ |
| O після reboot → confirmed=true, нова прошивка залишається | ✅ |
| Version change: `1.0.0-dev` → `1.0.1-ota-test` | ✅ |

**Механіка (ADR-007):**
1. Клавіша `'O'` → 30-секундне вікно + зворотний відлік на дисплеї
2. `POST /api/v1/ota/update` (Content-Type: application/octet-stream) → ESP-IDF OTA partition swap
3. Reboot → auto-rollback якщо не підтвердив `'O'` знову протягом 60с

**Backlog v2:** ECDSA підпис firmware binary (в v1 ризик прийнято — фізичний доступ = авторизація).

---

### A-5: Web UI (data/web/)

**Стек:** Vanilla HTML/CSS/JS без build step або Preact (~3 KB). Цільовий розмір < 200 KB gzip. Розміщення: `data/web/` → `pio run -e uploadfs-sys -t uploadfs` (Wave 7 pipeline вже готовий).

> **W-03 (audit):** Web UI завжди займає більше часу ніж очікується. Розбити на **A-5a (MVP)** і **A-5b (повна версія)** — це зменшує critical path фази 1 на ~2-3 дні.

#### A-5a: MVP — ✅ hw-verified 2026-03-18 (commit `1905a43`)

| Екран | Endpoint(s) | Статус |
|---|---|---|
| Status | `GET /status` + `/ota/status` + `/database` + `/sensor/state` | ✅ HW-verified STA |
| Match | `POST /database/match` | ✅ HW-verified STA |

**Реалізовано:** `data/web/index.html` + `app.js` + `style.css` (19.9 KB total, dark theme, vanilla JS).  
**Status tab:** 5-секундний polling 4 endpoints, heap warning < 20 KB, conf color-coding.  
**Match tab:** форма 5 полів (`dRp1`, `k1`, `k2`, `slope_rp_per_mm_lr`, `dL1`), result card + alternatives bar chart.  
**AP mode `http://192.168.4.1`:** ❌ не перевірено — зробити при наступному підключенні до AP `CoinTrace-F974`.

**Firmware зміна (A-5b prep):** `GET /api/v1/status` тепер повертає поле `meas_count` (commit A-5b-prep) — необхідно для навігації по Measurements tab.

#### A-5b: Повна версія — ✅ hw-verified 2026-03-18

| Екран | Endpoint(s) | Статус |
|---|---|---|
| Measurements | `GET /measure/{id}` + `meas_count` з `/status` | ✅ HW-verified |
| Log | `GET /api/v1/log` (REST poll 3s, `since_ms` cursor) | ✅ HW-verified |
| Settings | `GET /api/v1/settings` + `POST /api/v1/settings` | ✅ HW-verified |
| Sensor stream | WebSocket `"t":"sensor"` | ⏳ після A-6 + C-2 |

**HW-verified acceptance criteria:**
| Тест | Результат |
|---|---|
| `GET /settings` → 200 з `dev_name, lang, display_rot, brightness, log_level` | ✅ |
| `POST /settings` partial update (brightness, lang) → `{ok:true, needs_restart:false}` | ✅ |
| `POST /settings` невалідний lang="fr" → 400 | ✅ |
| `POST /settings` display_rot=9 → 400 | ✅ |
| `POST /settings` display_rot зміна → `needs_restart:true` | ✅ |
| Round-trip: значення persist після reboot (lang=uk, brightness=200) | ✅ |
| Log tab: `since_ms` cursor — дублювання відсутнє | ✅ (bug fix: `next_ms = lastMs + 1`) |
| Log tab: level filter INFO → DEBUG рядки зникають | ✅ |
| Log tab: Clear → 0 lines, cursor reset | ✅ |
| OTA status badge: Confirmed ✓ зелений / ⚠ Unconfirmed червоний | ✅ (bug fix) |
| Web total size: ~36 KB (`app.js` + `index.html` + `style.css`) | ✅ |

**Sensor stream відкладено:** stub → real після C-2 + A-6 WebSocket.

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

### A-7: BLE GATT Service — ⛔ Відкладено до v2 (PSRAM hardware)

**Рішення (2026-03-18, hw-measured):** BLE не реалізується в Wave 8 на поточному hardware.

**RAM аналіз:**

| Стан | Internal DRAM вільно |
|---|---|
| Після WiFi + HTTP (hw-виміряно) | ~30 KB |
| Після A-6 WebSocket server | ~22–25 KB |
| Після 1 WS клієнта | ~16–19 KB |
| BLE NimBLE (мінімум) потребує | **~35–50 KB** |
| **Дефіцит** | **~15–34 KB → hard OOM** |

**Чому PSRAM не рятує напряму:**  
BLE stack вимагає **internal DRAM** (DMA буфери, time-critical алокації). PSRAM не підходить.

**Шлях до BLE через PSRAM (непрямий):**  
`FingerprintCache::entries_[1000]` = **140 KB BSS у internal DRAM** → перенести на PSRAM:  
```cpp
// v2: heap_caps_malloc(sizeof(CacheEntry) * MAX_ENTRIES, MALLOC_CAP_SPIRAM)
```
Це звільнить ~140 KB internal DRAM → BLE NimBLE поміщається (~50 KB) з ~90 KB залишком.

**Чому BLE не потрібен для v1 функціонально:**
| BLE Use Case | Альтернатива в v1 |
|---|---|
| Field use без роутера | ✅ WiFi AP mode (`192.168.4.1`) |
| Real-time streaming | ✅ WebSocket (A-6) |
| WiFi provisioning | ✅ Keyboard (`promptSTA()`) — унікальна перевага Cardputer |
| Телефон без інсталяції | ✅ Web UI в браузері через WiFi AP |

**UUIDs зафіксовані назавжди** (ADR-004, не змінювати навіть у v2):
```
Service:  D589804A-228E-4171-BE8B-872534A652C6
MEASURE:  5AF690DC-5EA5-4166-80B4-2138AC7CF491  Write
RESULT:   9AF88885-31CE-412B-A567-94052E9598BC  Notify
STATUS:   E7FAB8DE-E68B-44B3-A851-4CC777486DF3  Read+Notify
LOG:      CAB81902-EF36-410E-A8AE-3891C00375CD  Notify
RAW:      CFF322C3-6E5C-4008-996C-7A08BDAD4C53  Notify
```

**Target platform v2:** ESP32-S3R8 (8 MB PSRAM варіант) або M5Stack CoreS3.  
**Передумови для v2:** FingerprintCache → PSRAM міграція + heap budget re-measurement.

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
0. **Передумова (ADR-CLKIN-002): підключити CLKIN** — фізично з'єднати **G4 (EXT Pin 3) → [22Ω опційно] → mikroBUS Pin 16**.
   Firmware (вже готовий): `ldc1101.clkin_gpio=4` в `data/plugins/ldc1101.json`, LEDC в `initialize()`.
   Без цього з'єднання: L_DATA=3 (сміт), fSENSOR=недосяжне значення (вже перевірено S-3).
1. Підключити LDC1101 і запустити пристрій
2. Прочитати boot log: `initialize()` і `calibrate()` обчислюють fSENSOR за Eq.6 (`fSENSOR = fCLKIN × RESP_TIME / (3 × L_DATA)`) та Eq.11 (`fSENSOR = LHR_DATA × 2 × fCLKIN / 2²⁴`); обидва значення логуються в `[LDC1101]` рядках. `REG_DIG_CONFIG` містить RESP_TIME/MIN_FREQ — не fSENSOR напряму.
3. Зафіксувати у вигляді `protocol_id` за форматом FINGERPRINT_DB §3.2
4. Оновити NVS "sensor"."proto_id" (в коді та seed data)

**Оновити після R-01:**
- `data/plugins/ldc1101.json` — замінити "p3_MIKROE3240_b06_012mm"
- `FINGERPRINT_DB_ARCHITECTURE.md §7` — "Frozen physical constants"
- Всі 5 synthetic seed entries в `data/plugins/ldc1101.json`

> ⚠️ До R-01 — жоден запис **не публікувати** в community DB. `protocol_id = "p3_MIKROE3240_b06_012mm"` робить їх community-incompatible (мовчазна несумісність при зміні protocol_id).

---

### C-2: Multi-position measurement state machine

**Поточний стан loop():** зберігає вимір при `COIN_REMOVED` з 1 позицією (`pos_count=1`).

**Цільова state machine (4 позиції + drift check):**

> ⚠️ **Оновлення 2026-03-25 (p2):** `coin placed` більше НЕ запускає STEP_BASE автоматично.
> При COIN_PRESENT у IDLE відображається **Quick Screen** (QUICK_SCREEN_SPEC.md) — live ΔRp% та ΔL.
> Сесія стартує лише після явного **ENTER** або **HTTP POST /measure/start**.

```
IDLE ──── coin placed ────► Quick Screen (IDLE sub-mode: live ΔRp%, ΔL, metal class)
                                │
                         keyboard ENTER або HTTP POST /measure/start
                                ▼
                             STEP_BASE  rp[0],l[0]   ← bare coin on 0.6mm base spacer (d≈0.6mm)
                                │
                 keyboard 'ENTER': "Place on 1mm spacer, press ENTER"
                           (timeout 120s → abort → IDLE)
                                ▼
                             STEP_1   rp[1],l[1]
                                │
                 keyboard 'ENTER': "Place on 2mm spacer, press ENTER"
                           (timeout 120s → abort → IDLE)
                                ▼
                             STEP_3   rp[2],l[2]    ← назва STEP_3 = дистанція
                                │
                 keyboard 'ENTER': "Return to base (tray), press ENTER"
                           (timeout 120s → abort → IDLE)
                                ▼
                          STEP_DRIFT  rp[3],l[3]    ← drift check: rp[3] ≈ rp[0]
                                │
                          STEP_COMPUTE → computeVector() → queryFingerprint() → save()
                                │
                              IDLE
```

> ⚠️ **ADR-SPACER-001:** Позиція STEP_BASE = монета в лотку (d ≈ 2mm від котушки). **Мінімальний зазор d_min ≥ 1.5mm є обов'язковим для феромагнітних монет** (нікель, сталь). Нульовий зазор (d=0mm) для феромагнітних монет спричиняє ефект pot-core: fSENSOR падає з 909 kHz до ~37-91 kHz → DRDYB=1 storm. Лоток забезпечує природній зазор ~2mm — цього достатньо. **Ніколи не використовувати 0mm як STEP_BASE для реальних вимірювань.**
>
> Cross-ref: `docs/audit/FERROMAGNETIC_COIN_INVESTIGATION_2026-03-24.md` §7, LDC1101_ARCHITECTURE.md ADR-SPACER-001

**Семантика `rp[3]` (W-07 ADR):** `rp[3]` = reading at base position після повернення монети (перевірка дрейфу). Спрощення: якщо `|rp[3] - rp[0]| / rp[0] > 0.05` (5%) → лог WARNING "Sensor drift detected" + зберегти вимір з `conf = 0.0` (не входить у matching). `rp[3]` **не входить** у VectorCompute — тільки rp[0..2].

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

> **W-10 (S-4b hw-verified 2026-03-24):** Перед реалізацією C-2 перевірити що `DIG_CONFIG` використовує `min_freq_nibble=6` (threshold=800 kHz, margin 109 kHz від baseline 909 kHz). Значення `nibble=0xD` (MikroE SDK legacy) відповідає порогу 2.67 MHz — **небезпечно для нашої котушки**, спричиняє DRDYB storm навіть при d≥1.5mm зі звичайними монетами. Поточна конфігурація firmware: `DIG_CONFIG=0x67` ✅. Cross-ref: LDC1101_ARCHITECTURE.md §ADR-MINFREQ-001.

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

// k2 = Rp(2mm) / Rp(0mm)  [dimensionless, 0..1]
// Second spatial ratio; combined with k1 gives 2D metal signature.
inline float k2(const Measurement& m) {
    if (m.rp[0] < 1.0f) return 0.0f;
    return m.rp[2] / m.rp[0];
}

// slope_rp_per_mm_lr = OLS linear regression coefficient of k-ratios vs distance [1/mm]
// p3 protocol x={0,1,2}: OLS closed form → slope = (k2−1)/2 (linear transform of k2).
// Slope adds ZERO independent info → full_weights[3]=0.0 in matcher.json for p3.
// Formula updated in Wave 8 C-5 (2026-03-27): x={0,1,3}→{0,1,2}, simplified to 3 lines.
// Cross-ref: STORAGE_ARCHITECTURE §8.3 field "slope_rp_per_mm_lr"; VectorCompute.h MATH NOTE
float slope(const Measurement& m);   // implemented in VectorCompute.cpp

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

**Статус:** ✅ hw-verified 2026-03-27 — C-5 hw-session (`p3_MIKROE3240_b06_012mm`, commit pending)

**Результати:**
- 25 вимірів: 5 монет × 5 циклів (XAG999, XCU, XZNNIP, XFE, XAL), IDs 56–80
- Sigma sweep 0.20–0.60: **25/25 правильних класифікацій** на всьому діапазоні
- Обрано `CONFIDENCE_SIGMA = 0.35f` — баланс між чутливістю та робастністю
- slope() OLS виправлено: x={0,1,3}→x={0,1,2}, формула `(k2−1)/2`
- Найближча пара: XCU vs XZNNIP (dist=0.8635), >2σ=0.70
- Примітка: rp[2] насичений (49152) для 20/25 вимірів; k1 (d=1.6mm) — основний дискримінатор
- 122/122 native tests PASSED після всіх змін
- Повний звіт: `docs/audit/C5_HW_AUDIT_REPORT_2026-03-27.md`

---

### C-6: Real FP DB seeding

**Статус:** ✅ hw-verified 2026-03-27 — виконано разом з C-5

**Результати:**
- `data/sd_seed/CoinTrace/database/index.json` — 5 реальних центроїдів замінили синтетичні
- `version: 2`, `generation: 2` (тригерить LittleFS cache invalidation на наступному boot)
- entry IDs: `xag999/c5_hw_2026-03-27`, `xcu/c5_hw_2026-03-27`, `xznnip/c5_hw_2026-03-27`, `xfe/c5_hw_2026-03-27`, `xal/c5_hw_2026-03-27`
- Виправлено знак k1: синтетичні дані мали k1 < 1.0 (фізично неможливо); реальні — k1 ∈ [1.22, 1.38]
- Повний звіт: `docs/audit/C5_HW_AUDIT_REPORT_2026-03-27.md`

---

### C-7: MetalMatcher + Quick Screen

**Статус:** ⚡ НАСТУПНИЙ — Специфікація готова (METAL_MATCHER_ARCHITECTURE.md v1.3.0, QUICK_SCREEN_SPEC.md v1.3.0, WAVE8_ROADMAP.md v2.5.0)

**Передумова:** C-4 (queryFingerprint wiring), C-5 (σ tuning з реальними монетами), C-6 (real FP DB)

**C-5 audit (важливо перед кодінгом):**
- `ferro_thresh_dL1_n` 0.05 → **99.0** (DISABLED): p3 dL1_n ≈ −2.4 для ВСІХ металів — re-enable після S-5
- `full_weights[4]` dL1_n 2.0 → **1.0**: нульова дискримінація між металами в p3
- `sigma` 0.3 → **0.35** (вже в FingerprintCache::CONFIDENCE_SIGMA з commit 97935ae)
- Quick Screen пороги: SILVER>25% → **40%**, COPPER>14% → **30%**, ALUM>5% → **15%** (p3 d=0.6mm)

**Нові артефакти:**
- `lib/StorageManager/src/MetalMatcher.h/.cpp` — standalone service (~150 рядків)
- `data/sd_seed/CoinTrace/matcher.json` — ваги `full_weights` + `quick_weights` + `sigma`
- `getLiveRp()`, `getLiveL()`, `recalibrate()` в `LDC1101Plugin.h/.cpp` (~30 рядків)
- `drawQuickScreen()` в `main.cpp` (~50 рядків)

**Ключові рішення:**
- MetalMatcher — standalone service class (не plugin); аналог FingerprintCache/MeasurementStore
- Quick Screen — IDLE sub-mode, не новий `MeasState` enum value
- Phase 1: threshold-based класифікація (>40%→Ag, >30%→Cu, >15%→Al, is_ferro→Fe) — p3 d=0.6mm
- Phase 2: `matchQuick()` через `quick_centroid` entries у DB — після Wave 9 S-5
- `full_weights[3]` (slope) = 0.0 у `matcher.json` (p2/p3 protocol: slope = лінійна функція k2)

**Sub-task dependency graph:**
```
C-7a MetalMatcher class ──┐
                           ├─► C-7d matcher.json ──► C-7e integration main.cpp
C-7b Quick Screen Phase 1 ┤                                ▲
                           │                               │
C-7c LDC1101 API extension ┘   C-7f FingerprintCache ─────┘
```
C-7a, C-7b, C-7c, C-7f — паралельні. C-7d залежить від C-7a. C-7e — фінальна інтеграція.

**C-7a: MetalMatcher class**
- [ ] CREATE `lib/StorageManager/src/MetalMatcher.h` — Config + MatchResult + Alternative structs, клас
- [ ] CREATE `lib/StorageManager/src/MetalMatcher.cpp` — `matchFull()`, `matchQuick()`, `doMatch()` (private), `loadConfig()`, `logTopCandidates()`
- [ ] CREATE `test/test_metal_matcher/test_metal_matcher.cpp` — 12 unit tests MM-01..MM-12 (native)
- [ ] 134 native tests PASS (122 existing + 12 new)

**C-7b: Quick Screen Phase 1**
- [ ] Додати `drawQuickScreen()` + `classifyQuick()` в `src/main.cpp` (~50 рядків)
- [ ] `static constexpr` пороги: SILVER=40%, COPPER=30%, ALUM=15% (p3 best-estimates, PENDING HW-QS-6)
- [ ] ENTER у Quick Screen → запускає STEP_BASE
- [ ] 'R' → `gLDC->recalibrate()`

**C-7c: LDC1101Plugin API extension**
- [ ] Перевірити: `getBaseline()` / `getLBaseline()` — вже публічні?
- [ ] Перевірити: поле `clkinGpio_` — чи існує?
- [ ] ADD `getLiveRp()`, `getLiveL()` (mutex-protected cache read) в `.h` + `.cpp`
- [ ] ADD `isLDataValid()` inline: `return clkinGpio_ >= 0`
- [ ] ADD `recalibrate()` decl + impl (N=10 avg, coin guard, ~250ms blocking)

**C-7d: matcher.json seed file**
- [ ] CREATE `data/sd_seed/CoinTrace/matcher.json` з виправленими значеннями (sigma=0.35, dL1=1.0, ferro=99.0)

**C-7e: Integration в main.cpp**
- [ ] `#include "MetalMatcher.h"` + global `MetalMatcher gMatcher;`
- [ ] `setup()`: `gMatcher.init(gFPCache)` + `gMatcher.loadConfig(&gSD, ctx.spiMutex)`
- [ ] `doMeasCompute()`: `gFPCache.query(...)` → `gMatcher.matchFull(m)` → Measurement fields
- [ ] IDLE handler: `isCoinPresent()` → `drawQuickScreen()` else `drawMeasIdle()`
- [ ] `HttpServer::setMatcher(MetalMatcher* m)` setter + POST /database/match через `gMatcher`

**C-7f: FingerprintCache weighted query extension**
- [ ] ADD `const float* weights = nullptr` параметр до `FingerprintCache::query()` (backward-compat)
- [ ] Оновити distance calculation на weighted form (weights=nullptr → рівні ваги = стара поведінка)

**HW verification (після флешу):**
- [ ] HW-QS-1: Quick Screen відображається при COIN_PRESENT в IDLE (live ΔRp%, ΔL, metal class)
- [ ] HW-QS-2: ENTER у Quick Screen → запускає STEP_BASE (не скидає baseline)
- [ ] HW-QS-3: 'R' → recalibrate — оновлені значення через ~250ms
- [ ] HW-QS-4: Phase 1 threshold: ≥3 правильних з 4 тестових монет (Ag/Cu/Al/Fe)
- [ ] HW-QS-5: MetalMatcher match() через HTTP POST /api/v1/database/match — weights з matcher.json
- [ ] HW-QS-6: Виміряти реальний baseline + dRpPct для кожного металу → скоригувати пороги якщо потрібно

**Acceptance criteria:**
- [ ] Quick Screen відображається при COIN_PRESENT в IDLE (live ΔRp%, ΔL, metal class)
- [ ] ENTER у Quick Screen → запускає STEP_BASE
- [ ] R → recalibrate baseline (10-reading average)
- [ ] Phase 1 threshold classification: ≥3 правильних з 4 тестових монет (Ag/Cu/Al/Fe)
- [ ] `MetalMatcher::matchFull()` через HTTP `POST /api/v1/database/match` — weights з matcher.json

---

## 5. Рекомендована послідовність

### Фаза 1: LDC1101 в дорозі

```
Завершено ✅:
  B-1  test_nvs_manager/          9/9 native tests PASSED
  B-2  test_fingerprint_cache/    6/6 native tests PASSED
  B-3  GPIO0 boot recovery        hw-verified (3s splash window)
  C-3  VectorCompute              9/9 native tests PASSED
  A-1  WiFiManager AP/STA         hw-verified 2026-03-17
  A-2  AsyncWebServer + mDNS      hw-verified 2026-03-18 — 9/9 REST tests
  A-3  HTTP REST endpoints        hw-verified 2026-03-18 (реалізовано з A-2)

Завершено ✅:
  A-4  OTA mechanism              hw-verified 2026-03-18 (`1530deb`)
  A-5a Web UI MVP (Status+Match)  hw-verified 2026-03-18 (`1905a43`)

Наступний ➜ A-5b (Measurements + Log tabs):
  A-5b Web UI Measurements+Log    ~2 дні   ← не в critical path фази 1
  A-6  WebSocket streaming        ~1 день   (status + log frames; sensor = stub)
```

### Фаза 2: Після прибуття LDC1101

```
C-1  R-01 → real protocol_id          ✅ hw-verified (`6628487`) — `p1_MIKROE3240_024mm`
C-2  Multi-position state machine     ✅ hw-verified trigger (`313b179`) — сесія стартує
C-4  queryFingerprint() wiring        ✅ hw-verified (2026-03-24) — sensor/state real MeasState + POST measure/start
C-5  σ tuning                         ✅ hw-verified 2026-03-27 (25/25, σ=0.35, slope() x={0,1,2} fixed)
C-6  Real FP DB seeding               ✅ hw-verified 2026-03-27 (5 real centroids, generation=2)
C-7  MetalMatcher + Quick Screen      ⚡ НАСТУПНИЙ (~2–3 дні: getLiveRp/L, drawQuickScreen, MetalMatcher, matcher.json)
A-x  WebSocket sensor frames (live)   ~0.5 дні (стаби → real)
A-x  POST /measure/start (full)       ~0.5 дні (C-2 готово)
A-7  BLE GATT → відкладено до v2 PSRAM    (current hw: ~30 KB free, NimBLE needs ~50 KB)
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

> **Hw-measured (2026-03-18, STA+HTTP ready, no WebSocket):** heap idle = 30 052 B, heap_max_block = 21 492 B (72%). Детальні дані: `docs/architecture/MEMORY_MAP.md §3`.  
> mDNS (−15 KB) наразі **вимкнено** (Bug B-03 fix). Re-enable після A-6 heap validation: якщо heap_min > 45 KB з 1 WS клієнтом → mDNS можна ввімкнути.  
> BLE не тестувалось — залишається теоретичним.

**Моніторинг:** `GET /api/v1/status` → поле `heap_min` (`ESP.getMinFreeHeap()`). Перевіряти після кожного підключення першого клієнта та після завантаження index.json.

---

## 7. Acceptance Criteria

### Wave 8 фаза 1 (до LDC1101):

**Connectivity & REST API:**
- [ ] WiFi AP mode: підключитись телефоном → `http://192.168.4.1` відкриває Web UI (A-5a: Status + Match screens)
- [ ] WiFi STA mode: ввести SSID/pass на клавіатурі → IP відображається на дисплеї та `GET /status` повертає 200
  > mDNS (`cointrace.local`) вимкнено — Bug B-03 OOM fix. Re-enable після A-6 heap validation якщо heap_min > 45 KB.
- [x] `GET /api/v1/status` → heap, uptime, wifi state, heap_max_block — hw-verified 2026-03-18
- [x] `GET /api/v1/sensor/state` → `{"state":"IDLE_NO_COIN"}` — hw-verified 2026-03-18
- [x] `POST /api/v1/database/match` → вручну введений vector → match result з synthetic DB — hw-verified 2026-03-18 (conf=0.919)
- [ ] `GET /api/v1/measure/{id}` → Wave 7 виміри (UNKN) відображаються в UI (потребує A-5a)

**OTA:**
- [x] OTA stub: `POST /api/v1/ota/update` без 'O' → 403 `ota_window_not_active` — hw-verified 2026-03-18
- [x] OTA full: flash → reboot into new firmware — hw-verified 2026-03-18 (`1.0.0-dev` → `1.0.1-ota-test`)
- [x] OTA rollback: 60s без 'O' → auto-rollback до попередньої прошивки — hw-verified 2026-03-18
- [x] OTA confirm: 'O' після reboot → `confirmed=true`, нова прошивка стійка після reset — hw-verified 2026-03-18

**Web UI (A-5a MVP):**
- [x] `data/web/index.html` + `app.js` + `style.css` — Status screen та Match screen реалізовані — hw-verified 2026-03-18 (`1905a43`)
- [x] `pio run -e uploadfs-sys -t uploadfs` → Web UI завантажено на пристрій, `LittleFS_data` не торкається — hw-verified 2026-03-18
- [x] STA mode: `http://192.168.88.53` відкриває index.html, uptime auto-refresh підтверджено — hw-verified 2026-03-18
- [ ] AP mode: `http://192.168.4.1` відкриває index.html — ❌ потребує hw-тесту (підключитись до `CoinTrace-F974`)

**WebSocket (A-6):**
- [ ] Live log stream відображається у Web UI при подіях (`{"t":"log"}` frames)
- [ ] heap_min > 50 KB після 5 хв роботи з 1 WebSocket клієнтом (W-10: mDNS + FP DB size decision)

**Infrastructure:**
- [x] Native tests: B-1 (9) + B-2 (6) + C-3 (9) + A-4 OTA NVS (14) + legacy (84) + A-4 config (14) = **136/136 PASSED** (2026-03-18)
- [x] GPIO0 held at boot → Serial: "formatting LittleFS_data..." → restart — hw-verified (B-3, 3s splash window)

### Wave 8 фаза 2 (після LDC1101):

- [x] R-01: fSENSOR=909.2 kHz, `protocol_id="p2_MIKROE3240_024mm"` hw-verified — commit `6628487`; оновлено до `p3_MIKROE3240_b06_012mm` (0.6mm base spacer, bare coin) 2026-03-26
- [~] Multi-position: реалізовано commit `313b179`; hw-verified: coin placed → STEP_BASE ✅, timeout 120s → abort ✅ (119962ms hw-measured); тест 7 (`GET /sensor/state` real MeasState) ✅ C-4; повний 4-step flow потребує hw-тесту з реальними spacers; WebSocket result frame — після A-6
- [x] `GET /api/v1/sensor/state` → real MeasState (`MEASURING_STEP_BASE/1/3/DRIFT`, `IDLE_COIN_PRESENT`, `IDLE_NO_COIN`) — hw-verified 2026-03-24 (C-4)
- [x] `POST /api/v1/measure/start` → `202 {"started":true}` (coin present, IDLE) + `409 already_measuring` (session active) — hw-verified 2026-03-24 (C-4)
- [x] Drift check: `|rp[3] - rp[0]| / rp[0] < 5%` — 25/25 вимірів C-5 пройшли; max 2.87% (Eagle ID 60); WARNING лог при перевищенні
- [x] `queryFingerprint()`: confidence > 0.7 — C-5: 25/25 correct at σ=0.35; real DB seeded (generation=2)
- [ ] WebSocket sensor frame: real-time rp/l/pos stream при COIN_PRESENT
- [x] FP DB: 5 реальних монет різних металів, entryCount()=5 (generation=2, hw 2026-03-27)
- [ ] Quick Screen (C-7): відображається при COIN_PRESENT — live ΔRp%, ΔL, metal class; ENTER → STEP_BASE

---

*Версія 1.7.0 — A-5a Web UI MVP hw-verified (2026-03-18, commit `1905a43`): Status tab (4-endpoint poll, heap warning, OTA/DB/sensor cards) + Match tab (5-field form, conf bar, alternatives). 19.9 KB total. STA hw-verified. AP mode pending. `GET /status` розширено полем `meas_count` (A-5b prep). A-5b scope: Measurements + Log + Settings (REST); Settings не залежить від A-6 або сенсора — NVSManager getters/setters вже існують, потрібні лише firmware endpoints в HttpServer.*  
*Версія 1.6.0 — A-4 OTA hw-verified (2026-03-18): flash ✅ confirm ✅ auto-rollback 60s ✅ (commits `1530deb` + `9801023` + `c0e9e3d`). Acceptance Criteria оновлено: OTA 4/4 пункти [x]; native tests 108→136/136; mDNS reформульовано (B-03 рішення зберігається до A-6); GPIO0 відмічено як hw-verified (B-3). Наступний: A-5a Web UI MVP.*  
*Версія 1.5.0 — A-2 + A-3 завершено та hw-verified (2026-03-18). 9/9 REST endpoints, 108/108 native tests. heap idle 30 KB, heap_max_block 72%, drift 948 B. LFS task stack 4096→3072 B. MEMORY_MAP.md та HW_TESTING.md додано. mDNS вимкнено (B-03 OOM fix) — рішення після A-6 heap measurement. Наступний: A-4 OTA mechanism.*  
*Версія 1.1.0 — [Wave8-Audit-v1] Впроваджено 9 знахідок зовнішнього аудиту: W-01 QR альтернативи (A-1); W-02 GET /api/v1/sensor/state (A-3, матриця, acceptance); W-03 A-5 split A-5a/A-5b + timeline revision; W-04 WebSocket sensor frame pos field (A-6); W-06 C-1 процедура Eq.6/Eq.11 замість DIG_CONFIG; W-07 rp[3] ADR — STEP_DRIFT + drift validation 5% threshold (C-2); W-08 timeout 120s (C-2); W-09 keyboard advance v1 (C-2); W-10 RAM budget audit note. W-11/W-12 false positive — STORAGE_ARCHITECTURE v1.7.1 вже виправлено.*  
*Версія 1.3.0 — B-3 GPIO0 recovery hw-verified: пристрій перезавантажується при утриманні G0 під час 3s splash-вікна; `LittleFSManager::formatData()` додано; `RTC_DATA_ATTR gRtcBootReason` для boot reason tracking; визуальний countdown на дисплеї. Попередня: v1.2.0 — Phase 1 batch B+C-3 реалізовано: B-3 GPIO0 recovery (`src/main.cpp`); C-3 `VectorCompute.h/.cpp` + OLS slope; B-1 `Preferences.h` in-memory KV mock + `test_nvs_manager/`; B-2 `loadTestEntry()` + `test_fingerprint_cache/`; `platformio.ini` розширено. 108/108 native tests PASSED. Наступний крок: A-1 WiFiManager.*  
*Версія 1.3.1 — [B-3-audit-fix] впроваджено 2 знахідки B3_Delta_Independent_Audit: Fix 1 — early-exit GPIO0 window (200ms quick poll, boot penalty 3000→200ms); Fix 2 — `formatData()` SAFETY comment (R-02 race condition); D-01 portability note (USB-CDC vs UART bridge). STORAGE_ARCHITECTURE → v1.8.1.*  
*Версія 1.4.0 — A-1 WiFiManager реалізовано та hw-verified (2026-03-17): `lib/WiFiManager/src/WiFiManager.h/.cpp` (AP+STA, `promptSTA()` keyboard provisioning); `PluginContext.h` розширено полем `WiFiManager* wifi`; `main.cpp` step [10] §17.2 + 'W' key handler; `platformio.ini` коментар виправлено. Bug fix: `buildAPSsid()` використовує `esp_efuse_mac_get_default()` замість `WiFi.macAddress()` (driver not yet init). AP hw-verified: SSID `CoinTrace-F974` видно на телефоні, підключення успішне. RAM: 60.4% (+8% WiFi stack). 108/108 native tests PASSED. Наступний крок: A-2 AsyncWebServer.*  
*Версія 2.5.0 — C-7 pre-coding prep (2026-03-27): C-7 розширено sub-task breakdown (C-7a..C-7f) + HW verification checklist (HW-QS-1..6) + C-5 audit findings (ferro_thresh 0.05→99.0 DISABLED, dL1 weight 2.0→1.0, Quick Screen пороги p3-verified). Spec docs синхронізовано: METAL_MATCHER_ARCHITECTURE.md v1.3.0 + QUICK_SCREEN_SPEC.md v1.3.0. Наступний: кодінг C-7 (паралельно C-7a + C-7b + C-7c + C-7f).*  
*Версія 2.4.0 — C-5 + C-6 hw-verified (2026-03-27): 25 вимірів (5 монет × 5 циклів, protocol `p3_MIKROE3240_b06_012mm`); slope() OLS виправлено x={0,1,3}→{0,1,2} → `(k2−1)/2`; CONFIDENCE_SIGMA 0.30→0.35 (25/25 correct, 122/122 native tests); index.json generation 1→2 (5 реальних центроїдів XAG999/XCU/XZNNIP/XFE/XAL); виправлено k1 polarity (синтетика < 1.0 → реальні ≥ 1.22); audit report `docs/audit/C5_HW_AUDIT_REPORT_2026-03-27.md`. Наступний: C-7 MetalMatcher + Quick Screen.*  
*Версія 2.3.0 — p3 protocol (2026-03-26): базовий spacer 0.6mm (3D-друк, hw-верифіковано: 10 грн не замикає контур при d=0.6mm); без капсули (bare coin); ефективні відстані 0.6/1.6/2.6mm (замінюють 1.4/2.4/3.4mm p2); protocol_id p2_MIKROE3240_024mm→p3_MIKROE3240_b06_012mm; seed data + всі документи оновлено. Наступний: hw-сесія C-5 з Eagle Ag999 + реальними монетами.*  
*Версія 2.2.0 — p2 protocol (2026-03-25): спейсер STEP_3 змінено 3мм→2мм (ефективні відстані 1.4/2.4/3.4мм — узгоджено з фізичною моделлю та hw-тестуванням Ag999 у капсулі); `coin_detect_threshold` 0.85→0.90, `coin_release_threshold` 0.92→0.96 (гістерезис 6%); авто-старт сесії прибрано (тільки HTTP POST /measure/start); seed data protocol_id + steps_mm оновлено. Наступний: hw-сесія з новими спейсерами → C-5 σ tuning.*
*Версія 2.1.0 — C-4 hw-verified (2026-03-24): `GET /api/v1/sensor/state` → real MeasState (MEASURING\_STEP\_BASE/1/3/DRIFT, IDLE\_COIN\_PRESENT, IDLE\_NO\_COIN); `POST /api/v1/measure/start` → 202/409/503; volatile `gMeasStartRequested` flag + lambda injection via `gHttp.setSensorState()`. T1–T6 hw-verified. RAM=61.7% Flash=57.4%. Наступний: spacer hw-session (C-2 тести 2/4/5/6) → C-5 σ tuning.*  
*Версія 2.0.0 — C-1 + C-2 hw-verified (2026-03-24): C-1 — `protocol_id="p2_MIKROE3240_024mm"` зафіксовано, NVSManager migration, Measurement.h розширено, HttpServer.cpp повертає protocol\_id (commit `6628487`); C-2 — `MeasState` enum + `MeasSession` struct, auto-start на edge COIN\_PRESENT, ENTER/Bksp handlers, 120s per-step timeout, drift check, `doMeasCompute()` (VectorCompute+FP query+save), display step/result/idle з partial redraw (commit `313b179`). LFS task stack 3072→3584 B (308→820 B headroom). hw-verified: coin placed → `Meas | Coin placed — session started (STEP_BASE)`. RAM=61.7% Flash=57.4%. Наступний: C-4 queryFingerprint() wiring.*
