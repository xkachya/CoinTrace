# NAU7802 Weight Sensor — Architecture Specification

**Версія:** 1.6.0  
**Дата:** 2026-04-10  
**Статус:** 🔄 Active — D-12c ✅ done (calibration wizard hw-verified); D-12d next  
**Hardware:** Nuvoton NAU7802 24-bit ADC + 100g load cell  
**Chip revision:** NAU7802 Rev 2.6 (datasheet EN)  
**Мотивація:** Wave 10 — 7D production vector (додається `mass_n`) для вирішення 5 критичних пар < 1.0σ у gen-6 DB  
**Cross-ref:**
- `PLUGIN_CONTRACT.md v1.0.0` — обов'язковий контракт
- `PLUGIN_INTERFACES_EXTENDED.md v1.2.0` — `ISensorPlugin`, `SensorType::WEIGHT`
- `LDC1101_ARCHITECTURE.md v1.6.0` — паралельний SPI-сенсор (окрема SPI шина, адреса 0x00)
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
0x00  PU_CTRL     8     [7] AVDDS | [6] OSCS | [5] CR | [4] CS | [3] PUR | [2] PUA | [1] PUD | [0] RR
                        RR=1  -> register reset
                        PUD=1 -> power up digital
                        PUA=1 -> power up analog
                        PUR   -> power-up ready (read-only, set by chip ~200us after PUD)
                        CS=1  -> start ADC conversions
                        CR=1  -> conversion result ready (read-only)
                        AVDDS=1 -> use internal LDO for load cell excitation (AVDD source)
                        ⚠️  Попередні версії доку мали хибне розміщення бітів (стара таблиця говорила AVDDS=bit3) — юзерська версія цієї таблиці виправлена
0x01  CTRL1       8     [7] CRP (CRDY polarity) | [6] DRDY_SEL | [5:3] VLDO[2:0] | [2:0] GAINS[2:0]
                        GAINS: 000=1x, 001=2x, 010=4x, 011=8x, 100=16x, 101=32x, 110=64x, 111=128x
                        VLDO:  000=4.5V LDO, 001=4.2V, 010=3.9V, 011=3.6V, 100=3.3V, 101=3.0V, 110=2.7V, 111=2.4V
                               ⚠️  B-08: стара версія доку мала [7:5] VLDO | [4:2] GAINS (CTRL1_VAL=0xBC) — неправильно!
                               Правильно: VLDO при [5:3], GAINS при [2:0] (CTRL1_VAL=0x2F пер Adafruit/SparkFun)
                               ЯКЩО AVDDS=0 (ext AVDD) -> VLDO ignored
0x02  CTRL2       8     [7] CHS | [6:4] CRS[2:0] | [3] CAL_ERR (r/o) | [2] CALS (cal start) | [1:0] CALMOD
                        CRS: 000=10SPS, 001=20SPS, 010=40SPS, 011=80SPS, 111=320SPS
                        CHS: bit 7 (0=CH1, 1=CH2)
                        CALMOD: 00=internal offset cal (nаш використовуємо), 10=system offset, 11=system gain
                        ⚠️  B-08: стара версія доку мала [7:5] CRS (CTRL2_VAL=0x60) — неправильно!
                        Правильно: CRS при [6:4] → CTRL2_VAL = (CRS<<4), CHS при bit[7] (Adafruit/SparkFun)
0x11  I2C_CTRL    8     [6] BGPCP | [4] TS | [3] BOPGA | [2] SI | [1] WPD | [0] SPE
                        SPE=1 -> strong pull-up on SDA (300mA max!) -- НЕ використовувати
0x12  ADCO_B2     8     ADC output MSB (bits 23:16)
0x13  ADCO_B1     8     ADC output byte 2 (bits 15:8)
0x14  ADCO_B0     8     ADC output LSB (bits 7:0)
0x15  ADC_CTRL    8     ADC control register — bits[5:4] = CLK_CHP (chopper clock frequency)
                        CLK_CHP=00 (reset default): max chopper freq → noise injection into ADC output
                        CLK_CHP=11 (required): optimal/disabled per §9.1 startup sequence
                        ⚠️  РАНІШЕ помилково названий "OTP_B1" — це НЕ OTP. Обидві reference libs
                        (Adafruit + SparkFun) пишуть 0x30 сюди при ініціалізації: "Turn off CLK_CHP"
                        ⚠️  audit 2026-04-10 X-02: код читав 0x15 як "OTP_B1" і ніколи не писав —
                        залишав CLK_CHP=00 (max noise). Виправлено: step 5 startup sequence.
0x1B  PGA         8     PGA config register
                        bit7: RD_OTP_SEL — OTP read select (set=read OTP mode, clear=normal)
                        bit6: LDOMODE — 0=low-ESR caps (low-noise), 1=standard caps
                        bit5: OUT_EN — PGA output enable
                        bit4: BYPASS_EN — 1 = PGA повністю bypassed, effective gain=1x незалежно від CTRL1!
                        bit3: INV — invert PGA input
                        bit0: CHP_DIS — disable PGA chopper clock
                        ⚠️  Reference libs (Adafruit, SparkFun): тільки clearBit(6) при ініціалізації.
                        ⚠️  audit 2026-04-10 X-01: код писав 0x30=(OUT_EN|BYPASS_EN) → BYPASS_EN=1 →
                        effective gain=1x. Підтверджено апаратно: 151 counts/g vs 21474 очікувано.
                        Виправлено: step 6 startup sequence тільки ~0x40 (LDOMODE clear).
0x1C  PGA_PWR     8     PGA power control
                        bit7: PGA_CAP_EN — активує 330pF decoupling capacitor (§9.14)
                        bits[6:4]: MSTR_BIAS_CURR — master bias current (залишати 000 = reset default)
                        bits[3:2]: ADC_CURR; bits[1:0]: PGA_CURR
                        ⚠️  audit 2026-04-10 X-03: PGA_PWR_VAL=0x30 помилково встановлював
                        MSTR_BIAS_CURR=011 замість PGA_CAP_EN. SparkFun: setBit(bit7).
                        Виправлено: PGA_PWR_VAL=0x80 (тільки bit7=PGA_CAP_EN).
0x1F  REVISION_ID 8    Chip revision (not I2C_CTRL -- попередня версія doc мала помилку)
```

### 2.3 Startup sequence (виправлена — audit 2026-04-10)

> **⚠️ BREAKING CHANGE від v1.4.0:** Кроки 5-8 повністю переписані на основі аудиту X-01/X-02/X-03 та cross-reference з Adafruit + SparkFun reference libs. Стара "OTP reload" sequence (кроки 6-8 в v1.4.0) встановлювала `BYPASS_EN=1` що обходило PGA повністю.

NAU7802 потребує специфічної послідовності ініціалізації після Power-On або Reset:

```
1.  Write 0x01 to PU_CTRL (0x00)   -- Reset (RR=1)
2.  Delay 10 ms                    -- reset settling (min 10ms; 1ms недостатньо — B-03 fix)
3.  Write PUD (0x02) to PU_CTRL    -- Power up digital, implicitly clears RR
4.  Wait for PUR bit (PU_CTRL[3]) = 1, timeout 200 ms
5.  Write AVDDS|PUA|PUD (0x86)     -- Power up analog + enable internal LDO

