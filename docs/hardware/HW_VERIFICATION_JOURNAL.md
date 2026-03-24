# CoinTrace — Hardware Verification Journal

**Призначення:** Журнал апаратної верифікації — хронологічний запис кожного важливого кроку тестування, результатів вимірювань та висновків.  
**Формат:** кожен запис = дата · що робили · результат · висновки · що далі.  
**Стосується:** LDC1101 Click Board (MIKROE-3240) на M5Stack Cardputer-Adv.

---

## Хронологія

---

### Сесія S-1 — Wave 8 Фаза 1 завершена (2026-03-18)

**Що робили:** Реалізація та hw-верифікація всього Connectivity stack без датчика.

**Результати:**

| Компонент | Результат |
|---|---|
| WiFiManager AP/STA | ✅ AP `CoinTrace-F974`, STA IP 192.168.88.x |
| HTTP REST API (9 ендпоінтів) | ✅ 9/9 hw tests PASSED |
| OTA (flash / rollback / confirm) | ✅ `1.0.0-dev` → `1.0.1-ota-test` → rollback 60s |
| Web UI A-5a (Status + Match) | ✅ STA mode, 19.9 KB total |
| Web UI A-5b (Measurements + Log + Settings) | ✅ hw-verified |
| Native tests | ✅ 136/136 PASSED (B-1/B-2/C-3/A-4/legacy) |
| GPIO0 boot recovery | ✅ 3s splash window |

**Heap (hw-виміряно, STA+HTTP, no WebSocket):**
- idle: 30,052 B free
- heap_max_block: 21,492 B (72% — no fragmentation)
- drift після 9 тестів: 948 B < 1 KB (no memory leak)

**Висновки:**
- Весь connectivity stack стабільний і готовий до сенсорної інтеграції
- `POST /api/v1/calibrate` та `POST /api/v1/measure/start` — навмисні заглушки 503 (очікують C-2)
- mDNS вимкнено (Bug B-03 OOM fix) — до A-6 heap validation

**Наслідки:** Готовність до Фази 2 (hardware-gated C-1/C-2/C-3/C-4).

---

### Сесія S-2 — Прибуття LDC1101, перше підключення (2026-03-23)

**Що робили:** Підключили LDC1101 Click Board (MIKROE-3240) до CoinTrace. Перший запуск firmware з датчиком.

**Апаратне підключення (підтверджено при ініціалізації):**

| Сигнал | GPIO ESP32-S3 | Примітка |
|---|---|---|
| SCK  | GPIO40 | VSPI (⚠️ JTAG MTDO) |
| MOSI | GPIO14 | VSPI |
| MISO | GPIO39 | VSPI (⚠️ JTAG MTCK) |
| CS   | GPIO5  | Активний LOW |
| VCC  | 3.3V | |
| GND  | GND | |

**Бутлог (перший успішний запуск):**

```
[   859ms] INFO  System         | CoinTrace 1.0.0-dev starting
[   859ms] INFO  System         | CPU: 240 MHz | Heap: 164992 B | PSRAM: 0 MB
[   869ms] DEBUG System         | Flash: 8 MB
[  1096ms] INFO  NVS            | Ready — meas_count=0 slot=0
[  1216ms] INFO  LFS            | sys mounted — free: 804 KB
[  1227ms] INFO  LFS            | /sys/config/device.json found
[  1260ms] INFO  LFS            | data mounted — free: 1672 KB
[  1260ms] INFO  LFS            | LittleFSTransport started
[  1316ms] INFO  SD             | SD card available (archive tier active)
[  1621ms] INFO  Cache          | FingerprintCache ready — 5 entries
[  1621ms] DEBUG Mem            | sizeof(LogEntry)=220 sizeof(CacheEntry)=140 (MAX=1000 entries)
[  1647ms] INFO  Storage        | StorageManager ready — NVS:ok LFS:ok SD:ok FP:5 entries
[  1655ms] INFO  LDC1101        | configure_: DIG_CONFIG=0xD7, RP_SET=0x26
[  1682ms] INFO  LDC1101        | Ready. CS=5, RESP_TIME=0x07, RP_SET=0x26
[  1682ms] INFO  PluginSystem   | LDC1101 v1.0.0: initialized OK
[  1714ms] INFO  System         | CoinTrace ready — 1/1 plugins initialised
...
[  2127ms] INFO  WiFi           | STA mode — SSID: [redacted]  IP: 192.168.88.x
[  2157ms] INFO  HTTP           | REST API ready — http://192.168.88.x/api/v1/status
[ 10005ms] DEBUG Stack          | LFS task watermark: 308 B free (of 3072 B stack)
```

