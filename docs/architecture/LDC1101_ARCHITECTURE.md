# LDC1101 в системі плагінів CoinTrace: Архітектурний аналіз

**Тип документа:** Технічний аналіз та специфікація реалізації  
**Версія:** 1.2.0  
**Дата:** 14 березня 2026 (оновлено: хвиля 5 — M-1: `staleFlag` без mutex в `getHealthStatus()`; M-3: polling contract `COIN_REMOVED`; L-1: `convTimeMs()` comment; I-1: `lastCalibrationTime` documented; I-2: `reconfigureSensor()` async comment)  
**Статус:** Готовий до імплементації  
**Джерело:** Texas Instruments LDC1101 Datasheet SNOSD01D (May 2015 – Revised October 2016)

---

## Зміст

1. [Огляд сенсора та роль в системі](#1-огляд-сенсора-та-роль-в-системі)
2. [Фізика вимірювання](#2-фізика-вимірювання)
3. [SPI протокол](#3-spi-протокол)
4. [Карта регістрів](#4-карта-регістрів)
5. [Стратегія інтеграції](#5-стратегія-інтеграції)
6. [SPI шина і конкурентний доступ](#6-spi-шина-і-конкурентний-доступ)
7. [Конфігураційний файл](#7-конфігураційний-файл)
8. [Референсна реалізація](#8-референсна-реалізація)
9. [Часові характеристики і точність](#9-часові-характеристики-і-точність)
10. [Відповідність архітектурним контрактам](#10-відповідність-архітектурним-контрактам)

---

## 1. Огляд сенсора та роль в системі

### Що вимірює LDC1101

LDC1101 (Texas Instruments) — індуктивний сенсор що вимірює зміни в котушці індуктивності спричинені присутністю провідного або феромагнітного об'єкта. Інтерфейс — **4-wire SPI** (піни `CSB`, `SCLK`, `SDI`, `SDO`). I2C не підтримується.

Сенсор одночасно видає два незалежні параметри:

| Параметр | Що відображає | Фізичний зміст |
|---|---|---|
| **RP** (паралельний опір) | Провідність металу | Срібло, мідь, алюміній дають різні значення |
| **L** (індуктивність) | Магнітна проникність | Феромагнітна сталь різко збільшує L відносно кольорових металів |

**Тільки комбінація RP + L** дозволяє однозначно відрізнити метал монети. Окремо кожен параметр не дає достатньої інформації для ідентифікації.

### Місце в сенсорному ансамблі CoinTrace

| Сенсор | Інтерфейс | Що вимірює | Час конверсії |
|---|---|---|---|
| HX711 | GPIO | Вага | ~100 мс |
| QMC5883L | I2C | Магнетизм | ~10 мс |
| BMI270 | I2C / SPI | IMU | ~1 мс |
| **LDC1101** | **SPI** | **RP + L (метал монети)** | **~0.1–4 мс** |
| BH1750 | I2C | Освітленість | ~120 мс |

LDC1101 є **первинним сенсором ідентифікації** — без нього система не може визначити метал. Решта сенсорів доповнюють картину.

---

## 2. Фізика вимірювання

### Цикл одного вимірювання

```
┌──────────────────────────────────────────────────────────────┐
│  Крок 1: Чіп в Active mode (FUNC_MODE = 0x00)               │
│           → запускає безперервні конверсії автоматично       │
├──────────────────────────────────────────────────────────────┤
│  Крок 2: Аналогова конверсія                                 │
│           Час = RESP_TIME_cycles / (3 × fSENSOR)             │
│           При fSENSOR = 2 MHz, RESP_TIME = 6144:             │
│           = 6144 / (3 × 2 000 000) = 1.024 мс               │
│           При fSENSOR = 500 kHz, RESP_TIME = 6144:           │
│           = 6144 / (3 × 500 000) = 4.096 мс                 │
├──────────────────────────────────────────────────────────────┤
│  Крок 3: Перевірка STATUS (0x20), біт DRDYB (bit 6)         │
│           DRDYB = 0 → дані готові   ← інвертована логіка!   │
│           DRDYB = 1 → конверсія ще триває                    │
├──────────────────────────────────────────────────────────────┤
│  Крок 4: Burst-читання 4 байтів за одну SPI транзакцію      │
│           0x21 RP_DATA_LSB — ЧИТАТИ ПЕРШИМ (розблоковує MSB)│
│           0x22 RP_DATA_MSB                                   │
│           0x23 L_DATA_LSB                                    │
│           0x24 L_DATA_MSB                                    │
│           SPI burst при 4 MHz: ~10 мкс                       │
└──────────────────────────────────────────────────────────────┘
```

### Критичні апаратні обмеження

**1. Порядок читання — обов'язковий.** Datasheet розділ 8.6.19:
> *"RP_DATA_LSB (Address 0x21) must be read prior to reading RP_DATA_MSB, L_DATA_LSB, and L_DATA_MSB to properly retrieve conversion results."*

`RP_DATA_LSB (0x21)` читається **першим** — це апаратно розблоковує решту регістрів. Читання в іншому порядку повертає некоректні дані без будь-якої індикації помилки.

**2. Конфігурація тільки в Sleep mode.** Datasheet розділ 8.5.1:
> *"It is necessary to configure the LDC1101 while in Sleep mode."*

Запис конфігураційних регістрів в Active mode призводить до нестабільної або некоректної роботи сенсора.

**3. DRDYB — інвертована логіка.** Bit 6 регістра STATUS: значення `0` означає "дані готові", значення `1` — "конверсія триває". Пряма перевірка `if (status & STATUS_DRDYB)` читає дані передчасно.

**4. RP_SET — критичний для осциляції.** Регістр `0x01` визначає динамічний діапазон RP вимірювання. Якщо котушка не вкладається в заданий діапазон — `STATUS_NO_OSC` спрацює і жодних вимірювань не буде.

> ⚠️ **ADR-LDC-001 (SYS-1b):** Два задокументовані значення для MIKROE-3240:
> - `0x07` (POR default): RP_MAX=0.75 kΩ / RP_MIN=0.75 kΩ — вузький діапазон
> - `0x26` (MikroE SDK `ldc1101_default_cfg()`, отримано 2026-03-13): RP_MAX=24 kΩ / RP_MIN=1.5 kΩ
>
> **Рішення:** Використовувати `0x26` як стартове значення (офіційний MikroE SDK для MIKROE-3240). Верифікувати на реальному пристрої. Якщо `STATUS_NO_OSC` — підбирати інше значення через `rp_set` в конфігурації.

### RESP_TIME і точність

| RESP_TIME bits[2:0] | Cycles | Час при 2 MHz | Час при 500 kHz | Шум |
|---|---|---|---|---|
| `b010` | 192 | 0.032 мс | 0.128 мс | Вищий |
| `b011` | 384 | 0.064 мс | 0.256 мс | Середній |
| `b100` | 768 | 0.128 мс | 0.512 мс | — |
| `b101` | 1536 | 0.256 мс | 1.024 мс | — |
| `b110` | 3072 | 0.512 мс | 2.048 мс | — |
| `b111` | 6144 | 1.024 мс | 4.096 мс | Нижчий |

`b000` і `b001` — зарезервовані datasheet. При передачі цих значень код використовує `b010` (192 cycles) як безпечний fallback.

**Формула:** `ConversionTime = RESP_TIME_cycles / (3 × fSENSOR)`

Для MIKROE-3240 fSENSOR ≈ 200–500 kHz (оцінка: MikroE DIG_CFG MIN_FREQ=118 kHz + LC-формула на PCB spiral coil M-size). При `RESP_TIME = 0x07` (6144 cycles) і fSENSOR=300 kHz: 6144/(3×300 000) ≈ 6.8 мс — в бюджет 20 мс `update()` вкладається. Використовувати `0x07` як стандартне значення.

> ⚠️ **ADR-RESP-001:** Архітектура використовує `RESP_TIME = 6144 cycles` (bits=`0x07`) як default, замість 768 cycles (bits=`0x04`) MikroE SDK.
>
> **Обгрунтування:** `SNR ∝ √RESP_TIME` → SNR(6144)/SNR(768) = √8 = **2.83×** вищий. Для нумізматичної класифікації (Ag925/Ag900 ΔRP ≈ 10–30%) вищий SNR є пріоритетом над throughput: ConversionTime при fSENSOR=300 kHz та 6144 cycles = 6.8 мс << 20 мс `update()` budget.
>
> **Ревізія:** якщо класифікація не потребує Ag925/Ag900 диференціації, можна знизити до `0x04` (768 cycles) для 8× вищого throughput без порушення контракту.

### TC1/TC2 і DIG_CFG — офіційні значення (MIKROE-3240)

Офіційна робоча конфігурація з MikroE SDK `ldc1101_default_cfg()` (отримано 2026-03-13):

| Регістр | Адреса | Значення | Параметри |
|---|---|---|---|
| RP_SET  | `0x01` | `0x26` | RP_MAX=24 kΩ / RP_MIN=1.5 kΩ (ADR-LDC-001) |
| TC1     | `0x02` | `0x1F` | C1=0.75 pF, R1=21.1 kΩ → τ₁=15.8 ns |
| TC2     | `0x03` | `0x3F` | C2=3 pF, R2=30.5 kΩ → τ₂=91.5 ns |
| DIG_CFG | `0x04` | `0xD4` | MIN_FREQ=118 kHz (0xD0) + RESP_TIME=768 cycles (0x04) |

TC1/TC2 компенсують паразитні ємності і опори PCB доріжок MIKROE-3240. Значення специфічні для цієї плати. Записувати в Sleep mode до переходу в Active mode.

**DIG_CFG=0xD4:** `MIN_FREQ=0xD0` (118 kHz) запобігає false halt при fSENSOR ≈ 200–300 kHz. При `MIN_FREQ=0x00` (500 kHz threshold) конверсія може зупинятись — небезпечно для MIKROE-3240.

### Семантика вихідних даних

`L_DATA` — **не є індуктивністю в мкГн**. Це кодова величина пов'язана з fSENSOR формулою (datasheet розділ 8.6.21):

```
fSENSOR = (fCLKIN × RESP_TIME_cycles) / (3 × L_DATA)
L [мкГн] = 1 / (4π² × fSENSOR² × C_SENSOR)
```

Для конвертації в мкГн потрібні `fCLKIN`, `RESP_TIME` і `C_SENSOR` (ємність котушки). Для порівняння монет між собою **raw L_DATA достатньо** — монети порівнюються відносно, абсолютне значення індуктивності не потрібне. `SensorData.value2` зберігає raw L_DATA code.

---

## 3. SPI протокол

### Формат транзакції

```
Байт команди: [R/W | A6 | A5 | A4 | A3 | A2 | A1 | A0]
               bit7                                  bit0

R/W = 1: Read операція
R/W = 0: Write операція
A[6:0]: 7-бітна адреса регістра
```

CSB тримається LOW протягом всієї транзакції. При extended read (кілька послідовних регістрів) адреса автоматично інкрементується — саме це дозволяє прочитати `RP_DATA_LSB..L_DATA_MSB` за одну транзакцію.

### Параметри SPI

| Параметр | Значення |
|---|---|
| Mode | SPI_MODE0 (CPOL=0, CPHA=0) |
| Bit order | MSBFIRST |
| Max clock | 4 MHz (консервативно) |
| CS | Активний LOW, окремий GPIO |

### Burst-читання результатів

```cpp
// Одна транзакція — 4 байти, адреса інкрементується автоматично
ctx->spi->beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
digitalWrite(csPin, LOW);
ctx->spi->transfer(0x21 | 0x80);      // Read bit | RP_DATA_LSB — ПЕРШИМ!
uint8_t rpLsb = ctx->spi->transfer(0); // 0x21: RP_DATA_LSB
uint8_t rpMsb = ctx->spi->transfer(0); // 0x22: RP_DATA_MSB
uint8_t lLsb  = ctx->spi->transfer(0); // 0x23: L_DATA_LSB
uint8_t lMsb  = ctx->spi->transfer(0); // 0x24: L_DATA_MSB
digitalWrite(csPin, HIGH);
ctx->spi->endTransaction();

uint16_t rpRaw = ((uint16_t)rpMsb << 8) | rpLsb;  // LSB першим → correct
uint16_t lRaw  = ((uint16_t)lMsb  << 8) | lLsb;
```

---

## 4. Карта регістрів

Верифікована по datasheet SNOSD01D, Table 3.

```cpp
// ── Конфігураційні регістри (писати ТІЛЬКИ в Sleep mode) ──────────
static const uint8_t REG_RP_SET          = 0x01; // RP Dynamic Range (default 0x07 = найширший)
static const uint8_t REG_TC1             = 0x02; // Internal Time Constant 1
static const uint8_t REG_TC2             = 0x03; // Internal Time Constant 2
static const uint8_t REG_DIG_CONFIG      = 0x04; // RESP_TIME[2:0] + MIN_FREQ[7:4]
static const uint8_t REG_ALT_CONFIG      = 0x05; // LOPTIMAL, SHUTDOWN_EN
static const uint8_t REG_RP_THRESH_H_LSB = 0x06; // RP High Threshold LSB
static const uint8_t REG_RP_THRESH_H_MSB = 0x07; // RP High Threshold MSB
static const uint8_t REG_RP_THRESH_L_LSB = 0x08; // RP Low Threshold LSB
static const uint8_t REG_RP_THRESH_L_MSB = 0x09; // RP Low Threshold MSB
static const uint8_t REG_INTB_MODE       = 0x0A; // INTB pin config (не використовується — polling)
static const uint8_t REG_START_CONFIG    = 0x0B; // FUNC_MODE: Active / Sleep / Shutdown
static const uint8_t REG_D_CONFIG        = 0x0C; // DOK_REPORT

// ── L Threshold ───────────────────────────────────────────────────
static const uint8_t REG_L_THRESH_HI_LSB = 0x16;
static const uint8_t REG_L_THRESH_HI_MSB = 0x17;
static const uint8_t REG_L_THRESH_LO_LSB = 0x18;
static const uint8_t REG_L_THRESH_LO_MSB = 0x19;

// ── Результати вимірювань (read-only) ─────────────────────────────
static const uint8_t REG_STATUS          = 0x20; // DRDYB, NO_SENSOR_OSC, POR_READ
static const uint8_t REG_RP_DATA_LSB     = 0x21; // ⚠ ЧИТАТИ ПЕРШИМ — розблоковує решту!
static const uint8_t REG_RP_DATA_MSB     = 0x22;
static const uint8_t REG_L_DATA_LSB      = 0x23;
static const uint8_t REG_L_DATA_MSB      = 0x24;

// ── High-Resolution L mode (LHR) ─────────────────────────────────
// Опціональний режим для точних абсолютних вимірювань індуктивності.
// Не використовується в v1 реалізації — документовано для майбутнього.
static const uint8_t REG_LHR_RCOUNT_LSB  = 0x30;
static const uint8_t REG_LHR_RCOUNT_MSB  = 0x31;
static const uint8_t REG_LHR_OFFSET_LSB  = 0x32;
static const uint8_t REG_LHR_OFFSET_MSB  = 0x33;
static const uint8_t REG_LHR_CONFIG      = 0x34;
static const uint8_t REG_LHR_DATA_LSB    = 0x38;
static const uint8_t REG_LHR_DATA_MID    = 0x39;
static const uint8_t REG_LHR_DATA_MSB    = 0x3A;
static const uint8_t REG_LHR_STATUS      = 0x3B;

// ── Device Identity (read-only) ───────────────────────────────────
static const uint8_t REG_RID             = 0x3E; // Device RID = 0x02
static const uint8_t REG_CHIP_ID         = 0x3F; // CHIP_ID = 0xD4

// ── FUNC_MODE значення для REG_START_CONFIG ───────────────────────
static const uint8_t FUNC_MODE_ACTIVE    = 0x00; // Continuous conversion
static const uint8_t FUNC_MODE_SLEEP     = 0x01; // Low-power, конфіг зберігається (POR default)
static const uint8_t FUNC_MODE_SHUTDOWN  = 0x02; // Мінімальне споживання, конфіг НЕ зберігається

// ── STATUS register bits ──────────────────────────────────────────
static const uint8_t STATUS_NO_OSC       = 0x80; // bit7: котушка не осцилює
static const uint8_t STATUS_DRDYB        = 0x40; // bit6: 0 = дані готові (інверт!), 1 = не готові
static const uint8_t STATUS_POR_READ     = 0x01; // bit0: 1 = POR відбувся (очищується читанням STATUS)
```

---

## 5. Стратегія інтеграції

### Обрана стратегія: State Machine в `update()`

Для CoinTrace v1 обрана **Стратегія A — State Machine** без FreeRTOS task.

**Принцип роботи:**

```
initialize():
    Sleep mode → конфігурація регістрів → Active mode (залишається назавжди)

update() виклик N:
    читає STATUS
    ├─ NO_OSC = 1  → log warning, increment failedReads, return
    ├─ DRDYB = 1   → конверсія ще триває, нічого не робити, return
    └─ DRDYB = 0   → burst read RP+L → validate → оновити кеш → increment totalReads
```

**Часова діаграма** при котушці 2 MHz, RESP_TIME = `0x07` (conv ~1 мс), `update()` @ 50 Hz (20 мс):

```
LDC1101:  [1ms][1ms][1ms][1ms][1ms][1ms][1ms][1ms][1ms][1ms][1ms]...
update(): ▲                   ▲                   ▲
          ✓ read              ✓ read              ✓ read
          |← ~20 мс ─────────|← ~20 мс ─────────|
```

Чіп виконує ~20 конверсій між викликами `update()`, але кеш оновлюється при кожному виклику — завжди містить найсвіжіші дані. Пропущений виклик `update()` не є проблемою.

**Чому не FreeRTOS task (Стратегія B):**
- 50 вимірювань за секунду достатньо для ідентифікації монети (користувач кладе монету і чекає)
- Немає потреби в `spiMutex` при Стратегії A — всі SPI операції в одному контексті
- Менша складність, менший stack ESP32, простіша діагностика

Стратегія B (окремий task @ ~1000 Hz) залишається архітектурно можливою якщо знадобиться real-time відстеження у майбутньому. При переході на Стратегію B — `spiMutex` стає обов'язковим (PLUGIN_CONTRACT.md §1.5).

---

### Детекція присутності монети: ADR-COIN-001

**Проблема (SYS-10):** Архітектура не визначала як firmware розпізнає момент покладання монети на сенсор. Без цього `update()` не знає коли акумулювати вимірювання для ідентифікації, а UI — коли відображати результат.

> ⚠️ **ADR-COIN-001:** Детекція присутності монети реалізується через **dual-threshold hysteresis** на основі RP_DATA відносно `calibrationRpBaseline`. Без окремого FreeRTOS task та крос-плагінних залежностей.
>
> **Рішення:** `DETECT = baseline × 0.85` та `RELEASE = baseline × 0.92`
>
> **Обгрунтування hysteresis (не single threshold):** Сигнал RP має шум ±1–2%. При single threshold монета на межі спричиняє нескінченне мерехтіння між `COIN_PRESENT` / `IDLE_NO_COIN`. Hysteresis gap 7% повністю поглинає noise budget сенсора при стабільній температурі.
>
> **Відхилено: Adaptive rolling baseline (Варіант C):** Монета може лежати на сенсорі хвилини. Rolling average адаптується до `RP_coin` через ~K/2 ітерацій і сприймає монету як новий baseline → помилковий `COIN_REMOVED`. Неприйнятно для нумізматичного use case (монету кладуть і чекають результату).
>
> **Відносний threshold:** Множники 0.85 / 0.92 застосовуються до `calibrationRpBaseline`, а не до абсолютних кодів. Архітектура не залежить від невідомого поки що реального baseline range MIKROE-3240. Обидва параметри виносяться в конфіг (`ldc1101.json`, §7) для верифікації при R5 hardware тесті.

#### Розширений State Machine

```
update() після успішного burst read:
  ├─ calibrationRpBaseline ≤ 100  → skip (calibrate() ще не викликався)
  └─ calibrationRpBaseline > 100  → updateCoinState(rpRaw)

updateCoinState(rpRaw):
  IDLE_NO_COIN:   rpRaw < baseline×0.85  (detectDebounceN=5 підряд) → COIN_PRESENT  [log info]
                  rpRaw ≥ baseline×0.85                              → detectCount = 0
  COIN_PRESENT:   rpRaw > baseline×0.92  (releaseDebounceM=3 підряд) → COIN_REMOVED [log info]
                  rpRaw ≤ baseline×0.92                              → releaseCount = 0
  COIN_REMOVED:   → (transient, 1 цикл update()) → IDLE_NO_COIN
```

**`COIN_REMOVED` — навіщо:** UI отримує явний перехідний сигнал після зняття монети (один цикл `update()`). Система може завершити анімацію або підтвердити збереження результату. Без цього стану момент зняття монети непомітний вищому рівню.

> ⚠️ **Polling contract (M-3):** `COIN_REMOVED` активний рівно **1 цикл `update()` = 20 мс @ 50 Hz**. Споживач (`CoinAnalyzer`, UI task) зобов'язаний опитувати `getCoinState()` з частотою ≥ частоти `update()` (мінімум 50 Hz) щоб гарантовано перехопити цей стан.
>
> | Polling rate споживача | P(побачити `COIN_REMOVED`) |
> |---|---|
> | 50 Hz (= update rate) | 100% ✅ |
> | 25 Hz | ~50% ⚠️ |
> | 10 Hz | ~20% ⚠️ |
>
> Callback альтернатива: `setCoinStateCallback()` (backlog, LA-17).

#### Симуляція: threshold coverage по металах

Відносний RP @ різних відстанях від котушки MIKROE-3240 (fSENSOR ≈ 300 kHz, baseline = B):

| Метал | σ (MS/m) | μr | RP @ 0 мм (% від B) | RP @ 1 мм | RP @ 3 мм | Detect @ 0 мм | Detect @ 3 мм |
|---|---|---|---|---|---|---|---|
| Ag999 | 62 | 1 | **20–30%** | 40–55% | 80–92% | ✅ | ✅ / ⚠️ |
| Ag925 | 25–35 | 1 | **27–43%** | 50–65% | 82–95% | ✅ | ✅ / ⚠️ |
| Cu | 58 | 1 | **22–33%** | 43–57% | 80–92% | ✅ | ✅ |
| Al | 37 | 1 | **33–50%** | 57–70% | 85–95% | ✅ | ⚠️ |
| Ni | 14 | 600 | **47–67%** | 67–80% | 90–97% | ✅ | ⚠️ |
| Сталь | 10 | 1000 | **57–77%** | 73–87% | 92–100% | ✅ | ❌ (≥3 мм) |

> ✅ = RP < 85% від B → впевнена детекція. ⚠️ = RP ≈ 82–90% → поруч із threshold, залежить від реального baseline. ❌ = не детектується (очікувано: система орієнтована на 0–1 мм).

#### Симуляція: debounce latency та immunity @ 50 Hz

| Параметр | N / M | Latency | Immunity | Оцінка |
|---|---|---|---|---|
| `detect_debounce_n` | 3 | 60 мс | ⚠️ ризик від вібрації при кладанні | не рекомендовано |
| **`detect_debounce_n`** | **5** | **100 мс** | **✅ баланс UX / immunity** | **рекомендовано** |
| `detect_debounce_n` | 10 | 200 мс | ✅✅ надійно | відчутна пауза |
| `release_debounce_m` | 2 | 40 мс | ⚠️ ризик false COIN_REMOVED | не рекомендовано |
| **`release_debounce_m`** | **3** | **60 мс** | **✅ стабільний перехід** | **рекомендовано** |
| `release_debounce_m` | 5 | 100 мс | ✅✅ надійно | зайва затримка UX |

---

## 6. SPI шина і конкурентний доступ

### SPI пристрої в CoinTrace

| Пристрій | CS пін |
|---|---|
| LDC1101 | Окремий GPIO (конфігурований) |
| SD карта (SDCardPlugin) | Окремий GPIO |
| Дисплей ST7789V2 | Окремий GPIO |

SPI шина вибирає пристрій через CS піни — всі три можуть бути на одній шині (`VSPI`) з різними CS.

### `spiMutex` — превентивна вимога

При Стратегії A race condition відсутня — всі SPI операції виконуються в одному контексті `update()`. Проте `spiMutex` в `PluginContext` є **обов'язковим** з двох причин:

1. Архітектура вже має `AsyncSensorPlugin` шаблон — перший розробник що запустить async task з SPI отримає race condition без попередження
2. `wireMutex` вже присутній в `PluginContext` за аналогічним принципом

**Правило контракту (PLUGIN_CONTRACT.md §1.5):** плагін що запускає власний FreeRTOS task з доступом до SPI шини **зобов'язаний** брати `spiMutex`. Плагіни без tasks — не беруть.

> ⚠️ **ADR-SPI-001 (SYS-4):** Дисплей ST7789V2 на тій самій VSPI шині. Операції `IDisplayPlugin::draw*()` виконуються **виключно** з контексту основного `loop()` (Strategy A). Якщо display операція коли-небудь викликатиметься з окремого FreeRTOS task — обов'язково брати `spiMutex` до першого SPI виклику. При Strategy A race condition відсутня.

```cpp
// include/PluginContext.h  (PLUGIN_ARCHITECTURE.md)
struct PluginContext {
    TwoWire*            wire;
    SemaphoreHandle_t   wireMutex;
    SPIClass*           spi;
    SemaphoreHandle_t   spiMutex;   // ← обов'язковий для async SPI плагінів
    ConfigManager*      config;
    Logger*             log;
};
```

---

## 7. Конфігураційний файл

```json
// data/plugins/ldc1101.json
{
  "name": "LDC1101",
  "enabled": true,
  "spi_cs_pin": 5,
  "resp_time_bits": 7,
  "rp_set": 38,
  "clkin_freq_hz": 16000000,
  "coin_detect_threshold": 0.85,
  "coin_release_threshold": 0.92,
  "detect_debounce_n": 5,
  "release_debounce_m": 3
}
```

| Параметр | Тип | Default | Опис |
|---|---|---|---|
| `spi_cs_pin` | int | 5 | GPIO для CS. Залежить від плати: M5Cardputer і CoreS3 мають різні піни. Обов'язковий. |
| `resp_time_bits` | uint8 | 7 | RESP_TIME bits[2:0]. Значення 7 = 6144 cycles = мінімальний шум. Допустимо 2–7. |
| `rp_set` | uint8 | 38 (`0x26`) | RP_SET регістр (0x01). MikroE SDK default для MIKROE-3240: RP_MAX=24 kΩ / RP_MIN=1.5 kΩ. Змінювати при `STATUS_NO_OSC`. (ADR-LDC-001) |
| `clkin_freq_hz` | uint32 | 16000000 | Частота зовнішнього CLKIN. Потрібна для розрахунку абсолютної індуктивності (якщо потрібно). |
| `coin_detect_threshold` | float | 0.85 | DETECT factor: `rpRaw < baseline × factor` → COIN_PRESENT. Верифікувати при R5 hardware тесті. (ADR-COIN-001) |
| `coin_release_threshold` | float | 0.92 | RELEASE factor: `rpRaw > baseline × factor` → COIN_REMOVED. Має бути > detect (hysteresis). (ADR-COIN-001) |
| `detect_debounce_n` | uint8 | 5 | Кількість consecutive samples нижче detect threshold для підтвердження COIN_PRESENT (~100 мс @ 50 Hz). |
| `release_debounce_m` | uint8 | 3 | Кількість consecutive samples вище release threshold для підтвердження COIN_REMOVED (~60 мс @ 50 Hz). |

---

## 8. Референсна реалізація

```cpp
// lib/LDC1101Plugin/LDC1101Plugin.h
#pragma once

#include "ISensorPlugin.h"
#include "IDiagnosticPlugin.h"
#include "PluginContext.h"
#include <SPI.h>

class LDC1101Plugin : public ISensorPlugin, public IDiagnosticPlugin {
public:

    // ── Coin detection state (ADR-COIN-001) ──────────────────────────
    enum class CoinState : uint8_t {
        IDLE_NO_COIN,   // RP > release threshold — монети на котушці немає
        COIN_PRESENT,   // RP < detect threshold (debounced) — монета присутня
        COIN_REMOVED    // transient: щойно забрали (1 цикл update() → IDLE_NO_COIN)
    };

private:

    // ── Карта регістрів ──────────────────────────────────────────────
    static const uint8_t REG_RP_SET          = 0x01;
    static const uint8_t REG_TC1             = 0x02;
    static const uint8_t REG_TC2             = 0x03;
    static const uint8_t REG_DIG_CONFIG      = 0x04;
    static const uint8_t REG_ALT_CONFIG      = 0x05;
    static const uint8_t REG_INTB_MODE       = 0x0A;  // не використовується: polling замість interrupt
    static const uint8_t REG_START_CONFIG    = 0x0B;
    static const uint8_t REG_D_CONFIG        = 0x0C;
    static const uint8_t REG_STATUS          = 0x20;
    static const uint8_t REG_RP_DATA_LSB     = 0x21;  // ⚠ завжди читати першим
    static const uint8_t REG_RP_DATA_MSB     = 0x22;
    static const uint8_t REG_L_DATA_LSB      = 0x23;
    static const uint8_t REG_L_DATA_MSB      = 0x24;
    static const uint8_t REG_CHIP_ID         = 0x3F;

    // ── FUNC_MODE ────────────────────────────────────────────────────
    static const uint8_t FUNC_MODE_ACTIVE    = 0x00;
    static const uint8_t FUNC_MODE_SLEEP     = 0x01;

    // ── STATUS bits ──────────────────────────────────────────────────
    static const uint8_t STATUS_NO_OSC       = 0x80;
    static const uint8_t STATUS_DRDYB        = 0x40;  // 0 = ready (інверт!)
    static const uint8_t STATUS_POR_READ     = 0x01;  // 1 = POR відбувся (очищується читанням STATUS)

    // ── Стан плагіна ─────────────────────────────────────────────────
    PluginContext* ctx     = nullptr;
    bool           ready   = false;
    bool           enabled = false;
    int            csPin   = -1;

    // ── Конфігурація ─────────────────────────────────────────────────
    uint8_t  respTimeBits = 0x07;
    uint8_t  rpSetValue   = 0x26;  // MikroE SDK default: RP_MAX=24kΩ/RP_MIN=1.5kΩ (ADR-LDC-001)
    uint32_t clkinFreqHz  = 16000000UL;

    // ── Кеш вимірювань (захищений dataMutex) ─────────────────────────
    SemaphoreHandle_t dataMutex = nullptr;

    struct MeasurementCache {
        uint16_t rpRaw     = 0;
        uint16_t lRaw      = 0;
        uint32_t timestamp = 0;
        bool     valid     = false;
    } cached;

    // ── Статистика ───────────────────────────────────────────────────
    struct {
        uint32_t     totalReads   = 0;
        uint32_t     failedReads  = 0;
        uint32_t     staleCount   = 0;  // consecutive DRDYB=1 calls without new data (LA-7)
        uint32_t     lastSuccess  = 0;
        HealthStatus status       = HealthStatus::UNKNOWN;
        ErrorCode    lastError    = {0, "No error"};
    } diag;

    float    calibrationRpBaseline = 0.0f;
    uint32_t lastCalibrationTime   = 0;    // ⚑ Reserved: NVS age-check при завантаженні baseline (M-2)
    bool     staleFlag             = false; // Атомарний stale-індикатор: set/clear в update(), read в getHealthStatus() без mutex (M-1)

    // ── Coin detection config (ADR-COIN-001) ─────────────────────────
    float   coinDetectThreshold  = 0.85f;  // DETECT:  RP < baseline × factor
    float   coinReleaseThreshold = 0.92f;  // RELEASE: RP > baseline × factor (hysteresis gap 7%)
    uint8_t detectDebounceN      = 5;      // consecutive samples below detect  → COIN_PRESENT
    uint8_t releaseDebounceM     = 3;      // consecutive samples above release → COIN_REMOVED

    struct {
        CoinState state        = CoinState::IDLE_NO_COIN;
        uint8_t   detectCount  = 0;   // consecutive below detect threshold
        uint8_t   releaseCount = 0;   // consecutive above release threshold
    } coin;

public:

    // ── Метадані ─────────────────────────────────────────────────────

    const char* getName()    const override { return "LDC1101"; }
    const char* getVersion() const override { return "1.0.0"; }
    const char* getAuthor()  const override { return "CoinTrace Team"; }
    SensorType  getType()    const override { return SensorType::INDUCTIVE; }

    SensorMetadata getMetadata() const override {
        return {
            .typeName   = "Inductive Sensor (SPI)",
            .unit       = "RP_code",
            // value1 = raw RP code (0–65535): відображає провідність металу
            // value2 = raw L_DATA code: відображає магнітну проникність.
            //          Конвертація в мкГн: fSENSOR = (fCLKIN * RESP_TIME) / (3 * L_DATA)
            //          L = 1 / (4π² * fSENSOR² * C_SENSOR)
            .minValue   = 0.0f,
            .maxValue   = 65535.0f,
            .resolution = 1.0f,
            .sampleRate = 50
        };
    }

    // ── Lifecycle ────────────────────────────────────────────────────

    bool canInitialize() override {
        // CS пін і наявність ctx->spi перевіряється в initialize()
        return true;
    }

    bool initialize(PluginContext* context) override {
        ctx = context;

        // Зчитати конфігурацію
        if (ctx->config) {
            csPin         = ctx->config->getInt("ldc1101.spi_cs_pin", 5);
            respTimeBits  = ctx->config->getUInt8("ldc1101.resp_time_bits", 0x07);
            rpSetValue    = ctx->config->getUInt8("ldc1101.rp_set", 0x26);  // ADR-LDC-001
            clkinFreqHz          = ctx->config->getUInt32("ldc1101.clkin_freq_hz", 16000000UL);
            coinDetectThreshold  = ctx->config->getFloat("ldc1101.coin_detect_threshold",  0.85f);
            coinReleaseThreshold = ctx->config->getFloat("ldc1101.coin_release_threshold", 0.92f);
            detectDebounceN      = ctx->config->getUInt8("ldc1101.detect_debounce_n",      5);
            releaseDebounceM     = ctx->config->getUInt8("ldc1101.release_debounce_m",     3);
        } else {
            csPin = 5;
            ctx->log->warning(getName(), "No config — using default CS pin 5");
        }

        if (csPin < 0 || !ctx->spi) {
            diag.lastError = {1, "CS pin or SPI bus not available"};
            ctx->log->error(getName(), diag.lastError.message);
            return false;
        }

        pinMode(csPin, OUTPUT);
        digitalWrite(csPin, HIGH);  // CS неактивний за замовчуванням

        // Чекаємо завершення Power-On Reset
        delay(5);
        uint32_t t = millis();
        while (spiRead(REG_STATUS) & STATUS_POR_READ) {
            if (millis() - t > 100) {
                diag.status    = HealthStatus::TIMEOUT;
                diag.lastError = {3, "POR timeout: chip not ready after 100ms"};
                ctx->log->error(getName(), diag.lastError.message);
                return false;
            }
            delay(1);
        }

        // Верифікація CHIP_ID після POR (до завершення POR може повернути 0xFF)
        uint8_t chipId = spiRead(REG_CHIP_ID);
        if (chipId != 0xD4) {
            char msg[56];
            snprintf(msg, sizeof(msg), "CHIP_ID mismatch: expected 0xD4, got 0x%02X", chipId);
            diag.status    = HealthStatus::SENSOR_FAULT;
            diag.lastError = {2, msg};
            ctx->log->error(getName(), msg);
            return false;
        }

        // Mutex для захисту кешу вимірювань
        dataMutex = xSemaphoreCreateMutex();
        if (!dataMutex) {
            diag.lastError = {4, "Failed to create data mutex"};
            ctx->log->error(getName(), diag.lastError.message);
            return false;
        }

        // Конфігурація сенсора (тільки в Sleep mode)
        if (!configureSensor()) {
            diag.status    = HealthStatus::INITIALIZATION_FAILED;
            diag.lastError = {5, "Sensor configuration failed"};
            ctx->log->error(getName(), diag.lastError.message);
            return false;
        }

        // Перехід в Active mode — запускає безперервні конверсії
        spiWrite(REG_START_CONFIG, FUNC_MODE_ACTIVE);

        ready          = true;
        enabled        = true;
        diag.status    = HealthStatus::OK;
        diag.lastError = {0, "No error"};
        ctx->log->info(getName(), "Ready. CS=%d, RESP_TIME=0x%02X, RP_SET=0x%02X",
                       csPin, respTimeBits, rpSetValue);
        return true;
    }

    // ── Update ───────────────────────────────────────────────────────
    // Контракт: max 10 мс (PLUGIN_CONTRACT.md §1.2). Фактично: ~2 SPI транзакції = < 0.1 мс.

    void update() override {
        if (!ready) return;

        // Stale detection (M-1): set atomically тут — читається в getHealthStatus() без mutex
        if (cached.valid && (millis() - cached.timestamp > 5000)) {
            staleFlag = true;
        }

        uint8_t status = spiRead(REG_STATUS);

        if (status & STATUS_NO_OSC) {
            diag.failedReads++;
            diag.status    = HealthStatus::NOT_FOUND;
            diag.lastError = {6, "Coil not oscillating — check wiring or RP_SET"};
            ctx->log->warning(getName(), diag.lastError.message);
            return;
        }

        if (status & STATUS_DRDYB) {
            // Conversion still in progress — normal. Track consecutive stale calls (LA-7).
            if (++diag.staleCount > 10) {
                ctx->log->warning(getName(),
                    "DRDYB=1 for %lu consecutive update() calls — conversion frozen?",
                    (unsigned long)diag.staleCount);
            }
            return;
        }

        uint16_t rpRaw, lRaw;
        if (!readMeasurementBurst(rpRaw, lRaw)) {
            diag.failedReads++;
            diag.status    = HealthStatus::COMMUNICATION_ERROR;
            diag.lastError = {7, "SPI burst read failed"};
            return;
        }

        if (rpRaw == 0 || rpRaw == 0xFFFF) {
            diag.failedReads++;
            diag.status    = HealthStatus::SENSOR_FAULT;
            diag.lastError = {8, "RP_DATA out of range (0x0000 or 0xFFFF)"};
            return;
        }

        // Оновлення кешу — timeout замість portMAX_DELAY (PLUGIN_CONTRACT.md §1.3)
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
            diag.failedReads++;  // mutex timeout — пропустити цикл, не блокувати
            return;
        }
        cached.rpRaw     = rpRaw;
        cached.lRaw      = lRaw;
        cached.timestamp = millis();
        cached.valid     = true;
        xSemaphoreGive(dataMutex);

        staleFlag        = false;  // M-1: нові дані отримано — скидаємо
        diag.staleCount  = 0;
        diag.totalReads++;
        diag.lastSuccess = millis();
        diag.status      = HealthStatus::OK;

        // ── Coin detection: dual-threshold hysteresis (ADR-COIN-001) ─
        // Guard: пропустити поки baseline не відкалібровано (calibrate() не викликався)
        if (calibrationRpBaseline > 100.0f) {
            updateCoinState(rpRaw);
        }
    }

    // ── Read ─────────────────────────────────────────────────────────
    // Контракт: max 5 мс (PLUGIN_CONTRACT.md §1.2). Фактично: mutex acquire + struct copy = < 0.1 мс.

    SensorData read() override {
        if (!ready || !dataMutex) {
            return {0, 0, 0.0f, millis(), false};
        }

        // timeout замість portMAX_DELAY (PLUGIN_CONTRACT.md §1.3)
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(3)) != pdTRUE) {
            return {0, 0, 0.0f, millis(), false};
        }
        SensorData result = {
            .value1     = static_cast<float>(cached.rpRaw),
            .value2     = static_cast<float>(cached.lRaw),
            .confidence = cached.valid ? 0.95f : 0.0f,
            .timestamp  = cached.timestamp,
            .valid      = cached.valid
        };
        xSemaphoreGive(dataMutex);
        return result;
    }

    // ── Shutdown ─────────────────────────────────────────────────────

    void shutdown() override {
        if (!ctx) return;

        spiWrite(REG_START_CONFIG, FUNC_MODE_SLEEP);  // Sleep зберігає конфіг

        if (dataMutex) {
            vSemaphoreDelete(dataMutex);
            dataMutex = nullptr;
        }
        ready   = false;
        enabled = false;
        ctx->log->info(getName(), "Shutdown complete");
    }

    // ── Health ───────────────────────────────────────────────────────

    bool isEnabled() const override { return enabled; }
    bool isReady()   const override { return ready; }

    // ── Coin detection state (ADR-COIN-001) ──────────────────────────
    CoinState getCoinState()  const { return coin.state; }
    bool      isCoinPresent() const { return coin.state == CoinState::COIN_PRESENT; }

    HealthStatus getHealthStatus() const override {
        if (!enabled) return HealthStatus::DISABLED;
        if (!ready)   return HealthStatus::INITIALIZATION_FAILED;

        // M-1: staleFlag оновлюється в update() — читаємо без mutex (bool = 1 байт, атомарний на Xtensa LX7)
        if (staleFlag) return HealthStatus::TIMEOUT;

        // Поріг > 10 щоб виявити ранні систематичні відмови
        if (diag.totalReads > 10) {
            float failRate = static_cast<float>(diag.failedReads) / diag.totalReads;
            if (failRate > 0.5f) return HealthStatus::DEGRADED;
            if (failRate > 0.1f) return HealthStatus::OK_WITH_WARNINGS;
        }

        return diag.status;
    }

    ErrorCode getLastError() const override { return diag.lastError; }

    // ── Calibration ──────────────────────────────────────────────────
    // ⚠ Увага: calibrate() використовує delay() — викликати поза update() циклом.

    bool calibrate() override {
        if (!ready) return false;
        ctx->log->info(getName(), "Calibration start — remove coin from sensor");
        delay(2000);

        float    sum = 0;
        uint32_t ok  = 0;
        for (int i = 0; i < 20; i++) {
            delay(convTimeMs() + 5);
            uint16_t rp, l;
            if (readMeasurementBurst(rp, l) && rp > 0 && rp < 65535) {
                sum += rp;
                ok++;
            }
        }

        if (ok < 10) {
            ctx->log->error(getName(), "Calibration failed: only %d valid readings", ok);
            return false;
        }

        calibrationRpBaseline = sum / ok;
        lastCalibrationTime   = millis();
        ctx->log->info(getName(), "Calibration OK. Baseline RP=%.0f (%d samples)",
                       calibrationRpBaseline, ok);
        return true;
    }

    // ── Diagnostics ──────────────────────────────────────────────────
    // ⚠ Увага: runSelfTest() використовує delay() — викликати поза update() циклом.

    bool runSelfTest() override {
        ctx->log->info(getName(), "Self-test start");

        uint8_t chipId = spiRead(REG_CHIP_ID);
        if (chipId != 0xD4) {
            char msg[48];
            snprintf(msg, sizeof(msg), "CHIP_ID: expected 0xD4, got 0x%02X", chipId);
            ctx->log->error(getName(), msg);
            return false;
        }
        ctx->log->info(getName(), "  CHIP_ID OK (0xD4)");

        uint8_t status = spiRead(REG_STATUS);
        if (status & STATUS_NO_OSC) {
            ctx->log->error(getName(), "  FAIL: NO_SENSOR_OSC — coil not oscillating");
            return false;
        }
        if (status & STATUS_POR_READ) {
            ctx->log->error(getName(), "  FAIL: POR_READ — chip still in reset");
            return false;
        }
        ctx->log->info(getName(), "  STATUS OK");

        // Тест стабільності: 5 послідовних вимірювань
        float readings[5] = {};
        for (int i = 0; i < 5; i++) {
            delay(convTimeMs() + 2);
            uint16_t rp, l;
            if (!readMeasurementBurst(rp, l)) {
                ctx->log->error(getName(), "  FAIL: SPI read error on sample %d", i);
                return false;
            }
            readings[i] = static_cast<float>(rp);
        }

        float mean = 0;
        for (float r : readings) mean += r;
        mean /= 5;

        float maxDev = 0;
        for (float r : readings) maxDev = max(maxDev, fabsf(r - mean));

        if (mean > 0 && maxDev > mean * 0.1f) {
            ctx->log->warning(getName(), "  Stability WARNING: maxDev=%.1f (%.1f%% of mean)",
                              maxDev, 100.f * maxDev / mean);
        } else {
            ctx->log->info(getName(), "  Stability OK (maxDev=%.1f)", maxDev);
        }

        ctx->log->info(getName(), "Self-test PASSED");
        return true;
    }

    DiagnosticResult runDiagnostics() override {
        DiagnosticResult result;
        result.timestamp = millis();

        if (!checkHardwarePresence()) {
            result.status = HealthStatus::NOT_FOUND;
            result.error  = {1, "CHIP_ID mismatch — SPI or CS issue"};
            return result;
        }
        if (!checkCalibration()) {
            result.status = HealthStatus::CALIBRATION_NEEDED;
            result.error  = {5, "Calibration baseline not set or out of range"};
            return result;
        }

        // Перевірка fail rate — консистентна з getHealthStatus()
        if (diag.totalReads > 10) {
            float failRate = static_cast<float>(diag.failedReads) / diag.totalReads;
            if (failRate > 0.5f) {
                result.status = HealthStatus::DEGRADED;
                result.error  = {9, "High fail rate (>50%)"};
                result.stats.totalReads  = diag.totalReads;
                result.stats.failedReads = diag.failedReads;
                result.stats.successRate = static_cast<uint16_t>(
                    100 - diag.failedReads * 100 / diag.totalReads);
                result.stats.lastSuccess = diag.lastSuccess;
                return result;
            }
        }

        result.status            = HealthStatus::OK;
        result.error             = {0, "All checks passed"};
        result.stats.totalReads  = diag.totalReads;
        result.stats.failedReads = diag.failedReads;
        result.stats.successRate = diag.totalReads > 0
            ? static_cast<uint16_t>(100 - diag.failedReads * 100 / diag.totalReads)
            : 100;
        result.stats.lastSuccess = diag.lastSuccess;
        return result;
    }

    DiagnosticResult getStatistics() const override {
        DiagnosticResult r;
        r.status            = diag.status;
        r.error             = diag.lastError;
        r.timestamp         = millis();
        r.stats.totalReads  = diag.totalReads;
        r.stats.failedReads = diag.failedReads;
        r.stats.successRate = diag.totalReads > 0
            ? static_cast<uint16_t>(100 - diag.failedReads * 100 / diag.totalReads)
            : 100;
        r.stats.lastSuccess = diag.lastSuccess;
        return r;
    }

    // ⚠ Strategy A only: SPI без spiMutex. НЕ викликати з async task.
    bool checkHardwarePresence() override {
        return spiRead(REG_CHIP_ID) == 0xD4;
    }

    // ⚠ Strategy A only: SPI без spiMutex. НЕ викликати з async task.
    bool checkCommunication() override {
        for (int i = 0; i < 3; i++) {
            if (spiRead(REG_CHIP_ID) != 0xD4) return false;
        }
        return true;
    }

    bool checkCalibration() override {
        return calibrationRpBaseline > 100.0f && calibrationRpBaseline < 60000.0f;
    }

private:

    // ── Coin state machine (ADR-COIN-001) ─────────────────────────────
    // Викликається з update() після кожного успішного вимірювання.
    // Dual-threshold hysteresis запобігає boundary oscillation при RP поруч із threshold.

    void updateCoinState(uint16_t rpRaw) {
        const float rp           = static_cast<float>(rpRaw);
        const float detectLimit  = calibrationRpBaseline * coinDetectThreshold;
        const float releaseLimit = calibrationRpBaseline * coinReleaseThreshold;

        switch (coin.state) {
            case CoinState::IDLE_NO_COIN:
                if (rp < detectLimit) {
                    if (++coin.detectCount >= detectDebounceN) {
                        coin.detectCount  = 0;
                        coin.releaseCount = 0;
                        coin.state        = CoinState::COIN_PRESENT;
                        ctx->log->info(getName(),
                            "Coin PRESENT (RP=%.0f, baseline=%.0f, ratio=%.2f)",
                            rp, calibrationRpBaseline, rp / calibrationRpBaseline);
                    }
                } else {
                    coin.detectCount = 0;  // non-consecutive — reset counter
                }
                break;

            case CoinState::COIN_PRESENT:
                if (rp > releaseLimit) {
                    if (++coin.releaseCount >= releaseDebounceM) {
                        coin.releaseCount = 0;
                        coin.detectCount  = 0;
                        coin.state        = CoinState::COIN_REMOVED;
                        ctx->log->info(getName(),
                            "Coin REMOVED (RP=%.0f, baseline=%.0f, ratio=%.2f)",
                            rp, calibrationRpBaseline, rp / calibrationRpBaseline);
                    }
                } else {
                    coin.releaseCount = 0;  // non-consecutive — reset counter
                }
                break;

            case CoinState::COIN_REMOVED:
                // Transient: одноцикловий сигнал для UI після зняття монети
                coin.state = CoinState::IDLE_NO_COIN;
                break;
        }
    }

    // ── Конфігурація сенсора (викликається тільки в Sleep mode) ──────

    bool configureSensor() {
        spiWrite(REG_START_CONFIG, FUNC_MODE_SLEEP);
        delay(2);

        // RP_SET: ADR-LDC-001 — default 0x26 (RP_MAX=24kΩ/RP_MIN=1.5kΩ, MikroE SDK MIKROE-3240).
        // Змінювати якщо STATUS_NO_OSC через «rp_set» в конфігурації.
        spiWrite(REG_RP_SET, rpSetValue);

        // TC1/TC2 — PCB compensation для MIKROE-3240 (MikroE SDK ldc1101.c, 2026-03-13):
        // TC1=0x1F: C1=0.75pF, R1=21.1kΩ (τ₁=15.8ns); TC2=0x3F: C2=3pF, R2=30.5kΩ (τ₂=91.5ns)
        spiWrite(REG_TC1, 0x1F);
        spiWrite(REG_TC2, 0x3F);

        // Verify TC1/TC2 writes (LA-5: consistent readback policy with RP_SET and DIG_CFG)
        uint8_t tc1ReadBack = spiRead(REG_TC1);
        if (tc1ReadBack != 0x1F) {
            char msg[60];
            snprintf(msg, sizeof(msg), "TC1 verify failed: wrote 0x1F, read 0x%02X", tc1ReadBack);
            ctx->log->error(getName(), msg);
            return false;
        }
        uint8_t tc2ReadBack = spiRead(REG_TC2);
        if (tc2ReadBack != 0x3F) {
            char msg[60];
            snprintf(msg, sizeof(msg), "TC2 verify failed: wrote 0x3F, read 0x%02X", tc2ReadBack);
            ctx->log->error(getName(), msg);
            return false;
        }

        // Verify RP_SET write — критично: неправильний RP_SET → STATUS_NO_OSC без діагнозу
        uint8_t rpReadBack = spiRead(REG_RP_SET);
        if (rpReadBack != rpSetValue) {
            char msg[60];
            snprintf(msg, sizeof(msg),
                     "RP_SET verify failed: wrote 0x%02X, read 0x%02X", rpSetValue, rpReadBack);
            ctx->log->error(getName(), msg);
            return false;
        }

        // DIG_CONFIG: MIN_FREQ=0xD0 (118 kHz, MikroE SDK) + RESP_TIME bits[2:0]
        // 0x00 (500 kHz threshold) спричиняє false halt при fSENSOR < 500 kHz (небезпечно для MIKROE-3240)
        uint8_t digCfg = 0xD0 | (respTimeBits & 0x07);
        spiWrite(REG_DIG_CONFIG, digCfg);

        // Verify DIG_CONFIG write (повний байт: MIN_FREQ[7:4] + RESP_TIME[2:0])
        uint8_t digReadBack = spiRead(REG_DIG_CONFIG);
        if (digReadBack != digCfg) {  // Fix LA-1: was (digReadBack & 0x07) — 3-bit mask
            char msg[60];             //   never equalled digCfg ∈ [0xD0,0xD7] → always false!
            snprintf(msg, sizeof(msg),
                     "DIG_CONFIG verify failed: wrote 0x%02X, read 0x%02X", digCfg, digReadBack);
            ctx->log->error(getName(), msg);
            return false;
        }

        spiWrite(REG_D_CONFIG,   0x00);  // DOK_REPORT = 0: вимагати регуляцію амплітуди
        spiWrite(REG_ALT_CONFIG, 0x00);  // RP і L вимірювання активні (LOPTIMAL = 0)

        ctx->log->info(getName(), "configureSensor: DIG_CONFIG=0x%02X, RP_SET=0x%02X",
                       digCfg, rpSetValue);
        return true;
        // Перехід в ACTIVE mode — в initialize() після повернення true
    }

    // ── Реконфігурація без повної ре-ініціалізації ────────────────────
    // Дозволяє змінити RESP_TIME або RP_SET в рантаймі.
    // ⚠️ Strategy A only — без mutex захисту (I-2).
    //    Виклик з async task потребує: spiMutex.take → SLEEP → configure → ACTIVE → spiMutex.give

    bool reconfigureSensor() {
        spiWrite(REG_START_CONFIG, FUNC_MODE_SLEEP);
        delay(2);
        bool ok = configureSensor();
        spiWrite(REG_START_CONFIG, FUNC_MODE_ACTIVE);
        return ok;
    }

    // ── Burst читання RP + L (одна SPI транзакція) ────────────────────

    bool readMeasurementBurst(uint16_t& rpRaw, uint16_t& lRaw) {
        if (!ctx || !ctx->spi || csPin < 0) return false;

        uint8_t buf[4] = {};
        ctx->spi->beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
        digitalWrite(csPin, LOW);
        ctx->spi->transfer(REG_RP_DATA_LSB | 0x80);  // Read bit | 0x21
        buf[0] = ctx->spi->transfer(0x00);            // 0x21 RP_DATA_LSB — першим!
        buf[1] = ctx->spi->transfer(0x00);            // 0x22 RP_DATA_MSB
        buf[2] = ctx->spi->transfer(0x00);            // 0x23 L_DATA_LSB
        buf[3] = ctx->spi->transfer(0x00);            // 0x24 L_DATA_MSB
        digitalWrite(csPin, HIGH);
        ctx->spi->endTransaction();

        rpRaw = (static_cast<uint16_t>(buf[1]) << 8) | buf[0];
        lRaw  = (static_cast<uint16_t>(buf[3]) << 8) | buf[2];
        return true;
    }

    // ── Одиночне читання регістра ─────────────────────────────────────

    uint8_t spiRead(uint8_t reg) {
        if (!ctx || !ctx->spi || csPin < 0) return 0xFF;
        uint8_t result;
        ctx->spi->beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
        digitalWrite(csPin, LOW);
        ctx->spi->transfer(reg | 0x80);
        result = ctx->spi->transfer(0x00);
        digitalWrite(csPin, HIGH);
        ctx->spi->endTransaction();
        return result;
    }

    // ── Запис регістра ────────────────────────────────────────────────

    void spiWrite(uint8_t reg, uint8_t value) {
        if (!ctx || !ctx->spi || csPin < 0) return;
        ctx->spi->beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
        digitalWrite(csPin, LOW);
        ctx->spi->transfer(reg & 0x7F);
        ctx->spi->transfer(value);
        digitalWrite(csPin, HIGH);
        ctx->spi->endTransaction();
    }

    // ── Розрахунок часу конверсії (мс) ───────────────────────────────
    // Формула: ceiling(cycles / 500kHz) + 2 мс margin. LA-9.
    // ВАЖЛИВО: 500 kHz у знаменнику дає МЕНШИЙ час ніж нижчий реальний fSENSOR.
    // Функція безпечна завдяки +2 мс margin (перекриває різницю при fSENSOR ≥ 200 kHz). (L-1)
    // Edge case: при fSENSOR=118 kHz (мін. MIN_FREQ) реальний час 17.4 мс > формула 15 мс;
    //   для MIKROE-3240 fSENSOR ≈ 200–300 kHz — формула достатньо консервативна.
    // Значення для Reserved індексів 0,1 встановлені як 192 — safe fallback.

    uint32_t convTimeMs() const {
        static const uint32_t cycles[] = {192, 192, 192, 384, 768, 1536, 3072, 6144};
        //                                 ^0   ^1: Reserved → fallback до мінімуму
        uint8_t idx = respTimeBits & 0x07;
        return (cycles[idx] + 499) / 500 + 2;  // ceiling(cycles / 500kHz) + 2 мс margin
    }
};
```

---

## 9. Часові характеристики і точність

### Ефективна частота при різних котушках

| Котушка fSENSOR | RESP_TIME bits | Час конверсії | Частота update() | Результат |
|---|---|---|---|---|
| 2 MHz | `0x07` (6144) | 1.024 мс | 50 Hz | 50 вимір/с, 25 за 0.5 с |
| 2 MHz | `0x02` (192) | 0.032 мс | 50 Hz | 50 вимір/с, без різниці |
| 500 kHz | `0x07` (6144) | 4.096 мс | 50 Hz | 50 вимір/с, конверсія < 20 мс |
| 500 kHz | `0x07` (6144) | 4.096 мс | 200 Hz | ~200 вимір/с (при вищому update rate) |

**Висновок:** При котушках ≥ 1 MHz і `update()` @ 50 Hz — вибір RESP_TIME не впливає на ефективну частоту. Використовувати `0x07` (максимальна точність, мінімальний шум). Компроміс між точністю і швидкістю актуальний тільки для котушок < 500 kHz.

### Рекомендоване значення RESP_TIME

**`0x07` (6144 cycles)** — стандартне значення для нумізматики. Використовувати для всіх котушок 1–5 MHz. Змінювати тільки якщо котушка < 500 kHz і потрібна вища частота вимірювань.

---

## 10. Відповідність архітектурним контрактам

### PLUGIN_CONTRACT.md — часові обмеження

| Метод | Ліміт контракту | Фактичний час | Запас |
|---|---|---|---|
| `canInitialize()` | — | ~0 мкс | — |
| `initialize()` | — | < 200 мс (POR + config) | Викликається один раз |
| `update()` | max 10 мс (§1.2) | < 0.5 мс (2 SPI reads) | 20× запас |
| `read()` | max 5 мс (§1.2) | < 0.1 мс (mutex + copy) | 50× запас |
| `shutdown()` | — | < 1 мс | — |

### PLUGIN_CONTRACT.md — загальні правила

| Правило | Посилання | Статус |
|---|---|---|
| `SPI.begin()` не викликається — шина ініціалізована системою | §1.3 | ✅ |
| `portMAX_DELAY` заборонено — всі mutex з timeout | §1.3 | ✅ `pdMS_TO_TICKS(5)` / `pdMS_TO_TICKS(3)` |
| Конфігурація через `ctx->config`, не хардкод | §1.3 | ✅ |
| Логування через `ctx->log`, не Serial | §1.3 | ✅ |
| CS пін конфігурований через `ldc1101.json` | §1.3 | ✅ |
| `spiMutex` береться при async task доступі | §1.5 | ✅ Стратегія A не потребує; задокументовано |
| `checkHardwarePresence()` / `checkCommunication()` — тільки Strategy A | §1.5 | ✅ Коментар у коді |

### Відкриті задачі (не блокуючі для v1)

| # | Задача | Файл |
|---|---|---|
| 1 | Задокументувати `value2` = raw L_DATA code (з формулою конвертації) | `PLUGIN_INTERFACES_EXTENDED.md` |
| 2 | Створити `data/plugins/ldc1101.json` з production значеннями | Новий файл |

---

*Документ підготовлено для архітектора та імплементора `LDC1101Plugin`.*  
*Версія: 1.0.0 | Дата: 10 березня 2026*  
*Верифіковано по: TI LDC1101 Datasheet SNOSD01D – May 2015 – Revised October 2016*