--- Analog init (кроки 6-8 — замінили стару "OTP reload" sequence) ---

6.  Read REG_ADC_CTRL (0x15); write back with bits[5:4]=11 (|=0x30)
    Мета: вимкнути CLK_CHP (ADC chopper clock) — datasheet §9.1 "power on sequencing"
    Назва "OTP_B1" для 0x15 у v1.4.0 — ПОМИЛКА. Це ADC Control Register.
    Adafruit comment: "Disable ADC chopper clock"
    SparkFun comment: "Turn off CLK_CHP from 9.1 power on sequencing"

7.  Read REG_PGA (0x1B); write back with bit6 cleared (&= ~0x40)
    Мета: LDOMODE=0 (low-ESR caps = low-noise mode)
    ТІЛЬКИ bit6 очищається. Інші біти (особливо bit4=BYPASS_EN) НЕ торкати!
    BYPASS_EN=1 обходить PGA: effective gain=1x замість 128x (silent failure).
    Adafruit + SparkFun: тільки clearBit(6, 0x1B) — нічого більше.

8.  Write 0x80 to REG_PGA_PWR (0x1C)
    Мета: PGA_CAP_EN=1 (bit7) — активувати 330pF decoupling cap (datasheet §9.14)
    ТІЛЬКИ bit7. Не писати 0x30 (встановлює MSTR_BIAS_CURR=011 — нестандартне).
    SparkFun: setBit(NAU7802_PGA_PWR_PGA_CAP_EN, NAU7802_PGA_PWR) = setBit(7, 0x1C)

----------------------------------------------------------------------

9.  Write CTRL1 (0x2F): VLDO=3.0V + GAINS=128x -- ПІСЛЯ analog init (ADR-NAU-007)
10. Write CTRL2 (0x30): CRS=80SPS + CH1          -- ПІСЛЯ analog init (ADR-NAU-007)
11. Enable conversions: read PU_CTRL, write PU_CTRL | CS (0x10)
12. Delay 15 ms, read and discard one sample (filter settling)
13. Trigger internal ADC zero-scale calibration: write CTRL2 | CALS (0x04)
    Wait CALS bit cleared (up to 400 ms), verify CAL_ERR == 0
14. Restore CTRL2 = 0x30 (80 SPS, no cal trigger)
    At this point ~400-500 ms have elapsed since power-up -- ADC settled
```

**Верифікація правильності ініціалізації (boot log):**
```
INFO  NAU7802 | Startup OK: PU_CTRL=0x9E REG_PGA=0x00 LDO=3.0V PGA=128 80SPS
DEBUG NAU7802 | Regs OK: CTRL1=0x2F CTRL2=0x30

REG_PGA=0x00 → BYPASS_EN=0, LDOMODE=0 → PGA active 128x ✅
Якщо з'явиться ERROR "BYPASS_EN=1" → перевірити step 6 (не писати 0x30 у REG_PGA!)
```

> **ADR-NAU-001 (оновлений):** "OTP Auto-load" — NAU7802 автоматично завантажує OTP при power-on. Явний OTP reload (стара логіка v1.4.0) не потрібний. Натомість потрібна analog init sequence (кроки 6-8 вище). Детальніше: ADR-NAU-001 у §11.

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

**Derived register constant values (B-08: виправлені бітові позиції пер Adafruit/SparkFun):**
```
CTRL1 = [7] CRP | [6] DRDY_SEL | [5:3] VLDO | [2:0] GAINS
  VLDO=3.0V  : code=101b=5, bits[5:3] → 5 << 3 = 0x28
  GAINS=128x : code=111b=7, bits[2:0] → 7 << 0 = 0x07
  CTRL1_VAL  = 0x28 | 0x07 = 0x2F         ← ПРАВИЛЬНО (B-08 fix, старе 0xBC = WRONG)

CTRL2 = [7] CHS | [6:4] CRS | [3] CAL_ERR | [2] CALS | [1:0] CALMOD
  CRS=80SPS  : code=011b=3, bits[6:4] → 3 << 4 = 0x30
  CHS=CH1    : bit 7 = 0
  CTRL2_VAL  = 0x30                        ← ПРАВИЛЬНО (B-08 fix, старе 0x60 = WRONG)

CRS bits mask (для зміни SPS у tare()):
  CRS_CLEAR_MASK = ~0x70   (очищення bits[6:4])   ← ПРАВИЛЬНО (B-08 fix, старе ~0xE0 = WRONG)
```

---

## 3. Апаратна інтеграція

### 3.1 Підключення до Cardputer-Adv

```
Adafruit NAU7802 Breakout #4538 -> Cardputer-Adv (ESP32-S3)
------------------------------------------------------------
Модуль Pin     ESP32-S3 Pin    Notes
-----------    ------------    ----------------------------------------------
VIN            3.3V            Digital power (на чіпі = VDD)
GND            GND             Спільна земля
SDA            GPIO8           I2C data  -- 10kOhm pullup вже є на модулі
SCL            GPIO9           I2C clock -- 10kOhm pullup вже є на модулі
DRDY           NC (optional)   ADR-NAU-002: polling RDY bit достатньо
AV             NC              AVDD output від внутр. LDO -- не підключати!

AVDD: внутрішній LDO активується через AVDDS=1 у _startupSequence() step 5
  (PU_CTRL: AVDDS|PUA|PUD). Пін "AV" -- це ВИХІД (2.4-4.0V), не вхід.
  Не підключати "AV" до 3.3V -- пошкодить LDO!

STEMMA QT (альтернатива без паяння):
  4-pin JST SH порядок: GND / VIN / SDA / SCL

------------------------------------------------------------
Load cell (4-wire Wheatstone bridge) -> NAU7802 terminal block
------------------------------------------------------------
Load cell wire   Термінал модуля   Функція (чіп-пін)
--------------   ---------------   -------------------------------------
RED              E+                Excitation+ (AVDD від внутр. LDO)
BLACK            E-                Excitation- (AGND)
GREEN            A+                Signal+ non-inverting (VIN1P / CH1+)
WHITE            A-                Signal- inverting   (VIN1N / CH1-)

УВАГА: кольори дротів залежать від виробника load cell:
  Adafruit/більшість: RED=E+, BLACK=E-, GREEN=A+, WHITE=A-
  Деякі виробники:    RED=E+, BLACK=E-, WHITE=A+, GREEN=A-
  Якщо маса від'ємна -- swap A+ <-> A- (або інвертуйте offset у коді).

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

### 3.4 Кабельна інтеграція та EMI — практичні рекомендації

> **Джерело:** C-13 hardware session 2026-04-10. sigma=96 counts (4.5 мг) при правильному gain=128x підтверджує присутність EMI від PC switching PSU. Усунення не блокує C-13 (4.5 мг << ±1.5% допуску), але критично для продакшн корпусу.

**Чому load cell wires вразливі до EMI:**