**Аналіз бутлогу:**

| Перевірка | Значення | Статус |
|---|---|---|
| CHIP_ID | 0xD4 (імпліцитно, ініціалізація OK) | ✅ |
| DIG_CONFIG | 0xD7 = 0xD0 (MIN_FREQ=118kHz) \| 0x07 (RESP_TIME=6144 cycles) | ✅ |
| RP_SET | 0x26 = MAX=24kΩ / MIN=1.5kΩ (MIKROE-3240 default) | ✅ |
| CS pin | GPIO5 | ✅ |
| configure_() readback verify | passed (TC1/TC2/RP_SET/DIG_CONFIG) | ✅ |
| Plugin init | LDC1101 v1.0.0: initialized OK | ✅ |
| Coil oscillation | no NO_OSC flag | ✅ |

**Проблема виявлена:**
- `POST /api/v1/calibrate` → 503 `{"error":"sensor_not_ready"}` — очікувана поведінка заглушки (HttpServer лінія 496 хардкод)
- `POST /api/v1/sensor/state` → `{"state":"IDLE_NO_COIN"}` — очікувана заглушка
- Плагін фізично готовий, але HTTP endpoints ще не підключені до нього (заплановано в C-2)

**Висновки:**
1. Датчик підключений правильно — SPI зв'язок встановлено, CHIP_ID підтверджений
2. Конфігурація регістрів відповідає MikroE SDK (RP_SET=0x26, DIG_CONFIG=0xD7)
3. Котушка осцилює нормально (NO_OSC відсутній)
4. `LFS task watermark: 308 B free` — близько до мінімуму (512 B recommended). Моніторити при наступній сесії

**Наслідки:** Готовність до Step-0 (baseline calibration).

---

### Сесія S-3 — Step-0: Baseline RP та fSENSOR (2026-03-23 — очікує заповнення)

**Що робили:** Запуск firmware з тимчасовим `calibrate()` в `setup()` для вимірювання реального baseline RP та оцінки fSENSOR.

**Конфігурація тесту (код у `src/main.cpp`, після `gPluginSystem.begin()`):**
```cpp
// STEP-0 block: delay(4000) → gLDC->calibrate() → log baseline
// calibrate(): 2s wait + 20 samples × (convTime + 5ms) → average RP
// RESP_TIME=6144 cycles, fSENSOR≈200–500 kHz → convTime≈6.8 ms → total≈15s
```

**Умови вимірювання:**
- [x] Монети на котушці відсутні (підтверджено візуально)
- [ ] Кімнатна температура: ____°C  *(не виміряно)*
- [ ] Напруга живлення 3.3V верифікована: ____V  *(не виміряно)*
- [x] RESP_TIME=0x07 (6144 cycles) — максимальна якість

**Фактичний лог (2026-03-23, COM4 / UART1 115200):**
```
[  1793ms] INFO  LDC1101        | configure_: DIG_CONFIG=0xD7, RP_SET=0x26
[  1793ms] INFO  LDC1101        | Ready. CS=5, RESP_TIME=0x07, RP_SET=0x26
[  1822ms] INFO  PluginSystem   | LDC1101 v1.0.0: initialized OK
[  1822ms] INFO  System         | CoinTrace ready — 1/1 plugins initialised
[  1853ms] INFO  Step0          | === HW VERIFICATION STEP-0 START ===
[  1853ms] INFO  Step0          | Remove ALL coins from sensor, then wait 4s...
[  5882ms] INFO  LDC1101        | Calibration start — remove coin from sensor
[  8282ms] INFO  LDC1101        | Calibration OK. Baseline RP=57344 (20 samples)
[  8282ms] INFO  Step0          | Baseline RP=57344  → record in HW_VERIFICATION_JOURNAL.md §Step-0
[  8308ms] INFO  Step0          | === HW VERIFICATION STEP-0 END ===
[  8308ms] DEBUG Heap           | before WiFi: 108116 B free
[  8848ms] DEBUG Heap           | after WiFi:  55192 B free
[  8848ms] INFO  WiFi           | STA mode — SSID: [redacted]  IP: 192.168.88.x
[  8878ms] DEBUG Heap           | after HTTP:  43352 B free
[  8878ms] INFO  HTTP           | REST API ready — http://192.168.88.x/api/v1/status
[ 10009ms] DEBUG Stack          | LFS task watermark: 212 B free (of 3072 B stack)
```

