# NAU7802 Weight Sensor — Architecture Specification

**Версія:** 1.1.0  
**Дата:** 2026-04-08 (оновлено: 2026-04-08 — D-12a WIP peer review)  
**Статус:** 🔄 Active — D-12a WIP написаний, під архітектурним review перед реалізацією  

> **⚠️ D-12a WIP delta — виправити до commit D-12a:**
> 1. `CTRL1_VAL = 0x27` — **WRONG** → правильно `0xBC` (VLDO=3.0V at bits[7:5], GAINS=128x at bits[4:2])
> 2. `CTRL2_VAL = 0x30` — **WRONG** → правильно `0x60` (CRS=80SPS at bits[7:5], shift=5)
> 3. `_startupSequence()`: CTRL1/CTRL2 написані ДО OTP reload — **порядок неправильний** (ADR-NAU-007)
> 4. `tare()` SPS mask `~0x70` — **WRONG** → правильно `~0xE0` (CRS at bits[7:5])
> 5. `SETTLE_MS = 200` → рекомендовано **500ms** (ADR-NAU-006; налаштовується через `nau7802.json`)
**Hardware:** Nuvoton NAU7802 24-bit ADC + 100g load cell  
**Chip revision:** NAU7802 Rev 2.6 (datasheet EN)  
**Мотивація:** Wave 10 — 7D production vector (додається `mass_n`) для вирішення 5 критичних пар < 1.0σ у gen-6 DB  
**Cross-ref:**
- `PLUGIN_CONTRACT.md v1.0.0` — обов'язковий контракт
- `PLUGIN_INTERFACES_EXTENDED.md v1.2.0` — `ISensorPlugin`, `SensorType::WEIGHT`
- `LDC1101_ARCHITECTURE.md v1.6.0` — паралельний сенсор, спільна I2C шина
- `MEMORY_MAP.md v1.0.0` — heap бюджет
- `MEASUREMENT_WORKFLOW.md v1.0.0` — state machine для інтеграції
- `DISCOVERY_MODE_SPEC.md v1.0.0` — NDJSON формат
- `FINGERPRINT_DB_ARCHITECTURE.md v1.5.1` — schema versioning
- `docs/external/2026-04-08.WAVE10_ARCHITECTURE_PLAN.md` — контекст Wave 10

---

## Зміст