PGA=128x підсилює **диференційний** сигнал між A+ і A−. Але тонкі монтажні дроти утворюють невелику антену. При несиметричній наводці (різна довжина A+ і A−) виникає диференційний EMI-сигнал, який PGA підсилює у 128 разів разом з корисним сигналом.

```
Тип наводки           CMRR        Підсилення PGA    Результат
-----------------------------------------------------------
Синфазна (EM поле)    ~60 dB      128x              -60dB + 128x → ~-18 dB залишку
Диференційна (асиметр.) 0 dB      128x              128x підсилення → домінує
```

**Симптоми поганого кабелю:**
- sigma STABLE після power-on settle (NOISY → STABLE), але sigma > 50 counts → EMI
- sigma змінюється при включенні/виключенні PC або WiFi → підтвердження EMI
- DC offset `zero_offset` відтворюваний між бутами але sigma висока → не механіка

**Технічні рішення (по пріоритету):**

```
1. TWISTED PAIR для A+ / A− (найважливіше)
   - Скрутити WHITE (A−) і GREEN (A+) разом кожні ~1.5 см
   - Twisted pair відміняє диференційну наводку від зовнішнього поля
   - Покращення sigma: типово 10-30x
   - Бюджет: 0 грн (перекрутити наявні дроти)

2. ЗАГАЛЬНИЙ ЕКРАН (shielded cable)
   - 4-жильний екранований кабель (напр., RVVP 4×0.3mm або аудіо-кабель)
   - Екран підключити до AGND Adafruit #4538 (НЕ до ESP32 GND!)
   - Підключати екран тільки з ОДНОГО боку (з боку модуля) — щоб уникнути ground loop
   - Покращення sigma: типово 5-20x додатково до twisted pair

3. ПРОКЛАДАННЯ кабелю
   - Тримати load cell cable ЯКОМОГА далі від:
     * ESP32-S3 WiFi antenna (одна з коротких сторін M5Cardputer)
     * USB кабель живлення (switching noise)
     * Трансформаторів і котушок індуктивності БЖ
   - Мінімальна відстань від ESP32: ≥ 5 см
   - Ніколи не прокладати паралельно до I2C/SPI кабелів на довжині > 5 см

4. ФЕРИТОВІ БУСИНИ
   - 1-2 феритових бусини (snap-on або петлева) на вихід кабелю з корпусу
   - Типове погашення: -10...-20 dB на частотах > 1 MHz
   - Бюджет: ~20-50 грн

5. КОНДЕНСАТОРИ НА ПЛАТІ (якщо присутні посадкові місця)
   - 100nF X7R між E+ і E− (паралельно мосту) — фільтр живлення мосту
   - 100nF X7R між A+ і AGND, між A− і AGND — синфазний фільтр
   - Adafruit #4538 не має цих посадкових місць; при виготовленні власної плати — закласти
```

**Очікуваний результат після twisted pair + shielded cable:**

```
Поточний стан (bare wires, PC desktop):  sigma ≈ 96 counts ≈ 4.5 мг
Після twisted pair:                      sigma ≈ 5-20 counts ≈ 0.2-0.9 мг
Після шилдованого кабелю:               sigma ≈ 2-8 counts ≈ 0.1-0.4 мг

Критерій B-07 (σ < 20): досягається twisted pair
Практична точність ±1.5% на 1г монеті (±15 мг): забезпечується при σ < 5 мг ✅
```

