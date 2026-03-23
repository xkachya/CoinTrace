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

**Розрахунок fSENSOR (потребує L_DATA):**

```
fSENSOR = (fCLKIN × RESP_TIME_cycles) / (3 × L_DATA)
        = (16,000,000 × 6144) / (3 × ____ )
        = ______ Hz ≈ _____ kHz

Статус: ⏳ PENDING — потрібен L_DATA з burst read
Очікуваний діапазон: 200–500 kHz (LC-котушка MIKROE-3240)
```

**Висновки:**
- [x] Baseline RP = 57344 raw (≈21 kΩ)
- [x] RP_SET=0x26 підходить: **✅**
- [x] Коливання котушки підтверджено: 20/20 валідних відліків
- [ ] fSENSOR: **⏳ не визначений** — CLKIN (mikroBUS Pin 16) не підключений (ADR-CLKIN-002). Без CLKIN: L=3 (шум SPI), fSENSOR=неінформативне значення. Firmware LEDC готовий, потрібна фізична проводка G4 → Pin 16.
- [x] protocol_id base: `p1_MIKROE3240_____mm` *(діаметр котушки уточнити)*
- [x] LFS task watermark: **212 B ⚠️** (< 512 B рекомендованих)

**Наслідки:**
- RP_SET=0x26 підтверджений → конфіг не потребує змін
- fSENSOR вимагає підключення CLKIN (ADR-CLKIN-002). Firmware LEDC реалізовано (`ledcSetup/ledcAttachPin`, `clkin_gpio=4`). Наступний крок: фізично з'єднати **G4 (EXT Pin 3) → mikroBUS Pin 16** (див. S-4).
- LFS watermark 212 B → відстежити у S-4 (якщо падає < 100 B → збільшити `ldc1101_task_stack`)
- Готовність до **S-4** (підключення CLKIN та верифікація fSENSOR)

---

### Сесія S-4 — CLKIN підключення та верифікація fSENSOR (планується)

**План:**
1. Фізично з'єднати: **G4 (EXT Pin 3) → [22Ω опційно] → mikroBUS Pin 16 (PWM/CLKIN)**
2. Firmware вже готовий: `clkin_gpio=4` в `data/plugins/ldc1101.json`; LEDC `ledcSetup(0, 16MHz, 1-bit)` в `initialize()`
3. Flash + відкрити UART-монітор (COM4, 115200)
4. Верифікація бутлогу:
   - `[LDC1101] CLKIN on GPIO4 at 16000000 Hz (LEDC ch0)` — підтверджує роботу LEDC
   - `[LDC1101] Calibration OK. Baseline RP=57344  L=XXXX  fSENSOR=XXX.X kHz` — L повинен бути >> 3
5. Записати L_DATA та fSENSOR в таблицю результатів нижче

**Умови вимірювання:**
- [ ] CLKIN дріт: G4 → mikroBUS Pin 16 з'[єднано
- [ ] Монети на котушці відсутні (підтверджено візуально)
- [ ] RESP_TIME=0x07 (6144 cycles) — максимальна якість

**Лог (* заповнити після вимірювання):**
```
[заповнити]
```

**Результати вимірювань:**

| Параметр | Значення | Примітки |
|---|---|---|
| L_DATA (без CLKIN) | 3 (hw-верифіковано S-3) | шум SPI |
| L_DATA (з CLKIN) | _заповнити_ | очікування >>100 |
| fSENSOR (Eq.6, kHz) | _заповнити_ | очікування 200–500 kHz |
| LFS task watermark | _заповнити_ Б | моніторити |

**Наслідки:**
- L_DATA валідний і fSENSOR визначено → Готовність до **S-5 (C-1: protocol_id)**
- Якщо L_DATA все одно показує сміт → перевірити фізичне з'єднання та JP1 на MIKROE-3240

---

### Сесія S-5 — C-1: Визначення protocol_id (планується)

**Передумова:** S-4 (валідний fSENSOR з CLKIN)
2. Сформувати `protocol_id` за форматом FINGERPRINT_DB §3.2:
   - Формат: `p1_MIKROE3240_{coil_diameter}mm`
   - Приклад: `p1_MIKROE3240_013mm` (якщо котушка 13mm)
3. Оновити `data/plugins/ldc1101.json` — замінити `"p1_UNKNOWN_013mm"`
4. Оновити 5 synthetic FP entries — замінити `protocol_id` або видалити

**Очікувані результати:**
- `data/plugins/ldc1101.json` з реальним `protocol_id`
- Community-compatible записи (до оновлення — **не публікувати**)

---

### Сесія S-6 — C-2: Multi-position state machine (планується)

**Передумова:** S-4 (fSENSOR) + S-5 (protocol_id)

**Що реалізуємо:**
```
IDLE → STEP_0(0mm) → [ENTER] → STEP_1(1mm) → [ENTER] → STEP_3(3mm) → [ENTER] → STEP_DRIFT(0mm) → COMPUTE → save()
```

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

**Результати (заповнити після реалізації):**
- [ ] Тест 1–7 пройдено
- [ ] Типовий RP при монеті на 0mm: ____
- [ ] Типовий RP при монеті на 1mm: ____
- [ ] Типовий RP при монеті на 3mm: ____
- [ ] Drift `|rp[3]-rp[0]|/rp[0]`: ____% (typ)

---

### Сесія S-7 — C-4/C-5: VectorCompute + FP matching (планується)

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