**Результати вимірювань:**

| Параметр | Значення | Примітки |
|---|---|---|
| Baseline RP (без монети) | **57344** raw ADC codes | `calibrationRpBaseline_` |
| valid samples (N) | **20 / 20** | ✅ всі 20 валідні |
| L_DATA | н/д | не логується `calibrate()` — потрібен окремий burst read |
| Час calibrate() | **~2400 ms** | 5882ms → 8282ms |
| heap після HTTP init | **43352 B** free | (~13% від 320KB RAM) |
| LFS task watermark | **212 B** free | ⚠️ (S-2 мало 308 B, -96 B — моніторити) |

**Аналіз baseline RP = 57344:**

```
raw_code = 57344 = 0xE000
% full scale = 57344 / 65536 = 87.5%

Приблизна RP за лінійним масштабом (∝ RP_MAX):
  RP_approx = (57344 / 65536) × 24000 Ω ≈ 21000 Ω = 21 kΩ

Перевірка RP_SET=0x26 (MAX=24kΩ / MIN=1.5kΩ):
  21000 < 24000  → ✅ нижче максимуму
  1500  < 21000  → ✅ вище мінімуму
  → RP_SET=0x26 є АДЕКВАТНИМ для цього датчика

20/20 валідних відліків (rp > 0 && rp < 65535) → котушка осцилює стабільно ✅
```

> **Примітка:** `calibrate()` не логує L_DATA окремо. Для точного розрахунку fSENSOR
> потрібно додати окреме читання `REG_INTB_MSB/LSB` (L_DATA) після Step-0.

**Розрахунок fSENSOR (верифіковано в S-4):**

```
fSENSOR = (fCLKIN × RESP_TIME_cycles) / (3 × L_DATA)
        = (16,000,000 × 6144) / (3 × 36042)
        = 98,304,000,000 / 108,126
        = 909,148 Hz ≈ 909.2 kHz  ✅

Статус: ✅ VERIFIED — виміряно в S-4 (2026-03-23)
Примітка: Попередня оцінка 200–500 kHz була неточною.
          MIKROE-3240 осцилює на ~909 kHz (вищий за очікування — нормально).
```

**Висновки:**
- [x] Baseline RP = 57344 raw (≈21 kΩ)
- [x] RP_SET=0x26 підходить: **✅**
- [x] Коливання котушки підтверджено: 20/20 валідних відліків
- [x] fSENSOR = **909.2 kHz** — виміряно в S-4 ✅
- [x] protocol_id base: `p1_MIKROE3240_____mm` *(діаметр котушки уточнити в S-5)*
- [x] LFS task watermark: **212 B** (S-3) → **260 B** (S-4) — покращення після розширення стеку

**Наслідки:**
- RP_SET=0x26 підтверджений → конфіг не потребує змін
- fSENSOR=909.2 kHz визначено → котушка придатна для вимірювання монет
- LFS watermark 260 B (< 512 B рекомендованих, але стабільно — моніторити)
- Готовність до **S-5 (C-1: protocol_id)**

---

### Сесія S-4 — CLKIN підключення та верифікація fSENSOR ✅ ЗАВЕРШЕНО 2026-03-23

**Виконані дії:**
1. ✅ Фізично з'єднано: **G4 (EXT Pin 3) → mikroBUS Pin 16 (PWM/CLKIN)** (пайка)
2. ✅ Знайдено та виправлено root cause: `ConfigManager::loadFromLittleFS()` не реалізовано → `ldc1101.json` ніколи не читався → `clkin_gpio` завжди = -1. Додано крок 4i в `main.cpp` — парсинг JSON через ArduinoJson, завантаження ключів у `gConfig` перед `PluginSystem::begin()`.
3. ✅ Firmware перепрошито + uploadfs-sys → бутлог підтверджений

**Умови вимірювання:**
- [x] CLKIN дріт: G4 → mikroBUS Pin 16 з'єднано
- [x] Монети на котушці відсутні (підтверджено)
- [x] RESP_TIME=0x07 (6144 cycles) — максимальна якість
- [x] `ldc1101.json: 11 keys loaded into ConfigManager` — конфіг завантажено