**Критичне для корпусного дизайну (D-13/механічна частина):**
```
[ ] Load cell cable ≤ 15 см (коротший = менша антена)
[ ] Twisted pair обов'язково (закласти у вимоги до кабелю в BOM)
[ ] Вивід кабелю від платформи через заземлений металевий штуцер (EM shield)
[ ] Load cell відстань від LDC1101 котушки ≥ 5 см (металевий корпус load cell збурює L)
[ ] Гумові ніжки корпусу або поролон-ізолятор — mechanical vibration decoupling
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

Total acquisition time: N * 12.5ms = 250 ms (N=20)  ← **тільки при update() ≥ 80 Hz**
Number of update() calls: ~25 (at 10Hz: 2000ms total) to ~250 (at 100Hz: 250ms total)
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
    // Поточний результат (mass_g / mass_n) -- valid тільки якщо isAcquisitionComplete()
    float        getLastMassG() const;
    float        getLastMassN() const;  // mass_g / MASS_REF_G; -1.0f якщо !isAcquisitionComplete()

    // === Constants (public для доступу з main.cpp) ===
    static constexpr float MASS_REF_G = 33.3f;  // XUSSR10, нормалізатор (найважча монета в DB)

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
    static constexpr uint16_t SETTLE_MS    = 500;    // ADR-NAU-006: 500ms (not 200)
    static constexpr uint8_t  SPS_80       = 0x03;  // CTRL2 CRS bits
    static constexpr uint16_t SPS_DEFAULT  = 80;    // Hz, used in tare()/calibrate() delays
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
    static constexpr uint16_t INIT_TIMEOUT_MS = 200;

    // --- Private methods ---
    bool     _writeReg(uint8_t reg, uint8_t val);
    uint8_t  _readReg(uint8_t reg);
    bool     _readAdc24(int32_t& out);  // 3 bytes ADCO, signed 24-bit two's complement
                                        // MUST sign-extend bit23→bit31:
                                        //   if (raw & 0x800000) raw |= 0xFF000000;
                                        // Without this, negative offsets become ~16M counts
    bool     _isReady();                // читає RDY bit
    bool     _startupSequence();        // analog init (CLK_CHP + LDOMODE + PGA_CAP_EN) + CTRL config
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

`data/plugins/nau7802.json` — **reference-only документація** значень за замовчуванням. Реальне джерело істини — `static constexpr` поля в `NAU7802Plugin.h`.

**Зчитується кодом:** лише `nau7802.i2c_addr` (дозволяє змінити I²C адресу без перекомпіляції). Ще 9 полів пока не зчитуються і не повинні змінюватись у JSON без відповідної зміни `static constexpr` (N-04).

```json
{
  "nau7802.i2c_addr":       42,
  "nau7802.sample_rate":    80,
  "nau7802.pga_gain":       128,
  "nau7802.n_samples":      20,
  "nau7802.settle_ms":      500,
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

> **⚠️ ADR-NAU-008 — Sequential workflow.** Стара ідея «паралельна NAU7802 acquisition всередині STEP_BASE» фізично неможлива: монета не може одночасно бути на вагах і на котушці. Правильна архітектура — послідовні кроки (детально §4.4). Реалізується в **D-12e**.

```
Sequential workflow (p4 protocol):

IDLE
  |
  | coin detected + ENTER
  [NAU7802 present && calibrated?]
  |
  ├─ YES:
  |   v
  | STEP_WEIGHT:                                          ← NEW (D-12e)
  |   ├── UI: "Покласти монету на ваги → ENTER"
  |   ├── gNAU->startAcquisition()
  |   │     settle(500ms) → 20 samples → COMPLETE
  |   ├── [user places coin on scale, presses ENTER]
  |   └── sMassG = gNAU->getLastMassG()
  |   |
  |   v
  | STEP_QUICK  (= STEP_BASE capture, без спейсерів):   ← RENAMED (D-12e)
  |   ├── UI: "Перекласти монету на котушку → ENTER"
  |   ├── LDC1101 settle + capture  -- unchanged
  |   ├── matchQuick(rpLive, rpBase, lLive, lBase, mass_n)  ← 7D
  |   ├── confidence ≥ 0.75  →  PROPOSE RESULT
  |   │     [C]onfirm / [F]ull measure / [N]ext coin
  |   └── confidence < 0.75  →  auto → STEP_1
  |
  └─ NO (6D fallback):
      → skip STEP_WEIGHT, sMassG = -1.0f, mass_n = -1.0f
      → STEP_QUICK directly (Wave 9 behavior, §6.3)
  |
  v
STEP_1 (1.6mm):  -- LDC1101 тільки
  ...
STEP_3 (2.6mm):  -- LDC1101 тільки
  ...
STEP_DRIFT:      -- LDC1101 тільки
  ...
COMPUTE:
  ├── production_vector (6D LDC1101) -- unchanged
  ├── mass_n = sMassG / NAU7802Plugin::MASS_REF_G          ← НОВЕ (D-12d)
  ├── matchFull(m, df_n, df1_n, mass_n)                    ← 7D   (D-12d)
  ├── NDJSON v8 з mass_n field                             ← НОВЕ (D-12d)
  └── → IDLE
```

**Ключова деталь:** STEP_QUICK і STEP_BASE — **один і той самий LDC1101 capture** (no spacers, base distance). Якщо user обирає [F]ull після Quick Screen — STEP_QUICK дані вже є, додаються тільки STEP_1 + STEP_3. Дублювання capture немає.

### 6.2 Інтеграція в main.cpp

```cpp
// --- Існуючий глобальний стан ---
// extern LDC1101Plugin* gLDC;
// Нове:
NAU7802Plugin* gNAU = nullptr;  // глобальний pointer, аналогічно gLDC
static float   sMassG = -1.0f;  // поточна маса; -1.0f = sentinel (не виміряно)

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
    gNAU = nullptr;  // 6D fallback mode -- система продовжує без ваги
}

// --- main loop update() ---
if (gNAU) gNAU->update();  // завжди викликаємо -- non-blocking

// --- Measurement workflow state machine (D-12e) ---

// Entry point: якщо NAU7802 calibrated → STEP_WEIGHT; інакше → STEP_QUICK (6D fallback)
case MeasState::IDLE:
    sMassG = -1.0f;  // скидаємо масу при кожному новому вимірі
    if (gNAU && gNAU->isCalibrated()) {
        nextState = MeasState::STEP_WEIGHT;
    } else {
        nextState = MeasState::STEP_QUICK;  // 6D fallback
    }
    break;

// --- STEP_WEIGHT: NAU7802 blocking acquisition ---
case MeasState::STEP_WEIGHT:
    // UI: "Покласти монету на ваги → ENTER"
    gNAU->startAcquisition();        // запускаємо acquisition; update() збирає семпли
    // polling loop або event очікує ENTER:
    //   if (gNAU->isAcquisitionComplete())  sMassG = gNAU->getLastMassG();
    //   else  sMassG = -1.0f;               // timeout
    nextState = MeasState::STEP_QUICK;
    break;

// --- STEP_QUICK (= STEP_BASE capture, без спейсерів): LDC1101 + matchQuick 7D ---
case MeasState::STEP_QUICK:
    // UI: "Перекласти монету на котушку → ENTER"
    gLDC->settle(...);               // LDC1101 settle unchanged
    // ... LDC1101 capture (rp_base, fSensor_base) ...
    {
        float mass_n = (sMassG > 0.0f) ? (sMassG / NAU7802Plugin::MASS_REF_G) : -1.0f;
        auto qr = gMatcher.matchQuick(rpLive, rpBase, lLive, lBase, mass_n);
        if (qr.confidence >= QUICK_CONF_THRESHOLD) {
            // Quick Screen: propose result; [C]onfirm / [F]ull / [N]ext
        } else {
            nextState = MeasState::STEP_1;   // auto-proceed to full measurement
        }
    }
    break;

// --- COMPUTE state ---
case MeasState::COMPUTE:
    // ... існуючий обчислення production_vector (6D components) ...

    // НОВЕ (D-12d): додати mass_n до 7D vector
    float mass_n = (sMassG > 0.0f) ? (sMassG / NAU7802Plugin::MASS_REF_G) : -1.0f;
    auto result = gMatcher.matchFull(sMeas.m, meas_df_n, meas_df1_n, mass_n);

    // ... збереження NDJSON v8 з mass_n ...
    break;
```

### 6.3 Degraded mode (NAU7802 недоступний)

```
Якщо gNAU == nullptr або !gNAU->isCalibrated():
  - mass_n = -1.0f у production_vector
  - FingerprintCache::query() -- якщо mass_n == -1.0f -> w_mass = 0.0f (ігнорує вагу)
  - Система продовжує як 6D матчинг (поведінка gen-6)
  - UI: іконка "W?" на Quick Screen

Це важливо для backward compatibility: старі пристрої без NAU7802
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
        delay(1000 / SPS_DEFAULT + 1);  // 13ms @ 80SPS
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
        delay(1000 / SPS_DEFAULT + 1);  // 13ms @ 80SPS
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

ЕКРАН 2: "Place 20g weight"     <- рекомендовано 20g (хороший SNR, середній діапазон)
         "on platform"          <- наявні: 1g, 2g, 5g, 10g, 20g, 50g
         "Press OK"
  -> calibrate(20.0f, 32)
  -> "Cal OK: XX.Xg" (verify reading)

ЕКРАН 3 (verify): "Verification"
         "Reading: XX.Xg"
         "Expected: 20.0g"
         "Error: X.Xg  OK/RETRY"
  -> якщо |reading - 20.0| < 0.5g -> ACCEPT   <- 2.5% tolerance при 20g
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
  "quick_weights": [2.0, 0.0, 0.0, 4.0, 2.5, 0.3, 5.0],
  "sigma": 0.35,
  "keys": ["dRp1_n", "k1", "k2", "df_n", "dL1_n", "df1_n", "mass_n"],
  "notes": {
    "mass_n": "Normalized mass: mass_g / 33.3 (XUSSR10 ref). w=5.0 resolves all 5 critical pairs from gen-6.",
    "mass_n_absent": "If mass_n = -1.0 (no NAU7802), w_mass is set to 0.0 (6D fallback mode)."
  }
}
```

---

### 8.4 C++ Interface Specification — D-12d (7D changes)

Explicit interface spec for the multi-file coordinated change in D-12d. All 4 constructs change together; implementor must update them consistently.

#### FingerprintCache::CacheEntry — новий `mass_n` field

```cpp
// lib/StorageManager/src/FingerprintCache.h
struct CacheEntry {
    char     id[32];
    char     metal_code[8];
    char     coin_name[48];
    char     protocol_id[24];
    float    dRp1_n;
    float    k1;
    float    k2;
    float    df_n;
    float    dL1_n;
    float    df1_n;
    float    mass_n;       // ← NEW (D-12d): centroid mass_n.
                           //   SENTINEL = -1.0f (gen-7 compat: parsed as missing → -1.0f)
    float    radius_95pct;
    uint16_t records_count;
};
// RAM delta: +4 bytes/entry. MAX_ENTRIES=64 → max +256 B. [OK, budget §4.1]

// Backward compat (gen-7 DB, no mass_n field):
//   if (!json["mass_n"].isNull()) entry.mass_n = json["mass_n"]; else entry.mass_n = -1.0f;
```

#### MetalMatcher::Config — [6] → [7]

```cpp
// lib/StorageManager/src/MetalMatcher.h
struct Config {
    float full_weights[7]    = {1.5f, 0.0f, 1.0f, 3.0f, 2.5f, 0.40f, 5.0f};  // was [6]
    float quick_weights[7]   = {2.0f, 0.0f, 0.0f, 4.0f, 2.5f, 0.30f, 5.0f};  // was [6]
    float sigma              = 0.35f;
    float min_confidence     = 0.3f;
    float ferro_thresh_dL1_n = 99.0f;
};
// Element [6] = mass_n weight (W_mass).
// Backward compat (matcher.json v5, 6 elements): w[6] = 0.0f (6D mode).
```

#### MatchResult — dist_components[7]

```cpp
// lib/StorageManager/src/MetalMatcher.h
struct MatchResult {
    // ... existing fields (coin_name, confidence, metal_code, alternatives) ...
    float dist_components[7];   // was [6]; positional: [0]=dRp1_n [1]=k1 [2]=k2
                                //   [3]=df_n [4]=dL1_n [5]=df1_n [6]=mass_n
};
// If mass_n == SENTINEL (-1.0f) or w[6] == 0.0f: dist_components[6] = 0.0f
```

#### MetalMatcher public API — нові сигнатури

```cpp
// lib/StorageManager/src/MetalMatcher.h

// 7D full match. mass_n = -1.0f → sentinel → w[6] forced 0.0f (6D mode)
MatchResult matchFull(const Measurement& m,
                      float df_n   = 0.0f,
                      float df1_n  = 0.0f,
                      float mass_n = -1.0f) const;   // ← ADDED param (was 3 params)

// 7D quick match. Same sentinel rule.
MatchResult matchQuick(float rpLive,  float rpBase,
                       float lLive,   float lBase,
                       float mass_n = -1.0f) const;  // ← ADDED param (was 4 params)
```

#### MetalMatcher::doMatch — private, нова сигнатура

```cpp
// lib/StorageManager/src/MetalMatcher.h (private)
MatchResult doMatch(float dRp1_n, float k1,    float k2,
                    float df_n,   float dL1_n,  float df1_n,
                    float mass_n,               // ← ADDED (7th dimension)
                    const float* weights, uint8_t algo) const;

// Sentinel rule inside doMatch:
//   if (mass_n < 0.0f) { effective_w6 = 0.0f; dist_components[6] = 0.0f; }
//   else               { effective_w6 = weights[6]; compute contribution normally; }
```

#### MeasState enum — STEP_WEIGHT + STEP_QUICK (D-12e)

```cpp
// src/main.cpp
enum class MeasState : uint8_t {
    IDLE,
    STEP_WEIGHT,   // ← NEW (D-12e): NAU7802 acquisition (skip if !calibrated)
    STEP_QUICK,    // ← RENAMED from STEP_BASE (D-12e): same LDC1101 capture + matchQuick 7D
    STEP_1,
    STEP_3,
    STEP_DRIFT,
    COMPUTE
};
// Migration note: all existing references to MeasState::STEP_BASE → MeasState::STEP_QUICK
// grep target: "case MeasState::STEP_BASE" (1 location in main.cpp)
```

#### Protocol ID (D-12d/D-12e)

```
p3_MIKROE3240_b06_012mm       ← Wave 9 production (currently hardcoded in main.cpp ×3)
p4_MIKROE3240_b06_012mm_mass  ← Wave 10 (STEP_WEIGHT + 7D vector)
```

Migration: replace the 3 hardcoded strings in `src/main.cpp` (see §9.5 B-05 for locations).

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
1. Settle 500ms (SETTLE_MS) — достатньо для механічного демпфування (f_natural для 50г платформи + 33г монети ~ 20-30 Hz, затухання < 100ms; ADR-NAU-006)
2. Медіана N=20 семплів — відкидає outlier spikes
3. Acquisition тільки на STEP_WEIGHT — до addon spacers (монета ще не переміщена на котушку)

**Статус:** вирішено архітектурно.

---

### 9.3 B-03: Temperature drift load cell

**Ризик:** Типовий drift load cell 0.05%FS/°C. При FS=100g і ΔT=10°C: drift = 0.05g. Для потреб проекту (мін. різниця 0.5g між класами) — прийнятно.

**Вирішення:** tare() при кожному старті пристрою (компенсує zero drift, але не span drift). Span drift 0.05%FS/°C при ΔT=10°C = 0.05g — значно менше 0.5g мінімальної різниці.

**Статус:** не блокує.

---

### 9.4 B-04: WiFi noise на analog chain

**Ризик:** ESP32 WiFi (2.4GHz) генерує pulse noise на 3.3V rail ~50mV pk-pk, що може індуктуватись в аналоговий ланцюг load cell. Додаткове обмеження: DVDD=3.3V → VLDO=3.0V, margin = 0.3V = мінімальний за datasheet. При WiFi TX burst DVDD може просідати до ~3.15V → margin 0.15V → LDO regulation degraded (see AI-5 audit 2026-04-08).

**Вирішення:**
1. NAU7802 AVDDS=1 (internal LDO) ізолює load cell excitation від power rail
2. N=20 медіана відкидає EMI spikes
3. 80SPS низькочастотний фільтр NAU7802 sigma-delta ADC відфільтровує 2.4GHz (оцифровує тільки DC..100Hz)
4. Wires load cell розміщувати якомога далі від WiFi антени

**Статус:** вирішено HW+SW.

---

### 9.5 B-05: Register bit field помилки у D-12a WIP

**Ризик:** D-12a WIP skeleton має 4 помилки в register constants (знайдено при peer review 2026-04-08). Silent failures: чіп ініціалізується без помилок, але SPS/LDO/PGA налаштовані неправильно.

| Константа | WIP (помилкове) | B-05 "виправлено" (**теж помилкове!**) | B-08 фінально правильне |
|---|---|---|---|
| `CTRL1_VAL` | `0x27` | ~~`0xBC`~~ | **`0x2F`** — `(0x05<<3)\|(0x07<<0)` |
| `CTRL2_VAL` | `0x30` | ~~`0x60`~~ | **`0x30`** — `(0x03<<4)` ← WIP-значення CTRL2 **вже** було правильним! |
| CRS mask `tare()` | `~0x70` | ~~`~0xE0`~~ | **`~0x70`** ← WIP-значення маски теж було правильним! |
| CTRL1/CTRL2 order | перед OTP | перед OTP | **після analog init** (ADR-NAU-007, v1.5.0) |

> **⚠️ HISTORICAL NOTE (N-01 full analysis 2026-04-09):** Таблиця вище відображає стан B-05 audit. Колонка "B-05 виправлено" містила помилкові значення (B-05 сам помилявся: неправильно визначив бітові позиції). B-08 (commit cd2aa56) виправив остаточно на основі datasheet Rev 2.6 + Adafruit/SparkFun libs. Актуальні значення: §2.4.

> **⚠️ Комбінований ефект Bug #1 + Bug #4 — катастрофічний:**  
> Bug #1 (`CTRL1=0x27`) встановлює PGA=2x замість 128x. Bug #4 (CTRL перед OTP) — OTP reload відбудеться після і скине PGA до 1x (OTP default). Результат: sensitivity = 1x замість 128x = **−42 dB**. Noise floor ~6.4g RMS (замість 5 mg). Калібрація технічно "виконається" (scale_factor буде 128× більшим), але noise >> signal — вимір **повністю даремний**. Ці два bugs мають виправлятись разом і перевірятись разом у D-12a HW test (sigma < 20 counts при порожній платформі = smoke test).

**Вирішення:** Виправлено в commit 3601235 (B-05 частково) та commit cd2aa56 (B-08 остаточно). Актуальні значення: §2.4 «Derived register constants».

**Статус:** ✅ Виправлено (B-08, commit cd2aa56).

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

### 9.7 B-07: Analog init sequence — silent failure ризик

**Ризик:** Якщо startup sequence (§2.3) реалізована некоректно — NAU7802 буде давати drift або некоректні значення без явного error. Це silent failure.

**Вирішення:**
1. Читабельний boot log: `REG_PGA` readback після ініціалізації — якщо `BYPASS_EN=1` → error log
2. Self-test аналіз `range` 5 зразків: STABLE (<100) / NOISY (>100) / DRIFTING (drift≥range/2)
3. Sigma quality interpretation у tare(): GOOD/MARGINAL/POOR/FAIL

**Статус:** ✅ Реалізовано. Boot log: `Startup OK: PU_CTRL=0x9E REG_PGA=0x00` + BYPASS_EN check.

---

### 9.8 B-08: Механічне зміщення платформи при спейсерах

**Ризик:** При кладанні addon spacers (+1mm, +2mm) оператор тисне на монету -> платформа навантажується > FS load cell. Але load cell 100g при натисканні 200-500g може дати пластичну деформацію.

**Вирішення:**
1. Маса вимірюється ТІЛЬКИ на STEP_WEIGHT — до addon spacers (до перекладання монети на котушку)
2. Механічний стопер: addon spacers не передають силу на платформу load cell (вони впираються в нерухому раму)
3. Load cell 100g: overload capacity typ 150-200% = 150-200g. Нормальна монета 1-33г -> безпечно. Тиск рукою max 500g при кладанні спейсера -> потрібно routing spacer force в нерухому раму

**Статус:** вирішується механічним дизайном.

---

### 9.9 B-09: Startup sequence містила BYPASS_EN=1 (audit 2026-04-10)

**Знайдено:** зовнішній аудит `docs/external/2026-04-10.NAU7802_IMPLEMENTATION_AUDIT.md`  
**Scope:** X-01 (CRITICAL), X-02 (CRITICAL), X-03 (HIGH)  
**Статус:** ✅ Виправлено в `lib/NAU7802Plugin/src/NAU7802Plugin.h/.cpp` (2026-04-10)

#### X-01 [CRITICAL]: BYPASS_EN=1 → effective gain = 1x замість 128x

**Симптом:** Startup sequence v1.4.0 читала регістр 0x15 (помилково `REG_OTP_B1`), потім записувала `(value & 0x38) | 0x30` у REG_PGA (0x1B). Оскільки 0x15 = 0x00 після reset, результат: `0x30 → REG_PGA`. `0x30 = OUT_EN(bit5) | BYPASS_EN(bit4)` → PGA bypassed.

**Апаратне підтвердження (boot logs 2026-04-10):**
- Спостережувана чутливість: 151 counts/g
- Модель bypass (gain=1x): 168 counts/g (розбіжність 10% — в межах load cell tolerance ±20%)  
- Очікувана при gain=128x: 21,474 counts/g → різниця **142x** ≈ підтверджено

**Reference libs:** обидва Adafruit і SparkFun тільки `&= ~0x40` (clear LDOMODE bit6). Ніхто не пише 0x30.

**Виправлення:**
```cpp
// старий код (WRONG — встановлював BYPASS_EN=1):
_writeReg(REG_PGA, (otp_b1 & 0x38) | 0x30);

// новий код (CORRECT):
uint8_t pga = _readReg(REG_PGA);
_writeReg(REG_PGA, pga & ~PGA_LDOMODE_BIT);  // clear bit6 only
```

#### X-02 [CRITICAL]: REG_ADC_CTRL (0x15) — CLK_CHP не вимкнений

**Симптом:** Регістр 0x15 — ADC Control Register (не OTP!). Bits[5:4]=CLK_CHP після reset = 00 (max chopper noise). Reference libs: `reg[0x15] |= 0x30` як один з перших кроків.

**Виправлення:** Додано step 6 у startup sequence: `_writeReg(REG_ADC_CTRL, adc_ctrl | ADC_CHP_DIS)` де `ADC_CHP_DIS = 0x30`.

#### X-03 [HIGH]: PGA_PWR_VAL = 0x30 — PGA_CAP_EN не активований

**Симптом:** `PGA_CAP_EN = bit7 = 0x80`. Старе `PGA_PWR_VAL = 0x30` встановлювало `MSTR_BIAS_CURR = 0b011` (bits[6:4]) — нестандартне значення. Cap (330pF §9.14) не активований.

**Виправлення:** `PGA_PWR_VAL = 0x80` (тільки PGA_CAP_EN).

#### Вплив X-01/X-02/X-03 на sigma в ГРАМАХ

```
sigma в грамах визначається EMI і НЕ змінюється принципово після виправлення:
  До виправлення (gain=1x):    sigma=14,258 counts / 151 counts/g = 94g
  Після виправлення (gain=128x): sigma=96 counts / 21400 counts/g = 4.5 мг ← реальна точність

Різниця:  94g → 4.5 мг. Виправлення відновило правильну чутливість.
sigma < 20 counts (B-07) при gain=128x = 0.9 мг — досягається після EMI reduction (§3.4).
```

#### Обов'язковий NVS reset після X-01

Якщо `calibrate()` виконувалась при `BYPASS_EN=1` → збережений `scale_factor` у 142x неправильний:
```
clearCalibration() → обов'язково перед повторним tare() + calibrate() після виправлення
```

Нові tare/calibrate відбуватимуться при gain=128x → scale_factor буде правильним.

### 9.10 B-10: tare()/calibrate() зависали після WiFi startup (2026-04-10)

**Знайдено:** hardware test D-12c (2026-04-10)  
**Scope:** `NAU7802Plugin::tare()`, `NAU7802Plugin::calibrate()`  
**Статус:** ✅ Виправлено — `_ensureConversionsRunning()` pre-flight

**Симптом:** Виклик `tare(32)` через ~15-30 секунд після `WiFi.begin()` завершувався помилкою `tare() capture failed` після 15-хвилинного (6400 ms) таймауту з 0 зібраними зразками. За одного запуску також з'явилась `I2C write failed: reg=0x02 err=5` на початку `calibrate()`.

**Причина:** ESP32 WiFi init може або (a) зависити I2C шину — тоді `_readReg()` повертає `0xFF` (NACK/timeout у Wire), або (b) спричинити короткий просідання напруги (~3.3V), яке скидає регістри NAU7802 зберігаючи `_initialized=true` — CS-bit (bit4 PU_CTRL) очищується, мікросхема зупиняє конверсії. `_blockingCaptureSamples()` крутить петлю `_isReady()` увесь deadline і повертає `collected=0 < n/2` → false.

**Виправлення:** Новий приватний метод `_ensureConversionsRunning()`, викликається на початку `tare()` і `calibrate()` (до `_blockingCaptureSamples()`):
- Якщо `PU_CTRL == 0xFF`: `Wire.end()` → `delay(5)` → `Wire.begin(sda, scl)` → `Wire.setClock(hz)` → повторний probe
- Якщо CS=0: повторний `_startupSequence()` → `delay(50)` для першої конверсії
- SDA/SCL/Hz зберігаються в `_sda`/`_scl`/`_i2cHz` з `initialize()` (дефолт 8/9/400kHz)

**Hardware logs (підтвердження):**
```
[44092ms] WARN  NAU7802 | pre-flight: PU_CTRL=0xFF — I2C bus hung; resetting (SDA=8 SCL=9 400kHz)
[44534ms] INFO  NAU7802 | Tare OK: zero_offset=-113845  sigma=78.4  n=32
[66089ms] WARN  NAU7802 | pre-flight: PU_CTRL=0xFF — I2C bus hung; resetting
[66513ms] INFO  NAU7802 | Calibrate OK: scale=0.00005337  ref=20.00g  sigma=143.1
```

**Де в коді:** `lib/NAU7802Plugin/src/NAU7802Plugin.cpp` — `_ensureConversionsRunning()`, `tare()`, `calibrate(float, uint16_t)`

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
  Command: 3 повних виміри (STEP_WEIGHT..СОМПУТЕ) з XUSSR10
  Pass:    mass_g = 33.0-33.6г у NDJSON; 7D wdist до xussr10 centroid < 0.3

HW-NAU-06: Degraded mode (NAU відключений)
  Command: від'єднати SDA, виміряти монету
  Pass:    measurement completes з mass_n=-1.0, 6D матчинг працює
```

---

## 11. Архітектурні рішення (ADR)

### ADR-NAU-001: OTP Auto-load — явний reload НЕ потрібний *(оновлено 2026-04-10, audit)*

**Контекст (v1.4.0):** Попереднє рішення вимагало явного OTP reload sequence у `_startupSequence()`, щоб відновити factory calibration trim values після power-on.

**Поточне рішення (v1.5.0 — ЗАМІНЮЄ попереднє):** NAU7802 **автоматично завантажує OTP** при power-on. Явний OTP reload (запис (OTP_B1 & 0x38)|0x30 → REG_PGA) є некоректним і призводить до X-01: `BYPASS_EN=1` → PGA bypassed → gain=1x замість 128x.

Натомість analog init sequence (§2.3 кроки 6-8) відповідає reference implementations (Adafruit, SparkFun):
- Крок 6: `REG_ADC_CTRL |= 0x30` — вимкнути CLK_CHP (X-02 fix)
- Крок 7: `REG_PGA &= ~0x40` — clear LDOMODE bit6 ONLY (не писати 0x30!)
- Крок 8: `REG_PGA_PWR = 0x80` — PGA_CAP_EN bit7 (X-03 fix)

**Апаратна верифікація (2026-04-10):** raw ADC ~+113K (vs ~-21K до виправлення), REG_PGA=0x00 в boot log, чутливість ~21,480 counts/g (128x підтверджено).

**Наслідки:** Жодних змін в наслідках (startup time аналогічний). CTRL1/CTRL2 записуються після кроків 6-8 (analog init).

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

**Наслідки:** +300ms до STEP_WEIGHT (загальний час ~750ms), NAU7802 залишається ≫ швидшим ніж LDC1101 capture window (2000ms). Нульовий вплив на загальний час виміру.

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

### ADR-NAU-007: Startup sequence — CTRL1/CTRL2 після analog init *(оновлено 2026-04-10, audit)*

**Контекст (v1.4.0):** Попереднє рішення вимагало порядку: OTP reload → CTRL1/CTRL2. Логіка: OTP може перезаписати PGA trim bits якщо CTRL записані раніше.

**Поточне рішення (v1.5.0 — ЗАМІНЮЄ попереднє):** OTP auto-loads при power-on (ADR-NAU-001 оновлено). Явного OTP reload немає. Замість цього — analog init sequence (кроки 6-8, §2.3). Порядок: analog init (6-8) → CTRL1/CTRL2 (9-10).

**Правильна послідовність для `_startupSequence()` (v1.5.0, §2.3):**
```
1:   Write RR=1 (ресет реєстрів)
2:   Delay 10ms
3:   Write PUD (підняття цифрової частини)
4:   Wait PUR bit (timeout 200ms)
5:   Write AVDDS|PUA|PUD (підняття аналогової + internal LDO)
6:   REG_ADC_CTRL (0x15) |= 0x30  -- CLK_CHP disable (X-02 fix)
7:   REG_PGA (0x1B) &= ~0x40      -- clear LDOMODE bit6 ONLY (НЕ писати 0x30!)
8:   REG_PGA_PWR (0x1C) = 0x80    -- PGA_CAP_EN bit7 (X-03 fix)
9:   CTRL1 = 0x2F  (GAINS=128x, VLDO=3.0V)   ← після analog init
10:  CTRL2 = 0x30  (CRS=80SPS, CHS=CH1)      ← після analog init
11:  Enable conversions (CS bit)
12:  Delay 15ms, discard first sample
13:  CALS (wait ≤400ms)
14:  Restore CTRL2 = 0x30
```

**Апаратна верифікація (2026-04-10):** `Startup OK: PU_CTRL=0x9E REG_PGA=0x00` — BYPASS_EN=0 підтверджено.

**Наслідки:** Жодних змін у публічному API. +0ms overhead.

---

## 12. Implementation Checklist

### D-12a: Базовий драйвер ✅ DONE (commit 3601235 + post-review + B-08 audit fixes)
- [x] Створити `lib/NAU7802Plugin/src/NAU7802Plugin.h/.cpp`
- [x] **FIX: startup sequence** — CTRL1/CTRL2 ПІСЛЯ OTP reload — ADR-NAU-007 (commit 3601235)
- [x] **FIX: SETTLE_MS = 500** (замість 200) — ADR-NAU-006 (commit 3601235)
- [x] **FIX: delay(10)** після RR=1 (замість 1ms) — §2.3 step 2 (post-review)
- [x] **FIX: NVS key "cal_mass_g"** (замість "cal_mass") — §7.1 (post-review)
- [x] **FIX B-08: CTRL1_VAL = 0x2F** `(0x05 << 3) | 0x07` — бітові позиції VLDO[5:3]+GAINS[2:0] (audit, старе 0xBC=WRONG)
- [x] **FIX B-08: CTRL2_VAL = 0x30** `(0x03 << 4)` — CRS[6:4] (audit, старе 0x60=WRONG — undefined rate)
- [x] **FIX B-08: tare() SPS mask** — `~0x70` (bits[6:4]) замість `~0xE0` (audit)
- [x] Реалізувати `_startupSequence()` з analog init sequence (ADR-NAU-001 оновлено: OTP auto-loads)
- [x] Реалізувати `_writeReg()`, `_readReg()`, `_readAdc24()` + sign-extend
- [x] Реалізувати `_isReady()` (polling CR bit 5 = 0x20, ADR-NAU-002)
- [x] Реалізувати `_computeMedian()` для float buf
- [x] Реалізувати `tare()` / `calibrate(known_g)` (blocking, 10 SPS)
- [x] Реалізувати `saveCalibration()` / `loadCalibration()` (NVS "nau7802")
- [x] Реалізувати non-blocking acquisition state machine
- [x] canInitialize() / initialize() / shutdown() per PLUGIN_CONTRACT
- [x] IDiagnosticPlugin: runDiagnostics(), runSelfTest(), checkHardwarePresence()
- [x] **FIX X-01: BYPASS_EN=0** — REG_PGA &= ~0x40 (audit 2026-04-10, §9.9)
- [x] **FIX X-02: CLK_CHP disabled** — REG_ADC_CTRL (0x15) |= 0x30 (audit 2026-04-10)
- [x] **FIX X-03: PGA_PWR_VAL=0x80** — PGA_CAP_EN bit7 (audit 2026-04-10)
- [x] Boot log: `REG_PGA` readback + BYPASS_EN runtime check (ERROR якщо BYPASS_EN=1)
- [x] Self-test STABLE/NOISY/DRIFTING classification на основі range 5 зразків
- [x] **B-07 виправлено:** sigma < 20 counts при gain=1x ≈ sigma < 2560 counts при gain=128x.
  C-13 результат: sigma=96 counts (4.5 мг) при gain=128x ✅ (достатньо для ±1.5% на 1г монеті)
- [x] Створити `data/plugins/nau7802.json` ✅

### D-12b: NVS калібрування ✅ DONE + audit NVS reset (реалізовано разом з D-12a)
- [x] `saveCalibration()` → NVS namespace "nau7802" (zero, scale, cal_ok, cal_ts, cal_mass_g)
- [x] `loadCalibration()` → відновлення _zeroOffset, _scaleFactor
- [x] Перевірка "cal_ok" при initialize()
- [x] `clearCalibration()` для скидання

### D-12c: Calibration UX ✅ DONE (2026-04-10, hw-verified)
- [x] `tare(N)` -- blocking (реалізовано, 80 SPS, розраховує mean/sigma)
- [x] `calibrate(known_g, N)` -- blocking (реалізовано, 80 SPS, валідація net_raw)
- [x] Клавіша 'K' в main.cpp → запуск `runCalibrationWizard()` (з `gNAU` null guard)
- [x] Екрани Step 1/2/3 на Cardputer display (tare → calibrate → confirm)
- [x] **FIX B-10:** `_ensureConversionsRunning()` pre-flight у `tare()` + `calibrate()` — WiFi-induced I2C hang/chip reset (PU_CTRL=0xFF або CS=0) відновлюється автоматично
- [x] Hardware verified: zero_offset=-113845, scale=0.00005337 g/count, ref=20.0g, sigma=78-143 MARGINAL

### D-12d: Acquisition integration
- [x] `startAcquisition()` / `isAcquisitionComplete()` / `getLastMassG()` / `getLastMassN()`
- [x] Non-blocking state machine в `_updateAcqStateMachine()`
- [ ] Виклик `gNAU->update()` в main loop
- [ ] Виклик `gNAU->startAcquisition()` в STEP_WEIGHT entry (ADR-NAU-008)
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

### C-13: Вагова сесія ✅ DONE (2026-04-10)
- [x] Hardware verified: sigma=96 counts (4.5 мг) при gain=128x
- [x] Виправлено 3 критичних баги (X-01/X-02/X-03) — gain тепер реальні 128x
- [x] Наявні гирі: 1г, 2г, 5г, 10г, 20г, 50г (аптечний набір) → D-12c
- [ ] Перевірити реальні маси всіх 13 класів з аптечними гирями → D-12c/D-12d
- [ ] Зібрати 3+ вимірів з mass_g на клас
- [ ] Верифікувати sigma_mass < 0.2г для кожного класу
- [ ] build_gen7_db.py: додати mass_n до centroid, масовий довідник

---

*Документ: `docs/architecture/NAU7802_ARCHITECTURE.md`*  
*Версія: 1.6.0 | Дата: 2026-04-10 — D-12c hw-verified: calibration wizard `runCalibrationWizard()` ('K' key), 3-screen flow (tare→calibrate→confirm), scale=0.00005337 g/count @ 20g ref; B-10: `_ensureConversionsRunning()` pre-flight у tare()/calibrate() усуває WiFi-induced I2C hang (PU_CTRL=0xFF) та chip reset (CS=0); §9.10 B-10 додано; §12 D-12c checklist [x]*  
*Версія: 1.4.0 | Дата: 2026-04-09 — B-08 post-commit audit: CTRL1/CTRL2 бітові позиції виправлені; CTRL1_VAL 0xBC→0x2F (VLDO[5:3]+GAINS[2:0]); CTRL2_VAL 0x60→0x30 (CRS[6:4]); mask ~0xE0→~0x70; §2.2/§2.3/§2.4 arch doc синхронізовано*  
*Версія: 1.3.0 | Дата: 2026-04-09 — post-review fixes: (1) §2.2 PU_CTRL bit table виправлена (AVDDS=bit7, не bit3); (2) I2C_CTRL 0x1F→0x11; REVISION_ID додано; (3) §2.3 startup sequence оновлена (імпл. AVDDS step 4, CALS steps 13-14 документовано); (4) §12 D-12a/b/c чекліст оновлено [x]; code: delay(1)→10ms, NVS key виправлено*  
*Версія: 1.2.0 | Дата: 2026-04-09 — §3.1 Adafruit #4538: pin name VIN, AV=output, A+/A- colors, STEMMA QT, 10kΩ pullup; WIP-нотатка прибрана*  
*Версія: 1.1.0 | Дата: 2026-04-08 — D-12a peer review: 5 bugs found, §2.4 derived constants + ADR-NAU-006/007 added*  
*Базується на: NAU7802 Datasheet Rev 2.6, PLUGIN_CONTRACT v1.0.0, MEMORY_MAP v1.0.0, Wave 10 Architecture Plan*  
*Наступне оновлення: після D-12a hw-verify (C-13 session)*
