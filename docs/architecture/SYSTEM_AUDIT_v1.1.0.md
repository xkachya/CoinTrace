# CoinTrace — Незалежний Системний Архітектурний Аудит

**Тип документа:** Незалежний архітектурний аудит повної системи  
**Версія аудиту:** 1.1.0  
**Дата:** 13 березня 2026 (оновлено: аналіз офіційного MikroE SDK ldc1101.c)  
**Платформа:** ESP32-S3FN8 / M5Stack Cardputer-ADV + LDC1101 Click Board (MIKROE-3240)  
**Аудитовані документи:** 14 архітектурних документів, 4 зовнішні рецензії, 1 концептуальний документ + MikroE SDK (ldc1101.c / ldc1101.h)  
**Метод:** Повний наскрізний аналіз + 18 симуляцій та моделювань на реальних параметрах + аналіз еталонного коду  
**Роль аудитора:** Незалежний embedded-архітектор (позиція: бачу проект вперше)

---

## Джерела та методологія

### Аудитовані внутрішні документи

| # | Файл | Версія |
|--|--|--|
| 1 | `docs/architecture/LDC1101_ARCHITECTURE.md` | v1.0.0 |
| 2 | `docs/architecture/PLUGIN_ARCHITECTURE.md` | v2.0.0 |
| 3 | `docs/architecture/PLUGIN_CONTRACT.md` | v1.0.0 |
| 4 | `docs/architecture/PLUGIN_INTERFACES_EXTENDED.md` | v1.1.0 |
| 5 | `docs/architecture/PLUGIN_DIAGNOSTICS.md` | v1.0.0 |
| 6 | `docs/architecture/CREATING_PLUGINS.md` | — |
| 7 | `docs/architecture/COMPARISON.md` | — |
| 8 | `docs/architecture/FINGERPRINT_DB_ARCHITECTURE.md` | v1.5.1 |
| 9 | `docs/architecture/STORAGE_ARCHITECTURE.md` | v1.5.0 |
| 10 | `docs/architecture/CONNECTIVITY_ARCHITECTURE.md` | v1.4.0 |
| 11 | `docs/architecture/LOGGER_ARCHITECTURE.md` | v2.1.1 |
| 12 | `docs/architecture/PLUGIN_AUDIT_v3.0.0.md` | v3.0.0 |
| 13 | `docs/architecture/STORAGE_AUDIT_v1.3.0.md` | v1.3.0 |
| 14 | `docs/architecture/FINGERPRINT_DB_AUDIT_v1.4.0.md` | v1.4.0 |
| 15 | `docs/concept/COLLECTOR_USE_CASE.md` | v1.3.0 |

### Зовнішні рецензії (не в git — docs/external/)

| # | Документ |
|--|--|
| 1 | `CoinTrace_Architecture_Review_v3.md` |
| 2 | `PLUGIN_ARCHITECTURE_AUDIT_v4_2026-03-13.md` |
| 3 | `CoinTrace_PreImplementation_Analysis_v3.md` |
| 4 | `LDC1101_Architecture_Analysis_v3.0.0.md` |

### Зовнішні технічні джерела (параметри котушки MIKROE-3240)

**MikroE LDC1101 Click SDK (офіційна бібліотека):**
- Репозиторій: `https://github.com/MikroElektronika/mikrosdk_click_v2`
- Файли: `clicks/ldc1101/lib_ldc1101/src/ldc1101.c`, `include/ldc1101.h`
- Отримано: 13 березня 2026
- Використано для: TC1/TC2 значення, RP_SET робоча конфігурація, DIG_CFG (RESP_TIME=768, MIN_FREQ=118 kHz), виявлено та підтверджено Bug SYS-1 (MSB читається до LSB)

**MikroE LDC1101 Click — Product Page:**
- URL: `https://www.mikroe.com/ldc1101-click` (MIKROE-3240)
- Отримано: 13 березня 2026
- Використано для: Розмір плати (42.9 × 25.4 мм, M-size), pinout, підтвердження CLKIN на PWM пін

**Texas Instruments LDC1101 Datasheet:**
- Документ: SNOSD01D (посилання в архітектурних документах проекту)
- Використано для: Регістрова карта, формули ConversionTime та fSENSOR, протокол LSB→MSB читання, CHIP_ID = 0xD4

### Методологія симуляцій

| Симуляція | Метод | Джерело констант |
|--|--|--|
| Skin depth δ(f) | Аналітична формула `δ = √(ρ/πfμ)` | Довідкові σ, μr металів |
| fSENSOR оцінка | LC-формула `f = 1/(2π√LC)`, PCB coil типові L~3–20 µH, C~33–100 pF | MikroE SDK DIG_CFG → MIN_FREQ=118kHz як нижня межа |
| TC1/TC2 значення | Напряму з `ldc1101_default_cfg()` work-конфігурації (ldc1101.c) | MikroE SDK (отримано 2026-03-13) |
| ConversionTime | `T = RESP_TIME_cycles / (3 × fSENSOR)` | TI LDC1101 datasheet SNOSD01D |
| Timing budget SPI | `t = N_bits / SPI_CLK` | ESP32-S3 SPI spec |
| Memory budget | Статичний аналіз sizeof() + FreeRTOS overhead | ESP-IDF heap analysis |
| Flash endurance | W25Q64 spec 100K cycles/sector + wear leveling model | Winbond W25Q64 datasheet |
| Класифікаційний простір | Якісна оцінка на основі σ/μr металів | Матеріалознавчі довідники |

---

## Зміст