**Лог (hw-verified 2026-03-23):**
```
[  1858ms] INFO  Config         | ldc1101.json: 11 keys loaded into ConfigManager
[  1859ms] INFO  LDC1101        | CLKIN on GPIO4 at 16000000 Hz (LEDC ch0)
[  1892ms] INFO  LDC1101        | configure_: DIG_CONFIG=0xD7, RP_SET=0x26
[  1892ms] INFO  LDC1101        | Ready. CS=5, RESP_TIME=0x07, RP_SET=0x26
[  5958ms] INFO  LDC1101        | Calibration start — remove coin from sensor
[  8359ms] INFO  LDC1101        | Calibration OK. RP=57344  L=36042  fSENSOR=909.2 kHz (20 samples)
[ 10001ms] DEBUG Stack          | LFS task watermark: 260 B free (of 3072 B stack)
```

**Результати вимірювань:**

| Параметр | Значення | Примітки |
|---|---|---|
| L_DATA (без CLKIN) | 3 (hw-верифіковано S-3) | шум SPI |
| L_DATA (з CLKIN) | **36042** ✅ | в 12000× більше ніж без CLKIN |
| fSENSOR (Eq.6, kHz) | **909.2 kHz** ✅ | в межах LDC1101 operating range (5kHz–10MHz) |
| LFS task watermark | **260 B** | покращення від 212 B (S-3) |

**Висновки S-4:**
- [x] CLKIN на GPIO4 підтверджено осцилографом (14V p-p квадрат 16 MHz → занадто великий рівень; після перевірки: G4 дійсно видавав тільки шум 0.05V доки `ConfigManager` не завантажував JSON). Root cause виявлено і виправлено.
- [x] L_DATA=36042 — валідне вимірювання індуктивності (vs L=3 без CLKIN)
- [x] fSENSOR=909.2 kHz — визначено, оцінка 200-500 kHz виявилась заниженою
- [x] ADR-CLKIN-002: CLKIN задіяно, L_DATA валідний → **закрито**
- ✅ **Готовність до S-5 (C-1: protocol_id)**

---

### Сесія S-4b — DRDYB=1 розслідування (феромагнітна монета) ✅ ЗАВЕРШЕНО 2026-03-24

**Тригер:** Цинк-нікелева монета 23.5mm / котушка 24mm (покриття 97.9%) при d≈0 спричиняла DRDYB=1 storm.

**Хронологія спроб (всі hw-верифіковано):**

| Крок | Config | DIG_CONFIG | Результат |
|---|---|---|---|
| 1 | bits=7 | `0xD7` | DRDYB storm при повному покритті |
| 2 | bits=5 | `0xD5` | DRDYB storm залишився |
| 3 | bits=3 | `0xD3` | DRDYB storm залишився |
| 4 | bits=3 + nibble=15 | `0xF3` | NO_OSC навіть **без монети!** |
| 5 | bits=7 (revert) + спейсер 1мм | `0xD7` | ✅ Стабільно |
| 6 | nibble=7 | `0x77` | ✅ hw-verified, margin=20 kHz |
| 7 | nibble=6 ← **FINAL** | `0x67` | ✅ hw-verified, margin=109 kHz |

**Root cause:** Нікелеве покриття (μr≈600, δ_skin≈10μm @ 909 kHz) при d=0 замикає магнітне коло як кришка pot-core → fSENSOR падає з 909 kHz до ~37-91 kHz → **два незалежних блокування**: переповнення L_DATA (16-bit) + watchdog MIN_FREQ (nibble=0xD → threshold=2.67 MHz).

**Корекція формули MIN_FREQ (з зовнішнього аудиту 2026-03-24):** Документація MikroE SDK вказувала 118 kHz для nibble=0xD — **помилка перекладу**. Правильно: `fSENSOR_min = 8 MHz / (16 − nibble)` (datasheet §8.6.5) → nibble=0xD → **2.67 MHz**.

**Рішення:**
- Фізичний мінімальний зазор d_min ≥ 1.5mm (лоток ≈ 2mm — достатньо) → **ADR-SPACER-001**
- `min_freq_nibble=6` (threshold=800 kHz, margin=109 kHz) → **ADR-MINFREQ-001**
- Фінальна конфігурація: `DIG_CONFIG=0x67` ✅

**Коміт:** `8c0309e`

**Cross-ref:** `docs/audit/FERROMAGNETIC_COIN_INVESTIGATION_2026-03-24.md` (повне розслідування + §11 відповідь на аудит)

---

### Сесія S-5 — C-1: Визначення protocol_id ✅ **hw-verified 2026-03-24** (commit `6628487`)