1. [Огляд і місце в системі](#1-огляд-і-місце-в-системі)
2. [Аналіз NAU7802 datasheet](#2-аналіз-nau7802-datasheet)
3. [Апаратна інтеграція](#3-апаратна-інтеграція)
4. [RAM та timing модель](#4-ram-та-timing-модель)
5. [Plugin архітектура](#5-plugin-архітектура)
6. [Capture state machine — інтеграція в Measurement Workflow](#6-capture-state-machine)
7. [Calibration підсистема](#7-calibration-підсистема)
8. [NDJSON та DB schema v8](#8-ndjson-та-db-schema-v8)
9. [Боттлнеки та ризики](#9-боттлнеки-та-ризики)
10. [Тестова стратегія](#10-тестова-стратегія)
11. [Архітектурні рішення (ADR)](#11-архітектурні-рішення-adr)
12. [Implementation checklist](#12-implementation-checklist)

---

## 1. Огляд і місце в системі

### 1.1 Мотивація

gen-6 DB (commit `30c011a`) має 5 критичних пар в 6D LDC1101-просторі:

```
XKENNED_B  <-> XUSSR10_A    0.27 sigma  OVERLAP   dm=21.8g
XFE_B      <-> XUSSR10_A    0.38 sigma  OVERLAP   dm=26.3g
XFE_B      <-> XUSSR10_A    0.82 sigma  OVERLAP   dm=26.3g
XAG999_B   <-> XKENNED_A    0.85 sigma            dm=19.6g
XFE_B      <-> XKENNED_B    0.97 sigma            dm= 4.5g
```

Маса є orthogonal dimension щодо всіх 6 компонентів production vector (dRp1_n, k1, k2, df_n, dL1_n, df1_n) — фізично не корелює з матеріалом у тому ж напрямку, що і EM-відгук. При W_mass=5.0 і sigma=0.35 всі 5 пар виходять > 2.0 sigma в 7D просторі.

### 1.2 Сенсорний ансамбль CoinTrace (оновлений)

```
Sensor          Bus    Measures              Conv time    Priority
LDC1101         SPI    RP + L (metal)        0.1-4 ms     PRIMARY
NAU7802         I2C    Mass (grams)          3-400 ms*    SECONDARY
                                             *залежить від SPS/N
```

> *LDC1101_ARCHITECTURE.md v1.6.0 §1 згадує HX711 — це застарілий план. NAU7802 є прямою заміною.*

### 1.3 Функціональні вимоги

| # | Вимога | Джерело |
|---|--------|---------|
| FR-01 | Зчитувати масу монети з точністю ±0.1г | Wave 10 Plan |
| FR-02 | Тарування з NVS persistence | Wave 10 Plan |
| FR-03 | 2-точкова калібровка (tare + known weight) | Wave 10 Plan |
| FR-04 | mass_g у NDJSON кожного запису | D-12d |
| FR-05 | mass_n у production_vector (7D) | D-12d |
| FR-06 | Зберегти CalibrationData в NVS namespace "nau7802" | D-12b |
| FR-07 | `update()` не блокує > 10 ms | PLUGIN_CONTRACT §2.1 |
| FR-08 | `read()` thread-safe (Core 0 + Core 1) | PLUGIN_CONTRACT §2.2 |
| FR-09 | Коректний `shutdown()` без `initialize()` | PLUGIN_CONTRACT §2.3 |
| FR-10 | Health monitoring через `getHealthStatus()` | PLUGIN_CONTRACT §2.5 |

---

## 2. Аналіз NAU7802 Datasheet

### 2.1 Ключові характеристики (Rev 2.6)

```
ADC:          24-bit sigma-delta
Channels:     2 differential (CH1 = основний, CH2 = резервний)
Interface:    I2C (400 kHz Fast Mode)
Address:      0x2A (ADDR pin -> GND) або 0x2B (ADDR pin -> VDD)
VDD:          2.7-3.6V  -- 3.3V ESP32 native, ІДЕАЛЬНО
Supply:       3.7 mA active, 0.4 uA power-down
Sample rates: 10, 20, 40, 80, 320 SPS (reg CTRL2 bits CRS[2:0])
PGA gain:     1, 2, 4, 8, 16, 32, 64, 128x (reg CTRL1 bits GAINS[2:0])
DRDY:         Output pin (active-low) + RDY bit у reg PU_CTRL
LDO:          Вбудований для load cell excitation (4.5V або 3.0V або AVDD)
```

### 2.2 Карта ключових регістрів

```
Addr  Name        Bits  Description
0x00  PU_CTRL     8     [7:4] reserved | [3] AVDDS | [2] CYCLE_START | [1] CYCLE_READY | [0] RR
                        RR=1 -> reset | CYCLE_READY -> conversion done | AVDDS -> internal LDO
0x01  CTRL1       8     [7:5] VLDO[2:0] | [4:2] GAINS[2:0] | [1:0] reserved
                        GAINS: 000=1x, 001=2x, ..., 111=128x
                        VLDO:  000=4.5V LDO, 001=4.2V, 010=3.9V, 011=3.6V, 100=3.3V, 101=3.0V
                               ЯКЩО AVDDS=0 (ext AVDD) -> VLDO ignored
0x02  CTRL2       8     [7:5] CRS[2:0] | [4] CAL_ERR (r/o) | [3] CALS (cal start) | [2] CALMOD | [1:0] CHS[1:0]
                        CRS: 000=10SPS, 001=20SPS, 010=40SPS, 011=80SPS, 111=320SPS
                        CHS: 00=CH1, 01=CH2
                        ⚠️  CRS at bits [7:5] → CTRL2_VAL = (CRS<<5), NOT (CRS<<4)
0x12  ADCO_B2     8     ADC output MSB (bits 23:16)
0x13  ADCO_B1     8     ADC output byte 2 (bits 15:8)
0x14  ADCO_B0     8     ADC output LSB (bits 7:0)
0x1F  I2C_CTRL    8     [6] BGPCP | [4] TS | [3] BOPGA | [2] SI | [1] WPD | [0] SPE
                        SPE=1 -> strong pull-up on SDA (300mA max!) -- НЕ використовувати
0x15  OTP_B1      8     Factory trim -- read-only, частина startup sequence
0x1B  OTP_B0      8     Factory trim -- read-only, частина startup sequence
```

### 2.3 Startup sequence (critical)

NAU7802 потребує специфічної послідовності ініціалізації після Power-On або Reset:

```
1.  Write 0x01 to PU_CTRL (0x00)  -- Reset (RR=1)
2.  Delay 10 ms
3.  Write 0x00 to PU_CTRL         -- Clear reset
4.  Write 0x06 to PU_CTRL         -- PUD=1 (power up digital) + PUA=1 (power up analog)
5.  Wait for PWRUP bit (PU_CTRL[3]) = 1, timeout 200 ms
6.  Read OTP_B1 (0x15), preserve bits
7.  Write OTP_B1 | 0x30 to reg 0x15  -- OTP reload step 1
8.  Write 0x00 to reg 0x00            -- OTP reload step 2 (clear RR)
9.  Write OTP_B1 | 0x30 to reg 0x15  -- OTP reload step 3
10. Read OTP_B0 (0x1B), write back unchanged
11. Write to CTRL1: gain + LDO config
12. Write to CTRL2: sample rate + channel
13. Write 0x30 to PU_CTRL: CYCLE_START=1 + AVDDS=1 (enable LDO for load cell)
14. Wait 600 ms for offset stabilization  -- CRITICAL: без цього перший результат невалідний
```

> **ADR-NAU-001:** Кроки 6-10 (OTP reload) — обов'язкові. Без них NAU7802 дає некоректні значення навіть якщо конверсія запускається. Це документовано в офіційному Application Note Nuvoton. Типова помилка при реалізації — пропускати OTP reload і отримувати drift 2-5% без видимої причини.

> **ADR-NAU-007 (порядок startup):** Кроки 11-12 (CTRL1/CTRL2) ПОВИННІ йти ПІСЛЯ завершення OTP reload (крок 10). Якщо CTRL1/CTRL2 написані ДО OTP reload — reload перезаписує trim bits (включно з PGA config), повертаючи PGA/LDO до factory defaults замість сконфігурованих значень → silent miscalibration. D-12a WIP має цей баг.

### 2.4 Вибір параметрів для CoinTrace

**PGA Gain:**
```
Load cell 100g: sensitivity ~1.0 mV/V (typical)
Excitation voltage (internal LDO 3.0V): FS_signal = 1.0 mV/V * 3.0V = 3.0 mV
NAU7802 AVDD = 3.3V, ADC Vref = AVDD:
  At PGA=128: effective input range = 3.3V / 128 = 25.8 mV
  FS coverage = 3.0 mV / 25.8 mV = 11.6%  -- добре, не клипує
  LSB = 3.0 mV / (128 * 2^24) = 1.4 nV/LSB (теоретичний)
  Practical noise: ~4-8 counts RMS @ 80SPS -> 0.004-0.008g RMS

ВИБІР: GAINS = 111 (128x)
```

**Sample Rate:**
```
Options:
  10 SPS  -> 100ms/sample, N=20 -> 2000ms  -- занадто повільно
  80 SPS  ->  12.5ms/sample, N=20 -> 250ms -- ОПТИМАЛЬНО (баланс noise/speed)
  320 SPS ->   3.1ms/sample, N=20 -> 62ms  -- швидко, але +noise, EMI-sensitive

ВИБІР: 80 SPS (CRS = 011) для production
       320 SPS (CRS = 111) опціонально через конфіг для fast_mode
```

**LDO Voltage:**
```
Internal LDO для load cell excitation: 3.0V (VLDO = 101)
Причина: 3.3V LDO AVDD = excitation, але VLDO=3.0V
дає кращу стабільність ніж AVDDS=0 (external AVDD)
```

**AVDDS (LDO enable):**
```
AVDDS = 1 -> вбудований LDO активований для excitation
Переваги: менше шуму від power rail ESP32 (3.3V з пульсаціями WiFi ~50mV)
NAU7802 built-in LDO ізолює load cell від WiFi noise
ВИБІР: AVDDS = 1 (internal LDO)
```

**Derived register constant values (правильне розміщення бітів):**
```
CTRL1 = [7:5] VLDO | [4:2] GAINS | [1:0] reserved
  VLDO=3.0V  : code=101b=5, bits[7:5] → 5 << 5 = 0xA0
  GAINS=128x : code=111b=7, bits[4:2] → 7 << 2 = 0x1C
  CTRL1_VAL  = 0xA0 | 0x1C = 0xBC         ← ПРАВИЛЬНО

CTRL2 = [7:5] CRS | [4] CAL_ERR | [3] CALS | [2] CALMOD | [1:0] CHS
  CRS=80SPS  : code=011b=3, bits[7:5] → 3 << 5 = 0x60
  CHS=CH1    : code=00 → 0
  CTRL2_VAL  = 0x60                        ← ПРАВИЛЬНО

CRS bits mask (для зміни SPS у tare()):
  CRS_CLEAR_MASK = ~0xE0   (очищення bits[7:5])   ← ПРАВИЛЬНО
  НЕ ~0x70 — це очищає тільки bits[6:4], залишаючи bit7 незмінним
```

---

## 3. Апаратна інтеграція

### 3.1 Підключення до Cardputer-Adv

```
NAU7802 Pin    ESP32-S3 Pin    Notes
-----------    ------------    ------
SDA            GPIO8           I2C шина -- вже використовується
SCL            GPIO9           I2C шина -- вже використовується
VDD            3.3V            Digital power
AVDD           3.3V            Analog power (або ВНУТРІШНІЙ LDO, тоді AVDDS=1)
GND            GND
DRDY           NC*             Polling через RDY bit; DRDY pin опціональний

Load cell wiring:
  RED   (E+)  -> NAU7802 AVDD (excitation+)
  BLACK (E-)  -> NAU7802 AGND (excitation-)
  WHITE (A+)  -> NAU7802 CH1+ (signal+)
  GREEN (A-)  -> NAU7802 CH1- (signal-)

I2C address: 0x2A (ADDR pin -> GND, default)
```

> *DRDY pin може бути підключений до вільного GPIO для interrupt-driven reads у майбутньому (ADR-NAU-002), але polling через RDY bit є достатнім для поточної реалізації.*

### 3.2 I2C шина — конкурентність

NAU7802 ділить I2C шину з іншими пристроями Cardputer (якщо є). Відповідно до `PLUGIN_CONTRACT.md §1.5`:

```
Правило: update() викликається з main task (Core 0) -> mutex НЕ потрібен для update()
         read() може викликатись з будь-якого task -> internal data mutex потрібен

НЕ плутати:
  ctx->wireMutex  -- для I2C доступу з async task (NAU7802Plugin не має async task)
  internal mutex  -- для _cachedMass між update() та read()
```

NAU7802Plugin реалізується за **Strategy A** (без окремого FreeRTOS task) — всі I2C транзакції в `update()`, яка викликається з main task. Це виключає потребу в `ctx->wireMutex` всередині `update()`.

**Важливо:** довга acquistion (250ms при 80SPS/N=20) — блокуюча операція, несумісна з `update()` контрактом (<10ms). Вирішується через **non-blocking state machine** всередині `update()` (детально у §5.3).

### 3.3 Механічна інтеграція

```
Механічна схема (cross-section):

     [монета]
        |
  [платформа / спейсер 0.6мм]
        |
  [load cell platform]    <-- 3D-друкована платформа, 0.5-1mm алюміній
        |
  [load cell (100g)]      <-- наклеєний або bolt-mounted
        |
  [base frame]            <-- нерухома основа
        |
  [котушка LDC1101]       <-- під нерухомою основою або збоку

Вимоги до платформи:
  - Монета знаходиться на тій самій точці при кожному вимірі (+-1mm)
  - Load cell не деформується від сили притискання спейсерів (< 0.5N)
  - Центр маси платформи збігається з center of load cell
  - Маса платформи (< 50г): тарується при кожному старті
```

---

## 4. RAM та Timing модель

### 4.1 RAM бюджет

**Поточний стан heap (MEMORY_MAP.md §3, hw-verified 2026-03-18):**
```
Idle steady state: ~30-34 KB free heap
Hard floor (OOM):  ~15 KB
```

**NAU7802Plugin RAM:**
```cpp
// Static object (BSS/stack):
struct NAU7802Plugin {
    PluginContext*    ctx;           //  4 B
    bool             initialized;   //  1 B
    bool             calibrated;    //  1 B
    uint8_t          addr;          //  1 B
    uint8_t          errorCount;    //  1 B
    int32_t          zeroOffset;    //  4 B
    float            scaleFactor;   //  4 B
    float            cachedMassG;   //  4 B
    uint32_t         cachedTs;      //  4 B
    ErrorCode        lastError;     // ~64 B (code + 60-char string)
    SemaphoreHandle_t mutex;        //  4 B (handle, 4B; object in heap)
    // Acquisition state machine:
    AcqState         acqState;      //  1 B (enum)
    uint8_t          sampleIdx;     //  1 B
    float            sampleBuf[20]; // 80 B (N_SAMPLES_MAX=20)
    uint32_t         acqStartMs;    //  4 B
    // Padding:                      ~3 B
    // TOTAL:                       ~181 B
};

// FreeRTOS mutex object (heap-allocated in initialize()):
//   ~80 B (StaticSemaphore_t або dynamic за ESP-IDF)

// РАЗОМ: ~261 B heap + BSS
// КОНТРАКТ: <= 8192 B  -> запас 7931 B  [OK]
```

**Порівняння з LDC1101Plugin:**
```
LDC1101Plugin RAM: ~2-4 KB (dual cache, StabilityTracker, SPI buffers)
NAU7802Plugin RAM: ~261 B
РАЗОМ нових plugin: < 5 KB  -> в межах бюджету
```

### 4.2 Вплив на heap після інтеграції

```
Поточний idle heap:  ~30-34 KB
NAU7802Plugin:           -261 B
FreeRTOS mutex:           -80 B
NVS namespace "nau7802": -200 B (approx)
────────────────────────────────
Projected idle heap: ~29.5-33.5 KB

При вимірюванні (sampleBuf + I2C frames ephemeral):
  sampleBuf [20 float]: 80 B (static у plugin, не heap)
  ArduinoJson (NDJSON): ~800 B (існуюча ephemeral)
  I2C Rx buffer:         32 B (stack-local у readRaw())
────────────────────────────────
Peak during measurement: ~29 KB  -> вище hard floor (15 KB)  [OK]
```

**Висновок:** NAU7802 не створює memory pressure. Heap бюджет залишається в безпечній зоні.

### 4.3 Timing модель

#### I2C транзакції (400 kHz Fast Mode)
```
Читання ADC output (3 bytes, addr 0x12..0x14):
  START + addr_write (0x54) + reg (0x12) = 2 bytes + START(2us) + STOP(2us)
  Repeated START + addr_read (0x55) + 3 data bytes
  Total bits: (1+8+1) * 4 = 40 bits @ 400kHz = 100 us
  + overhead (stretch, ACK): ~75 us
  TOTAL: ~175 us per raw read

Check RDY bit (PU_CTRL read, 1 byte):
  ~75 us

Full cycle (check RDY + read ADC):
  ~250 us  (вкладається в LDC1101 ~1ms cycle)
```

#### Non-blocking acquisition (80 SPS, N=20):
```
update() call frequency: >= 10 Hz (PLUGIN_CONTRACT guarantee)
Sample interval at 80 SPS: 12.5 ms

Кожен update():
  - Check RDY bit: ~75 us
  - If ready: read ADC: ~175 us, store to sampleBuf[idx++]
  - If idx == N: compute median, update cache, signal complete
  - TOTAL per update(): <= 250 us  << 10 ms limit  [OK]

Total acquisition time: N * 12.5ms = 250 ms (N=20)
Number of update() calls: ~25 (at 10Hz base) to ~250 (at 100Hz)
```

#### Порівняння режимів:
```
Mode        SPS    N     Acq time   Noise RMS   Use case
fast        320    10     31 ms      ~0.02g     debug/fast сесія
production   80    20    250 ms      ~0.004g    standard вимір
precise      10    32   3200 ms      ~0.001g    калібрування
```

### 4.4 Measurement Workflow — Sequential architecture (ADR-NAU-008)

> **⚠️ Фізичне обмеження:** NAU7802 (ваги) та LDC1101 (котушка) — **окремі фізичні позиції**. Монета не може одночасно бути на вагах і на котушці. Паралельна acquisition архітектурно неможлива. Потрібні послідовні кроки.

**Sequential Measurement Workflow (p4 protocol):**

```
═══════════════════════════════════════════════════════════════════
 STEP_WEIGHT:  "Покласти монету на ваги → ENTER"
   settle(500ms) → acquisition(250ms, 80SPS/N=20) → mass_g
   Час: ~750ms
═══════════════════════════════════════════════════════════════════
   ↓  ENTER pressed (маса захоплена)
═══════════════════════════════════════════════════════════════════
 STEP_QUICK (= STEP_BASE на котушці, без спейсерів):
   "Перекласти на котушку → ENTER"
   LDC1101 captureStep() → dRp_base, fSensor_base
   matchQuick(rp, l, fSensor, mass_n)  ← Quick Screen Phase 2!
   Час: ~2000ms
   confidence ≥ threshold → PROPOSE RESULT → [C]onfirm/[F]ull/[N]ext
═══════════════════════════════════════════════════════════════════
   ↓  якщо low confidence → auto-proceed
═══════════════════════════════════════════════════════════════════
 STEP_1 / STEP_3 (спейсери 1.6mm / 2.6mm):
   LDC1101 captureStep() × 2 → k1, k2, df1_n
   matchFull(dRp1_n, k1, k2, df_n, dL1_n, df1_n, mass_n)  ← 7D
   Час: ~3000ms
═══════════════════════════════════════════════════════════════════
 RESULT: 7D match, coin_name, confidence, mass_g displayed
═══════════════════════════════════════════════════════════════════

6D fallback (NAU7802 відсутній або не скалiбрований):
   Пропускається STEP_WEIGHT → workflow = Wave 9 production (6D)
   Backward compatible з gen-7 DB
```

**Timing порівняння workflow:**

```
Mode              Steps                         Total time     Resolution
─────────────────────────────────────────────────────────────────────────
Wave 9 (6D)      BASE+S1+S3                    ~4.5s          6D, 5 pairs <1σ
Weight only       STEP_WEIGHT                   ~0.75s         1D, ~40% classes
Quick 7D          STEP_WEIGHT + STEP_QUICK       ~2.75s         7D quick, ~70% classes
Full 7D           STEP_WEIGHT + BASE+S1+S3      ~5.25s         7D full, all classes
```

> **Ключовий insight:** STEP_QUICK і STEP_BASE — той самий LDC1101 capture step (без спейсерів). Після quick result пропозиції, якщо користувач обирає [F]ull — STEP_BASE дані вже є, додаються тільки S1 та S3. Немає дублювання capture.

---

## 5. Plugin архітектура

### 5.1 Ієрархія класів

```
IPlugin
  └── ISensorPlugin (SensorType::WEIGHT)
        └── NAU7802Plugin
                │
                ├── AcquisitionStateMachine  (вбудований enum)
                │     IDLE | SETTLING | SAMPLING | COMPLETE | ERROR
                │
                └── CalibrationData          (проста struct)
                      int32_t zero_offset
                      float   scale_factor
```

### 5.2 Public API

```cpp
// lib/NAU7802Plugin/src/NAU7802Plugin.h

class NAU7802Plugin : public ISensorPlugin {
public:
    // === IPlugin lifecycle ===
    bool         canInitialize() override;
    bool         initialize(PluginContext* ctx) override;
    void         update() override;        // non-blocking, <= 250 us/call
    void         shutdown() override;

    // === ISensorPlugin ===
    // read() -> SensorData { value = mass_g, value2 = mass_n, valid = bool }
    SensorData         read()             override;
    SensorMetadata     getMetadata() const override;
    SensorType         getType()     const override { return SensorType::WEIGHT; }

    // === Calibration (blocking -- call outside update() loop) ===
    // Відповідно до PLUGIN_CONTRACT §3.4: calibrate() може блокувати
    bool         tare(uint16_t samples = 32);
    // 2-point calibration: після tare(), з відомою масою на платформі
    bool         calibrate(float known_mass_g, uint16_t samples = 32);

    // === Acquisition control ===
    // Запустити non-blocking acquisition (викликається з Measurement Workflow)
    void         startAcquisition();
    // true якщо acquisition завершена і результат готовий
    bool         isAcquisitionComplete() const;
    // Поточний результат (mass_g) -- valid тільки якщо isAcquisitionComplete()
    float        getLastMassG() const;

    // === NVS persistence ===
    bool         saveCalibration();
    bool         loadCalibration();

    // === Diagnostics ===
    HealthStatus getHealthStatus() override;
    ErrorCode    getLastError()    const override;
    const char*  getName()         const override { return "NAU7802Plugin"; }
    const char*  getVersion()      const override { return "1.0.0"; }

private:
    // --- Hardware ---
    PluginContext*     _ctx         = nullptr;
    uint8_t            _addr        = 0x2A;

    // --- Calibration ---
    int32_t            _zeroOffset  = 0;
    float              _scaleFactor = 1.0f;
    bool               _calibrated  = false;

    // --- Acquisition state machine ---
    enum class AcqState : uint8_t { IDLE, SETTLING, SAMPLING, COMPLETE, ERROR };
    AcqState           _acqState    = AcqState::IDLE;
    uint8_t            _sampleIdx   = 0;
    uint32_t           _acqStartMs  = 0;
    static constexpr uint8_t  N_SAMPLES    = 20;
    static constexpr uint16_t SETTLE_MS    = 200;
    static constexpr uint8_t  SPS_80       = 0x03;  // CTRL2 CRS bits
    float              _sampleBuf[N_SAMPLES];        // 80 B, static

    // --- Cached result (thread-safe via mutex) ---
    float              _cachedMassG = 0.0f;
    float              _cachedMassN = 0.0f;
    uint32_t           _cachedTs    = 0;
    bool               _cachedValid = false;
    SemaphoreHandle_t  _mutex       = nullptr;

    // --- Health ---
    uint8_t            _errorCount  = 0;
    bool               _initialized = false;
    ErrorCode          _lastError   = {0, ""};

    // --- Constants ---
    static constexpr float    MASS_REF_G = 33.3f;  // XUSSR10, нормалізатор
    static constexpr uint16_t INIT_TIMEOUT_MS = 200;

    // --- Private methods ---
    bool     _writeReg(uint8_t reg, uint8_t val);
    uint8_t  _readReg(uint8_t reg);
    bool     _readAdc24(int32_t& out);  // читає 3 байти ADCO
    bool     _isReady();                // читає RDY bit
    bool     _startupSequence();        // OTP reload + config
    float    _computeMedian(float* buf, uint8_t n);
    void     _updateAcqStateMachine();  // викликається з update()
};
```

### 5.3 Non-blocking Acquisition State Machine

Головне архітектурне рішення: acquisition **не блокує** `update()`. Замість `for(i<N) { wait; read; }` — state machine:

```cpp
void NAU7802Plugin::_updateAcqStateMachine() {
    switch (_acqState) {
    case AcqState::IDLE:
        break;  // startAcquisition() не викликався -- нічого не робимо

    case AcqState::SETTLING:
        // Чекаємо SETTLE_MS після монети (механічне демпфування)
        if (millis() - _acqStartMs >= SETTLE_MS) {
            _sampleIdx = 0;
            _acqState = AcqState::SAMPLING;
        }
        break;  // << виходимо одразу, <= 75us (перевірка millis())

    case AcqState::SAMPLING:
        // Один sample per update() call -- якщо ready
        if (!_isReady()) break;  // << ~75us, виходимо якщо не готово

        int32_t raw;
        if (!_readAdc24(raw)) {  // << ~175us
            _errorCount++;
            if (_errorCount > 10) {
                _acqState = AcqState::ERROR;
                _lastError = {4, "NAU7802: ADC read failed > 10 times"};
            }
            break;
        }

        // Зберігаємо raw -> конвертуємо в грами пізніше (уникаємо float в ISR path)
        _sampleBuf[_sampleIdx] = static_cast<float>(raw);
        _sampleIdx++;

        if (_sampleIdx >= N_SAMPLES) {
            // Усі N семплів зібрані -> compute + publish result
            float median_raw = _computeMedian(_sampleBuf, N_SAMPLES);
            float mass_g = (median_raw - _zeroOffset) * _scaleFactor;
            if (mass_g < 0.0f) mass_g = 0.0f;

            // Thread-safe update cached result
            if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                _cachedMassG    = mass_g;
                _cachedMassN    = mass_g / MASS_REF_G;
                _cachedTs       = millis();
                _cachedValid    = true;
                xSemaphoreGive(_mutex);
            }
            _acqState = AcqState::COMPLETE;
            _ctx->log->debug("NAU7802", "mass=%.2fg (n=%d)", mass_g, N_SAMPLES);
        }
        break;  // << TOTAL per update() в SAMPLING: ~250us  [OK]

    case AcqState::COMPLETE:
        break;  // чекаємо явного startAcquisition()

    case AcqState::ERROR:
        break;  // чекаємо reset або reinitialize
    }
}
```

**Перевірка контракту:**
```
update() time budget:
  IDLE:     0 us          [OK < 10ms]
  SETTLING: ~5 us         [OK < 10ms]
  SAMPLING (not ready): ~75 us  [OK < 10ms]
  SAMPLING (ready):    ~250 us  [OK < 10ms]
  COMPLETE:  0 us         [OK < 10ms]
```

### 5.4 Thread Safety модель

```
Core 0 (main task):           Core 1 (background task, якщо є):
  update()                       read()
    _updateAcqStateMachine()        xSemaphoreTake(_mutex)
      xSemaphoreTake(_mutex)          _cachedMassG  [READ]
        _cachedMassG [WRITE]          _cachedTs     [READ]
        _cachedTs    [WRITE]          _cachedValid  [READ]
        _cachedValid [WRITE]        xSemaphoreGive(_mutex)
      xSemaphoreGive(_mutex)

Timeout: pdMS_TO_TICKS(5) -- узгоджено з PLUGIN_CONTRACT §2.2 (ADR-ST-008: не portMAX_DELAY)
```

### 5.5 Конфігураційний файл

`data/plugins/nau7802.json` (завантажується через ConfigManager):

```json
{
  "nau7802.i2c_addr":       42,
  "nau7802.sample_rate":    80,
  "nau7802.pga_gain":       128,
  "nau7802.n_samples":      20,
  "nau7802.settle_ms":      200,
  "nau7802.mass_ref_g":     33.3,
  "nau7802.ldo_voltage":    3,
  "nau7802.enable_ldo":     true,
  "nau7802.fast_mode_sps":  320,
  "nau7802.fast_mode_n":    10
}
```

---

## 6. Capture State Machine — інтеграція в Measurement Workflow

### 6.1 Де запускати acquisition

Відповідно до `MEASUREMENT_WORKFLOW.md §2`, вимір проходить через стани:
`IDLE → STEP_BASE → STEP_1 → STEP_3 → STEP_DRIFT → COMPUTE → IDLE`

NAU7802 вимірює масу **один раз** — на STEP_BASE (монета при 0.6мм, мінімальна відстань, найстабільніша позиція).

```
Нова схема станів (p4 protocol):

IDLE
  |
  | coin detected + ENTER
  v
STEP_BASE:
  ├── LDC1101 settle (500ms) -- unchanged
  ├── NAU7802 startAcquisition()  ──┐ запускаємо одночасно з LDC1101 settle
  │                                  │
  │   [update() loop running]       │
  │   NAU7802 state machine:        │
  │     SETTLING -> SAMPLING        │
  │     -> COMPLETE (250ms total) ──┘ завершується всередині LDC1101 window
  │
  ├── LDC1101 capture (2000ms) -- unchanged
  ├── [NAU7802 isAcquisitionComplete() == true на цьому етапі]
  ├── mass_g = gNAU->getLastMassG()  ← зчитуємо результат
  └── ENTER
  |
  v
STEP_1 (1.6mm):  -- LDC1101 тільки, NAU7802 не активна
  ...
STEP_3 (2.6mm):  -- LDC1101 тільки
  ...
STEP_DRIFT:      -- LDC1101 тільки
  ...
COMPUTE:
  ├── обчислюємо production_vector (6D LDC1101 компоненти) -- unchanged
  ├── mass_n = mass_g / MASS_REF_G                          ← НОВЕ
  ├── додаємо mass_n до production_vector (7D)              ← НОВЕ
  ├── зберігаємо NDJSON (оновлений формат)                  ← НОВЕ
  └── → IDLE
```

### 6.2 Інтеграція в main.cpp

```cpp
// --- Існуючий глобальний стан ---
// extern LDC1101Plugin* gLDC;
// Нове:
NAU7802Plugin* gNAU = nullptr;  // глобальний pointer, аналогічно gLDC

// --- setup() ---
gNAU = new NAU7802Plugin();
if (gNAU->initialize(pluginCtx)) {
    if (!gNAU->loadCalibration()) {
        gLog.warn("main", "NAU7802 not calibrated -- run calibration wizard");
        // UI: відобразити попередження, не блокувати старт
    }
    gLog.info("main", "NAU7802 ready: cal=%s", gNAU->isCalibrated() ? "yes" : "no");
} else {
    gLog.error("main", "NAU7802 init failed: %s", gNAU->getLastError().message);
    delete gNAU;
    gNAU = nullptr;  // система продовжує без ваги (degraded mode)
}

// --- main loop update() ---
if (gNAU) gNAU->update();  // завжди викликаємо -- non-blocking

// --- STEP_BASE entry (measurement workflow) ---
case MeasState::STEP_BASE:
    gLDC->settle(...);
    if (gNAU && gNAU->isCalibrated()) {
        gNAU->startAcquisition();  // запускаємо паралельно зі settle
    }
    // ... LDC1101 capture як раніше
    
    // Зчитуємо масу після LDC1101 capture
    if (gNAU && gNAU->isAcquisitionComplete()) {
        sMassG = gNAU->getLastMassG();
    } else {
        sMassG = -1.0f;  // N/A (NAU не готовий або не калібрований)
    }
    break;

// --- COMPUTE state ---
case MeasState::COMPUTE:
    // ... існуючий обчислення production_vector ...
    
    // НОВЕ: додати mass_n
    float mass_n = (sMassG > 0.0f) ? (sMassG / NAU7802Plugin::MASS_REF_G) : -1.0f;
    pv.mass_n = mass_n;  // -1.0f = N/A якщо вага недоступна
    
    // ... збереження NDJSON ...
    break;
```

### 6.3 Degraded mode (NAU7802 недоступний)

```
Якщо gNAU == nullptr або !gNAU->isCalibrated():
  - mass_n = -1.0f у production_vector
  - FingerprintCache::query() -- якщо mass_n == -1.0f -> w_mass = 0.0f (ігнорує вагу)
  - Система продовжує як 6D матчинг (поведінка gen-6)
  - UI: іконка "W?" на Quick Screen

Это важливо для backward compatibility: старі пристрої без NAU7802
автоматично ігнорують вагову вісь.
```

---

## 7. Calibration підсистема

### 7.1 NVS схема

```
Namespace: "nau7802"
Key            Type       Description
"zero"         int32_t    zero_offset (raw ADC при порожній платформі)
"scale"        float      scale_factor (gramas per raw ADC unit)
"cal_ok"       uint8_t    1 = calibrated, 0 = not
"cal_ts"       uint32_t   Unix timestamp калібрування (для drift monitor)
"cal_mass_g"   float      Відома маса при калібруванні (для re-verify)
```

### 7.2 2-точкова калібровка

```cpp
// Процедура (blocking -- викликати з UI, поза update() loop):

bool NAU7802Plugin::tare(uint16_t samples) {
    // Передумова: платформа порожня
    int64_t sum = 0;
    for (uint16_t i = 0; i < samples; i++) {
        _waitReady(100);
        int32_t raw;
        if (!_readAdc24(raw)) return false;
        sum += raw;
        delay(1000 / _sps + 1);  // чекаємо наступний sample
    }
    _zeroOffset = static_cast<int32_t>(sum / samples);
    // scale_factor поки не змінюємо
    _ctx->log->info("NAU7802", "Tare: zero_offset=%ld", _zeroOffset);
    return true;
}

bool NAU7802Plugin::calibrate(float known_mass_g, uint16_t samples) {
    // Передумова: tare() вже виконано, known_mass_g на платформі
    if (known_mass_g <= 0.0f) return false;
    
    int64_t sum = 0;
    for (uint16_t i = 0; i < samples; i++) {
        _waitReady(100);
        int32_t raw;
        if (!_readAdc24(raw)) return false;
        sum += raw;
        delay(1000 / _sps + 1);
    }
    int32_t raw_avg = static_cast<int32_t>(sum / samples);
    int32_t delta = raw_avg - _zeroOffset;
    
    if (delta <= 0) {
        _lastError = {5, "NAU7802: calibration weight not detected (delta<=0)"};
        return false;
    }
    
    _scaleFactor = known_mass_g / static_cast<float>(delta);
    _calibrated = true;
    
    _ctx->log->info("NAU7802", "Cal: scale=%.6f g/cnt (%.2fg @ delta=%ld)",
                    _scaleFactor, known_mass_g, delta);
    return saveCalibration();
}
```

### 7.3 UX Flow на пристрої

```
Активація: при старті (NVS "cal_ok" == 0) або клавіша 'K'

ЕКРАН 1: "SCALE CALIBRATION"
         "Remove all weight"
         "Press OK"
  -> tare(32)  [blocking ~4s при 10SPS, ~400ms при 80SPS]
  -> "Zero set OK"

ЕКРАН 2: "Place 20g weight"
         "on platform"
         "Press OK"
  -> calibrate(20.0f, 32)
  -> "Cal OK: XX.Xg" (verify reading)

ЕКРАН 3 (verify): "Verification"
         "Reading: XX.Xg"
         "Expected: 20.0g"
         "Error: X.Xg  OK/RETRY"
  -> якщо |reading - 20.0| < 0.3g -> ACCEPT
  -> інакше -> RETRY (повернутись до ЕКРАН 1)
```

### 7.4 Drift моніторинг (майбутнє)

```
При кожному tare() перед сесією:
  current_zero = _zeroOffset
  saved_zero = NVS["zero"]
  drift_pct = abs(current_zero - saved_zero) / abs(saved_zero) * 100

  if drift_pct > 5%:
    log.warn("NAU7802", "Zero drift %.1f%% since last cal", drift_pct)
    UI: "Scale drift detected, recalibrate?"
```

---

## 8. NDJSON та DB schema v8

### 8.1 Оновлений NDJSON формат (Discovery + Production)

```json
{
  "index": 42,
  "coin_name": "USSR 10 Rubles Ag900 (Side B)",
  "metal_code": "XUSSR10",
  "mass_g": 33.28,
  "mass_n": 0.9994,
  "steps": [
    {
      "step": "base_0.6mm",
      "mass_g": 33.28,
      "rp_median": 39420,
      "rp_mean": 39426.1,
      "rp_sigma": 25.3,
      "rp_n": 110,
      "l_median": 22678,
      "fSensor_hz": 757200
    },
    { "step": "addon_1.6mm", "...": "..." },
    { "step": "addon_2.6mm", "...": "..." },
    { "step": "drift_0.6mm", "...": "..." }
  ],
  "production_vector": {
    "dRp1_n": -17.44,
    "k1": 1.412,
    "k2": 1.521,
    "df_n": 0.866,
    "dL1_n": -2.578,
    "df1_n": 0.520,
    "mass_n": 0.9994
  },
  "match_result": { "...": "..." }
}
```

**Backward compatibility:**
- Стара схема (gen-6, 6D): `production_vector` без `mass_n`
- Нова схема (gen-7, 7D): `production_vector` з `mass_n`
- FingerprintCache::query(): якщо `mass_n` відсутній в записі або = -1.0 -> `w_mass = 0.0`

### 8.2 index.json DB schema v8

```json
{
  "version": 8,
  "schema_version": 2,
  "generated_at": "2026-04-XX",
  "entries": [
    {
      "id": "xussr10_b/c13_hw_2026-04-XX",
      "protocol_id": "p4_MIKROE3240_b06_012mm_mass",
      "metal_code": "XUSSR10",
      "coin_name": "USSR 10 Rubles Ag900 (Side B)",
      "centroid": {
        "dRp1_n": -17.44,
        "k1": 1.412,
        "k2": 1.521,
        "df_n": 0.866,
        "dL1_n": -2.578,
        "df1_n": 0.520,
        "mass_n": 0.9994
      },
      "radius_95pct": 0.085,
      "records_count": 20,
      "mass_g_mean": 33.29,
      "mass_g_sigma": 0.04
    }
  ]
}
```

**Нові поля в entry:** `mass_g_mean`, `mass_g_sigma` — для діагностики і calibration verify.

### 8.3 matcher.json v6

```json
{
  "version": 6,
  "generated_at": "2026-04-XX",
  "full_weights": [1.5, 0.0, 1.0, 3.0, 2.5, 0.4, 5.0],
  "quick_weights": [2.0, 0.0, 0.0, 4.0, 2.5, 0.3, 3.0],
  "sigma": 0.35,
  "keys": ["dRp1_n", "k1", "k2", "df_n", "dL1_n", "df1_n", "mass_n"],
  "notes": {
    "mass_n": "Normalized mass: mass_g / 33.3 (XUSSR10 ref). w=5.0 resolves all 5 critical pairs from gen-6.",
    "mass_n_absent": "If mass_n = -1.0 (no NAU7802), w_mass is set to 0.0 (6D fallback mode)."
  }
}
```

---

## 9. Боттлнеки та ризики

### 9.1 B-01: I2C конфлікт при одночасному доступі

**Ризик:** NAU7802 (I2C 0x2A) і потенційні майбутні I2C пристрої на одній шині. LDC1101 — SPI, не конкурує.

**Аналіз:** NAU7802Plugin використовує Strategy A (все в `update()`, main task). Немає окремого FreeRTOS task -> немає race condition з `ctx->wireMutex`. Якщо в майбутньому з'явиться async I2C плагін — він правильно візьме `ctx->wireMutex`.

**Статус:** не блокує. Моніторити при додаванні нових I2C плагінів.

---

### 9.2 B-02: Механічний шум при вимірюванні

**Ризик:** Вібрації при натисканні кнопок Cardputer або при кладанні спейсерів можуть давати spike у масі.

**Вирішення:**
1. Settle 200ms — достатньо для механічного демпфування (f_natural для 50г платформи + 33г монети ~ 20-30 Hz, затухання < 100ms)
2. Медіана N=20 семплів — відкидає outlier spikes
3. Acquisition тільки на STEP_BASE — до кладання addon spacers

**Статус:** вирішено архітектурно.

---

### 9.3 B-03: Temperature drift load cell

**Ризик:** Типовий drift load cell 0.05%FS/°C. При FS=100g і ΔT=10°C: drift = 0.05g. Для потреб проекту (мін. різниця 0.5g між класами) — прийнятно.

**Вирішення:** tare() при кожному старті пристрою (компенсує zero drift, але не span drift). Span drift 0.05%FS/°C при ΔT=10°C = 0.05g — значно менше 0.5g мінімальної різниці.

**Статус:** не блокує.

---

### 9.4 B-04: WiFi noise на analog chain

**Ризик:** ESP32 WiFi (2.4GHz) генерує pulse noise на 3.3V rail ~50mV pk-pk, що може індуктуватись в аналоговий ланцюг load cell.

**Вирішення:**
1. NAU7802 AVDDS=1 (internal LDO) ізолює load cell excitation від power rail
2. N=20 медіана відкидає EMI spikes
3. 80SPS низькочастотний фільтр NAU7802 sigma-delta ADC відфільтровує 2.4GHz (оцифровує тільки DC..100Hz)
4. Wires load cell розміщувати якомога далі від WiFi антени

**Статус:** вирішено HW+SW.

---

### 9.5 B-05: Register bit field помилки у D-12a WIP

**Ризик:** D-12a WIP skeleton має 4 помилки в register constants (знайдено при peer review 2026-04-08). Silent failures: чіп ініціалізується без помилок, але SPS/LDO/PGA налаштовані неправильно.

| Константа | WIP значення | Правильне | Ефект помилки |
|---|---|---|---|
| `CTRL1_VAL` | `0x27` | `0xBC` | LDO≠3.0V, PGA≠128x — неправильний масштаб |
| `CTRL2_VAL` | `0x30` | `0x60` | SPS невизначений (CRS bits у reserved zone) |
| CRS mask у `tare()` | `~0x70` | `~0xE0` | Неповне очищення CRS bits при 10SPS switch |
| CTRL1/CTRL2 order | перед OTP | після OTP | OTP скидає GAINS/LDO → PGA=1x, LDO wrong |

**Вирішення:** Виправити всі 4 перед commit D-12a. Деталі: §2.4 «Derived register constants».

**Статус:** 🔴 Blocks D-12a commit.

---

### 9.6 B-06: Backward compatibility DB v7 -> v8

**Ризик:** Existing gen-6 entries не мають `mass_n` в centroid. FingerprintCache::query() має обробляти 6D та 7D entries.

**Вирішення (PLUGIN_CONTRACT compliant):**
```cpp
// FingerprintCache::query() -- додати перевірку:
float w_mass = 0.0f;
float q_mass_n = query_vec.mass_n;  // -1.0f якщо N/A

if (q_mass_n >= 0.0f && entry.centroid.hasMassN()) {
    float entry_mass_n = entry.centroid.mass_n;
    w_mass = matcher.full_weights[6];  // 5.0
    // включати в wdist
} else {
    w_mass = 0.0f;  // 6D fallback
}
```

**Статус:** вирішується в FingerprintCache при D-12d.

---

### 9.7 B-07: OTP reload sequence критичність

**Ризик:** Якщо startup sequence (§2.3 кроки 6-10) реалізована некоректно — NAU7802 буде давати drift або некоректні значення без явного error. Це silent failure.

**Вирішення:**
1. Self-test після ініціалізації: виміряти 10 семплів з порожньою платформою, перевірити sigma < 20 counts (при правильному OTP: sigma ~ 5-10 counts)
2. Якщо sigma > 100 counts -> `_lastError = {6, "NAU7802: high noise - OTP reload may have failed"}`
3. Порівняти CHIP_REV (OTP_B1 bits) після reload — повинні бути ненульові

**Статус:** реалізувати в `_startupSequence()` з self-test.

---

### 9.8 B-08: Механічне зміщення платформи при спейсерах

**Ризик:** При кладанні addon spacers (+1mm, +2mm) оператор тисне на монету -> платформа навантажується > FS load cell. Але load cell 100g при натисканні 200-500g може дати пластичну деформацію.

**Вирішення:**
1. Маса вимірюється ТІЛЬКИ на STEP_BASE — до addon spacers
2. Механічний стопер: addon spacers не передають силу на платформу load cell (вони впираються в нерухому раму)
3. Load cell 100g: overload capacity typ 150-200% = 150-200g. Нормальна монета 1-33г -> безпечно. Тиск рукою max 500g при кладанні спейсера -> потрібно routing spacer force в нерухому раму

**Статус:** вирішується механічним дизайном.

---

## 10. Тестова стратегія

### 10.1 Unit тести (native-test, без hardware)

```cpp
// test/test_nau7802/test_nau7802.cpp

TEST_CASE("NAU7802Plugin: shutdown before initialize") {
    NAU7802Plugin plugin;
    plugin.shutdown();  // не повинен крашитись
    CHECK(plugin.getHealthStatus() != HealthStatus::OK);
}

TEST_CASE("NAU7802Plugin: read() before initialize returns invalid") {
    NAU7802Plugin plugin;
    SensorData d = plugin.read();
    CHECK(d.valid == false);
}

TEST_CASE("NAU7802Plugin: acquisition state machine IDLE->SETTLING->SAMPLING->COMPLETE") {
    // MockContext + MockWire (stub I2C)
    // startAcquisition() -> SETTLING
    // N calls to update() -> SAMPLING x N_SAMPLES
    // final update() -> COMPLETE
    // isAcquisitionComplete() == true
    // getLastMassG() > 0
}

TEST_CASE("NAU7802Plugin: mass normalization") {
    // mass_g = 33.3 -> mass_n = 1.0
    // mass_g = 16.65 -> mass_n = 0.5
    // mass_g = 0.0 -> mass_n = 0.0
}

TEST_CASE("NAU7802Plugin: calibrate() returns false for negative known_mass") {
    // calibrate(-1.0f, 10) -> false
}

TEST_CASE("NAU7802Plugin: median computation") {
    // buf = {5.0, 1.0, 9.0, 2.0, 3.0} -> median = 3.0
    // buf = {1.0, 2.0} -> median = 1.5 (N=2 edge case)
}

TEST_CASE("NAU7802Plugin: update() timing < 10ms") {
    // Виміряти час виконання update() в різних AcqState
    // Всі < 10ms (mock без blocking)
}

TEST_CASE("NAU7802Plugin: read() thread safety") {
    // Запустити update() та read() з двох task (FreeRTOS test)
    // Перевірити відсутність race condition (Assert::noCorruption)
}
```

### 10.2 Hardware верифікація (hw-test)

```
HW-NAU-01: I2C presence check
  Command: initialize() -> перевірити відгук на 0x2A
  Pass:    initialize() returns true, log "NAU7802 ready"
  Fail:    log "I2C NACK at 0x2A"

HW-NAU-02: Tare + calibration
  Command: tare(32) з порожньою платформою
           calibrate(20.0f, 32) з гирею 20г
  Pass:    getLastMassG() при 20г = 19.8-20.2г

HW-NAU-03: Stability (sigma)
  Command: 100 measurements без переміщення монети
  Pass:    sigma < 0.1г

HW-NAU-04: Repeatability
  Command: 10 measurements, знімати/класти монету між ними
  Pass:    max - min < 0.2г

HW-NAU-05: Integration з LDC1101
  Command: 3 повних vимірів (STEP_BASE..COMPUTE) з XUSSR10
  Pass:    mass_g = 33.0-33.6г у NDJSON; 7D wdist до xussr10 centroid < 0.3

HW-NAU-06: Degraded mode (NAU відключений)
  Command: від'єднати SDA, виміряти монету
  Pass:    measurement completes з mass_n=-1.0, 6D матчинг працює
```

---

## 11. Архітектурні рішення (ADR)

### ADR-NAU-001: OTP Reload обов'язковий

**Контекст:** NAU7802 datasheet §9.3 описує Factory Calibration Registers (OTP). Без reload після power-on чіп використовує default trim values що призводять до систематичної похибки ~2-5%.

**Рішення:** Реалізувати повну OTP reload sequence (14 кроків §2.3) в `_startupSequence()`. Пропустити неможна.

**Наслідки:** +15ms startup time. Acceptable.

---

### ADR-NAU-002: DRDY pin — не підключати в v1

**Контекст:** NAU7802 має DRDY output pin для interrupt-driven reads. Альтернатива — polling RDY bit через I2C.

**Рішення:** v1 — polling. Причини: (1) GPIO дефіцитний на Cardputer, (2) при 80SPS polling overhead мізерний (75us/sample, ~0.6% CPU), (3) спрощує State Machine (немає ISR).

**Майбутнє:** якщо SPS > 320 або CPU tight -> DRDY interrupt через вільний GPIO.

---

### ADR-NAU-003: Strategy A (no async task) для update()

**Контекст:** PLUGIN_CONTRACT §1.5 описує два варіанти: Strategy A (все в update(), без task) та Strategy B (async FreeRTOS task з mutex).

**Рішення:** Strategy A. Причини: (1) I2C транзакції 175us << 10ms limit, (2) non-blocking state machine вирішує проблему тривалого acquisition, (3) без необхідності брати ctx->wireMutex, (4) менша складність = менше багів.

**Наслідки:** update() повинен викликатись >= 80Hz для максимального throughput 80SPS (кожен call = 1 sample). При стандартних >= 10Hz: один sample per 100ms -> N=20 за 2000ms. Цього достатньо для виміру всередині LDC1101 capture window.

---

### ADR-NAU-004: mass_n = -1.0f як sentinel для "N/A"

**Контекст:** Не всі пристрої матимуть NAU7802. DB повинна підтримувати 6D та 7D entries.

**Рішення:** mass_n = -1.0f означає "маса недоступна". FingerprintCache::query() при mass_n=-1.0f встановлює w_mass=0.0 (ігнорує вагову вісь). Валідний mass_n >= 0.0f.

**Наслідки:** Мінімальна зміна в FingerprintCache. Backward compatible з gen-6 DB (entries без mass_n розглядаються як -1.0f).

---

### ADR-NAU-005: MASS_REF_G = 33.3 (XUSSR10)

**Контекст:** Нормалізація mass_n потребує reference. Вибір reference впливає на значення weight W_mass в matcher.

**Рішення:** MASS_REF_G = 33.3g (XUSSR10 — найважча монета в DB). mass_n = mass_g / 33.3 -> range [0.058, 1.0] для поточних класів.

**Наслідки:** При додаванні більш важких монет (> 33.3g) -> mass_n > 1.0 (валідно, але виходить за [0,1]). Оновити MASS_REF_G якщо з'являється клас > 35g.

---

### ADR-NAU-006: SETTLE_MS = 500ms (перша C-13 сесія)

**Контекст:** Механічне затухання платформи load cell після кладання монети. Природна частота ~20-30 Hz для 100g платформи → τ ≈ 50-100ms. Але будь-яке тертя або underdamped mount збільшує settling до 300-600ms.

**Рішення:** SETTLE_MS = 500ms для першої C-13 сесії (conservative). Налаштовується через `nau7802.json` → `nau7802.settle_ms`. Після R-1 experiment і верифікації repeatability — можна знизити до 300ms якщо sigma < 0.05g.

**Порівняння:**
```
SETTLE_MS    Acq total    Timing risk    Рекомендація
200 ms        450 ms       HIGH           Не використовувати до верифікації
300 ms        550 ms       MEDIUM         OK після R-1 verify
500 ms        750 ms       LOW            DEFAULT для C-13 (перша сесія)
```

**Наслідки:** +300ms до STEP_BASE (21.4% overhead), від якого NAU7802 залишається ≫ швидшим ніж LDC1101 capture window (2000ms). Нульовий вплив на загальний час виміру.

---

### ADR-NAU-008: Sequential workflow — STEP_WEIGHT окремо від LDC1101

**Контекст:** NAU7802 (load cell) та LDC1101 (котушка) потребують різних фізичних позицій монети. Монета не може одночасно лежати на вагах і бути відцентрованою над котушкою без спеціального механічного рішення, яке суттєво ускладнює конструкцію і погіршує точність обох сенсорів.

**Рішення:** Три-кроковий sequential workflow: STEP_WEIGHT → STEP_QUICK → (опц.) STEP_FULL. Відповідно:
1. Ваги виконуються першими — маса як pre-screen (швидко виключає унікальні маси)
2. STEP_QUICK = STEP_BASE на котушці (без спейсерів) + matchQuick(7D включаючи mass_n)
3. STEP_FULL = додати S1+S3 тільки якщо quick confidence < threshold

**Наслідки для API:** `startAcquisition()` викликається в обробнику STEP_WEIGHT (окремий стан у state machine), а не в STEP_BASE. `getLastMassG()` зберігає результат між кроками.

**Наслідки для matchQuick():** Wave 10 D-12e розширює `matchQuick()` з 4 до 5 параметрів: `+float mass_n`. Це розблоковує A-3 Quick Screen Phase 2 (відкладено з Wave 9 саме через відсутність mass_n). `quick_weights` в matcher.json v6 включають `5.0` як останній ваговий коефіцієнт для mass_n.

**Відхилені альтернативи:**
- _Parallel acquisition (ваги під котушкою)_: Потребує спеціальної механіки, збільшує відстань від монети до котушки, підвищує noise від load cell platform resonance → відхилено для прототипного етапу.
- _Weight-only pre-screen_: Маса ~12g у 3 різних класів (XKENNED/XCUZN/XAG800) → без котушки неможливо розрізнити → недостатньо standalone.

---

### ADR-NAU-007: Startup sequence — CTRL1/CTRL2 після OTP reload

**Контекст:** NAU7802 datasheet (Rev 2.6) вимагає OTP reload ПЕРЕД конфігурацією CTRL registers. OTP reload відновлює factory trim values для PGA і LDO. Якщо GAINS/VLDO записані до OTP reload — OTP перезапише trim bits, і ефективний PGA gain може стати 1x замість 128x (silent failure).

**Рішення:** Startup sequence (§2.3) чітко визначена: OTP reload = кроки 6-10, CTRL1/CTRL2 = кроки 11-12. D-12a WIP має неправильний порядок — виправити перед commit.

**Правильна послідовність для `_startupSequence()`:**
```
1-4: Reset + power-up digital + wait PUR + power-up analog+LDO
5:   OTP_B1 read
6-7: OTP reload (write OTP_B1|0x30 до PGA_CAP reg)
8:   PGA_PWR config
9:   CTRL1 (GAINS=128x, VLDO=3.0V)   ← тільки ПІСЛЯ OTP
10:  CTRL2 (80SPS, CH1)               ← тільки ПІСЛЯ OTP
11:  Enable conversions (CS bit)
12:  Discard first N samples (filter settling)
```

**Наслідки:** Жодних змін у публічному API. +0ms overhead (просто правильний порядок рядків).

---

## 12. Implementation Checklist

### D-12a: Базовий драйвер (WIP існує — виправити баги, потім commit)
- [x] Створити `lib/NAU7802Plugin/src/NAU7802Plugin.h/.cpp` ← WIP готовий
- [ ] **FIX: CTRL1_VAL = 0xBC** (замість 0x27): `(0x05 << 5) | (0x07 << 2)` — ADR-NAU-007, B-05
- [ ] **FIX: CTRL2_VAL = 0x60** (замість 0x30): `(0x03 << 5)` — ADR-NAU-007, B-05
- [ ] **FIX: startup sequence** — перенести CTRL1/CTRL2 ПІСЛЯ OTP reload кроків — ADR-NAU-007
- [ ] **FIX: tare() SPS mask** — `~0xE0` замість `~0x70` — B-05
- [ ] **FIX: SETTLE_MS = 500** (замість 200) — ADR-NAU-006
- [x] Реалізувати `_startupSequence()` з OTP reload (14 кроків ADR-NAU-001) ← WIP є
- [x] Реалізувати `_writeReg()`, `_readReg()`, `_readAdc24()` ← WIP є
- [x] Реалізувати `_isReady()` (polling RDY bit) ← WIP є
- [x] Реалізувати `_computeMedian()` для float buf ← WIP є
- [x] canInitialize() / initialize() / shutdown() per PLUGIN_CONTRACT ← WIP є
- [ ] Self-test в initialize(): sigma < 20 counts при порожній платформі — після HW

### D-12b: NVS калібрування
- [ ] `saveCalibration()` -> NVS namespace "nau7802" (zero, scale, cal_ok, cal_ts, cal_mass_g)
- [ ] `loadCalibration()` -> відновлення _zeroOffset, _scaleFactor
- [ ] Перевірка "cal_ok" при initialize()

### D-12c: Calibration UX
- [ ] `tare(N)` -- blocking, для виклику з UI
- [ ] `calibrate(known_g, N)` -- blocking, для виклику з UI
- [ ] Клавіша 'K' в main.cpp -> запуск calibration wizard
- [ ] Екрани Step 1/2/3 на Cardputer display

### D-12d: Integration
- [ ] `startAcquisition()` / `isAcquisitionComplete()` / `getLastMassG()`
- [ ] Non-blocking state machine в `_updateAcqStateMachine()`
- [ ] Виклик `gNAU->update()` в main loop
- [ ] Виклик `gNAU->startAcquisition()` в STEP_BASE entry
- [ ] Зчитування `mass_g` в COMPUTE state
- [ ] mass_n = mass_g / MASS_REF_G в production_vector (7D)

### D-12e: NDJSON + DB schema
- [ ] Додати `mass_g`, `mass_n` в NDJSON top-level та production_vector
- [ ] Оновити `FingerprintCache::query()` для 7D (ADR-NAU-004)
- [ ] DB schema **version 7 -> 8** (gen-7 вже зайнятий D-11d/`df626f2`)
- [ ] Protocol ID "p3_..." -> "p4_MIKROE3240_b06_012mm_mass"
- [ ] matcher.json v5 -> v6: `full_weights=[1.5,0.0,1.0,3.0,2.5,0.4,5.0]`, `keys=[...,'mass_n']`

### D-12f: Tests
- [ ] Unit тести (§10.1): shutdown, read, state machine, median, thread safety
- [ ] HW тести (§10.2): HW-NAU-01..HW-NAU-06
- [ ] Всі існуючі 137/137 тести залишаються PASS

### C-13: Вагова сесія
- [ ] Перевірити реальні маси всіх 13 класів з аптечними гирями
- [ ] Зібрати 3+ вимірів з mass_g на клас
- [ ] Верифікувати sigma_mass < 0.2г для кожного класу
- [ ] build_gen7_db.py: додати mass_n до centroid, масовий довідник

---

*Документ: `docs/architecture/NAU7802_ARCHITECTURE.md`*  
*Версія: 1.1.0 | Дата: 2026-04-08 (оновлено: 2026-04-08 — D-12a peer review: 5 bugs found, §2.4 derived constants + ADR-NAU-006/007 added)*  
*Базується на: NAU7802 Datasheet Rev 2.6, PLUGIN_CONTRACT v1.0.0, MEMORY_MAP v1.0.0, Wave 10 Architecture Plan*  
*Наступне оновлення: після D-12a hw-verify (C-13 session)*