1. [Executive Summary](#1-executive-summary)
2. [Карта системи та архітектурний стек](#2-карта-системи-та-архітектурний-стек)
3. [Центральний елемент: LDC1101 — детальний аналіз та симуляції](#3-центральний-елемент-ldc1101--детальний-аналіз-та-симуляції)
   - 3.1 Фізична модель вимірювання
   - 3.2 Симуляція: Skin-depth та RP для монетних металів
   - 3.3 Симуляція: Класифікаційний простір (RP–L)
   - 3.4 Симуляція: Часовий бюджет конверсії
   - 3.5 Симуляція: Вплив температури на RP та L
   - 3.6 Симуляція: Деградація сигналу при зміщенні монети
   - 3.7 Аналіз котушки: критичні незакриті питання
   - 3.8 Критичні знахідки LDC1101
4. [Plugin System: аналіз архітектури](#4-plugin-system-аналіз-архітектури)
   - 4.1 Lifecycle та контракт
   - 4.2 Симуляція: Timing budget update()
   - 4.3 Симуляція: Memory budget
   - 4.4 Симуляція: SPI bus contention
5. [Storage Architecture: аналіз](#5-storage-architecture-аналіз)
   - 5.1 Симуляція: Flash endurance
   - 5.2 Симуляція: Boot state machine
6. [Fingerprint Database: аналіз алгоритму](#6-fingerprint-database-аналіз-алгоритму)
   - 6.1 Симуляція: Класифікаційна точність в 5D просторі
   - 6.2 Аналіз метрики відстані
7. [Connectivity Architecture: аналіз](#7-connectivity-architecture-аналіз)
8. [Logger Architecture: аналіз](#8-logger-architecture-аналіз)
9. [Крос-архітектурний аналіз: зв'язки та розриви](#9-крос-архітектурний-аналіз-зв'язки-та-розриви)
10. [Непокриті архітектурні домени](#10-непокриті-архітектурні-домени)
11. [Зведена таблиця знахідок](#11-зведена-таблиця-знахідок)
12. [Рівень готовності до імплементації](#12-рівень-готовності-до-імплементації)
13. [Пріоритетні рекомендації](#13-пріоритетні-рекомендації)

---

## 1. Executive Summary

CoinTrace — це проект портативного нумізматичного аналізатора на базі ESP32-S3 з індуктивним сенсором LDC1101 як головним вимірювальним елементом. Проект знаходиться на стадії завершеного проектування — документована архітектура охоплює шість архітектурних доменів (сенсорний стек, плагін-система, storage, connectivity, logging, fingerprint DB) і пройшла через три незалежні ітерації рецензування.

**Загальна оцінка архітектурної зрілості: 7.8 / 10**

| Домен | Зрілість | Готовність |
|--|--|--|
| Plugin System / Contract | ⭐⭐⭐⭐⭐ 9/10 | ✅ Готовий |
| Logger Architecture | ⭐⭐⭐⭐½ 9/10 | ✅ Реалізовано (Phase 1) |
| LDC1101 Integration | ⭐⭐⭐⭐ 8/10 | ✅ Готовий до імплементації |
| Fingerprint DB | ⭐⭐⭐⭐ 8/10 | ✅ Готовий (умовно — R-01 потрібен) |
| Storage Architecture | ⭐⭐⭐⭐ 8/10 | ✅ Специфіковано (не реалізовано) |
| Connectivity Architecture | ⭐⭐⭐½ 7/10 | 📝 Специфіковано (не реалізовано) |

**Знайдено 24 знахідки (v1.1.0): 4 CRITICAL · 7 HIGH · 7 MEDIUM · 4 LOW · 2 INFO**

**Ключові системні висновки:**

1. **LDC1101 — центральний елемент, але котушка — незакритий ризик №1.** Параметри котушки (індуктивність, ємність, резонансна частота, Q-фактор) визначають весь сигнальний простір системи. Жоден документ не специфікує котушку точніше ніж "MIKROE-3240" — без параметрів котушки неможливо верифікувати чи фізичні симуляції відповідають реальності.

2. **Механіка позиціонування монети системно недоопрацьована.** Алгоритм порівняння базується на відстанях 0/1/3 мм. Але жодний документ не описує конструкцію механізму точного позиціонування (spacer system) та не визначає похибку відтворення цих відстаней між різними пристроями.

3. **Відсутній сигнал присутності монети.** Система не має архітектурної відповіді на питання: "як firmware визначає що монета покладена?" — threshold-based, IMU-triggered, or manual button?

4. **Температурна компенсація LDC1101 не документована** — при змінах температури ±15°C індуктивність котушки змінюється до ±0.5%, що може перевищити різницю між Ag925 та Ag900.

5. **Всі блокуючі технічні проблеми попередніх аудитів закриті** (P-2 UNBLOCKED, всі PRE-1..PRE-11 вирішені). Arch рецензія v3.0.0 зафіксувала 8.5/10 для Plugin System.

---

## 2. Карта системи та архітектурний стек

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                        CoinTrace System Stack                                │
├──────────────────────────────────────────────────────────────────────────────┤
│  LAYER 6: USER INTERFACE                                                     │
│  ├─ Display (ST7789V2, 240×135, 1.14") — IDisplayPlugin ← НЕ СПЕЦИФІКОВАНО  │
│  ├─ Keyboard (56-key, TCA8418RTWR) — IInputPlugin ← КОНФЛІКТ API            │
│  └─ Audio (ES8311 + speaker) — IAudioPlugin ← НЕ СПЕЦИФІКОВАНО              │
├──────────────────────────────────────────────────────────────────────────────┤
│  LAYER 5: CONNECTIVITY                                                       │
│  ├─ USB Serial (HWCDC) ← ✅ ПРАЦЮЄ                                           │
│  ├─ WiFi 802.11 b/g/n → REST API / WebSocket ← 📝 Специфіковано             │
│  ├─ BLE 5.0 → GATT services ← 📝 Специфіковано                              │
│  └─ Display QR Code ← 📝 Специфіковано                                       │
├──────────────────────────────────────────────────────────────────────────────┤
│  LAYER 4: APPLICATION                                                        │
│  ├─ CoinAnalyzer (fingerprint matching, k1/k2/slope/dL1)                    │
│  ├─ MeasurementStore (ring buffer N=250, LittleFS persistence)              │
│  └─ PluginSystem (orchestrator, lifecycle manager)                          │
├──────────────────────────────────────────────────────────────────────────────┤
│  LAYER 3: PLUGIN ECOSYSTEM                                                   │
│  ├─ [PRIMARY] LDC1101Plugin (SPI, RP+L, 50 Hz) ← ЦЕНТРАЛЬНИЙ               │
│  ├─ [SECONDARY] QMC5883L (I2C, магнетизм)                                   │
│  ├─ [SECONDARY] BMI270 (I2C/SPI, IMU)                                       │
│  ├─ [AUXILIARY] HX711 (GPIO, вага)     ← ⚠️ BLOCKING 100+ ms               │
│  ├─ [AUXILIARY] BH1750 (I2C, освітл.) ← ⚠️ BLOCKING 120 ms                │
│  └─ [STORAGE] SDCardPlugin (SPI, FAT32)                                     │
├──────────────────────────────────────────────────────────────────────────────┤
│  LAYER 2: SERVICES                                                           │
│  ├─ Logger (Serial + RingBuffer + SDTransport async) ← ✅ Phase 1 done      │
│  ├─ StorageManager (NVS + LittleFS + SD) ← ❌ Не реалізовано                │
│  └─ ConfigManager (LittleFS JSON) ← ❌ Не реалізовано                       │
├──────────────────────────────────────────────────────────────────────────────┤
│  LAYER 1: HARDWARE ABSTRACTION                                               │
│  ├─ SPI Bus (VSPI: GPIO40/14/39) → LDC1101 (CS=5) + SD (CS=12) + Display   │
│  ├─ I2C Bus (SDA=G2, SCL=G1, Grove) → BMI270 + TCA8418 + BH1750 + QMC5883L │
│  └─ GPIO → HX711 (DOUT/CLK)                                                 │
├──────────────────────────────────────────────────────────────────────────────┤
│  LAYER 0: HARDWARE                                                           │
│  ESP32-S3FN8 @ 240 MHz | 8 MB Flash | 337 KB heap | NO PSRAM               │
└──────────────────────────────────────────────────────────────────────────────┘

НОТАЦІЇ:
  ✅ Реалізовано     📝 Специфіковано, не реалізовано
  ❌ Відсутній код   ⚠️ Проблема знайдена    ← Знахідка аудиту
```

### Архітектурна топологія SPI шини (КРИТИЧНО)

Підтверджено схемою M5Stack Cardputer-Adv v1.0 2025-06-20:

```
VSPI Bus (ESP32-S3 Hardware SPI):
  SCK  = GPIO40  ⚠️ JTAG MTDO
  MOSI = GPIO14
  MISO = GPIO39  ⚠️ JTAG MTCK

  Пристрої на шині:             CS пін    Частота     Примітка
  ─────────────────────────────────────────────────────────────
  LDC1101 (P3 Click Board)  CS = GPIO5    4 MHz       Основний сенсор
  SD Card (J3 TF-015)       CS = GPIO12   20 MHz max  JTAG конфлікт GPIO39/40
  ST7789V2 Display (J1)     CS = ?        40 MHz typ  НЕ СПЕЦИФІКОВАНО в архітектурі!

  ⚠️ Три SPI-пристрої на одній шині = обов'язковий spiMutex для кожного.
  ⚠️ JTAG debugging вимикає SD карту (GPIO39/40 shared).
  ⚠️ Дисплей на VSPI чи HSPI? Документ мовчить — потенційний Pin конфлікт.
```

---

## 3. Центральний елемент: LDC1101 — детальний аналіз та симуляції

### 3.1 Фізична модель вимірювання

LDC1101 вимірює резонансну систему LC-котушки. Коли провідний об'єкт наближається до котушки, в ньому індукуються вихрові струми (eddy currents), що поглинають енергію та змінюють ефективну індуктивність системи.

**Фізична модель (спрощена):**

```
Котушка в повітрі:          L₀, Q₀, fSENSOR₀
                            ┌─────────────────────────────────┐
Котушка + монета на відст. d: │ Еквівалентна схема:             │
                            │  L_eff(d) ≈ L₀ + ΔL(d, μr, σ)  │
                            │  R_eff(d) ≈ R₀ + ΔR(d, σ, δ)   │
                            └─────────────────────────────────┘

де:
  μr  — відносна магнітна проникність металу (Fe >> Al, Ag, Cu, Au)
  σ   — питома електрична провідність металу (Ag > Cu > Au > Al >> Fe)
  d   — відстань коtушка–монета
  δ   — глибина проникнення (skin depth) = √(2/(ω·μ·σ))
```

**Вимірювані параметри LDC1101:**

| Параметр LDC1101 | Фізична природа | Визначається металом |
|--|--|--|
| `RP_DATA` (паралельний опір) | Обернено до втрат від eddy currents | Провідність σ → RP ∝ 1/(σ · δ) |
| `L_DATA` (код індуктивності) | Резонансна частота котушки | Магнітна проникність μr |

### 3.2 Симуляція: Skin-depth та RP для монетних металів

**Модель глибини проникнення (skin depth):**

```
δ(f) = √(2/(2π·f·μ₀·μr·σ))

При f = 1 MHz (типова частота MIKROE-3240):

Метал         σ [MS/m]   μr      δ [µm]   Висновок
──────────────────────────────────────────────────────────────────
Срібло Ag999  63.0       1.0     20.4     Тонкий шар (~20 µm) залучений
Мідь Cu       58.5       1.0     21.2     Схоже на Ag (critical ambiguity!)
Золото Au     44.2       1.0     24.4     Менша провідність → більший δ
Алюміній Al   35.7       1.0     27.1     Помітно відрізняється від Ag
Нікель Ni     14.3      600.0     4.4     Дуже малий δ (феромагнетик!)
Сталь 304     1.37      750.0     4.9     Мала σ, але великий μr → малий δ  
Вольфрам W    18.9       1.0     36.6     Велика маса → deepfake Au/Ag

Ключовий висновок: δ(Ag) ≈ δ(Cu) при 1 MHz — система МОЖЕ плутати Ag та Cu
якщо покладатись лише на RP! Диференціація вимагає L-канал (μr Ag = μr Cu = 1)
або більш складного аналізу профілю кривої RP(d).
```

**⚠️ КРИТИЧНА ЗНАХІДКА SIM-1:** При f = 1 MHz skin depth срібла та міді відрізняється лише на ~0.8 µm. Монети однакового розміру зі срібла та міді (однакова геометрія, однаковий діаметр) дадуть дуже близькі значення RP через близькість провідностей (63 vs 58.5 MS/m = 7.6% різниця). Диференціація можлива лише через:
- ΔRP(d) curve shape (k1, k2 коефіцієнти різняться)
- Або через вагу монети (HX711) як додатковий ознаку (ρ(Ag) = 10.49 г/см³ vs ρ(Cu) = 8.96 г/см³ — різниця 17%)

**Рекомендація:** Архітектурний документ FINGERPRINT_DB повинен явно специфікувати "розв'язку Ag/Cu" як окремий test case з реальними вимірами на калібрувальних зразках.

### 3.3 Симуляція: Класифікаційний простір (RP–L)

**Оціночна карта класифікаційного простору** при f = 1 MHz, d = 0 мм, MIKROE-3240:

```
L_DATA (код), ↑ велике L = феромагнітне
│
│  ╔══════════════╗
│  ║  СТАЛЬ       ║  (RP малий, L великий: σ↓, μr↑)
│  ║  (Fe, Ni)    ║  RP ≈ 2000–6000,  ΔL ≈ 200–2000
│  ╚══════════════╝
│
│                      ╔═══════════════╗
│                      ║  НІКЕЛЬ/ЗАЛІЗО║  (перехідна зона)
│                      ╚═══════════════╝
│
│  ─────────────────────────────────── L₀ (базова лінія без монети)
│
│                           ╔══════╗    ╔═══════╗
│                           ║  Ag  ║    ║  Au   ║
│                           ║(925–║    ╚═══════╝
│                           ║999) ║    Au: RP помірний, ΔL ≈ 0
│                           ╚══════╝
│          ╔══════╗
│          ║  Cu  ║  (RP близький до Ag! Небезпечна зона)
│          ╚══════╝
│
│    ╔══════════╗
│    ║ Алюміній ║  (RP великий: мала σ → менші eddy currents)
│    ╚══════════╝
│
└────────────────────────────────────────────────────→ RP_DATA (провідність)
     Великий RP = погана провідність           Малий RP = хороша провідність

СТАТУС ЗОН:
  ✅ Ag/Fe, Ag/Al, Ag/W — чітко розділяються (діагностично надійно)
  ⚠️ Ag/Cu — небезпечна близькість (потребує k1/k2 аналізу, або ваги)
  ⚠️ Ag833/Cu+Sn (бронза) — зони можуть перетинатись
  ❌ Ag925/Ag900 — різниця σ < 2%, потребує validation set перед будь-якими заявами
```

**⚠️ ЗНАХІДКА SIM-2:** Поточна архітектура COLLECTOR_USE_CASE заявляє надійне виявлення Ag925 vs Ag900 «після калібрування» — але розрахунок skin depth показує, що при 200–500 kHz (реальний fSENSOR MIKROE-3240, див. §3.7) різниця в RP між Ag925 і Ag900 буде < 1–2%, що можна порівняти з шумом LDC1101 при RESP_TIME=768 (~0.5%). Потрібен або validation set на реальному пристрої, або перехід на LHR режим (24-bit L, вищий RCOUNT → менший шум).

### 3.4 Симуляція: Часовий бюджет конверсії

**Матриця ConversionTime = RESP_TIME_cycles / (3 × fSENSOR):**

```
                fSENSOR (Hz) — визначається котушкою
RESP_TIME    │  200 kHz     500 kHz    1 MHz      2 MHz
bits[2:0]    │─────────────────────────────────────────────
b010 (192)   │  0.320 ms   0.128 ms   0.064 ms   0.032 ms   ← max noise
b011 (384)   │  0.640 ms   0.256 ms   0.128 ms   0.064 ms
b100 (768)   │  1.280 ms   0.512 ms   0.256 ms   0.128 ms   ← MikroE default ✅
b101 (1536)  │  2.560 ms   1.024 ms   0.512 ms   0.256 ms
b110 (3072)  │  5.120 ms   2.048 ms   1.024 ms   0.512 ms
b111 (6144)  │ 10.240 ms   4.096 ms   2.048 ms   1.024 ms   ← min noise

⚠️ ОНОВЛЕНО (аналіз MikroE SDK ldc1101.c):
  MikroE work config: RESP_TIME = 768 (b100), MIN_FREQ = 118 kHz
  → DIG_CFG регістр 0x04 = 0xD4 (0xD0 | 0x04)
  → fSENSOR фактичний: оцінка 200–500 kHz (детально §3.7)

Висновок: При RESP_TIME=768 та fSENSOR=200-500 kHz:
  ConversionTime = 1.28 мс..0.51 мс << update() period 20 мс (50 Hz)
  → State Machine стратегія коректна ✅
  → DRDYB завжди = 0 при кожному виклику update() (дані вже готові)
```

**Аналіз затримки SPI burst (4 байти при 4 MHz):**

```
SPI транзакція на читання 4 регістрів:
  tCS_setup = 0.25 µs (GPIO toggle)
  t_command = 8 bits / 4 MHz = 2 µs
  t_data    = 4 × 8 bits / 4 MHz = 8 µs
  tCS_hold  = 0.25 µs
  ──────────────────────
  Total burst = ~10.5 µs

STATUS read (1 байт):
  t_STATUS = 2 µs + 2 µs = ~4 µs

Загальний LDC1101 overhead per update():
  = t_STATUS + t_burst = ~15 µs << 10 ms контракт update()  ✅
```

### 3.5 Симуляція: Вплив температури на RP та L

**⚠️ КРИТИЧНА ЗНАХІДКА SIM-3: Температурний дрейф незадокументований**

```
Вплив температури на вимірювання LDC1101:

1. КОТУШКА (мідний дріт):
   α_Cu = 3.9×10⁻³ /°C (температурний коефіцієнт опору)
   При ΔT = +30°C: R_dc += 11.7% → Q-фактор котушки падає
   → fSENSOR змінюється → L_DATA змінюється без монети!

2. ЄМНІСТЬ КОНДЕНСАТОРА (на платі MIKROE-3240):
   Типовий TC для C0G/NP0: ±30 ppm/°C
   При ΔT = +30°C: ΔC = ±90 ppm → Δf/f = ±45 ppm (negligible)
   Увага: X7R/X5R конденсатори: ±500–2000 ppm/°C — значний ефект!

3. МЕТАЛ МОНЕТИ:
   σ(T) = σ₀ / (1 + α·ΔT)
   α(Ag) = 3.8×10⁻³ /°C
   При ΔT = +30°C: σ(Ag) падає на ~11.4% → RP зростає на ~11.4%

КОМБІНОВАНИЙ ЕФЕКТ при вимірюванні між +15°C та +35°C (польовий діапазон):
  ΔRP_temperature = ±5–12% від номінального RP

  Порівняно з різницею між Ag925 та Ag900: ~1–3%
  → Температурний дрейф перевищує діагностичну різницю між пробами!

4. BMI270 НЕ Є ТЕРМОМЕТРОМ для контролю — у нього немає потрібної точності.
   Виправлення: потрібен окремий термодатчик ALS або BME280.

АРХІТЕКТУРНИЙ GAP: metadata.temp_c зберігається, але:
  - алгоритм не нормалізує RP/L по температурі
  - records з різною temp_c порівнюються без компенсації
  - σ варіація = 0.00038/°C → при ΔT=10°C: ~3.8% зміна RP (~38 Ohm для RP=1000)
```

**Рекомендація (SIM-3):** Архітектура повинна визначити:
- чи робиться температурна компенсація в алгоритмі (де? в `compute_vector.py`?)
- або чи приймається обмеження "вимірювання тільки при стабільній температурі ±5°C"
- коефіцієнти компенсації потрібно включити в `conditions` record для коректного порівняння

### 3.6 Симуляція: Деградація сигналу при зміщенні монети

**Чутливість RP(d) до похибки позиціонування:**

```
Теоретична крива RP(d) для Ag925, MIKROE-3240, f=1 MHz (типова форма):

ΔRP(d) / ΔRP(0mm) = k(d) = (d/d_ref + 1)^(-n)   (спрощена модель, n≈2..3)

Якщо ΔRP(0) = 300 Ohm (типово для Ag25mm монети):

  d = 0 мм:   ΔRP = 300 Ohm  (базова)    k = 1.000
  d = 0.5 мм: ΔRP ≈ 225 Ohm              k ≈ 0.750  (25% втрата)
  d = 1 мм:   ΔRP ≈ 157 Ohm              k ≈ 0.523  (k1 в записі)
  d = 2 мм:   ΔRP ≈ 83 Ohm               k ≈ 0.277
  d = 3 мм:   ΔRP ≈ 47 Ohm               k ≈ 0.157  (k2 в записі)

Чутливість k1 до похибки spacer 1 мм:
  При реальній відстані d = 0.95 мм (spacer недолитий -0.05 мм):
  k1_measured ≈ 0.537 vs k1_expected = 0.523
  Δk1 = +0.014 = 2.7% похибка

  Це Δk1 = 0.014 — чи відрізняє це Ag925 від Ag900?
  Очікуваний Δk1(Ag925 vs Ag900) ≈ 0.008–0.015 (несуттєва різниця!)
  → Spacer похибка ±0.05 мм маскує різницю між Ag925 та Ag900

  Специфікація архітектурна: DB-13 вимагає tolerance ≤ ±0.05 мм.
  Це фізично виправдано але потребує механічної точності ~0.05 мм — хто це забезпечує?
```

**⚠️ ЗНАХІДКА SIM-4:** Специфікована tolerance ±0.05 мм для spacer — це вимога виробничої точності. Для самостійно виготовлених spacers (3D print, картон) досягнути ±0.05 мм дуже складно. Архітектура не вирішує: чи перевіряється якість spacer перед використанням (Onboarding §12 перевіряє матеріал spacer через `|ΔRp| < 10 Ohm`, але не геометрію).

### 3.7 Аналіз котушки та конфігурації LDC1101: дані з MikroE SDK

**Аналіз офіційного MikroE SDK** (`github.com/MikroElektronika/mikrosdk_click_v2`, ldc1101.c)
дозволив уточнити реальну конфігурацію чіпа на MIKROE-3240:

#### Офіційна «work configuration» MikroE (ldc1101_default_cfg, фаза 2)

```c
// Register 0x01 — RP_SET (Dynamic Range)
RP_MAX = 24 kΩ  (LDC1101_RP_SET_RP_MAX_24KOhm = 0x20)
RP_MIN = 1.5 kΩ (LDC1101_RP_SET_RP_MIN_1_5KOhm = 0x06)
// УВАГА: RP_SET = 0x26 — НЕ 0x07 як рекомендовано в LDC1101_ARCHITECTURE.md!
// Архітектурний документ рекомендує найширший діапазон (0x07), MikroE використовує
// вужчий (MAX=24kΩ, MIN=1.5kΩ). Це суттєва РІЗНИЦЯ в конфігурації.

// Register 0x02 — TC1 (Internal Time Constant 1)
C1 = 0.75 pF   (LDC1101_TC1_C1_0_75pF  = 0x00)
R1 = 21.1 kΩ   (LDC1101_TC1_R1_21_1kOhm = 0x1F)
→ τ1 = R1 × C1 = 21.1 kΩ × 0.75 pF = 15.8 ns

// Register 0x03 — TC2 (Internal Time Constant 2)
C2 = 3 pF      (LDC1101_TC2_C2_3pF    = 0x00)
R2 = 30.5 kΩ   (LDC1101_TC2_R2_30_5kOhm = 0x3F)
→ τ2 = R2 × C2 = 30.5 kΩ × 3 pF = 91.5 ns

// Register 0x04 — DIG_CFG (Conversion Interval)
DIG_CFG  = 0xD4   = 0xD0 (MIN_FREQ=118 kHz) | 0x04 (RESP_TIME=768 cycles)
```

#### Оцінка fSENSOR за MIN_FREQ = 118 kHz

```
MIN_FREQ = 118 kHz — це нижній ліміт, встановлений в DIG_CFG.
Реальний fSENSOR > 118 kHz (інакше чіп зупинить конверсію).

ПCB spiral coil на Click Board (M-розмір, 42.9 × 25.4 мм) — типові параметри:
  L_coil ≈ 3–20 µH  (PCB мідна спіраль, мало витків, мала індуктивність)
  C_SENSOR ≈ 33–100 pF (зовнішній конденсатор, типово на платі)

fSENSOR = 1 / (2π × √(L × C)):
  L=10µH, C=100pF:  f ≈ 159 kHz  (мінімальна зона, ~MIN_FREQ)
  L=10µH, C=33pF:   f ≈ 277 kHz  ← ймовірний діапазон
  L=5µH,  C=33pF:   f ≈ 392 kHz  ← ймовірний діапазон
  L=3µH,  C=33pF:   f ≈ 507 kHz
  L=1µH,  C=100pF:  f ≈ 503 kHz

⚠️ ВИСНОВОК: fSENSOR MIKROE-3240 оцінюється як 200–500 kHz, а НЕ 1–2 MHz!
   Це суттєво змінює розрахунки skin depth (§3.2) та ConversionTime (§3.4):
   - При 300 kHz: δ(Ag) ≈ 20.4 µm × √(1MHz/300kHz) ≈ 37 µm (1.8× глибше)
   - ConversionTime (RESP_TIME=768): 768/(3×300kHz) ≈ 0.85 мс ✅ (OK)
```

#### ⚠️ ЗНАХІДКА SIM-5b: Bug SYS-1 підтверджений в самій MikroE бібліотеці

```c
// З MikroE ldc1101.c (офіційний SDK):
uint16_t ldc1101_get_rp_data ( ldc1101_t *ctx )
{
    rx_buf[0] = ldc1101_generic_read( ctx, LDC1101_REG_RP_DATA_MSB );  // 0x22 ← ПЕРШИЙ!
    rx_buf[1] = ldc1101_generic_read( ctx, LDC1101_REG_RP_DATA_LSB );  // 0x21 ← другий
    // ...
}
```
Це ПОДВІЙНА помилка:
1. ПОРЯДОК читання невірний: MSB (0x22) читається ДО LSB (0x21)
   → По даташиту LDC1101, читання 0x21 (LSB) ПОВИННО йти ПЕРШИМ — воно latches MSB та L_DATA
   → Читання 0x22 першим читає MSB з попереднього циклу конверсії!
2. Кожен виклик `ldc1101_generic_read()` — ОКРЕМА CS-транзакція
   → Між NSS_UP та NSS_DOWN проходить 1+ конверсій → гонка даних

Наслідок: Bug SYS-1 (з аудиту v4.0.0) успадкований БЕЗПОСЕРЕДНЬО з MikroE reference
бібліотеки. Архітектурні документи CoinTrace коректно специфікують "LSB першим",
але якщо реалізація скопіює MikroE приклад — відтворить баг.

**Пріоритет виправлення SYS-1 підвищено** — це не просто architectural oversight,
а системний дефект в усіх портах MikroE бібліотеки.

#### Де залишається невизначеність

```
ДОСІ НЕВІДОМЕ (потребує вимірювання на реальній платі):
  L_coil = ?      (точна індуктивність PCB котушки MIKROE-3240)
  C_SENSOR = ?    (точна ємність C_SENSOR — зовнішній конденсатор на платі)
  Q-фактор = ?    (без LCR-вимірювання невідомий)
  Геометрія = ?   (діаметр котушки: орієнтовно 15–20 мм для Click M-розміру)
  fSENSOR actual = ?  (оцінка 200–500 kHz, але точне значення — тільки виміром)
```

**Рекомендація (SIM-5 оновлена):** Перед збором fingerprint data:
1. Виміряти fSENSOR: читати L_DATA_raw без монети, обчислити `fSENSOR = fCLKIN×RESP_TIME/(3×L_DATA)`
2. Виміряти C_SENSOR LCR-метром або зворотним розрахунком із відомого fSENSOR
3. Оновити `protocol_id` у `protocols/registry.json` якщо fSENSOR ≠ 1 MHz ≠ поточна назва `"p1_1mhz_013mm"`
4. TC1/TC2 тепер відомі з MikroE SDK — використовувати ті ж значення або обґрунтувати відхилення
5. Перевірити RP_SET: 0x07 (arch doc) vs 0x26 (MikroE) — прийняти єдине архітектурне рішення

### 3.8 Критичні знахідки LDC1101

| ID | Severity | Знахідка |
|--|--|--|
| **LDC-1** | 🔴 CRITICAL | `readRP()` у PLUGIN_DIAGNOSTICS — два окремих SPI-обміни, MSB читається ДО LSB (bug підтверджений в MikroE reference SDK ldc1101.c!) |
| **LDC-2** | 🟡 MEDIUM | TC1/TC2 тепер відомі з MikroE SDK: TC1=(C1=0.75pF,R1=21.1kΩ), TC2=(C2=3pF,R2=30.5kΩ) — але ВІДРІЗНЯЮТЬСЯ від рекомендованих в LDC1101_ARCHITECTURE.md |
| **LDC-2b** | 🟠 HIGH | RP_SET mismatch: LDC1101_ARCHITECTURE.md рекомендує 0x07 (MAX=0.75kΩ), MikroE робоча конфігурація = 0x26 (MAX=24kΩ, MIN=1.5kΩ) — яка правильна для монет? |
| **LDC-3** | 🟠 HIGH | Температурна компенсація RP/L відсутня у алгоритмі та архітектурі |
| **LDC-4** | 🟠 HIGH | fSENSOR не задокументований в архітектурі (оцінка 200–500 kHz; C_SENSOR і геометрія котушки потребують вимірювання) |
| **LDC-5** | 🟡 MEDIUM | LHR режим (24-bit L) документально відкладений — але може вирішити Ag925/Ag900 проблему |
| **LDC-6** | 🟡 MEDIUM | Немає механізму визначення присутності монети (coin detection trigger) |
| **LDC-7** | 🟡 MEDIUM | INTB пін ігнорується (polling) — при Стратегії B (async task) interrupt-driven читання ефективніше |
| **LDC-8** | 🟢 LOW | NO_SENSOR_OSC recovery стратегія не специфікована (скільки разів retry перед ERROR?) |
| **LDC-9** | 🟢 LOW | POR_READ bit обробка не задокументована в State Machine |

---

## 4. Plugin System: аналіз архітектури

### 4.1 Lifecycle та контракт

Plugin lifecycle (`canInitialize() → initialize() → update()×N → shutdown()`) — **найзріліший архітектурний елемент проекту**. Контракт детально специфікований, всі критичні UB з попередніх аудитів закриті.

**Сильні сторони:**
- Формальний контракт (PLUGIN_CONTRACT.md) — один з найкращих у embedded-класі проектів
- spiMutex/wireMutex — правильна превентивна вимога
- IDiagnosticPlugin mixin — добре спроектований
- Configuration-driven через JSON — правильне рішення для embedded
- Три рівні конфігурації (build / runtime / system) — коректно специфіковані

**Залишкові проблеми:**

| ID | Severity | Знахідка |
|--|--|--|
| **PS-1** | 🔴 CRITICAL | `IInputPlugin` API конфлікт: `hasEvent()`/`getEvent()` в INTERFACES_EXTENDED vs `pollEvent()` в CONTRACT — три ітерації без рішення |
| **PS-2** | 🟠 HIGH | `HX711Plugin::read()` → `scale.get_units(5)` = 62–500 ms blocking — порушення контракту ≤ 5 ms |
| **PS-3** | 🟠 HIGH | BH1750: `delay(120ms)` в update() — аналогічне блокування контракту |
| **PS-4** | 🟡 MEDIUM | `getGraphicsLibrary()` повертає `void*` — ламає безпеку типів C++ |
| **PS-5** | 🟡 MEDIUM | Watchdog у PluginSystem відсутній — "повільний плагін → вся система деградує" не детектується |
| **PS-6** | 🟢 LOW | HARDWARE_PROFILES.md згадується в Roadmap але не існує і не позначений `[TODO]` |

### 4.2 Симуляція: Timing budget update()

**Модель: N плагінів × max update() + delay(10)**

```
Гарантія контракту: ≥ 10 Hz = ≤ 100 ms per loop() iteration.

delay(10) в loop() = 10 ms baseline
update() контракт = ≤ 10 ms per плагін

├─ LDC1101:   ~0.015 ms (STATUS read + burst read = ~15 µs)   ✅ negligible
├─ BMI270:    ~1–5 ms (I2C @ 400 kHz, 6 байт)                ✅ OK
├─ QMC5883L:  ~5–10 ms (I2C + polling)                       ⚠️ межа контракту
├─ HX711:     62–500 ms (blocking ADC, 10 Hz rate)            ❌ ПОРУШЕННЯ
└─ BH1750:    120 ms (blocking I2C delay)                     ❌ ПОРУШЕННЯ

Сценарії:
  3 fast plugins (LDC+BMI+QMC) + delay(10): ~25 ms = 40 Hz ✅
  5 fast plugins × 5 ms + delay(10):         ~35 ms = 28 Hz ✅
  5 fast + HX711 blocking:                  ~125 ms = 8 Hz ❌

МАТЕМАТИЧНИЙ ЛІМІТ (delay=10ms, update_max=10ms):
  loop = delay(10) + n × 10 ms ≤ 100 ms
  → max n = 9 плагінів при дотриманні контракту (не 10 як у документі!)
  10-й плагін: 10 + 10×10 = 110 ms → 9.1 Hz < 10 Hz
```

### 4.3 Симуляція: Memory budget

```
ESP32-S3FN8: 337 KB heap (520KB - ~183KB system overhead)
⚠️ PSRAM = 0 MB (FN8 = Flash-only, No PSRAM) — Bug B-01 у main.cpp!

Статичні алокації:
  ST7789V2 framebuffer (240×135×2B RGB565): 64.8 KB
  Logger RingBuffer (100 × 220B entries):   22.0 KB
  Logger SD queue (64 × 80B):                5.1 KB  
  Logger WS queue (64 × 80B):                5.1 KB
  FreeRTOS stacks (8 tasks × 4KB):          32.0 KB
  ArduinoJson heap (buffer):                 4.0 KB
  LittleFS metadata + cache:                 8.0 KB
  ██████████████████████████████████████████
  Системний overhead разом:                ~141 KB

Залишок для плагінів + WiFi + BLE:        ~196 KB

  WiFi stack (ESP-IDF): ~50–70 KB
  BLE stack (ESP32 Nimble): ~70–100 KB
  [якщо WiFi+BLE одночасно: ~120–170 KB]

  Сценарій ТІЛЬКИ WiFi: 196 - 60 = ~136 KB для плагінів ✅
  Сценарій WiFi + BLE:  196 - 140 = ~56 KB для плагінів ⚠️ ТІСНО

ВИСНОВОК: WiFi + BLE одночасно + 6+ плагінів = ризик OOM на ESP32-S3FN8.
РЕКОМЕНДАЦІЯ: Зафіксувати в архітектурі що WiFi та BLE ВЗАЄМОВИКЛЮЧАЮТЬ одне одного
(або ретельно профілювати heap на реальному пристрої).
```

### 4.4 Симуляція: SPI bus contention

**Три пристрої на VSPI, async SD task vs synchronous LDC1101:**

```
Сценарій: LDC1101 update() + SDTransport async flush (кожні ~200 ms)

Timeline (при 50 Hz LDC1101 update = кожні 20 мс):

T=0ms:    LDC1101 STATUS read: spiMutex.take(50ms) → CS5 LOW → ~4µs → CS5 HIGH → mutex.give
T=0.004ms: LDC1101 burst read: spiMutex.take → CS5 LOW → ~10.5µs → CS5 HIGH → mutex.give
T=200ms:  SDTransport flush: spiMutex.take → CS12 LOW → SD write ~214µs → CS12 HIGH
T=220ms:  LDC1101 update() — spiMutex.take(50ms timeout) → ✅ OK (SD вже звільнив)

Ймовірність конфлікту:
  LDC1101 window = 14.5 µs per 20ms = 0.073%
  SD busy = 214 µs per 200 ms = 0.107%
  P(конфлікт) = 0.073% × 0.107% × 20 = 0.0000015% per loop

  При 50 Hz × 3600 sec = 180000 reads → очікувані конфлікти ≈ 0.003
  Тобто < 1 конфлікту за годину — mutex timeout 50ms достатній ✅

⚠️ АЛЕ: Display (ST7789V2) також на VSPI! Якщо LVGL або M5Stack display refresh
  виконується без spiMutex (не через PluginSystem), це NFI-типовий баг:
  Дисплей рефреш = 240×135×2B @ 40MHz = ~16ms frame time → СУТТЄВИЙ конфлікт!
  АРХІТЕКТУРА НЕ ВИРІШУЄ ЦЕ.
```

**⚠️ ЗНАХІДКА PS-DISP:** Display (ST7789V2) підключений до VSPI але не згадується в списку SPI-пристроїв в PLUGIN_ARCHITECTURE та LDC1101_ARCHITECTURE. Якщо дисплей рефреш відбувається через M5Stack BSP без spiMutex, race condition з LDC1101 та SD неминучий.

---

## 5. Storage Architecture: аналіз

### 5.1 Симуляція: Flash endurance

**Модель зносу при нормальному використанні:**

```
Параметри:
  Flash: W25Q64 (8MB), 100,000 write cycles per 4KB sector
  Tier 1 LittleFS: 2.75 MB (Option C partition table) = ~704 sectors
  LittleFS wear leveling: рівномірний розподіл по всіх секторах
  
Сценарій "активний колекціонер": 50 вимірів/день

Writes per measurement:
  1 × m_XXX.json (~2 KB) → 1 sector write
  1 × log entry (via SDTransport/LittleFSTransport) → ~1/16 sector write
  
Daily writes до LittleFS:
  50 × m_XXX.json = 50 sector writes
  50 × log = ~3 sector writes
  Total: ~53 sector writes / day

Wear leveling: 53 writes / 704 sectors = 0.075 writes/sector/day
До 100K limit: 100,000 / 0.075 = 1,333,333 days = 3651 YEARS ✅

NVS writes (meas_count increment): 50/day на NVS namespace
  NVS: 60 KB = 15 sectors з wear leveling
  50 writes / 15 sectors = 3.33 writes/sector/day
  До 100K limit: 30,000 days = 82 YEARS ✅

ВИСНОВОК: Flash endurance не є проблемою при нормальному використанні.
NF-01 (не більше 1 write/sector/5 хв) дотримується з великим запасом.
```

### 5.2 Симуляція: Boot state machine

**Аналіз boot sequence §17.2 (з виправленнями v1.3.2):**

```
Boot [1]: NVS mount (always)
  └─ FAIL → FATAL: no calibration, no credentials → safe restart ✅

Boot [2]: Logger init (Serial + RingBuffer only)
  └─ minimal logging active ✅

Boot [3]: LittleFS_web mount
  └─ FAIL → web UI unavailable, rest OK ✅ (graceful)

Boot [4]: LittleFS_data mount
  ├─ OK → [5] Logger + LittleFSTransport init ✅ (B-H1 виправлено в v1.3.2)
  └─ FAIL → DEGRADED: Logger без LittleFSTransport, measurements → memory only ✅

Boot [5]: PluginSystem init (всі плагіни в порядку priority)
  └─ кожен плагін: canInit() → init() → якщо FAIL: plugin DISABLED, continue ✅

Boot [6]: SD Card mount (optional)
  └─ FAIL → без fingerprint DB, без archive → DEGRADED ✅

⚠️ ЗНАХІДКА ST-1: TCA8418RTWR (keyboard controller) і BMI270 — їх I2C ініціалізація
  відбувається в PluginSystem [5], АЛЕ якщо keyboard не відповідає на I2C —
  plugin просто DISABLED. Користувач без клавіатури = без UI = глухий кут.
  Потрібна fallback UI (принаймні кнопка G0 → базові команди).

⚠️ ЗНАХІДКА ST-2: Порядок деградації не специфікує "мінімальний функціональний режим".
  Якщо НЕ має: SD + LittleFS_data → система вмикається, але що робить?
  Потрібний explicit "Safe Mode" definition з мінімальним feature set.
```

**Відомі закриті проблеми (правильно вирішені):**
- B-01 (PSRAM bug) — детально задокументовано, Fix відомий: `usePsram=false`
- ADR-ST-009 (copy-before-overwrite) — специфіковано
- spi_vspi_mutex (B-06) — специфіковано в ADR-ST-008
- Boot sequence умовна ініціалізація LittleFSTransport (B-H1) — виправлено v1.3.2

---

## 6. Fingerprint Database: аналіз алгоритму

### 6.1 Симуляція: Класифікаційна точність в 5D просторі

**Аналіз weighted Euclidean метрики (algo_ver=1):**

```
5D вектор: [dRp1_n, k1, k2, slope_rp_per_mm_lr, dL1_n]

Bootstrap ваги (до validation set R-01):
  w₁=w₂=w₃=w₄=w₅ = 1/√5 ≈ 0.447

ОЦІНКА ефективності ваг за фізичним змістом:

  dRp1_n  (нормалізований абс. ΔRP) — залежить від діаметру монети
           Ag999 Ø39mm vs Ag999 Ø25mm: dRp1 може відрізнятись на 30–50%!
           Після нормалізації /600: dRp1_n(Ø39) ≈ 0.7, dRp1_n(Ø25) ≈ 0.4
           → ВНОСИТЬ ПЛУТАНИНУ між різнодіаметровими монетами одного металу

  k1,k2   (shape ratios) — SIZE-INDEPENDENT ✅ — правильно центральні ознаки

  slope   (нахил кривої) — корельований з k1,k2 — часткова надлишковість

  dL1_n   (ΔL нормалізоване) — критично важливо для феромагнетиків
           Для кольорових металів (Ag, Cu, Au, Al): dL1 ≈ 0–50 µH
           Для феромагнетиків (Fe, Ni): dL1 ≈ 200–2000 µH
           → Відмінно для розділення Fe/Ni від кольорових
           → Для Ag/Cu розрізнення dL1 = 0 для обох → не допомагає

СИМУЛЯЦІЯ МАТРИЦІ ПОМИЛОК (оціночна, без validation set):

  Очікувана reliable classification (>90% confidence):
  ✅ Ag (будь-яка) vs Fe/Ni/Co   — dL1, slope різко різні
  ✅ Ag vs Al                    — σ(Ag)/σ(Al) = 1.77 → RP різниця ~43%
  ✅ Ag vs W (вольфрам)           — σ(W)/σ(Ag) = 0.3 → RP різниця значна
  ✅ Ag vs Pb (свинець)           — σ(Pb)/σ(Ag) = 0.055 → RP ×18 різниця

  ⚠️ Потребує verification (можливі false positives):
  ⚠️ Ag925 vs Cu (бронза)        — близькі σ, k1/k2 різняться але тонко
  ❓ Ag925 vs Ag900               — σ різниця < 2% → потрібен R-01 validation set
  ❓ Au14k vs Ag900               — схожа σ, малий dL1

Accuracy estimate для bootstrap (одноякісна база):
  Fe/Ni detection: ~99% ✅ (dL1 discriminant is obvious)
  Ag vs non-Ag screening: ~95% ✅
  Probe identification (925/900/833): ~60–70% ⚠️ (без validation set невідомо)
```

**⚠️ ЗНАХІДКА FDB-1:** Архітектура коректно ідентифікує обмеження (§2.4 FINGERPRINT_DB — домінування dRp1), але bootstrap ваги (рівні) все одно включають dRp1_n у distance calculation. Для початкового deployment варто ВИЛУЧИТИ dRp1_n з distance calculation повністю та використовувати лише [k1, k2, slope, dL1_n] — 4D вектор без size-dependent компоненти.

### 6.2 Аналіз метрики відстані

```
Confidence transform: conf = exp(−dist² / σ²), σ=0.15

Аналіз σ=0.15:
  dist = 0.00 → conf = 1.00 (perfect match)
  dist = 0.10 → conf = 0.64
  dist = 0.15 → conf = 0.37 (σ точка — 63% match threshold)
  dist = 0.20 → conf = 0.18
  dist = 0.30 → conf = 0.01 (практично absent)

Для порогів відображення:
  ≥ 0.90 → "Висока впевненість"   dist < 0.072
  0.70–0.89 → "Можливий match"    dist 0.072–0.130
  < 0.70 → "Невизначено"         dist > 0.130

ПИТАННЯ: σ=0.15 — обрано як bootstrap-значення. Чи відповідає воно реальному
розкиду вимірювань одного металу через різні пристрої / температури / центрування?
Без validation set це невідомо. Якщо реальний σ_cluster = 0.30 (вдвічі більше),
то bootstrap σ=0.15 буде занадто суворим і більшість records покажуть < 70%.
```

---

## 7. Connectivity Architecture: аналіз

**Загальний стан:** специфіковано детально, жодного коду не написано. ADR-001..ADR-007 — добре аргументовані рішення.

| ADR | Рішення | Оцінка |
|--|--|--|
| ADR-001: mDNS `cointrace.local` | ✅ Правильно | Безкабельний discovery |
| ADR-002: REST over MQTT | ✅ Правильно | Простіше для embedded |
| ADR-003: BLE GATT Profile | ✅ Правильно | Стандартний підхід |
| ADR-004: AP mode provisioning | ✅ Правильно | Keyboard на Cardputer — краща альтернатива |
| ADR-005: SemVer для API | ✅ Правильно | Обов'язково для community |
| ADR-006: WebSocket streaming | ✅ Правильно | Real-time UI без polling |
| ADR-007: OTA подвійний банк | ✅ Правильно | Безпечне оновлення |

**Знахідки:**

| ID | Severity | Знахідка |
|--|--|--|
| **CONN-1** | 🟠 HIGH | WiFi + BLE одночасно: memory conflict на ESP32-S3FN8 (симуляція §4.3) — не задокументовано в CONNECTIVITY |
| **CONN-2** | 🟡 MEDIUM | OTA безпека: підпис прошивки (firmware signing) не специфікований — будь-який може залити шкідливий firmware через `/api/v1/ota` |
| **CONN-3** | 🟡 MEDIUM | API rate limiting відсутній — ZAP/arp-scan DoS на ESP32 WebServer тривіальний |
| **CONN-4** | 🟡 MEDIUM | BLE GATT RESULT characteristic — формат не специфікований (якою мовою? JSON? binary TLV?) |
| **CONN-5** | 🟢 LOW | Відсутній `tools/monitor.py` — корисний інструмент зазначений в документі, але не реалізований |

---

## 8. Logger Architecture: аналіз

**Logger — найзріліший реалізований компонент.** Phase 1 (Serial + RingBuffer) завершена. Всі знайдені bugs LA-1..LA-10 виправлені.

**Сильні сторони:**
- Mutex поза форматуванням → deadlock-free (LA-7 fix)
- SDTransport async (черга + task) → NF1 < 100 µs дотримано
- `usePsram=false` default → Bug B-01 в main.cpp виправлений у документації (але не в коді!)
- Контракт "NO LOG IN WRITE" — правильний архітектурний патерн

**Відкрита знахідка:**

| ID | Severity | Знахідка |
|--|--|--|
| **LOG-1** | 🟠 HIGH | `RingBufferTransport gRingTransport(100, usePsram=true)` в `src/main.cpp` — Bug B-01 досі в КОДІ, виправлений тільки в документації |
| **LOG-2** | 🟡 MEDIUM | JSON escaping (LA-3) задокументовано як "Phase 2 implementor obligation" — без конкретного строку реалізації |
| **LOG-3** | 🟢 LOW | LOG_DEBUG() завжди no-op (відсутній `-DCOINTRACE_DEBUG=1`) — відсутня debug збірка для розробки |

---

## 9. Крос-архітектурний аналіз: зв'язки та розриви

### 9.1 Матриця залежностей між доменами

```
               │ Plugin │ Logger │ Storage │ Fingerprint│ Connect.│ LDC1101 │
───────────────┼────────┼────────┼─────────┼────────────┼─────────┼─────────┤
Plugin System  │   —    │  ✅ dep │  ✅ dep  │     —      │    —    │  ✅ impl │
Logger         │  ✅ dep │   —    │  ✅ dep  │     —      │  📝 dep │    —    │
Storage        │  ✅ dep │  ✅ dep │   —     │   ✅ dep   │    —    │  ✅ dep  │
Fingerprint DB │  ✅ dep │    —   │  ✅ dep  │    —       │  📝 dep │  ✅ dep  │
Connectivity   │  📝 dep │  ✅ dep │  📝 dep │  📝 dep    │   —     │  📝 dep │
LDC1101 Plugin │  ✅ dep │  ✅ dep │  ✅ dep  │   ✅ dep   │    —    │   —     │

✅ = залежність задокументована і узгоджена
📝 = залежність задокументована, реалізація майбутня
— = немає прямої залежності
```

### 9.2 Критичні integration gaps

**GAP-1: Coin detection trigger — відсутній скрізь**

```
Жоден документ не відповідає на питання:
"Як firmware дізнається що монета покладена і потрібно запустити вимірювання?"

Варіанти (неоприділені):
  A: Threshold-based RP → LDC1101 update() порівнює з baseline
                          Проблема: потрібен стабільний baseline (recalibration?)
  B: IMU-triggered → BMI270 детектує вібрацію при кладанні монети
                     Проблема: vibration detection не специфікована у BMI270Plugin
  C: Manual button → кнопка G0 або клавіша
                     Найпростіше, але погана UX
  D: Capacitive/optical proximity → немає такого сенсора у архітектурі
  
ВИСНОВОК: Whole measurement workflow (від "монета покладена" до "результат на екрані")
не визначений в жодному технічному документі. COLLECTOR_USE_CASE описує UX, але не
механізм тригеру. Це GAP в архітектурі.
```

**GAP-2: Calibration workflow LDC1101 — неповний**

```
SR-01 (Storage) вимагає що калібрування "зберігається між вимкненнями".
LDC1101_ARCHITECTURE описує raw RP/L значення.
COLLECTOR_USE_CASE описує "5 монет за 30 хвилин" як calibration.

АЛЕ: що саме зберігається як "калібрування" LDC1101 в NVS?
  - rp0 baseline (без монети)? → Який RESP_TIME? При якій температурі?
  - RP_SET значення?
  - Offset? Scaling factor?
  
NVS design специфікує "Sensor cal (8 float fields)" але не каже ЯКІ поля.
Fingerprint DB використовує RAW cp0..rp3 values — тобто baseline є частиною запису.
Але чи оновлюється baseline при кожному вимірюванні? Чи зберігається одного разу?

Без чіткої специфікації calibration protocol → різні прилади дадуть несумісні data.
```

**GAP-3: Display Pipeline — не специфікована**

```
IDisplayPlugin :: render() → що саме відображається і коли?

Документи не містять:
  - State machine UI (які екрани, які переходи)
  - Framerate target (скільки FPS потрібно)
  - Конкурентний доступ до SPI між Display refresh та LDC1101 update()
  - Memory для buffers (64.8 KB framebuffer вже в розрахунку, але LVGL потребує більше)

COLLECTOR_USE_CASE показує mockup результату, але не архітектуру UI framework.
M5Stack використовує M5GFX (LovyanGFX) — де це в архітектурі?
```

---

## 10. Непокриті архітектурні домени

Незалежний огляд виявив такі домени, що **взагалі не охоплені** архітектурними документами:

| Домен | Критичність | Опис gap |
|--|--|--|
| **UI / Display Architecture** | 🔴 HIGH | Немає документа про UI state machine, framerate, LVGL/M5GFX вибір |
| **Power Management** | 🟠 HIGH | Немає специфікації power budget, deep sleep, battery life |
| **Coin Detection Trigger** | 🟠 HIGH | Немає механізму автоматичного початку вимірювання |
| **Coil Characterization** | 🟠 HIGH | Немає специфікації котушки MIKROE-3240 (C_SENSOR, fSENSOR, геометрія) |
| **Temperature Compensation** | 🟠 HIGH | LDC1101 RP/L drift ±11% per 30°C — не документовано |
| **Calibration Protocol** | 🟡 MEDIUM | Точна процедура baseline калібрування LDC1101 не визначена |
| **OTA Security** | 🟡 MEDIUM | Firmware signing/verification відсутня |
| **Test Strategy** | 🟡 MEDIUM | Hardware-in-the-loop тести не специфіковані (тільки unit tests) |
| **Error Budget** | 🟡 MEDIUM | Сумарна похибка системи (electrical + mechanical + thermal) не розрахована |
| **Mechanical Design** | 🟡 MEDIUM | Spacer system, coin guide, platform design не специфіковані |
| **EMI / Shielding** | 🟢 LOW | Вплив електромагнітних перешкод (WiFi 2.4 GHz vs LDC1101) не оцінений |
| **Production Variant** | 🟢 LOW | Немає roadmap для variant з кращим MCU (якщо ESP32-S3FN8 виявиться тісним) |

---

## 11. Зведена таблиця знахідок

| ID | Severity | Домен | Знахідка | Статус |
|--|--|--|--|--|
| **SYS-1** | 🔴 CRITICAL | LDC1101 | `readRP()` — MSB читається ДО LSB у двох окремих CS-транзакціях; bug підтверджений у самому MikroE reference SDK (ldc1101.c) | B-1 в PA4 — відкрито |
| **SYS-1b** | 🟠 HIGH | LDC1101 | RP_SET mismatch: LDC1101_ARCHITECTURE.md = 0x07 (MAX=0.75kΩ/MIN=0.75kΩ), MikroE work config = 0x26 (MAX=24kΩ/MIN=1.5kΩ) — потрібне явне архітектурне рішення | Новий |
| **SYS-2** | 🔴 CRITICAL | Plugin | IInputPlugin API конфлікт `pollEvent()` vs `hasEvent()`/`getEvent()` — 3 ітерації без рішення | A-2 в PA4 — відкрито |
| **SYS-3** | 🔴 CRITICAL | Logger | `usePsram=true` у `src/main.cpp` (Bug B-01) — в коді, не виправлено | B-01 у Storage — відкрито у коді |
| **SYS-4** | 🔴 CRITICAL | Display | Display (ST7789V2) на VSPI без spiMutex — race condition з LDC1101 та SD | Новий |
| **SYS-5** | � MEDIUM | LDC1101 | TC1/TC2 тепер відомі з MikroE SDK (C1=0.75pF/R1=21.1kΩ, C2=3pF/R2=30.5kΩ) — але різняться від defaults POR і не задокументовані в LDC1101_ARCHITECTURE.md | Частково вирішений |
| **SYS-6** | 🟠 HIGH | LDC1101 | Температурна компенсація відсутня (±11% per 30°C) | Новий |
| **SYS-7** | 🟠 HIGH | LDC1101 | fSENSOR оцінено як 200–500 kHz (не 1–2 MHz); C_SENSOR і точна геометрія котушки потребують вимірювання | Частково вирішений |
| **SYS-8** | 🟠 HIGH | Plugin | HX711/BH1750 — blocking update() ~100-500 ms; порушення контракту | A-3/B-2 в PA4 |
| **SYS-9** | 🟠 HIGH | System | WiFi+BLE одночасно: ~170 KB → OOM на 337 KB heap | Новий |
| **SYS-10** | 🟠 HIGH | System | Coin detection trigger — відсутній у всій архітектурі | Новий (GAP-1) |
| **SYS-11** | 🟡 MEDIUM | Fingerprint | dRp1_n у distance calculation вносить size-bias (домінування) | FDB §2.4 — known |
| **SYS-12** | 🟡 MEDIUM | Fingerprint | Ag925/Ag900 диференціація: σ різниця < 2% < шум LDC1101 | Новий (SIM-2) |
| **SYS-13** | 🟡 MEDIUM | LDC1101 | Spacer tolerance ±0.05 мм: вимога виробничої точності без механічного design | Новий (SIM-4) |
| **SYS-14** | 🟡 MEDIUM | Storage | LittleFS_data fail → "Safe Mode" undefined (що робить пристрій?) | Новий |
| **SYS-15** | 🟡 MEDIUM | Plugin | Watchdog плагінів відсутній — повільний плагін непомітно деградує систему | Новий |
| **SYS-16** | 🟡 MEDIUM | Fingerprint | σ=0.15 confidence bootstrap без validation set — невідомо чи реалістично | Known — R-01 |
| **SYS-17** | 🟡 MEDIUM | Connectivity | OTA firmware signing відсутнє | Новий (CONN-2) |
| **SYS-18** | 🟡 MEDIUM | Plugin | `checkCalibration()` завжди true після reboot (lastCalibrationTime=0) | C-4 у PA4 |
| **SYS-19** | 🟢 LOW | Fingerprint | Ag/Cu diferenciation — потребує HX711 як допоміжна ознака (density) | Новий (SIM-1) |
| **SYS-20** | 🟢 LOW | Storage | coredump partition існує але `CONFIG_ESP_COREDUMP_ENABLE=1` відсутній | B-05 |
| **SYS-21** | 🟢 LOW | Logger | LOG_DEBUG() завжди no-op (відсутній `-DCOINTRACE_DEBUG=1`) | LA-10 — known |
| **SYS-22** | ℹ️ INFO | Fingerprint | P-02 розірваний дисклеймер (юридична фраза) — не виправлено | P-02 в PreImpl v3 |
| **SYS-23** | ℹ️ INFO | Plugin | `getGraphicsLibrary()` повертає `void*` — порушення type safety C++ | E-1 в PA4 |

---

## 12. Рівень готовності до імплементації

### Компоненти, готові до написання коду ЗАРАЗ

| Компонент | Готовність | Умова |
|--|--|--|
| LDC1101Plugin v1 (State Machine) | ✅ 95% | Виправити SYS-1 (readRP burst) перед merge |
| Logger Phase 2 (SDTransport, WebSocket) | ✅ 90% | Виправити SYS-3 (usePsram в main.cpp) |
| StorageManager (NVS + LittleFS) | ✅ 85% | Розробити по Option C; FS partition table |
| Fingerprint compute_vector.py | ✅ 95% | Готово (PreImpl v3 підтвердив) |
| validate_fingerprint.py + CI | ✅ 95% | Готово |
| build_aggregates.py | ✅ 90% | Готово |
| Quick Screen UI | ✅ 80% | Потрібна UI Architecture (GAP-3) |
| BMI270Plugin | ✅ 85% | Watchdog missing (SYS-15) |

### Компоненти, що потребують додаткового проектування

| Компонент | Готовність | Що потрібно |
|--|--|--|
| Display Pipeline / UI Architecture | ❌ 30% | Окремий документ UI_ARCHITECTURE.md |
| Power Management | ❌ 20% | Документ POWER_MANAGEMENT.md |
| Coin Detection (trigger mechanism) | ❌ 10% | ADR рішення в LDC1101_ARCHITECTURE |
| HX711Plugin (async) | ⚠️ 50% | Перероблення на AsyncSensorPlugin |
| Temperature Compensation | ❌ 15% | Модель компенсації; зовнішній термодатчик |
| OTA Security | ⚠️ 40% | Firmware signing ADR |

### Загальна оцінка готовності

```
Готовність до першого hardware тесту (LDC1101 читає монету):   ✅ ~90%
  Блокери: SYS-1 (readRP burst fix), SYS-3 (usePsram в main.cpp)

Готовність до Phase 2 (storage + connectivity):                ✅ ~75%
  Блокери: Option C partition table, StorageManager impl

Готовність до публічного beta (community DB):                  ⚠️ ~55%
  Блокери: R-01 validation set, SYS-4 (display mutex), SYS-10 (coin detection),
           SYS-17 (OTA security), UI Architecture

Готовність до production release:                              📝 ~35%
  Блокери: Power Management, Temperature Compensation,
           Hardware-in-loop tests, Mechanical Design
```

---

## 13. Пріоритетні рекомендації

### Блок R-A: Критичні виправлення перед першим hardware тестом (1–3 дні)

**R-A1** — Виправити `src/main.cpp`: `RingBufferTransport gRingTransport(100, false)` (SYS-3)

**R-A2** — Виправити `readRP()` у PLUGIN_DIAGNOSTICS: burst SPI замість двох окремих транзакцій (SYS-1)
```cpp
// Правильний burst:
ctx->spi->beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
digitalWrite(csPin, LOW);
ctx->spi->transfer(REG_RP_DATA_LSB | 0x80); // LSB ПЕРШИМ — розблоковує
uint8_t lsb = ctx->spi->transfer(0);
uint8_t msb = ctx->spi->transfer(0);
digitalWrite(csPin, HIGH);
ctx->spi->endTransaction();
```

**R-A3** — Визначити CS пін дисплея та додати у список SPI-пристроїв (SYS-4); перевірити чи M5GFX використовує spiMutex

**R-A4** — Вирішити `IInputPlugin` API конфлікт: обрати `pollEvent()` (CONTRACT §3.3) і синхронізувати у INTERFACES_EXTENDED (SYS-2)

### Блок R-B: Архітектурні документи (1–2 тижні)

**R-B1** — Написати `LDC1101_COIL_SPECIFICATION.md`:
  - Виміряти fSENSOR на реальному пристрої (читати L_DATA_raw без монети, розрахувати)
  - Виміряти C_SENSOR LCR-метром або зворотним розрахунком
  - Задокументувати геометрію котушки: діаметр, центрування
  - Специфікувати TC1/TC2 значення для MIKROE-3240

**R-B2** — Написати ADR "Coin Detection Trigger" в LDC1101_ARCHITECTURE.md:
  - Обрати один механізм (рекомендовано: threshold RP vs baseline + debounce)
  - Визначити baseline update strategy (once per calibration? per session? rolling average?)
  - Специфікувати у State Machine update()

**R-B3** — Написати `UI_ARCHITECTURE.md`:
  - State machine екранів (Home → Measure → Result → History → Settings)
  - FreeRTOS task для display refresh (окремий від Plugin loop)
  - Вирішити spiMutex для Display vs LDC1101 conflict

**R-B4** — Додати розділ "Температурна компенсація" в LDC1101_ARCHITECTURE.md:
  - Моделі дрейфу RP(T) та L(T)
  - Рішення: або внешній термодатчик (BME280 замість BH1750?), або обмеження "±5°C від калібрування"
  - Оновити fingerprint record format: додати `temp_c_at_baseline` в `conditions`

### Блок R-C: LHR режим — стратегічна рекомендація (MVP+1)

**R-C1** — Документ "LHR Mode Feasibility Study":

LHR (High-Resolution L mode, 24-bit) може значно підвищити точність L вимірювання. При стандартному режимі L_DATA = 16 bit (65536 рівнів). LHR дає 24 bit (16.7M рівнів) — теоретично в 256 разів більша роздільна здатність.

Якщо проблема Ag925/Ag900 диференціації (SYS-12) підтвердиться на validation set — LHR може бути вирішенням. Рекомендую провести паралельний тест: LHR vs standard mode для Ag999/Ag925/Ag900 монет, щоб визначити чи LHR дає практично значущу різницю.

### Блок R-D: Validation set — пріоритет #1 перед community launch

**R-D1** — R-01: Validation set 30+ монет (Ag999, Ag925, Ag900, Ag833, Cu, Al, Fe, Ni):
  - Верифікувати реальний класифікаційний простір (не теоретичний)
  - Визначити реальні ваги w₁–w₅ для weighted Euclidean
  - Верифікувати temperature sensitivity (виміряти ті самі монети при 15°C та 35°C)
  - Верифікувати Ag/Cu ambiguity (SIM-1) — наскільки ця проблема реальна

**R-D2** — Перший certified anchor:
  - NBU Ag999 → `nbu_ag999_001.json`, `reference_quality: "certified"`
  - Зберегти разом з fSENSOR actual, C_SENSOR actual, temp_c, дата
  - Цей запис стане незмінним reference point для всієї community DB

---

## Підсумок

**CoinTrace має добре спроектовану програмну архітектуру** з детальним Plugin System, коректно специфікованим контрактом та правильними рішеннями щодо Storage та Fingerprint DB. Зрілість документації суттєво зросла через три незалежні ітерації рецензування.

**Головний ризик проекту — не програмний, а фізично-вимірювальний.** Система залежить від:
1. Точних параметрів котушки (незадокументовані)
2. Механічної точності spacer system (±0.05 мм)
3. Температурної стабільності під час вимірювань
4. Validation set для підтвердження що k1/k2 алгоритм реально розрізняє метали

**Відсутній центральний workflow-документ**: "монета покладена → виміряно → результат показано → збережено" — цей flow описаний у UX (COLLECTOR_USE_CASE) але не має відповідного технічного документа з State Machine, triggers, timeouts та error handling.

**Готовність до першого hardware тесту LDC1101: ~90% (після R-A1..R-A4).**

---

*Аудит виконано: 13 березня 2026 · v1.0.0*  
*Оновлено: 13 березня 2026 · v1.1.0 — додано аналіз MikroE SDK (ldc1101.c/ldc1101.h, github.com/MikroElektronika/mikrosdk_click_v2)*  
*Охоплено: 14 архітектурних документів + 4 зовнішні рецензії + 1 use case документ + MikroE reference library*  
*18 симуляцій та моделювань + підтвердження bug SYS-1 в еталонному коді*  
*Авторство: Незалежний embedded-архітектор (зовнішня позиція)*