**Передумова:** S-4 ✅

**Результат:**

| Параметр | Значення |
|---|---|
| `protocol_id` | `"p1_MIKROE3240_024mm"` |
| fSENSOR | 909.2 kHz |
| Котушка | MIKROE-3240, діаметр 24mm |
| d_base | 1.5mm (підлога лотка) |
| Перша тестова монета | Україна 1 гривня Ag999, ⌀38.6mm, 31.1g (1oz) |

**Зміни:**
- `NVSManager.h`: `proto_id[16]→[20]` (19 симв+NUL)
- `NVSManager.cpp`: авто-migration з `"p1_UNKNOWN_013mm"` при завантаженні
- `Measurement.h`: поле `protocol_id[20]` додано до struct
- `MeasurementStore.cpp`: `save()` + `load()` серіалізація
- `HttpServer.cpp`: `GET /measure/{id}` повертає `protocol_id`
- `FINGERPRINT_DB_ARCHITECTURE.md §7`: заморожені константи оновлено

**hw-verified:** `GET /api/v1/measure/50` → `"protocol_id": "p1_MIKROE3240_024mm"` ✅

---

### Сесія S-6 — C-2: Multi-position state machine ⚠️ **реалізовано** (commit `313b179`) — hw-тест з spacers pending

**Передумова:** S-4 (fSENSOR) + S-5 (protocol_id)

**Що реалізуємо:**
```
IDLE → STEP_BASE(~2mm, tray) → [ENTER] → STEP_1(1mm) → [ENTER] → STEP_3(3mm) → [ENTER] → STEP_DRIFT(base) → COMPUTE → save()
```
> ⚠️ ADR-SPACER-001: STEP_BASE = монета в лотку (~2mm від котушки), **ніколи не 0mm** для феромагнітних монет.

**Acceptance criteria (перед початком):**

| | Тест | Очікування |
|---|---|---|
| 1 | Coin placed → STEP_0 auto-detect | `getCoinState() == COIN_PRESENT` |
| 2 | ENTER при STEP_0 | перехід STEP_1, лог + дисплей |
| 3 | Timeout 120s без ENTER | abort → IDLE, WARNING log |
| 4 | ENTER при STEP_3 → STEP_DRIFT | RP[3] ≈ RP[0] для стабільного сенсора |
| 5 | `|rp[3]-rp[0]|/rp[0] > 5%` | WARNING "Sensor drift detected", conf=0.0 |
| 6 | Full 4-step sequence | Measurement saved з `pos_count=4` |
| 7 | `GET /sensor/state` | реальний стан (не IDLE_NO_COIN заглушка) |

**Spacers:** _______ (матеріал, перевірена товщина)

**Результати (commit `313b179`, 2026-03-24):**
- [x] Тест 1: Coin placed → STEP_BASE auto-start — hw-verified: `Meas | Coin placed — session started (STEP_BASE)` ✅
- [x] Тест 3: Timeout 120s без ENTER → abort → IDLE — hw-verified: `146630ms − 26668ms = 119962ms ≈ 120.0s`, лог `WARN Meas | Step 1 timeout — session aborted` ✅
- [ ] Тест 2, 4: ENTER transitions — потребує hw-тесту з реальними spacers
- [ ] Тест 5: Drift check > 5%
- [ ] Тест 6: Full 4-step sequence → `pos_count=4` saved
- [x] Тест 7: `GET /sensor/state` повертає `MEASURING_STEP_BASE` — hw-verified: C-4 wired, `{"state":"MEASURING_STEP_BASE"}` ✅
- [ ] Типовий RP при монеті на tray (d=1.5mm): \_\_\_\_ (Ag999 hw-session планується)
- [ ] Типовий RP при монеті на +1mm spacer: \_\_\_\_
- [ ] Типовий RP при монеті на +3mm spacer: \_\_\_\_
- [ ] Drift `|rp[3]-rp[0]|/rp[0]`: \_\_\_\_% (typ)

**Spacers:** 3D-друк (PLA, товщина верифікується штангенциркулем)

---

### Сесія S-7 — C-4: HTTP endpoints hw-verification (2026-03-24)

**Що робили:** hw-верифікація C-4 (`GET /api/v1/sensor/state` real MeasState + `POST /api/v1/measure/start`).

**Тести (всі через `Invoke-RestMethod` на 192.168.88.53):**

