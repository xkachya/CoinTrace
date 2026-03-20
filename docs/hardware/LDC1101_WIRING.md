# LDC1101 — Hardware Wiring Guide
# M5Stack Cardputer-Adv → MIKROE-3240 (LDC1101 Click board)

**Версія:** 1.1.0  
**Дата:** 2026-03-20  
**Підтверджено по:** `src/main.cpp`, `platformio.ini`, `data/plugins/ldc1101.json`,  
`docs/guides/UART_DEBUG_SETUP.md`, [mikroe.com/ldc1101-click](https://www.mikroe.com/ldc1101-click) (офіційний пінаут)

---

## ⚠️ Критична перевірка перед підключенням — JP1 jumper

На платі MIKROE-3240 є **SMD-перемичка JP1 (MODE SEL)**:

| Позиція JP1 | Поведінка |
|---|---|
| **Left (default) ← використовувати** | Pin 5 (MISO) = **SDO** — SPI дані, нормальна робота |
| Right | Pin 5 (MISO) = **INTB** — interrupt вихід, **SPI не працює!** |

> **JP1 ОБОВ'ЯЗКОВО має бути в позиції Left (default).** Якщо JP1 зміщено вправо — MISO пін переключається на interrupt, SPI читання повертає сміт, і `CHIP_ID` не буде 0xD4.

---

## 1. Огляд підключення

LDC1101 підключається через **SPI** до ESP32-S3FN8 на M5Stack Cardputer-Adv.
Всі 4 SPI сигнали + CS виведені на **задній EXT 2.54-14P роз'єм** — більше нічого не потрібно.

```
M5Cardputer-Adv                      MIKROE-3240 (LDC1101 Click)
EXT 2.54-14P                         mikroBUS header (офіційний пінаут)
─────────────────                    ──────────────────────────────────────
Pin  7  G40  SCK  ──────────────────→ SCK  (Bus Pin 4, chip: SCLK)
Pin  9  G14  MOSI ──────────────────→ MOSI (Bus Pin 6, chip: SDI  — data MCU→sensor)
Pin 11  G39  MISO ←──────────────────  MISO (Bus Pin 5, chip: SDO  — data sensor→MCU)
Pin 13  G5   CS   ──────────────────→ CS   (Bus Pin 3, chip: CSB  — active LOW)
Pin  4  GND       ──────────────────→ GND  (Bus Pin 8)
3.3V (¹)          ──────────────────→ 3V3  (Bus Pin 7)
```

> **¹ Джерело 3.3V — дивись §3.**

---

## 2. Повний пінаут EXT 2.54-14P

Задній 14-пінний роз'єм (вид зверху, ліворуч=непарні, праворуч=парні):

```
 ФУНКЦІЯ    ПІН     L   R   ПІН    ФУНКЦІЯ
 ─────────  ───  ──── ────  ───  ─────────────────────────────
 RESET      G3    1 ● ● 2   5VIN  ← 5V вхід (НЕ для LDC!)
 INT        G4    3 ● ● 4   GND   ← ┐
 BUSY       G6    5 ● ● 6   5VOUT ← 5V вихід (НЕ для LDC!)
 SCK ✅     G40   7 ● ● 8   G8       I2C SDA
 MOSI ✅    G14   9 ● ● 10  G9       I2C SCL
 MISO ✅    G39  11 ● ● 12  G13      UART RX (debug)
 CS ✅      G5   13 ● ● 14  G15      UART TX (debug)
```

---

## 3. CLKIN (mikroBUS Pin 16) — wiring & PCB checklist

This section gives precise wiring and PCB rules to ensure a reliable 16 MHz CLKIN for LHR mode.

- Physical mapping:
  - mikroBUS Pin 16 is labeled **PWM** on the mikroBUS header — this is the `CLKIN` input for LDC1101 when used in LHR mode.
  - Determine the board-specific GPIO ↔ mikroBUS mapping for your carrier board and record it as `CLKIN_GPIO` in the board hardware README. Do NOT assume a GPIO number across boards.

- Electrical rules:
  - Voltage: **3.3 V TTL** only. Do not drive CLKIN with 5 V. If your source is 5 V, use a level shifter.
  - Series resistor: add a small series resistor (22–47 Ω) in the CLKIN line close to the MCU pin to dampen reflections when routing >20 mm.
  - Avoid pull-ups/pull-downs on the CLKIN net; the LDC1101 expects a clean clock input.

- Routing rules (PCB):
  - Route CLKIN as a single short trace; keep it as short as practicable (target < 50 mm).
  - Keep CLKIN trace on the top layer with a continuous ground plane on the adjacent layer underneath to reduce EMI and jitter.
  - Avoid vias on the CLKIN net where possible; if unavoidable, minimize via count and keep them close to the source.
  - Do not route CLKIN underneath noisy power circuitry (switching regulators) or adjacent to high-speed switching nets (USB, high-current traces).
  - If CLDO (chip clock output) is to be exposed on a custom board, treat it the same as CLKIN for routing (short, ground plane, series resistor optional).

- Decoupling and power:
  - Place the LDC1101 recommended decoupling capacitor (0.1 µF) close to its VDD pin per datasheet.
  - Keep GND return path short from LDC1101 to the ground plane.

- JP1 jumper check (MIKROE-3240):
  - JP1 **MUST** be Left (SDO) for SPI operation. Document this on the assembly drawing and BOM for production.

- Mechanical / connector notes:
  - If using a header cable to connect the Click board, avoid long loose ribbon cables for CLKIN; use short shielded or twisted pair wiring and keep CLKIN paired with its ground return.

- Test points and debugging:
  - Add a test pad or small SMA header for CLKIN near the mikroBUS connector for oscilloscope probing during bring-up.
  - Add a test pad for LDC1101 `DEVICE_ID` (SPI MISO/SCLK/CS accessible) for reading `DEVICE_ID`=0xD4 to confirm SPI comms.

- Acceptance criteria (bring-up):
  - With MCU generator enabled and no LDC1101 attached: measured on scope at the mikroBUS Pin16 pad: 3.3 V TTL, 16.000 MHz ±0.5%, duty ≈ 50%.
  - With LDC1101 attached and LHR enabled: `calibrate()` returns stable `lhrRaw` values and `fSENSOR` consistent with expected coil frequency.

---

## 4. CLDO (chip clock output) note

The LDC1101 includes a clock output (CLDO) in some reference schematics. On MIKROE-3240 CLDO is not routed to mikroBUS by default. If you design a custom breakout and expect to use CLDO as a clock source for the MCU, follow the same routing and buffering rules as for CLKIN and ensure level compatibility (CLDO is typically TTL-level but verify per datasheet). Prefer buffering CLDO through a low-skew driver if using it as the MCU system clock input.

---

## 5. Firmware & mapping checklist

- Ensure the board-specific mapping file documents `CLKIN_GPIO` and that the platform bring-up code exports it to `ldc1101.json` or board config.
- Provide two firmware methods for generating CLKIN (documented in `LDC1101_CLKIN_INTEGRATION.md`): `SPI SCLK + DMA` (preferred) and `LEDC` (fallback). Include a runtime config option `ldc1101.hw.clkin_source = "spi"|"ledc"`.

---

Add these wiring checks to the production assembly checklist and to the `R-01 hardware test` procedure (ADR-LDC-001).

**✅ = піни LDC1101 SPI шини — всі знаходяться на лівій стороні (pins 7, 9, 11, 13)**

> **Перевірка:** ці ж GPIO підтверджені в коді:
> - `src/main.cpp`: `SPI.begin(40, 39, 14)` → SCK=G40, MISO=G39, MOSI=G14
> - `data/plugins/ldc1101.json`: `"spi_cs_pin": 5` → CS=G5

---

## 3. Живлення (3.3V)

LDC1101 та MIKROE-3240 Click board потребують **3.3V**. На 14-пінному EXT 3.3V пін **не виведений** — є тільки 5V.

### Варіант A — зовнішній джерело 3.3V (рекомендується для прототипу)

Якщо у вас є будь-яке джерело 3.3V (макетна плата, регулятор, лабораторний БЖ):

```
Зовнішній 3.3V → VCC pin MIKROE-3240
EXT Pin 4 (GND) → GND pin MIKROE-3240  (спільна земля обов'язкова!)
```

### Варіант B — Grove порт Cardputer

Стандартний M5Cardputer має Grove порт (4-pin 2mm):

```
Grove Pin 1 = GND
Grove Pin 2 = VCC (5V або 3.3V — залежить від версії)
```

> ⚠️ Перевірте вольтметром напругу на Grove VCC **перед** підключенням MIKROE-3240!
> Якщо 5V — потрібен AMS1117-3.3 або інший LDO регулятор.

### Варіант C — 3.3V від LDO на макетній платі

AMS1117-3.3 або LE33:

```
EXT Pin 6 (5VOUT) → LDO Vin
LDO Vout (3.3V)   → VCC MIKROE-3240
EXT Pin 4 (GND)   → LDO GND → GND MIKROE-3240
```

> **Рекомендація:** для першого підключення — варіант A з лабораторним БЖ (найпростіший і безпечний).

---

## 4. Діаграма підключення (кольорові дроти)

```
EXT 2.54-14P        MIKROE-3240
(лівий ряд)         (LDC1101 Click)
──────────┐         ┌──────────
Pin 7  G40│──жовт──→│SCLK
Pin 9  G14│──синій─→│SDI
Pin 11 G39│←зелен──│SDO
Pin 13 G5 │──білий─→│CSB
Pin 4  GND│──чорн──→│GND
3.3V (¹)  │──черв──→│VCC
──────────┘         └──────────
```

---

## 5. MIKROE-3240 — повна таблиця пінів (офіційний пінаут)

MIKROE-3240 — LDC1101 Click board від MikroElektronika. mikroBUS має 16 пінів (по 8 з кожного боку).

| mikroBUS Pin | Назва | Сигнал LDC1101 | Підключити до (Cardputer) | Примітка |
|:---:|---|---|---|---|
| 1 | AN | NC | — | Не підключати |
| 2 | RST | NC | — | Не підключати |
| **3** | **CS** | **CSB** | **EXT Pin 13 (G5)** | ✅ Chip Select |
| **4** | **SCK** | **SCLK** | **EXT Pin 7 (G40)** | ✅ SPI Clock |
| **5** | **MISO** | **SDO** ¹ | **EXT Pin 11 (G39)** | ✅ Data out (потребує JP1=Left!) |
| **6** | **MOSI** | **SDI** | **EXT Pin 9 (G14)** | ✅ Data in |
| **7** | **3.3V** | Живлення | **3.3V (варіант §3)** | ✅ НЕ 5V |
| **8** | **GND** | GND | **EXT Pin 4 (GND)** | ✅ Земля |
| 9 | GND | GND | EXT Pin 4 (GND) | Спільна земля (з'єднана з Pin 8) |
| 10 | 5V | NC | — | Не підключати |
| 11 | SDA | NC | — | Не підключати |
| 12 | SCL | NC | — | Не підключати |
| 13 | TX | NC | — | Не підключати |
| 14 | RX | NC | — | Не підключати |
| 15 | **INT** | INTB | — | Polling mode — не підключати ² |
| 16 | **PWM** | CLKIN | — | LHR mode clock — не підключати ³ |

> **¹ SDO/INTB multiplexed:** Pin 5 (MISO) є спільним для SDO (SPI) та INTB (interrupt). JP1 jumper вибирає режим. **JP1 = Left (default) = SDO активний = SPI працює.**

> **² INT (Pin 15):** CoinTrace v1 використовує polling (читання STATUS регістра в `update()`). INT не задіяний.

> **³ PWM/CLKIN (Pin 16):** Потрібний тільки для **LHR (24-bit) режиму** (ADR-LHR-001). Подати 16 MHz clock (= `clkin_freq_hz` в конфігу). Для базового RP+L режиму — не потрібен.

---

## 6. SPI параметри (для довідки)

| Параметр | Значення |
|---|---|
| Режим | SPI_MODE0 (CPOL=0, CPHA=0) |
| Порядок бітів | MSBFIRST |
| Тактова частота | 4 MHz |
| CS логіка | Active LOW |
| Напруга логіки | 3.3V |

Підтверджено з `LDC1101Plugin.h`:
```cpp
ctx_->spi->beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
```

---

## 7. Конфігурація прошивки

Файл `data/plugins/ldc1101.json` — не потребує змін при стандартному підключенні:

```json
{
  "name": "LDC1101",
  "enabled": true,
  "spi_cs_pin": 5,
  "resp_time_bits": 7,
  "rp_set": 38,
  "clkin_freq_hz": 16000000,
  "coin_detect_threshold": 0.85,
  "coin_release_threshold": 0.92
}
```

Ключові параметри:
- `spi_cs_pin: 5` → GPIO5 = EXT Pin 13 ✅
- `rp_set: 38` (0x26) → стандартне значення MIKROE-3240 (MikroE SDK default)
- `resp_time_bits: 7` → RESP_TIME = 6144 cycles, максимальна якість/SNR

---

## 8. Перевірочний список підключення

- [ ] **JP1 (MODE SEL) на MIKROE-3240 — в позиції Left (default)** ← найважливіше!
- [ ] MIKROE-3240 3V3 (Pin 7) підключено до **3.3V** (перевірити вольтметром)
- [ ] Спільна земля: EXT Pin 4 ↔ GND Pin 8 MIKROE-3240
- [ ] SCK: EXT Pin 7 (G40) → mikroBUS Pin 4 (SCK)
- [ ] MOSI: EXT Pin 9 (G14) → mikroBUS Pin 6 (MOSI/SDI)
- [ ] MISO: EXT Pin 11 (G39) ← mikroBUS Pin 5 (MISO/SDO)
- [ ] CS: EXT Pin 13 (G5) → mikroBUS Pin 3 (CS/CSB)
- [ ] INT пін (mikroBUS Pin 15) — вільний (не підключати)
- [ ] PWM/CLKIN пін (mikroBUS Pin 16) — вільний для базового RP+L режиму
- [ ] 5V пін (mikroBUS Pin 10) та EXT 5V піни (Pin 2, 6) — не підключати до MIKROE-3240

---

## 9. Поширені помилки

| Проблема | Причина | Рішення |
|---|---|---|
| `CHIP_ID mismatch: expected 0xD4, got 0xFF` | CS пін неправильний або не підключений | Перевірити G5 → CS (Bus Pin 3) |
| `CHIP_ID mismatch: expected 0xD4, got 0x00` | MISO не підключено (SDO вільний, тягнеться до GND) | Перевірити G39 → MISO (Bus Pin 5) |
| `CHIP_ID mismatch` + JP1 не перевірений | JP1 в позиції Right → MISO = INTB, не SDO | Перемістити JP1 в позицію **Left** |
| `POR timeout: chip not ready after 100ms` | VCC не подано або недостатня напруга | Перевірити 3.3V на Bus Pin 7 (3V3) |
| `Coil not oscillating` | RP_SET не відповідає котушці | Підібрати `rp_set` в конфігу (§9 чекліста) |
| Нестабільні значення RP | SCK або MOSI поганий контакт | Перевірити пайку/дроти G40 (Pin 4) / G14 (Pin 6) |
| LHR режим не працює | PWM/CLKIN (Pin 16) не підключено | Подати 16 MHz clock на Pin 16 (ADR-LHR-001) |

---

## 10. Наступні кроки після підключення

1. Flash firmware: `pio run -e cointrace-dev -t upload`
2. Відкрити лог: `pio device monitor --port COM4 --baud 115200`
3. Шукати: `Ready. CS=5, RESP_TIME=0x07, RP_SET=0x26`
4. Провести self-test та калібрування — дивись `docs/LDC1101_INTEGRATION_CHECKLIST.md`

---

**Файл підготовлено:** 2026-03-20 | CoinTrace Hardware Documentation