| # | Тест | Результат |
|---|---|---|
| T1 | `GET /sensor/state` (монети немає) | ✅ `{"state":"IDLE_NO_COIN"}` |
| T2 | монета → `GET /sensor/state` (auto-start) | ✅ `{"state":"MEASURING_STEP_BASE"}` |
| T3 | `POST /measure/start` з IDLE (монета є, немає нового edge) | ✅ `202 {"started":true}` |
| T4 | `GET /sensor/state` одразу після POST start | ✅ `{"state":"MEASURING_STEP_BASE"}` |
| T5 | `POST /measure/start` (сесія активна) | ✅ `409 {"error":"already_measuring"}` |
| T6 | після timeout abort → `GET /sensor/state` | ✅ `{"state":"IDLE_NO_COIN"}` |

**Ключові деталі реалізації:**
- `sensorStateFn_` lambda читає `sMeas.state` (uint8_t, atomic на ESP32) без mutex — safe від lwIP thread
- `measStartFn_` lambda: перевіряє `sMeas.state == IDLE` → встановлює `volatile bool gMeasStartRequested` → MainLoop споживає на наступному тіку
- T2: auto-start від edge `IDLE_NO_COIN→COIN_PRESENT` відбувся раніше за `GET` — очікувана поведінка
- T3: перевіряє шлях `gMeasStartRequested` (монета лишилась після timeout, `sPrevCoinState==COIN_PRESENT` → немає нового edge)

**Висновки:**
1. `GET /sensor/state` повертає правильний стан в усіх 6 сценаріях ✅
2. `POST /measure/start` 202/409/503 працює коректно ✅
3. Race condition відсутній: volatile flag + single consumer (MainLoop)

**Наслідки:** C-4 hw-verified → закриває S-6 тест 7. Наступний: spacer hw-session (S-6 тести 2/4/5/6) → C-5 σ tuning.

---

### Сесія S-8 — C-4/C-5: VectorCompute + FP matching (планується)

**Передумова:** S-6 (C-2 готовий)

**Тестові монети (заповнити після вимірювань):**

| Монета | Метал | rp[0] | rp[1] | rp[2] | l[0] | dRp1 | k1 | k2 | slope | dL1 | match conf |
|---|---|---|---|---|---|---|---|---|---|---|---|
| | Ag999 | | | | | | | | | | |
| | Ag925 | | | | | | | | | | |
| | Cu | | | | | | | | | | |
| | (сталь) | | | | | | | | | | |

**Висновки про класифікаційний простір (заповнити):**
- Ag/Cu розрізняються: ✅/❌ (очікується >0.7 confidence)
- Ag999/Ag925 розрізняються: ✅/❌ (може потребувати σ tuning)

---

## Довідка: конфігурація сенсора

**Активна конфігурація (з бутлогу S-2):**

| Регістр | Значення | Параметр |
|---|---|---|
| RP_SET | 0x26 | MAX=24kΩ / MIN=1.5kΩ |
| TC1 | 0x1F | C1=0.75pF, R1=21.1kΩ |
| TC2 | 0x3F | C2=3pF, R2=30.5kΩ |
| DIG_CONFIG | 0xD7 | MIN_FREQ=118kHz + RESP_TIME=6144 cycles |

**GPIO:**

| | Значення |
|---|---|
| CS pin | GPIO5 |
| SPI bus | VSPI (SCK=40, MOSI=14, MISO=39) |
| SPI speed | 4 MHz |
| SPI mode | MODE0, MSBFIRST |

**Heap reference (STA+HTTP+LDC1101, no WebSocket, 2026-03-23):**

| Метрика | Значення |
|---|---|
| Heap при старті | 164,992 B |
| Після WiFi | ~55,208 B |
| Після HTTP | ~43,348 B |
| LFS task watermark | 308 B free (of 3072 B) |

> ⚠️ LFS watermark 308B < 512B recommended. Якщо при наступному тесті < 200B → збільшити stack у `LittleFSTransport.cpp`.

---

## Шаблон запису нової сесії

```markdown
### Сесія S-N — [Назва] (YYYY-MM-DD)

**Що робили:** [опис дій]

**Умови:** температура ___°C, напруга 3.3V = ___V

**Результати:**
| Параметр | Значення | Статус |
|---|---|---|
| | | |

**Ключові рядки лозу:**
```
[timestamp] LEVEL COMP | message
```

**Висновки:**
1. ...

**Наслідки / наступний крок:**
- [наступна дія]
```

---

**Оновлено:** 2026-03-23 (S-2: LDC1101 hw arrival verified)
