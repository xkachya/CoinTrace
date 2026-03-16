# Storage Architecture — CoinTrace™

**Статус:** ✅ Accepted v1.7.0 — [PRE-1]..[PRE-8] вирішено; [F-05] protocol_id placeholder виправлено; P-1 Acceptance Criteria додано; §18 Factory Reset to Stock Firmware додано; §14.3 Soft Shutdown (v1.5 backlog); §17.2 boot [1.4] deep sleep check  
**Версія:** 1.7.0  
**Дата:** 2026-03-16  
**Автор:** Yuriy Kachmaryk

> ⚠️ **КРИТИЧНО:** Рішення щодо partition layout та NVS namespace structure неможливо змінити після
> першого public release без breaking change для всіх користувачів (їхні дані будуть втрачені).
> Цей документ фіксує всі варіанти та компроміси до написання першого рядка storage-коду.

---

## Зміст

1. [Навіщо цей документ](#1-навіщо-цей-документ)
2. [Вимоги (Requirements)](#2-вимоги-requirements)
3. [Інвентар: залізо та його обмеження](#3-інвентар-залізо-та-його-обмеження)
4. [Поточний стан та виявлені блокери](#4-поточний-стан-та-виявлені-блокери)
5. [Три-тирна архітектура зберігання](#5-три-тирна-архітектура-зберігання)
6. [Варіанти Partition Table (Options A / B / C)](#6-варіанти-partition-table-options-a--b--c)
7. [Tier 0 — NVS: дизайн namespace](#7-tier-0--nvs-дизайн-namespace)
8. [Tier 1 — LittleFS: структура файлів](#8-tier-1--littlefs-структура-файлів)
9. [Tier 2 — SD Card: структура та деградація](#9-tier-2--sd-card-структура-та-деградація)
10. [Критичні питання для обговорення](#10-критичні-питання-для-обговорення)
11. [Архітектурні рішення (ADR)](#11-архітектурні-рішення-adr)
12. [Integration: Logger, Plugin System, Connectivity](#12-integration-logger-plugin-system-connectivity)
13. [Wear leveling та довговічність flash](#13-wear-leveling-та-довговічність-flash)
14. [Factory Reset Strategy](#14-factory-reset-strategy)
15. [Фазовий roadmap](#15-фазовий-roadmap)
16. [Матриця виживаємості даних](#16-матриця-виживаємості-даних)

---

## 1. Навіщо цей документ

CoinTrace є embedded пристроєм, що зберігає **три принципово різних категорії даних**:

- **Системні дані** — WiFi credentials, калібрування датчика, налаштування (повільно змінюються,
  ніколи не мають загубитись)
- **Операційні дані** — виміри, результати матчингу, логи (записуються часто, можуть ротувати)
- **Контентні дані** — web UI, fingerprint DB, зображення монет (велике, рідко оновлюється)

Між цими категоріями існують фундаментальні конфлікти:
- **uploadfs** повністю знищує Tier 1 (LittleFS) — якщо виміри там, вони пропадуть при оновленні UI
- **OTA firmware update** не торкається LittleFS та NVS — але flash partition table теж не змінюється
- **100K write cycles** на сектор — якщо логи пишуть в один файл без LittleFS wear leveling, flash здохне за місяці
- **Калібрування датчика** займає ~45 секунд реального часу — якщо губиться при `uploadfs`, це катастрофа

Рішення прийняті зараз (partition table, NVS namespaces, файлова ієрархія) **неможливо змінити після першого зовнішнього користувача** без міграційного скрипту та breaking change. Цей документ фіксує всі варіанти, аргументи за/проти та відкриті питання для вирішення до початку імплементації.

---

## 2. Вимоги (Requirements)

### 2.1 Функціональні вимоги

| ID | Вимога | Пріоритет | Джерело |
|---|---|---|---|
| **SR-01** | Калібрування датчика LDC1101 зберігається між вимкненнями та OTA оновленнями | MUST | COLLECTOR_USE_CASE |
| **SR-02** | WiFi credentials зберігаються між вимкненнями, не губляться при `uploadfs` | MUST | CONNECTIVITY v1.2.0 |
| **SR-03** | Останні N вимірів доступні після перезавантаження. Реалізація: LittleFS_data `/data/measurements/m_XXX.json`, N=RING_SIZE=250 (персистентні між reboot). DEGRADED (LittleFS_data fail): N=0 після cold boot — API повертає 404. | MUST | COLLECTOR_USE_CASE |
| **SR-04** | Налаштування системи (яскравість, мова, ім'я пристрою) зберігаються між вимкненнями | MUST | CONNECTIVITY v1.2.0 |
| **SR-05** | Web UI assets (HTML/JS/CSS) доступні через HTTP без SD карти | MUST | CONNECTIVITY v1.2.0 |
| **SR-06** | Plugin config (`data/config.json`) завантажується при старті | MUST | PLUGIN_ARCHITECTURE |
| **SR-07** | Логи зберігаються з ротацією (не переповнюють flash) | MUST | LOGGER_ARCHITECTURE |
| **SR-08** | Система коректно стартує та деградує без SD карти | MUST | COLLECTOR_USE_CASE |
| **SR-09** | Fingerprint DB завантажується для алгоритму матчингу (SD-first) | SHOULD | FINGERPRINT_DB v1.4.0 |
| **SR-10** | Зображення монет відображаються на дисплеї (якщо є SD) | COULD | COLLECTOR_USE_CASE |
| **SR-11** | Виміри архівуються на SD для подальшого аналізу. Тригер: при overwrite слоту (`meas_count >= RING_SIZE`) — скопіювати витісняємий `m_XXX.json` на SD **до** перезапису LittleFS слоту (ADR-ST-009). DEGRADED (SD недоступна): `LOG_WARN("MeasurementStore", "SD unavailable, slot %u lost", slot)`. DEGRADED (SD write fail): `LOG_WARN("MeasurementStore", "SD write failed, slot %u lost", slot)`. Архів не блокує основний flow. | COULD | COLLECTOR_USE_CASE |
| **SR-12** | Crash dump зберігається для post-mortem аналізу | SHOULD | LOGGER_ARCHITECTURE |
| **SR-13** | Factory reset (soft) відновлює WiFi/settings, зберігає калібрування | MUST | COLLECTOR_USE_CASE |
| **SR-14** | Factory reset (hard) відновлює завод-стан повністю | SHOULD | OTA / Servicing |
| **SR-15** | Схема вміру зберігається разом з вимірами для майбутньої реобробки | MUST | FINGERPRINT_DB v1.4.0 §5 |
| **SR-16** | OTA metadata (версія до оновлення, rollback лічильник) зберігається | MUST | CONNECTIVITY v1.2.0 ADR-007 |

### 2.2 Нефункціональні вимоги

| ID | Вимога | Метрика |
|---|---|---|
| **NF-01** | Flash endurance — not a bottleneck за 5 років use case | Не більше 1 write/sector/5хв для будь-якого одного сектору |
| **NF-02** | Читання налаштувань при startup < 50 мс | NVS read latency |
| **NF-03** | Запис вимірів не блокує головний loop > 5 мс | Асинхронний запис або < 4 KB write |
| **NF-04** | Power-fail safe: дані не корумпуються при відключенні живлення в будь-який момент | LittleFS atomic writes |
| **NF-05** | Система стартує без SD карти з деградацією функціонала | SD.begin() failure graceful |
| **NF-06** | Heap overhead від storage layer < 20 KB | RAM budget (337 KB total) |
| **NF-07** | uploadfs оновлює тільки web UI, не знищує виміри та налаштування | Workflow protection |

### 2.3 Виключені вимоги (поза скоупом v1)

- Шифрування даних на LittleFS (NVS AES-XTS — в рамках скоупу, LittleFS — ні)
- Хмарна синхронізація вимірів (майбутня фаза)
- RAID/mirror між LittleFS та SD

---

## 3. Інвентар: залізо та його обмеження

### 3.1 Flash та RAM

```
ESP32-S3FN8:
  Flash:  8 MB (SPI QSPI 80 MHz)  — вбудований, незнімний
  RAM:    337 KB heap               — фізично 520 KB, ~183 KB зарезервовано системою
  PSRAM:  ВІДСУТНІЙ                 — "FN8" = Flash-only, No PSRAM
  
Flash сектор:    4 KB (мінімальна одиниця стирання)
Flash page:    256 B  (мінімальна одиниця програмування)
Flash endur.:  100,000 write cycles/sector (Winbond W25Q64, typical)
```

> ⚠️ **Критична знахідка:** `main.cpp` ініціалізує `RingBufferTransport gRingTransport(100, /*usePsram=*/true)`.
> ESP32-S3**FN8** не має PSRAM. `usePsram=true` може призвести до `heap_caps_malloc(PSRAM)` → NULL → 
> fallback або crash. Це **Bug B-01** — потребує виправлення до будь-якої storage роботи.

### 3.2 SD Card Slot

M5Stack Cardputer-Adv має microTF slot підключений через **SPI** (J3 TF-015, підтверджено схемою Sch_M5CardputerAdv_v1_0_2025_06_20):

```
SD SPI pins (M5Stack Cardputer-Adv, підтверджено схемою):
  MOSI: GPIO 14  (J3 pin 3, через R21 33Ω)
  MISO: GPIO 39  (J3 pin 7, через R19 33Ω)  ⚠️ MTCK (JTAG)
  SCK:  GPIO 40  (J3 pin 5, через R22 33Ω)  ⚠️ MTDO (JTAG)
  CS:   GPIO 12  (J3 pin 2, через R20 33Ω)
```

> ⚠️ **JTAG конфлікт:** GPIO39 (MTCK) та GPIO40 (MTDO) — це JTAG debug pins.
> SD card та JTAG debugging (OpenOCD) **взаємовиключають одне одного** під час роботи.
>
> ℹ️ **33Ω резистори (R19–R22):** Series termination обмежує ефективну швидкість SPI шини.
> Рекомендований максимум: `SD.begin(SD_CS_PIN, spi_sd, 20000000)` — 20 MHz.
>
> ⚠️ **OQ-01 уточнено:** LDC1101 Click Board (P3) підключений до **того самого VSPI bus**:
> SCK=GPIO40, MOSI=GPIO14, MISO=GPIO39, CS=**GPIO5** (схема P3: pin4=SCK=G40, pin5=MOSI=G14, pin6=MISO=G39, pin7=CS=G5).
> GPIO3=RESET, GPIO4=INT, GPIO6=BUSY — сигнали управління, **не SPI**.
> Конфлікт між SD та LDC1101 реальний. Mutex **обов'язковий** (ADR-ST-008).

### 3.3 Поточна Partition Table (default_8MB.csv)

```csv
# Поточна — СУБОПТИМАЛЬНА для CoinTrace
Name,      Type,  SubType,  Offset,    Size
nvs,       data,  nvs,      0x9000,    0x5000    #  20 KB — ЗАМАЛО
otadata,   data,  ota,      0xe000,    0x2000    #   8 KB — достатньо
app0,      app,   ota_0,    0x10000,   0x330000  # 3.19 MB — НАДЛИШОК (firmware ~1.2-1.5 MB)
app1,      app,   ota_1,    0x340000,  0x330000  # 3.19 MB — НАДЛИШОК
spiffs,    data,  spiffs,   0x670000,  0x180000  # 1.5 MB — ЗАМАЛО та ЗАСТАРІЛИЙ filesystem
coredump,  data,  coredump, 0x7F0000,  0x10000   #  64 KB — достатньо
```

**Проблеми:**
1. NVS 20 KB — вистачить на WiFi (+~2 KB) + кілька ключів, але для калібрування (float × 10 полів) і налаштувань при тисячах записів ризик переповнення
2. SPIFFS — застарілий filesystem без wear leveling, не power-fail safe. LittleFS — правильна заміна
3. App slots 3.19 MB × 2 = 6.38 MB — при firmware реально < 1.5 MB це 2.88 MB втрата (37% flash!)
4. LittleFS partition лише 1.5 MB — web UI (~500 KB) + plugin config + measurements + logs = тісно
5. Лише одна FS partition — `uploadfs` знищує все: виміри, кеш index.json, логи

---

## 4. Поточний стан та виявлені блокери

### 4.1 Inventory: що зараз є в коді

| Компонент | Статус | Файл |
|---|---|---|
| Logger (Serial + RingBuffer) | ✅ Реалізовано | `lib/Logger/` |
| `RingBufferTransport(100, usePsram=true)` | ⚠️ Bug B-01 (no PSRAM) | `src/main.cpp` |
| `SDTransport` | 📝 Спроектовано (LOGGER_ARCHITECTURE v2.0) | Не реалізовано |
| Plugin config `data/config.json` | 📝 Спроектовано | Не реалізовано |
| Partition Table (custom) | ❌ Відсутня | `boards/m5cardputer-adv.json` → `default_8MB.csv` |
| NVS Manager | ❌ Відсутній | — |
| LittleFS Manager | ❌ Відсутній | — |
| SD Card Manager | ❌ Відсутній | — |
| Storage Manager (фасад) | ❌ Відсутній | — |

### 4.2 Виявлені блокери (Blocker Analysis)

#### B-01 — PSRAM Bug у RingBufferTransport [HIGH]
```cpp
// src/main.cpp, рядок ~14
static RingBufferTransport gRingTransport(100, /*usePsram=*/true);
//                                                          ^^^^
// ESP32-S3FN8 не має PSRAM. heap_caps_malloc(MALLOC_CAP_SPIRAM) повертає NULL.
// При init RingBuffer, якщо NULL не перевіряється → undefined behavior або assertion fail.
```
**Вплив:** потенційний crash при старті ще до будь-якого storage коду.
**Fix:** `usePsram=false` або `#ifdef BOARD_HAS_PSRAM`.

#### B-02 — uploadfs знищує дані користувача [HIGH]
При `pio run -t uploadfs` вся LittleFS-партиція (зараз `spiffs`) форматується і перезаписується.
Якщо там лежать:
- **виміри** — втрата без попередження
- **кешований index.json** — потребує перезавантаження з SD
- **plugin config** — пристрій не стартує без config.json

**Fix:** розподіл за категоріями з чітким workflow (див. §8.4).

#### B-03 — Відсутність версіонування у записі вимірів [HIGH]
Специфікація FINGERPRINT_DB v1.4.0 §5 вимагає "Raw Measurements as Source of Truth":
raw дані зберігаються з `algo_ver` та `protocol_id` для **майбутньої реобробки**.
Якщо перший вимір записується без `algo_ver` + `protocol_id` → ці дані безцінні для майбутнього
community DB (неможливо перерахувати вектор при зміні алгоритму).

**Fix:** кожен файл вимірів MUST містити `algo_ver`, `protocol_id`, `protocol_ver`, `schema_ver`.

#### B-04 — NVS 20 KB — потенційне переповнення [MEDIUM]
NVS зберігає дані у pages 4 KB, entries 32 B. Розрахунок:
```
WiFi (ssid 32B + pass 63B + mode 1B) ≈ 3 entries × 32B = 96 B
Sensor cal (rp0, rp1, rp2, rp3, l0...l3 = 8 floats + 2 timestamps + valid) ≈ 15 entries
System settings (brightness, language, log_level, dev_name) ≈ 8 entries
Storage bookkeeping (meas_count) ≈ 1 entry  # [PRE-11] log_gen видалено, rotation → lfs_file_size()
OTA metadata ≈ 5 entries
BLE pairing data (bonding keys) ≈ 10 entries per device × N devices
NVS overhead + wear leveling pages ≈ 30% overhead
```
При 20 KB і wear leveling NVS реально використовує ~10-12 KB для даних.
**З BLE bonding + 5+ devices + future namespaces — ризик переповнення реальний**.
**Fix:** збільшити NVS до 60 KB у кастомній partition table.

#### B-05 — Відсутність coredump обробки [LOW]
Coredump partition існує (64 KB), але для читання потрібно:
1. `CONFIG_ESP_COREDUMP_ENABLE=1` у sdkconfig
2. `espcoredump.py` або spеціальний boot handler

Без цього 64 KB flash марнується і crash info губиться.
При наявності Logger + SD — варто зберігати coredump summary у людино-читальному форматі в SD.

#### B-06 — SPI Bus Conflict між LDC1101 та SD [MEDIUM, вирішено: spi_vspi_mutex]
SD (CS=GPIO12) та LDC1101 Click Board P3 (CS=GPIO5) підключені до **одного VSPI bus**
(підтверджено схемою P3 HDR-SMD_14P-P2.54: pin4=SCK=GPIO40, pin5=MOSI=GPIO14, pin6=MISO=GPIO39, pin7=CS=GPIO5).
SDTransport (async FreeRTOS task) та MainLoop (LDC1101 SPI burst) конкурують за спільну VSPI шину.
**Рішення:** `spi_vspi_mutex` — mutex береться на один SPI-барст LDC1101, звільняється після. Зафіксовано ADR-ST-008.

---

## 5. Три-тирна архітектура зберігання

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         StorageManager (Facade)                          │
│         Єдина точка входу. Routing: NVS / LittleFS / SD.                │
│         Визначає доступність Tier 2, забезпечує graceful degradation.   │
└──────────────────┬─────────────────────┬────────────────────────────────┘
                   │                     │                   │
         ┌─────────▼──────┐    ┌─────────▼──────┐  ┌────────▼──────────┐
         │   Tier 0: NVS  │    │ Tier 1: LittleFS│  │  Tier 2: SD Card  │
         │   60 KB        │    │   2.75 MB        │  │  4–32 GB (opt.)   │
         │   Key-Value    │    │   Filesystem     │  │  FAT32            │
         │   Wear-leveled │    │   Wear-leveled   │  │  Arduino SD.h     │
         │   AES-XTS opt. │    │   Power-fail safe│  │  Graceful degrad. │
         └────────────────┘    └──────────────────┘  └───────────────────┘
              ЗАВЖДИ є               ЗАВЖДИ є             МОЖЕ не бути

Що там:                         Що там:                  Що там:
• WiFi credentials              • Web UI assets           • Fingerprint DB
• Sensor calibration            • Plugin configs          • Coin images
• System settings               • Recent measurements     • Measurement archive
• Storage bookkeeping           • Log files (rotating)    • Log archive
• OTA metadata                  • index.json cache        • SD-archived coredumps
• BLE bonding keys (future)     • Protocol registry
```

### 5.1 Принцип розподілу

| Тип даних | Tier | Причина |
|---|---|---|
| Все, що **не файл**, інтенсивно читається при старті | NVS | Атомарність, wear leveling, без filesystem overhead |
| Все, що є **файлом**, оновлюється рідко | LittleFS | Power-fail safe, зручна ієрархія |
| Все, що **велике** або **опційне** | SD | Необмежений розмір, замінний носій |
| Калібрування, credentials | NVS | **Не повинні гинути при `uploadfs`** |
| Web UI | LittleFS | Оновлюється через `uploadfs`, окремий директорій |

---

## 6. Варіанти Partition Table (Options A / B / C)

### Option A — Мінімальна зміна (Conservative)

Збільшити тільки NVS та замінити SPIFFS на LittleFS. App slots не змінювати.

```csv
# Option A: Conservative — мінімальна ризикована зміна
nvs,       data,  nvs,      0x9000,    0xF000    #  60 KB (+40 KB) ← зміна
otadata,   data,  ota,      0x18000,   0x2000    #   8 KB
app0,      app,   ota_0,    0x20000,   0x330000  # 3.19 MB (незмінно)
app1,      app,   ota_1,    0x350000,  0x330000  # 3.19 MB (незмінно)
littlefs,  data,  spiffs,   0x680000,  0x160000  # 1.375 MB ← зміна (subtype=spiffs для LittleFS OK!)
coredump,  data,  coredump, 0x7E0000,  0x10000   #  64 KB
```

**Note:** LittleFS у PlatformIO використовує `subtype=spiffs`. Це нормально — тип файлової системи
визначається `board_build.filesystem = littlefs` в platformio.ini, не subtype у CSV.

| Плюси | Мінуси |
|---|---|
| Мінімальний ризик сумісності | LittleFS лише 1.375 MB — тісно |
| App slots незмінні — більше свободи для великого firmware | Не вирішує проблему uploadfs destroying data |
| Простий перехід | SPIFFS → LittleFS потребує повного форматування раз |

---

### Option B — Оптимізована (Recommended) ★ — **IMPLEMENTED (Wave 7 P-1)**

Зменшити app slots до реалістичного розміру, звільнивши місце для двох LittleFS partitions.

> **Wave 7 P-1 revision:** адреси змінено для сумісності з PlatformIO esptool.
> PlatformIO Arduino-ESP32 hardcode-ує `boot_app0.bin → 0xe000` та `firmware → 0x10000`.
> Оригінала схема (app0=0x20000, otadata=0x18000) приводила до boot failure.
> NVS зменшено з 60 KB до 20 KB (стандарт); для поточного NVSManager (settings +
> calibration + meas_count) це більш ніж достатньо. Всі offset-и в active CSV
> (`partitions/cointrace_8MB.csv`) є остаточними.

```csv
# Option B: Optimized — РЕКОМЕНДОВАНА (IMPLEMENTED)
# PlatformIO-compatible: standard ESP32 addresses for esptool
nvs,           data,  nvs,      0x9000,    0x5000    #  20 KB (standard; достатньо для NVSManager)
otadata,       data,  ota,      0xe000,    0x2000    #   8 KB (standard 0xe000 = boot_app0.bin)
app0,          app,   ota_0,    0x10000,   0x280000  # 2.5 MB (standard 0x10000 = firmware upload)
app1,          app,   ota_1,    0x290000,  0x280000  # 2.5 MB
littlefs_sys,  data,  spiffs,   0x510000,  0x100000  # 1.0 MB (web UI + configs)
littlefs_data, data,  spiffs,   0x610000,  0x1C0000  # 1.75 MB (measurements + logs + cache)
coredump,      data,  coredump, 0x7D0000,  0x30000   # 192 KB (збільшено vs original 128 KB)
# Flash використано повністю: 0x800000 = 8 MB
```

**Ключова ідея: дві LittleFS партиції з різною lifecycle:**

| Партиція | Lifecycle | Оновлюється командою |
|---|---|---|
| `littlefs_sys` | Оновлюється при deployрозробника | `pio run -t uploadfs -e sys` |
| `littlefs_data` | Дані користувача, ніколи не форматується | — (тільки Factory Reset) |

| Плюси | Мінуси |
|---|---|
| `uploadfs` НІКОЛИ не знищить виміри та логи | Ускладнює partition init (монтувати 2 партиції) |
| LittleFS_data 1.75 MB — комфортно для вимірів + логів | PlatformIO `uploadfs` потребує custom env-і |
| Coredump 128 KB — вміщає повний ESP32-S3 exception frame | Більш складна документація workflow |
| App slots 2.5 MB — realistically sized | app0+app1 = 5 MB; firmware 1.5 MB → 1 MB запас |

---

### Option C — Максимальна (Three-FS)

Три окремі зони: `sys`, `data`, `cache`. Для проектів зі складним lifecycle.

```csv
# Option C: Maximum separation — для майбутнього масштабування
nvs,            data,  nvs,      0x9000,    0xF000    #  60 KB
otadata,        data,  ota,      0x18000,   0x2000
app0,           app,   ota_0,    0x20000,   0x230000  # 2.19 MB (tight але OK)
app1,           app,   ota_1,    0x250000,  0x230000  # 2.19 MB
littlefs_sys,   data,  spiffs,   0x480000,  0xC0000   # 768 KB (web UI only)
littlefs_data,  data,  spiffs,   0x540000,  0x180000  # 1.5 MB (measurements + logs)
littlefs_cache, data,  spiffs,   0x6C0000,  0xF0000   # 960 KB (index.json + protocol cache)
coredump,       data,  coredump, 0x7B0000,  0x40000   # 256 KB
# 0x7F0000–0x800000 = 64 KB free
```

| Плюси | Мінуси |
|---|---|
| Максимальна ізоляція | App slots 2.19 MB — ризик при великому зростанні коду |
| Cache можна форматувати при оновленні DB | Три fs_mount() + складний StorageManager |
| 256 KB coredump — повний backtrace будь-якої глибини | Overhead монтування ~3 ms per partition |

**Рекомендація: Option B** — оптимальний баланс між ізоляцією та складністю.

---

## 7. Tier 0 — NVS: дизайн namespace

### 7.1 Розмір та capacity

NVS розміщується на 20 KB (Option B, Wave 7 revision). Фізична ємність:
```
20 KB / 4 KB page = 5 pages
NVS використовує 1 overhead page + 1 wear-leveling spare = 3 робочих pages
Entries per page: (4096 - 32 header) / 32 = 126 entries
Total: 3 × 126 = 378 entries max
Overhead NVS namespace index: ~5% = ~18 entries
Net usable: ~360 entries × 32 B = ~11 KB корисних даних
Достатньо для: wifi(4) + sensor(12) + system(3) + storage(2) + ota(1) + plugin(N) entries
```

### 7.2 Namespace Layout

```
NVS (20 KB)
│
├── namespace: "wifi"                    # WiFi provisioning
│   ├── "ssid"      : String (≤32 B)
│   ├── "pass"      : String (≤63 B)
│   ├── "mode"      : UInt8   (0=AP, 1=STA)
│   └── "hostname"  : String  (default "cointrace")
│
├── namespace: "sensor"                  # LDC1101 Calibration — КРИТИЧНО
│   ├── "rp0"       : Float   # Baseline Rp @ dist 0 mm (Ω)
│   ├── "rp1"       : Float   # Baseline Rp @ dist 1 mm (Ω)
│   ├── "rp2"       : Float   # Baseline Rp @ dist 2 mm (Ω)  
│   ├── "rp3"       : Float   # Baseline Rp @ dist 3 mm (Ω)
│   ├── "l0"        : Float   # Baseline L  @ dist 0 mm (µH)
│   ├── "l1"        : Float   # Baseline L  @ dist 1 mm (µH)
│   ├── "l2"        : Float   # Baseline L  @ dist 2 mm (µH)
│   ├── "l3"        : Float   # Baseline L  @ dist 3 mm (µH)
│   ├── "freq_hz"   : UInt32  # Operating frequency (default 5000000)
│   ├── "cal_ts"    : Int64   # Unix timestamp last calibration
│   ├── "cal_valid" : Bool    # Calibration passed validation
│   └── "proto_id"  : String  # Protocol ID під час калібрування
                             # ⚠️ [F-05] Значення буде визначено після R-01 (перше вимірювання).
                             # Реальний fSENSOR MIKROE-3240 ≈ 200–500 kHz (залежить від котушки),
                             # НЕ 1 MHz і НЕ 5 MHz. Placeholder: "p1_UNKNOWN_013mm".
│
├── namespace: "system"                  # User-facing settings
│   ├── "brightness": UInt8   (0–255, default 128)
│   ├── "lang"      : String  ("uk", "en", default "en")
│   ├── "log_level" : UInt8   (0=DEBUG .. 4=FATAL, default 2=INFO)
│   ├── "dev_name"  : String  (≤20 B, default "CoinTrace")
│   └── "display_rot": UInt8  (display rotation, default 1)
│
├── namespace: "storage"                 # Storage bookkeeping
│   └── "meas_count": UInt32  # Total measurements ever (monotonic, НІКОЛИ не скидати)
│                             # Slot index = meas_count % RING_SIZE (обчислюється, не зберігається)
│                             # [PRE-11] видалено log_gen: rotation контролюється через lfs_file_size()
│                             # (LittleFS-native, без NVS), не через окремий write-first counter
│
└── namespace: "ota"                     # OTA metadata (CONNECTIVITY ADR-007)
    ├── "confirmed" : Bool    # OTA confirmed by physical key press
    ├── "pre_ver"   : String  # Firmware version before current OTA
    ├── "rb_count"  : UInt8   # Rollback attempt counter
    └── "ota_ts"    : Int64   # Timestamp of last OTA attempt
```

### 7.3 NVS API Wrapper: `NVSManager`

```cpp
// src/storage/NVSManager.h (концептуальний API)

class NVSManager {
public:
    struct SensorCalibration {
        float rp[4];         // rp0..rp3 (Ω)
        float l[4];          // l0..l3 (µH)
        uint32_t freq_hz;
        int64_t  cal_ts;
        bool     cal_valid;
        char     proto_id[16];
    };

    bool begin();
    bool isCalibrationValid() const;
    bool loadCalibration(SensorCalibration& out) const;
    bool saveCalibration(const SensorCalibration& cal);
    
    bool loadWifi(char* ssid, char* pass, uint8_t& mode) const;
    bool saveWifi(const char* ssid, const char* pass, uint8_t mode);
    
    uint8_t  getBrightness() const;
    void     setBrightness(uint8_t val);
    
    uint32_t getMeasCount() const;
    uint8_t  getMeasSlot() const;     // Обчислює: getMeasCount() % RING_SIZE (не зберігається в NVS)
    void     incrementMeasCount();    // [PRE-10] THREADING: MUST be called exclusively from MainLoop task.
                                      // Внутрішньо виконує два NVS calls (read + write) — не атомарно
                                      // між concurrent callers. Mutex не потрібен якщо constraint дотриманий.
    
    bool factoryResetSettings();      // erase "wifi", "system" — ЗБЕРІГАЄ "sensor"
    bool factoryResetAll();           // erase всі namespaces
    
private:
    Preferences wifi_;
    Preferences sensor_;
    Preferences system_;
    Preferences storage_;
    Preferences ota_;
};
```

> **Чому Preferences а не `esp_nvs_*` напряму:** `Preferences.h` — тонка обгортка з type-safety та
> автоматичним namespace management. При `esp_nvs_*` легко помилитись з ключами та типами.

---

## 8. Tier 1 — LittleFS: структура файлів

### 8.1 Filesystem вибір

**LittleFS замість SPIFFS** (детальніше у ADR-ST-001 §11):
- Wear leveling на рівні файлової системи (SPIFFS — ні)
- Atomic writes: при відключенні живлення файл або записаний, або ні (SPIFFS може зіпсувати)
- Підтримка директоріїв (SPIFFS — плоска структура з `/` у назві)
- Активно підтримується Espressif (SPIFFS — deprecated у IDF 5.x)

### 8.2 Структура директоріїв (Option B: дві партиції)

#### Partition `littlefs_sys` (1.0 MB) — Developer-updated

```
/                                   (root littlefs_sys, монтується як /sys)
├── web/                            # Web UI assets (макс 400 KB мініфіковано; gzip обов'язковий)
│   ├── index.html                  # SPA entry point
│   ├── app.js                      # Frontend bundle (мінімізований)
│   ├── app.css
│   └── favicon.ico
│
└── config/                         # Static configs (~50 KB)
    ├── device.json                 # Plugin config (PLUGIN_ARCHITECTURE data/config.json)
    ├── protocols/
    │   └── registry.json           # Measurement protocol registry
    └── ble_uuids.json              # BLE UUID registry (довідка, локальна копія)
```

> **uploadfs для цієї партиції:** `pio run -t uploadfs -e cointrace-dev`
> Форматує та перезаписує лише `littlefs_sys`. Дані користувача не торкаються.

#### Partition `littlefs_data` (1.75 MB) — User data, ніколи не форматується через uploadfs

```
/                                   (root littlefs_data, монтується як /data)
│
├── measurements/                   # Ring buffer вимірів (≤250 × ~700 B = ~175 KB)
│   ├── m_000.json                  # Вимір 0 (перезаписується при overflow)
│   ├── m_001.json
│   └── ...m_249.json
│
├── cache/                          # Кешовані дані з SD (~84 KB)
│   ├── index.json                  # Fingerprint DB index (FINGERPRINT_DB §8, ~80 KB)
│   │                               # Недійсний якщо SD DB оновилась (generation check або CRC32)
│   ├── index_crc32.bin             # CRC32 хеш index.json (4 bytes, little-endian uint32_t)
│   └── sd_generation.bin           # Generation counter SD index.json (4 bytes, little-endian uint32_t)
│                                   # Порівнюється з SD:/database/index.json "generation" при boot [7a]
│
└── logs/                           # Rotating logs (max 400 KB = 2 × 200 KB)
    ├── log.0.jsonl                 # Поточний (max 200 KB, JSON Lines, тримається ВІДКРИТИМ)
    └── log.1.jsonl                 # Архів (копіюється на SD при ротації, потім видаляється)
```

> Для **Option A** (одна партиція): використовується `littlefs` єдина, і директорій `/sys/` та `/data/`
> розмежовані логічно. `uploadfs` в такому разі **руйнує /data/** — тому Option A потребує
> спеціального workflow (backup скрипт + restore).

### 8.3 Формат файлу вимірів

Кожен `m_XXX.json` (де XXX = `meas_count % RING_SIZE`, a `RING_SIZE = 250`):

```json
{
  "schema_ver": 1,
  "algo_ver": 1,
  "protocol_id": "p1_UNKNOWN_013mm",
  "protocol_ver": "1.0",
  "device_id": "CoinTrace-A1B2",
  "ts": 1741780000,
  "cal_ts": 1741779000,
  "complete": true,
  "raw": {
    "rp": [8340.5, 8027.1, 7710.3, 7349.8],
    "l":  [1204.1, 1186.3, 1168.2, 1149.5]
  },
  "vector": {
    "dRp1": 313.4,
    "k1":   0.7124,
    "k2":   0.7298,
    "slope_rp_per_mm_lr": -0.137,
    "dL1":  17.8
  },
  "match": {
    "metal_code": "Ag925",
    "coin_name": "Austrian Maria Theresa Thaler",
    "conf": 0.94,
    "alternatives": [
      {"metal_code": "XAG900", "coin_name": "US Morgan Dollar 1881", "conf": 0.71}
    ]
  }
}
```

> **SR-15 compliance:** поля `algo_ver`, `protocol_id`, `protocol_ver`, `cal_ts` — обов'язкові.
> Без них raw дані неможливо переобробити при зміні алгоритму (breaking blocker для community DB).
>
> **`complete: true`** — поле присутнє **тільки** у файлах, що були повністю записані. При crash
> під час write файл не матиме цього поля → при читанні такий запис відкидається як invalid.
>
> **`device_id`** генерується як `"CoinTrace-" + hex(esp_efuse_mac_get_default()[4:6])` — унікально
> для кожного пристрою на основі ESP32 eFuse MAC (останні 2 байти).
> ⚠️ [A-01] Використовувати `esp_efuse_mac_get_default()`, **не** `esp_efuse_get_custom_mac()`.
> `get_custom_mac()` повертає `ESP_ERR_INVALID_ARG` якщо custom MAC не записаний в eFuse (типовий випадок).
>
> **`slope_rp_per_mm_lr`** — безрозмірний коефіцієнт (одиниця: 1/mm), лінійна регресія k-ratio векторів
> по просторових позиціях (0, 1, 3 мм). Типове значення ≈ -0.137. НЕ Ohm/mm.

### 8.4 uploadfs Workflow Protection

**Проблема:** `pio run -t uploadfs` за замовчуванням форматує **всю** partition і перезаписує.

**Рішення для Option B:**
```ini
; platformio.ini — два окремих upload env для filesystem
[env:cointrace-fs-sys]
; Оновлює тільки sys partition (web UI + configs)
; Дані користувача НЕ торкаються
board_build.filesystem = littlefs
board_build.partitions = partitions/cointrace_8MB.csv
custom_fs_partition = littlefs_sys
upload_command = ... ; custom LittleFS upload з вказаним partition label

[env:cointrace-dev]
; Звичайний firmware upload
...
```

> **Відкрите питання OQ-02:** PlatformIO не має вбудованого `custom_fs_partition` параметру.
> Потрібно або:
> (a) custom Python upload script через `extra_scripts`
> (b) два окремих `partition_name` через ESP-IDF menuconfig
> (c) залишитись на одній FS партиції з backup/restore workflow
>
> **Рекомендація:** (a) — найменш інвазивно, сумісно з PlatformIO.

---

## 9. Tier 2 — SD Card: структура та деградація

### 9.1 Filesystem вибір для SD

**FAT32:** підтримується Arduino `SD.h`, сумісний з будь-яким ПК без drivers, max file size 4 GB.
**exFAT:** немає вбудованої підтримки в Arduino ESP32, потребує сторонньої бібліотеки, max >4 GB.

**Рішення: FAT32** (ADR-ST-004). Обмеження 4 GB на файл — не проблема (fingerprint DB < 100 MB навіть для 100K монет).

### 9.2 Структура каталогів на SD

```
SD:/CoinTrace/                      # Root для всіх даних CoinTrace
│
├── database/                       # Fingerprint Database (FINGERPRINT_DB spec)
│   ├── index.json                  # Quick Screen index (~80 KB, вантажиться в RAM)
│   ├── schema/
│   │   └── fingerprint_v1.json     # JSON Schema для валідації
│   └── coins/
│       ├── Ag925/
│       │   ├── aggregate.json      # Aggregate vector для металу
│       │   └── raw/
│       │       └── <session_id>.json
│       └── Au999/
│           └── aggregate.json
│
├── images/                         # Coin images (JPEG, ~150 KB/image)
│   ├── Ag925/
│   │   ├── front.jpg
│   │   └── back.jpg
│   └── Au999/
│       ├── front.jpg
│       └── back.jpg
│
├── measurements/                   # Historical archive
│   ├── 2026/
│   │   └── 03/
│   │       └── 2026-03-12.jsonl    # Newline-delimited JSON (compact)
│   └── export/
│       └── pending.jsonl           # Queue for community DB submission (future)
│
└── logs/
    └── archive/
        └── 2026-03-12.txt          # Archived logs (copied від LittleFS при ротації)
```

### 9.3 Graceful Degradation без SD

```
Функціонал        | SD є    | SD немає
─────────────────────────────────────────────
Вимірювання       | ✅ Full  | ✅ Full (ring buffer 250)
Матчинг (Quick)   | ✅ Full  | ⚠️ Тільки якщо index.json у /data/cache/
Матчинг (Deep)    | ✅ Full  | ❌ Недоступно
Зображення монет  | ✅ Full  | ❌ Показується placeholder
Архів вимірів     | ✅ Full  | ❌ Тільки останні 250
Логи (архів)      | ✅ Full  | ⚠️ Тільки 3 файли ротації в LittleFS
Fingerprint DB    | ✅ Full  | ⚠️ Стара версія з cache/ (якщо є)
```

### 9.4 SD та LDC1101 — Shared VSPI Bus та Conflict Resolution

**Апаратний факт (підтверджено схемою P3 HDR-SMD_14P-P2.54):**

SD card та LDC1101 Click Board підключені до **одного VSPI bus**, різні тільки CS:

| Пристрій | SCK | MOSI | MISO | CS | Інші сигнали |
|---|---|---|---|---|---|
| SD card (J3 TF-015) | GPIO40 | GPIO14 | GPIO39 | GPIO12 | — |
| LDC1101 (Click P3) | GPIO40 | GPIO14 | GPIO39 | **GPIO5** | RESET=GPIO3, INT=GPIO4, BUSY=GPIO6 |

**Один `SPIClass(VSPI)` — обидва пристрої на спільній шині, `spi_vspi_mutex` для координації:**

```cpp
// Правильна ініціалізація — один SPIClass(VSPI), два CS
SPIClass spi_vspi(VSPI);
spi_vspi.begin(40 /*SCK*/, 39 /*MISO*/, 14 /*MOSI*/);

SD.begin(12 /*CS_SD*/, spi_vspi, 20000000);  // 20 MHz (обмеження 33Ω резисторів R19–R22)
LDC1101 ldc(spi_vspi, 5 /*CS_LDC*/);

// Mutex для координації: SDTransport (async task) vs MainLoop (LDC SPI burst)
// ⚠️ Naming note (X-02): spi_vspi_mutex у псевдокоді цього документа =
//    ctx->spiMutex з PluginContext (PLUGIN_CONTRACT.md §1.3).
//    В реальному коді: gCtx.spiMutex = xSemaphoreCreateMutex() (main.cpp).
//    Один mutex — два імені в різних документах; не створювати окремий!
SemaphoreHandle_t spi_vspi_mutex = xSemaphoreCreateMutex();
```

**Критично важливий scope mutex (R-04) — per SPI burst, НЕ per 4-measurement cycle:**

```cpp
for (int dist = 0; dist < 4; dist++) {
    xSemaphoreTake(spi_vspi_mutex, portMAX_DELAY);  // ← береться
    ldc.readRpL(&rp[dist], &l[dist]);               // SPI burst ~10–500 ms
    xSemaphoreGive(spi_vspi_mutex);                 // ← одразу звільняється
    // ПАУЗА 5–30 с: SDTransport може писати на SD вільно
}
```

**Чому mutex per burst, не per cycle:**
- Вимірювання 4 дистанцій — користувач фізично переміщує сенсор між вимірами (5–30 с пауза). Протягом паузи VSPI шина вільна для SD.
- Блокування SD на весь цикл (20–120 с) переповнить FreeRTOS queue SDTransport → silent log loss.
- `slope_rp_per_mm_lr` обчислюється з **просторових позицій** (0, 1, 3 мм), не з часових інтервалів → пауза між барстами **не впливає** на якість вектору.

**Наслідки:**
- `spi_vspi_mutex` обов'язковий для LDC SPI bursts та SDTransport writes.
- 33Ω series resistors (R19–R22) між ESP32 та SD (LDC — без резисторів) → max 20 MHz для `SD.begin()`.
- JTAG debugging (OpenOCD) та SD **взаємовиключають одне одного** (GPIO39=MTCK, GPIO40=MTDO).
- Зафіксовано у ADR-ST-008.

---

## 10. Критичні питання для обговорення

### OQ-01 — LDC1101 та SD на одному SPI? [✅ Вирішено: shared VSPI bus + spi_vspi_mutex]
Підтверджено схемою P3 HDR-SMD_14P-P2.54: SD (CS=GPIO12) та LDC1101 (CS=GPIO5) на одному VSPI bus
(SCK=GPIO40, MOSI=GPIO14, MISO=GPIO39 — спільні). GPIO3=RESET, GPIO4=INT, GPIO6=BUSY — не SPI.
Mutex обов'язковий. **Рішення:** `spi_vspi_mutex` per SPI burst. Зафіксовано в ADR-ST-008.

### OQ-02 — Одна чи дві LittleFS partitions? [✅ Вирішено: Option B]
Прийнято Option B — дві LittleFS partitions (`sys` + `data`). Зафіксовано в ADR-ST-002 (✅ Прийнято).
`uploadfs` для sys partition не торкається `data` partition з вимірами користувача. Custom PIO upload script — Phase P-3.

### OQ-03 — Ring buffer size [✅ Вирішено: 250 вимірів]
50 вимірів × ~5 KB = 250 KB — розумно. Але:
- Якщо вимір займає в середньому 3 хв → 50 вимірів = 2.5 годин роботи. Достатньо?
- Більший ring buffer → більше LittleFS space → менше для логів
- **Потребує product decision:** скільки вимірів має бути доступних "offline" без SD?

**Рішення:** реальний розмір файлу ~700 B. Обрано 250 вимірів (F-02: margin 3%→14%):
- 250 × ~700 B = ~175 KB (10% LittleFS_data, ~1.6 MB лишається для логів/кешу)
- 250 вимірів при ~3 хв = ~12.5 год автономної роботи — комфортно
- Зафіксовано в ADR-ST-006.

### OQ-04 — Де зберігати plugin config.json? [✅ Вирішено: /sys/config/device.json]
Plugin config зберігається в LittleFS_sys: `/sys/config/device.json`.
Оновлюється через `uploadfs -e fs-sys`. При відсутності — hardcoded fallback (`loadDefaultConfig()`), див. §12.2.

### OQ-05 — NVS flash encryption [✅ Вирішено: без шифрування для v1]
NVS підтримує flash encryption. WiFi password та BLE ключі будуть у plain text без неї.

**Варіант обговорення:**
- A) Без шифрування (v1) → простіше, можна додати пізніше ← для v1 достатньо
- B) З шифруванням (Production) → потребує `FLASH_ENCRYPTION_MODE` в sdkconfig, одноразово
- ⚠️ Flash encryption після upload неможливо вимкнути (одноразовий eFuse burn). Потрібне **рішення до першого production release**.

**Рішення:** без NVS encryption для v1. Обґрунтування:
- WiFi credentials в plain text — прийнятно для dev/maker use case
- Flash encryption унеможливлює debugging через esptool — занадто велика ціна на ранній стадії
- Зафіксовано в ADR-ST-005.

### OQ-06 — Де зберігати "session ID" вимірювань? [✅ Вирішено: детерміновано]
`session_id = device_id + "_" + meas_count_at_session_start` — детерміновано, без NVS overhead.
При community DB submission об'єднує виміри однієї сесії. `device_id` генерується з eFuse MAC (§8.3).

### OQ-07 — Скільки ротацій логів і який максимальний розмір? [✅ Закрито]

**Рішення [PRE-1, Варіант A]:** 2 ротації × 200 KB = 400 KB hot storage в LittleFS_data.

- **LittleFSTransport** = live logs у `log.0.jsonl`; при > 200 KB → ротація (.0→.1, copy .1→SD, delete .1).
- **SDTransport** = cold archive: при ротації `log.1.jsonl` копіюється на SD (batch, не per-entry).
- Окрема `LittleFSTransport` є імплементацією, не повторенням Logger transport chain.
- Зафіксовано у §12.1 та R-09 (batch writes SD).
- **Обґрунтування Варіанта A:** 3×300KB (900KB логічно) = 563 LittleFS blocks > 448 доступних. 2×200KB (400KB) = ~420 blocks — вільно в partition. 400 KB hot storage достатньо для local debugging.

---

## 11. Архітектурні рішення (ADR)

### ADR-ST-001: LittleFS замість SPIFFS

**Статус:** ✅ Прийнято

**Контекст:** Espressif deprecated SPIFFS у IDF 5.x. LittleFS — активно підтримуваний, power-fail safe, wear-leveled.

**Рішення:** Використовувати LittleFS для всіх файлових операцій на internal flash.
- `board_build.filesystem = littlefs` у platformio.ini
- Subtype у partition CSV залишається `spiffs` (PlatformIO convention)
- При першому прошиванні: `pio run -t uploadfs` форматує та ініціалізує

**Наслідки:**
- Потребує `arduino-esp32fs-plugin` або PlatformIO built-in LittleFS upload
- Incompatible з існуючими SPIFFS образами (не актуально, їх немає)

---

### ADR-ST-002: Partition Table — Option B (Optimized)

**Статус:** ✅ Прийнято

**Контекст:** Default 8MB partition table субоптимальна. Вибір між Option A, B, C.

**Рішення:** Option B — дві LittleFS partitions (`sys` + `data`), app slots 2.5 MB.

**Аргументи:**
- App slots 2.5 MB: firmware наразі ~800 KB debug / ~500 KB release. Навіть з повним WiFi+BLE+AsyncWebServer+ArduinoJson+AlertMgr — реалістично ≤1.5 MB. Запас є.
- Дві FS partitions: `uploadfs` безпечний для даних користувача
- NVS 60 KB: достатньо для всіх namespaces + BLE bonding + майбутніх розширень

**Наслідки:**
- Потребує custom Python upload script для PlatformIO
- Перехід з default_8MB.csv: повне стирання flash (один раз при впровадженні)
- `boards/m5cardputer-adv.json` оновлюється: `"partitions": "cointrace_8MB.csv"`

---

### ADR-ST-003: Calibration в NVS, не в LittleFS

**Статус:** ✅ Прийнято

**Контекст:** Калібрування LDC1101 займає ~45 секунд реального часу. Якщо губиться при `uploadfs` — unacceptable UX.

**Рішення:** Всі поля calibration — виключно у NVS namespace `"sensor"`.

**Наслідки:**
- `uploadfs` ніколи не торкається NVS → calibration safe
- OTA firmware update також не торкається NVS → safe
- Тільки `factoryResetAll()` або `nvs_flash_erase()` знищує calibration → explicit user action

---

### ADR-ST-004: FAT32 для SD Card

**Статус:** ✅ Прийнято

**Контекст:** Вибір між FAT32 та exFAT для SD.

**Рішення:** FAT32 через Arduino `SD.h`.

**Аргументи:** Universal compatibility (Windows/Mac/Linux без drivers), Arduino built-in, достатній для fingerprint DB та images. exFAT — надлишок для < 32 GB SD.

**Обмеження:** max file size 4 GB (не проблема для поточних use cases).

---

### ADR-ST-005: StorageManager як Singleton Service

**Статус:** ✅ Прийнято

**Контекст:** Як надавати доступ до storage layer плагінам та іншим компонентам?

**Варіант A: Singleton**
```cpp
StorageManager& sm = StorageManager::instance();
sm.saveCalibration(cal);
```
Плюси: простий доступ, одна ініціалізація. Мінуси: не testable без mocking.

**Варіант B: Injected Interface**
```cpp
class IStorageManager { virtual bool saveCalibration(...) = 0; };
// Інжектується в Plugin через Context або конструктор
plugin->init(context_with_storage);
```
Плюси: testable (MockStorage у unit tests), loose coupling з Plugin System. Мінуси: потребує Context передачі.

**Рішення:** Варіант B — відповідає `IPlugin` / `ILogTransport` стилю проекту.

**Thread safety contract:**

| Метод | Потокобезпечний? | Примітка |
|---|---|---|
| `saveCalibration()` / `loadCalibration()` | ✅ Так | NVS Preferences — mutex всередині |
| `incrementMeasCount()` | ✅ Так | Безпечно: тільки MainLoop task; **НЕ atomic** (NVS get+put) ([SA2-1]) |
| `getMeasSlot()` | ✅ Так | Pure computation |
| `LittleFSTransport::write()` queue push | ✅ Так | `xQueueSend()` — thread-safe |
| LittleFS_data file ops (open/write/close) | ⚠️ Потребує `lfs_data_mutex_` | `esp_littlefs` не thread-safe між tasks |
| LittleFS_sys file ops | ✅ Так | Read-only після boot |
| `SDTransport::write()` | ⚠️ Потребує `spi_vspi_mutex` | VSPI shared з LDC1101, mutex per write |
| `FingerprintCache::query()` | ⚠️ Read-only | Concurrent reads safe; write під `lfs_data_mutex_` |

**LittleFS_data concurrency — обов'язковий `lfs_data_mutex_`:**

Три FreeRTOS задачі одночасно читають/пишуть LittleFS_data:
- **MainLoop** (prio=3): `MeasurementStore::save()` — write m_XXX.json
- **LittleFSTransport bg** (prio=2): append log.0.jsonl
- **WebServer handler** (prio=1): read m_XXX.json + cache/index.json

`esp_littlefs` не є thread-safe на рівні file operations між різними FreeRTOS tasks. Без явного mutex — ризик filesystem corruption або torn write.

**RAII guard (без heap alloc):**
```cpp
// src/storage/LittleFSManager.h
class LittleFSDataGuard {
    SemaphoreHandle_t& mtx_;
    bool acquired_;
public:
    // [SA2-8] timeout=500ms: LittleFS rotation тримає mutex ~150-350ms (rename+SD copy+remove).
    //         Якщо ok()=false — caller MUST return error response (HTTP 503 або еквівалент).
    //         НЕ продовжувати file operations без захисту mutex — filesystem corruption!
    explicit LittleFSDataGuard(SemaphoreHandle_t& mtx, uint32_t timeout_ms = 500)
        : mtx_(mtx),
          acquired_(xSemaphoreTake(mtx, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        if (!acquired_) LOG_ERROR("lfs_data lock timeout");
    }
    bool ok() const { return acquired_; }
    ~LittleFSDataGuard() { if (acquired_) xSemaphoreGive(mtx_); }
    LittleFSDataGuard(const LittleFSDataGuard&) = delete;
};
// LittleFS_sys: read-only після boot → mutex НЕ потрібен
```

**FreeRTOS task priorities (зафіксовано):**

| Task | Priority | Мотив |
|---|---|---|
| MainLoop (measurement) | 3 — High | Час-критичний: LDC1101 SPI burst |
| LittleFSTransport bg | 2 — Normal | Буферизований queue, не критичний |
| WebServer handler | 1 — Low | I/O-bound, може чекати |

`lfs_data_mutex_` timeout: 500 мс для всіх задач. Timeout → `LOG_ERROR` + повернення `false`.

**OQ-05 рішення:** Без NVS flash encryption для v1. WiFi credentials в plain text — прийнятно
для dev/maker use case. Flash encryption необоротна (eFuse burn) і унеможливлює debugging через
esptool. Буде переглянуто перед production release.

---

### ADR-ST-006: Ring Buffer 250 вимірів у LittleFS_data

**Статус:** ✅ Прийнято (OQ-03 вирішено)

**Контекст:** Скільки вимірів зберігати on-device без SD.

**Рішення:** 250 файлів `m_000.json` .. `m_249.json`. Slot index обчислюється з `meas_count`:
`slot = meas_count % RING_SIZE = meas_count % 250`.

**Аргументи:**
- Реальний розмір файлу ~700 B (раніше оцінювали ~5 KB — помилка)
- 250 × ~700 B = ~175 KB — комфортно в 1.75 MB partition (10% об'єму)
- 250 вимірів при ~3 хв/вимір = ~12.5 год автономної роботи
- Зменшено з 300 з міркувань надійності (F-02): LittleFS margin 3%→14% на випадок недоступності SD
- Файловий ring (не in-memory) → виміри виживають при reboot та crash

**Наслідки:**
- **Тільки `meas_count` (UInt32) зберігається в NVS.** Write-first invariant: `lfs_file_close()` ПЕРЕД `putUInt`. Безпечно від race condition тому що **тільки MainLoop task** викликає `incrementMeasCount()` — не через атомарність NVS ([SA2-2]).
- `meas_head` та `meas_size` — **не зберігаються** в NVS (видалено за рішенням R-01)
- `slot = meas_count % RING_SIZE` обчислюється при кожному read/write
- Поле **`"complete": true`** присутнє тільки у повністю записаних файлах. При crash під час write поле відсутнє → файл відкидається при читанні як invalid.
- `GET /api/v1/measure/{id}` використовує `id = meas_count` (монотонний ID), не slot index. Так уникаємо id collision після повної ротації ring buffer.
- **Порядок запису (інваріант):** file write ПЕРШИМ (`lfs_file_close()`) → NVS `meas_count++` ДРУГИМ.
  Причина: `meas_count` = підтверджений лічильник валідних вимірів. Crash між write та NVS increment
  створює ghost-файл у slot N, але `meas_count` залишається N−1 → ghost недосяжний через API.
  NVS-first ламає цей інваріант: `meas_count` обганяє реальні файли → HTTP 404 або stale data.

---

### ADR-ST-007: Версіонування записів вимірів

**Статус:** ✅ Прийнято (FINGERPRINT_DB §5 compliance)

**Контекст:** FINGERPRINT_DB v1.4.0 §5 вимагає raw measurements as source of truth.

**Рішення:** Кожен `m_XXX.json` **обов'язково** містить:
```json
{
  "schema_ver": 1,
  "algo_ver": 1,
  "protocol_id": "p1_UNKNOWN_013mm",
  "protocol_ver": "1.0",
  "cal_ts": 1741779000
}
```
> ⚠️ **[F-05]** `protocol_id` буде уточнено після R-01 (перше реальне вимірювання з MIKROE-3240).
> Реальний fSENSOR ≈ 200–500 kHz, значно менше за 1 MHz. Placeholder `"p1_UNKNOWN_013mm"`
> запобігає помилковому внеску у community DB до підтвердження частоти.
> Cross-ref: FINGERPRINT_DB_ARCHITECTURE.md §7 (Frozen physical constants).

Без цих полів файл вважається invalid та ігнорується при community submission.

---

### ADR-ST-008: SD та LDC1101 — Shared VSPI Bus з Mutex

**Статус:** ✅ Прийнято (підтверджено схемою P3 HDR-SMD_14P-P2.54)

**Контекст:** SD card (J3 TF-015) та LDC1101 Click Board P3 підключені через SPI.
Схемотехнічна верифікація (P3 connector HDR-SMD_14P-P2.54) підтвердила:
pin4=SCK=GPIO40, pin5=MOSI=GPIO14, pin6=MISO=GPIO39, pin7=CS=GPIO5 — **той самий** VSPI bus що і SD.
GPIO3(RESET), GPIO4(INT), GPIO6(BUSY) — сигнали керування LDC1101, не SPI.

**Рішення:** Один `SPIClass(VSPI)`, два CS (GPIO12 для SD, GPIO5 для LDC), `spi_vspi_mutex` per burst.

| GPIO | Підключення | Роль | Примітка |
|---|---|---|---|
| GPIO40 | J3 pin 5 (R22 33Ω) + P3 pin 4 | VSPI SCK | ⚠️ MTDO — JTAG конфлікт |
| GPIO14 | J3 pin 3 (R21 33Ω) + P3 pin 5 | VSPI MOSI | — |
| GPIO39 | J3 pin 7 (R19 33Ω) + P3 pin 6 | VSPI MISO | ⚠️ MTCK — JTAG конфлікт |
| GPIO12 | J3 pin 2 (R20 33Ω) | SD CS | — |
| GPIO5  | P3 pin 7 | LDC1101 CS | — |
| GPIO3  | P3 pin 1 | LDC1101 RESET | не SPI |
| GPIO4  | P3 pin 2 | LDC1101 INT | не SPI |
| GPIO6  | P3 pin 3 | LDC1101 BUSY | не SPI |

**Mutex scope (R-04):** `spi_vspi_mutex` береться на **один SPI-барст** (`ldc.readRpL()`), не на весь 4-дистанційний цикл. Детально: §9.4.

**Наслідки:**
- `spi_vspi_mutex` **обов'язковий** для LDC SPI bursts (MainLoop) та SDTransport writes (async task).
- 33Ω series resistors між ESP32 та SD (не LDC) → max 20 MHz для `SD.begin()`.
- JTAG (OpenOCD) та SD **взаємовиключають одне одного** під час роботи.
- **`spi_vspi_mutex` scope для SDTransport ([PRE-3]):** mutex охоплює `SD.open()` → `file.write()` → `file.close()` як **одну атомарну операцію**. НЕ тільки `file.write()` call. Причина: FAT metadata записується при open та close — LDC burst між ними призводить до FAT corruption.
  ```cpp
  // Правильно ([SPI-3] timeout 50 ms — покриває LDC1101 burst ~45 ms + margin):
  if (xSemaphoreTake(spi_vspi_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
      LOG_WARN("SDCardManager", "spi_vspi_mutex timeout, SD write skipped");
      return false;  // не блокувати caller
  }
  File f = SD.open(path, FILE_APPEND);  // FAT metadata
  f.write(data);
  f.close();                            // FAT finalize
  xSemaphoreGive(spi_vspi_mutex);
  // НЕ правильно: mutex тільки навколо f.write()
  // portMAX_DELAY ЗАБОРОНЕНО — ризик system hang при hardware failure ([SPI-3])
  ```
- `SDCardManager.h/.cpp` та `MeasurementStore.h/.cpp` містять `spi_vspi_mutex` guard перед кожним SPI write.

> ⚠️ **[SA2-6] Lock ordering contract (ОБОВ'ЯЗКОВО дотримуватись у всьому codebase):**
> Якщо потрібні обидва mutex одночасно — завжди отримувати в порядку:
> 1. `lfs_data_mutex_`  (LittleFS_data context) — ПЕРШИМ
> 2. `spi_vspi_mutex`   (VSPI bus context) — ДРУГИМ
>
> НІКОЛИ у зворотньому порядку → **deadlock** при співпаданні з `rotate()`.
> Поточні caller sites що тримають обидва: `LittleFSTransport::rotate()` (lfs → spi).
> Інші caller sites тримають лише один mutex — не потребують ordering.

---

### ADR-ST-009: MeasurementStore SD Archive — copy-before-overwrite при overflow

**Статус:** ✅ Прийнято (SR-11)

**Контекст:** LittleFS ring buffer містить 250 слотів. При `meas_count >= RING_SIZE` старий слот
перезаписується. Без явного порядку операцій витісняємий вимір губиться безповоротно —
SD archive (SR-11) не отримує запис.

**Рішення:** Copy-before-overwrite інваріант у `MeasurementStore::save()`. Порядок кроків:
1. Якщо `is_overwrite` — прочитати витісняємий слот у локальний буфер (`lfs_data_mutex_`, окремий lock)
2. Передати буфер у `SDCardManager::writeMeasurement(globalId, buf, len)` — пише на SD (`spi_vspi_mutex` 50 ms, окремий lock)
3. Записати новий вимір у LittleFS (`lfs_data_mutex_`)
4. `incrementMeasCount()` — NVS, **завжди останнє** (ADR-ST-006 write-first invariant)

Два mutex ніколи не тримаються одночасно → lock ordering [SA2-6] виконується структурно.

```cpp
void MeasurementStore::save(const Measurement& m) {
    uint32_t slot         = meas_count_ % RING_SIZE;
    bool     is_overwrite = (meas_count_ >= RING_SIZE);

    // [ADR-ST-009] Крок 1+2: SD archive до перезапису слоту
    if (is_overwrite) {
        uint8_t buf[MAX_MEAS_JSON];
        size_t  len = 0;
        { LittleFSDataGuard g(lfs_data_mutex_); len = readSlotRaw(slot, buf, sizeof(buf)); }
        if (len > 0) {
            // sdMgr_.writeMeasurement() логує обидва DEGRADED сценарії самостійно
            sdMgr_.writeMeasurement(meas_count_ - RING_SIZE, buf, len);
        }
    }
    // Крок 3: LittleFS write (lfs_file_close() всередині)
    { LittleFSDataGuard g(lfs_data_mutex_); writeLittleFS(slot, m); }
    // Крок 4: NVS завжди останнє
    incrementMeasCount();
}
```

**`SDCardManager::writeMeasurement(uint32_t globalId, const uint8_t* buf, size_t len)`:**
- Нова публічна функція; не знає про LittleFS — тільки SD.
- Шлях: `SD:/CoinTrace/measurements/m_<globalId>.json`
- `ensureDir("SD:/CoinTrace/measurements/")` при першому записі.
- `spi_vspi_mutex` timeout **50 ms** ([SPI-3]). При timeout: `LOG_WARN("SDCardManager", "spi_vspi_mutex timeout, SD write skipped")` → `return false`.
- При `!sdAvailable()`: `LOG_WARN("MeasurementStore", "SD unavailable, slot %u lost", slot)` → `return false`.
- При SD write fail: `LOG_WARN("MeasurementStore", "SD write failed, slot %u lost", slot)` → `return false`.
- НЕ викликає `Logger::*` під час SD IO (аналогія SPI-3 контракту).

**`MAX_MEAS_JSON`** — константа (рекомендовано 2048 B; типовий `m_XXX.json` ≈ 500–800 B).

**Power-fail поведінка:**

| Момент падіння | Результат |
|---|---|
| Під час читання LittleFS (крок 1) | LittleFS слот не змінений. `meas_count` не змінився → при наступному boot `save()` повториться. SD файл відсутній — прийнятно (COULD). |
| Під час SD write (крок 2) | SD файл відсутній або неповний. LittleFS слот ще не перезаписаний — старий запис цілий. `meas_count` не змінився → наступний `save()` повторить кроки 1+2 (SD файл перезапишеться тим же вмістом). |
| Під час LittleFS write (крок 3) | LittleFS atomic write — файл або старий, або новий. `meas_count` не змінився → ghost slot НЕ досяжний через API (ADR-ST-006). SD вже має копію. |
| Між LittleFS write та NVS increment (крок 4) | Новий файл записаний, `meas_count` вказує на попередній → ghost slot. При наступному `save()` той самий slot перезапишеться. Коректно. |

**DEGRADED (SD недоступна):** крок 2 пропускається, `LOG_WARN`, система продовжує роботу нормально. SD archive = COULD вимога, не MUST.

---

## 12. Integration: Logger, Plugin System, Connectivity

### 12.1 Logger ↔ Storage

Logger v2.0 архітектура (LOGGER_ARCHITECTURE v2.0.0) вже спроектувала `SDTransport` як async FreeRTOS queue. Coordination з LittleFS logs:

```
Logger dispatch chain (Cardputer-Adv, Модель S):
  SerialTransport     → Serial output (sync, < 100 µs)
  RingBufferTransport → In-memory 250 entries (sync, < 5 µs)
  LittleFSTransport   → /data/logs/log.0.jsonl (async queue, power-fail safe)
  # SDTransport — НЕ в dispatch chain для Cardputer-Adv
  # SD archive виконується через LittleFSTransport::rotate() (batch copy log.1.jsonl → SD)
```

> **✅ OQ-07 закрито (архівна семантика SD, [PRE-1] Варіант A, Модель S):**
> SDTransport **НЕ додається** до Logger dispatch chain для Cardputer-Adv.
> SD archive: `LittleFSTransport::rotate()` копіює `log.1.jsonl` → SD (batch, не per-entry).
> LittleFS = «гарячі» (recent 400 KB = 2 × 200 KB), SD = «холодний архів» (повні ротації).

**LittleFSTransport** (новий, потребує реалізації):
- `write()` кладе в FreeRTOS queue (< 5 µs, non-blocking)
- Background task дренує queue, **тримає `log.0.jsonl` відкритим** між записами:
  - `lfs_file_open(APPEND)` один раз при старті або після rotation
  - `lfs_file_sync()` після кожного запису (power-fail safe, без close overhead)
  - `lfs_file_close()` **тільки** при rotation або при зупинці системи
- При розмірі `log.0.jsonl` > 200 KB: ротація (rename `.0`→`.1`, copy `.1`→SD якщо є, delete `.1`, create новий `.0`)
- Rotation mutex scope: `lfs_data_mutex_` на весь rename+copy+delete цикл; WebServer timeout = 500 ms для reads (збільшено від 100 ms через [FUTURE-1])

**Coordination з RingBufferTransport:**
- RingBuffer (100 entries in RAM) для API endpoint `GET /api/v1/log`
- LittleFSTransport для persistent logs між reboot
- SDTransport для архіву — лише якщо SD доступна

> ⚠️ **Bug B-01 fix:** `RingBufferTransport(250, /*usePsram=*/false)` — ESP32-S3FN8 не має PSRAM. RING_SIZE=250 (ADR-ST-006).

> ⚠️ **LittleFS_data ватчдог пороги (рекомендація):**
> При `LittleFSTransport` перевіряти вільне місце LittleFS_data після кожного запису логу:
> - **< 50 KB вільно**: примусова ротація незалежно від розміру поточного лог-файлу. LOG_WARN.
> - **< 20 KB вільно**: зупинити запис логів в LittleFS; виміри продовжують заходитись в RAM тільки (RingBuffer). LOG_ERROR.
> Обидва пороги: filesystem corruption від write при повному партишні. Сенсор унеможливлює перезапис вимірів.
>
> **[SA2-7] Mutex scope:** `esp_littlefs_info()` (→ `lfs_fs_size()`) **не є thread-safe** між FreeRTOS tasks.
> Виклик **обов'язково** виконувати всередині `lfs_data_mutex_` scope у LittleFSTransport bg task,
> після `lfs_file_sync()`, перед звільненням mutex. НЕ виконувати з WebServer handler або MainLoop.

### 12.2 Plugin System ↔ Storage

Plugin System (PLUGIN_ARCHITECTURE) визначає `data/config.json` як джерело конфігурації. З двома FS partitions:

```
Plugin discovery:
  1. LittleFSManager::sys().open("/config/device.json")  ← літальна помилка якщо немає
  2. Parse JSON → plugin list
  3. Для кожного plugin з "storage" type → SDCardManager::mount()
```

**Важливо:** якщо `device.json` відсутній (наприклад після factory reset) → система має мати **hardcoded default config** як fallback, щоб пристрій не "цегленів".

```cpp
// Псевдокод Plugin init
bool PluginManager::loadConfig() {
    if (!lfs_sys_.exists("/config/device.json")) {
        LOG_WARN("Config not found, using defaults");
        return loadDefaultConfig();  // Hardcoded minimal config
    }
    return parseConfigFile("/config/device.json");
}
```

### 12.3 Connectivity ↔ Storage

Endpoints визначені в CONNECTIVITY_ARCHITECTURE v1.2.0, що торкаються storage:

| Endpoint | Storage operation |
|---|---|
| `GET /api/v1/status` | NVS read: heap, version, bat, wifi, ble + LittleFS/SD stats. Поле `"storage"` у відповіді: `{"lfs_free_kb": 1200, "lfs_total_kb": 1750, "sd_free_mb": 12400, "sd_total_mb": 15000, "meas_count": 42, "ring_used": 42, "ring_capacity": 250}`. Примітка: `ring_used = min(meas_count, RING_SIZE)` — при `meas_count ≤ 250` дорівнює meas_count; при `meas_count > 250` повертається 250 (буфер заповнений). |
| `GET /api/v1/measure/{id}` | LittleFS read: `/data/measurements/m_XXX.json` де `slot = id % RING_SIZE`. `id` = монотонний `meas_count` (не slot index). TTL 60s. **Обов'язкова перевірка ([PRE-2]):** `if (meas_count == 0 \|\| id >= meas_count \|\| (meas_count > RING_SIZE && id < meas_count - RING_SIZE)) → HTTP 404`. Без цієї перевірки клієнт отримує silent wrong data при ring overflow. |
| `POST /api/v1/database/match` | LittleFS read: `/data/cache/index.json` або SD read |
| `GET /api/v1/log` | RingBufferTransport::getEntries() + LittleFS log |
| `POST /api/v1/ota/update` | NVS write: ota namespace (confirmed, pre_ver) |
| `GET /` (web UI) | LittleFS read: `/sys/web/index.html` |

---

## 13. Wear leveling та довговічність flash

### 13.1 Аналіз write frequency

| Дані | Де | Частота | Сектори/добу | Ресурс @ 100K cycles |
|---|---|---|---|---|
| Лічильник вимірів (NVS) | NVS | 1/вимір, ~20/добу | 0.16 (NVS сам розподіляє) | ~600,000 діб |
| Файл вимірів (LittleFS) | LittleFS_data | 1/вимір, ~20/добу | 0.3 (wear leveled) | ~330,000 діб |
| Логи (LittleFS) | LittleFS_data | ~1 KB/5 хв = 288 KB/добу | 72 / N_pages | залежить від розміру partition |
| Логи (SD) | SD FAT32 | ~288 KB/добу | 72 sectors / total | SM cards — 10K-100K cycles |
| Web UI (uploadfs) | LittleFS_sys | 1/deploy (~weekly) | весь partition | необмежено при weekly deploys |

**Критичний висновок щодо логів:**  
`LittleFS_data = 1.75 MB = 448 секторів (1792 KB ÷ 4 KB)`. Steady-state block usage:
```
Block budget (LittleFS, block_size=4KB, inline_max=256B):
  measurements/ : 250 files × 1 block = 250 blocks = 1000 KB
  logs/         : 2 files × 200KB/4KB = 2 × 50 blocks = 100 blocks = 400 KB
  cache/        : index.json 80KB = 21 blocks + index_crc32.bin 4B (inline) + sd_generation.bin 4B (inline) = 84 KB
  metadata      : dirs + superblock ≈ 14 blocks = 56 KB
  ──────────────────────────────────────────────────────────
  TOTAL NEEDED  : 385 blocks = 1540 KB
  AVAILABLE     : 448 blocks = 1792 KB
  FREE MARGIN   : 63 blocks = 252 KB (~14%)
```
При 72 writes/добу по wear-leveled filesystem:
```
448 sectors × 100,000 cycles / 72 daily writes = ~622,000 днів ≈ 1704 роки
(з WA=2 для active write areas: ~852 роки — достатньо для lifetime use)
```
Wear leveling на LittleFS розподіляє writes рівномірно → ніяких проблем.

**Критичний висновок щодо SD:**  
Дешеві SD карти (A1 class) мають 10,000-30,000 P/E cycles при правильному wear leveling.
288 KB logs/добу = 72 writes × 4 KB sectors → при 10K cycles → ~139 діб до failure!

> ⚠️ **Рекомендація:** Логи на SD писати **не суцільним потоком** а батчами (наприклад, при ротації LittleFS — раз на кілька годин dump). Це зменшує SD writes з ~72→~24 на добу.
>
> **[SA2-5] Виправлений SD lifetime розрахунок (post-PRE-8 batch model):**
> ```
> Batch writes: ротація ~кожні 2-3 год = ~24 SD writes/добу (не 72 per-entry!)
> При 10K cycles (дешеві A1 SD): 10,000 / 24 ≈ 417 діб ≈ 1.1 рік — все ще тривожно!
> При 40K cycles (Samsung PRO Endurance / Sandisk MAX Endurance): ~4 роки — прийнятно.
> Рекомендація: використовувати high-endurance SD (UHS-I U3/A2 class, наприклад Samsung PRO Endurance).
> ```

### 13.2 NVS wear leveling

NVS має вбудований wear leveling на рівні NVS pages. Monotonic counter `meas_count` у NVS:
- NVS entries 32 B у pages 4 KB
- При кожному write entry: NVS шукає вільний slot у сусідній page
- 60 KB / 4 KB = 15 pages; 13 active; rotation: 100K × 13 = 1.3M writes на namespace

При 20 writes/добу (вимірів) + 50 writes/добу (логи metadata): ~70 writes/добу → 1.3M / 70 ≈ **18,000 діб ≈ 49 років**. Без проблем.

---

## 14. Factory Reset Strategy

### 14.1 Два рівні скидання

#### Soft Reset (key combo, наприклад `Fn+Del` 3 секунди)

```
Soft Reset = відновлення до "щойно налаштованого" стану БЕЗ втрати калібрування:

1. NVS erase namespace "wifi"     → пристрій знову в AP mode
2. NVS erase namespace "system"   → default brightness, мова, ім'я
3. **Виміри НЕ видаляються ([PRE-5])** — вони залишаються в LittleFS_data і будуть
   природньо перезаписані новими вимірами через ring overflow semantics.
   Причина: якщо видалити файли, але залишити `meas_count` (namespace `storage` не стирається
   при Soft Reset) — `ring_used = min(meas_count, 250)` показує 250, реально файлів 0 → API 404.
   Ring overwrite є коректним механізмом «очищення» без inconsistency.
4. ЗБЕРІГАЮТЬСЯ: "sensor" (calibration!), "ota", "storage" (meas_count для continuity)
5. Restart → boot у AP provisioning mode
```

**Use case:** передати пристрій іншому колекціонеру, не втрачаючи калібрування.

#### Hard Reset (тривале натискання, наприклад `Fn+Del` 10 секунд з підтвердженням на дисплеї)

```
Hard Reset = повне відновлення заводського стану:

1. Display: "HARD RESET? Press ENTER to confirm, ESC to cancel"
2. Якщо підтверджено:
   a0. LittleFSTransport::stop()      → xTaskDelete bg task + lfs_file_close(currentFile_)
       ; [SA2-4] ОБОВ'ЯЗКОВО до format() — bg task тримає log.0.jsonl відкритим (open-once pattern)
       ; format() з відкритим lfs_file_t handle = undefined behavior / LittleFS corruption
   a.  NVS nvs_flash_erase()          → всі namespaces
   b.  LittleFS_data.format()         → всі дані користувача
   c.  esp_ota_set_boot_partition(app0) → rollback до slot 0
   d.  LOG_WARN("Hard reset executed") → зберігається в Serial тільки (LittleFS вже форматований)
   e.  esp_restart()                  → reboot
3. Після reboot: пристрій як щойно з коробки
```

> ⚠️ **Hard Reset знищує калібрування.** UI повинен явно попередити:
> *"Calibration data will be lost. The sensor will need to be recalibrated (~45 seconds)."*

### 14.2 Захист від випадкового Hard Reset

- Двоетапне підтвердження (key combo + ENTER)
- Display progress bar під час стирання
- Неможливий через API (POST /api/v1/factory_reset) без фізичного підтвердження (аналог ADR-007 OTA)

---

### 14.3 Soft Shutdown (v1.5 backlog)

> **Пріоритет:** MEDIUM — UX покращення + захист SD FAT32. Не блокує v1 (NVS і LittleFS power-fail safe; SD batch model ~24 writes/добу мінімізує ризик FAT corruption при апаратному вимкненні).

#### Навіщо

Cardputer-Adv вимикається апаратним перемикачем (SW2 SPDT). ESP32-S3 не отримує попередження — живлення зникає миттєво. Після появи SD archive (P-4) і WiFi (Wave 8) graceful shutdown стає практично важливим:
- SD FAT metadata записується при `close()` — перерване вимкнення може пошкодити FAT
- WiFi/BLE клієнти отримують timeout замість чистого disconnect
- Open-once log file (`log.0.jsonl`) потребує `lfs_file_close()` для гарантованого flush

#### Тригер

**`Fn + Q`** — натиснути і тримати 2 секунди. `Fn + Del` вже зарезервовано для Factory Reset (§14.1).

#### UX flow

```
T=0.0s  Fn+Q натиснуто
T=0.5s  Display: "Hold to shut down..." + прогрес-бар
T=2.0s  Display: "Shutting down..." → запуск послідовності
T=2.5s  Display: "Safe to power off ✓" → esp_deep_sleep_start()
        Якщо клавішу відпустити до T=2s — shutdown скасовується
```

#### Послідовність завершення (~550 мс загалом)

```
[S1]  gShutdownRequested = true          ; зупинити measurement pipeline (50 мс)
[S2]  gPluginSystem.shutdownAll()        ; LIFO: LDC1101 → Sleep mode (100 мс)
      ; ⚠️ Display плагін НЕ вимикається на цьому кроці — потрібен для [S6]
[S3]  WiFi.disconnect() + BLE::deinit() ; клієнти отримують чистий disconnect (200 мс)
[S4]  gLfsTransport.end()               ; lfs_file_sync() + lfs_file_close() (100 мс)
[S5]  gSDCard.umount()                  ; SD.end() — FAT metadata finalized (50 мс)
[S6]  Display: "Safe to power off ✓"   ; brightness dim (50 мс)
[S7]  esp_deep_sleep_start()            ; без wakeup source → тільки power cycle будить
```

**Deep sleep vs Light sleep:** deep sleep (~10 µA) обрано через 400× менше споживання ніж light sleep (~4 mA). RAM не потрібна — всі дані збережені на flash/SD. Вихід: тільки hardware SW2 OFF→ON → повний boot через `setup()`.

#### Діаграма станів

```
NORMAL OPERATION
      │ Fn+Q held 0.5s
      ▼
SHUTDOWN_PENDING ──── released ──→ NORMAL OPERATION
      │ Fn+Q held 2.0s
      ▼
SHUTDOWN_ACTIVE → [S1]..[S7] → DEEP SLEEP (~10 µA)
                                        │ SW2 OFF→ON
                                        ▼
                                   setup() (full boot)
```

#### Порівняльна таблиця операцій скидання

| Аспект | Soft Shutdown (Fn+Q 2s) | Soft Reset (Fn+Del 3s) | Hard Reset (Fn+Del 10s) |
|---|---|---|---|
| NVS | Зберігається | WiFi+system стираються | Все стирається |
| LittleFS_data | Зберігається | Зберігається | Форматується |
| SD | `umount()` — FAT safe | Не торкається | Не торкається |
| Calibration | Зберігається | Зберігається | Стирається |
| Кінцевий стан | Deep sleep | Reboot | Reboot |

#### GPIO0 dual-role (важлива нотатка)

GPIO0 має **дві різні ролі** залежно від контексту:
- **Boot time** (§17.2 [1.5]): 5-секундний holddown → `LittleFS_data.format()`. Перевіряється **тільки в `setup()`** — не конфліктує з runtime.
- **Runtime** (fallback): якщо TCA8418 (клавіатура) недоступна → GPIO0 long press (2s) → soft shutdown. Реалізувати разом з TCA8418 fault handling.

#### Edge cases

| Ситуація | Поведінка |
|---|---|
| `CoinState == COIN_PRESENT` при Fn+Q | Display: "Remove coin first". Shutdown відкладається до COIN_REMOVED/IDLE. Force після 5s warning. |
| `calibrate()` активне | Завершується природньо (max +3 сек). `gShutdownRequested` перевіряється в `loop()`, не в `calibrate()`. |
| OTA update активне (Wave 8) | OTA callback перевіряє `gShutdownRequested` → reject OTA → proceed with shutdown. ESP-IDF auto-rollback при наступному boot. |
| TCA8418 I2C failure | GPIO0 runtime long press як fallback (div §17.2). |

---

## 15. Фазовий roadmap

### Фаза P-1: Foundation (блокери → реалізація)

| Артефакт | Що робить | Пріоритет |
|---|---|---|
| **Fix B-01** | `usePsram=false` у `main.cpp` | URGENT |
| `partitions/cointrace_8MB.csv` | Кастомна partition table (Option B) | P-1 |
| `boards/m5cardputer-adv.json` update | `"partitions": "cointrace_8MB.csv"` | P-1 |
| `platformio.ini` update | `board_build.filesystem = littlefs` | P-1 |

> ⚠️ **Критична пастка при першому flash:** зміна partition table **вимагає** повного `erase_flash`
> перед upload. Без нього bootloader знаходить app0 за старим offset → boot loop.
>
> Правильна послідовність (один раз при зміні partition table):
> ```
> esptool.py --chip esp32s3 erase_flash
> pio run -t upload          # firmware
> pio run -t uploadfs        # LittleFS sys
> ```

#### P-1 Acceptance Criteria

Фаза P-1 вважається завершеною коли виконані **всі** перевірки:

- [ ] Partition table flashed without boot loop
- [ ] `NVSManager::begin()` returns true (Serial log: `NVS: all namespaces open`)
- [ ] NVS round-trip: `putString("test","ok")` → reboot → `getString("test")` == "ok"
- [ ] `LittleFSManager::mountSys()` success → file `/sys/config/device.json` readable
- [ ] `LittleFSManager::mountData()` success → can create and read `/data/test.txt`
- [ ] `SDCardManager::tryMount()` — graceful DEGRADED log if no SD card present
- [ ] `SDCardManager::tryMount()` with card → can create file on SD
- [ ] `meas_count` persists across reboot (NVS write → power off → boot → same value)
- [ ] Sensor calibration data persists across reboot
- [ ] `pio run -t uploadfs -e fs-sys` does **not** destroy LittleFS_data content
- [ ] Soft Reset: WiFi erased, calibration preserved
- [ ] Hard Reset: everything erased, device boots cleanly to defaults
- [ ] GPIO0 held at boot: LittleFS_data formatted, device restarts

### Фаза P-2: NVS Layer

| Артефакт | Що робить |
|---|---|
| `src/storage/NVSManager.h/.cpp` | Wrapper над Preferences: wifi, sensor, system, storage, ota namespaces |
| `test/storage/test_nvs_manager.cpp` | Unit tests (native або on-device) |

### Фаза P-3: LittleFS Layer

| Артефакт | Що робить |
|---|---|
| `src/storage/LittleFSManager.h/.cpp` | Mount sys + data partitions, dir creation, error handling |
| `src/storage/MeasurementStore.h/.cpp` | Ring buffer 250 × m_XXX.json, versioned format |
| `src/storage/LittleFSTransport.h/.cpp` | Async Logger transport (FreeRTOS queue + log rotation) |
| `data/config/device.json` | Default plugin config (для uploadfs) |
| `data/web/` | Web UI placeholder (для uploadfs) |

### Фаза P-4: SD Card Layer

| Артефакт | Що робить |
|---|---|
| `src/storage/SDCardManager.h/.cpp` | Optional SD mount, graceful failure (VSPI shared з LDC1101, `spi_vspi_mutex` обов'язковий per write) |
| `src/storage/SDTransport.h/.cpp` | Logger SD async transport (LOGGER_ARCHITECTURE спроектував) |
| `src/storage/FingerprintCache.h/.cpp` | index.json load → RAM, hash validation, invalidation |

### Фаза P-5: StorageManager Facade

| Артефакт | Що робить |
|---|---|
| `src/storage/IStorageManager.h` | Interface for injection into Plugin System |
| `src/storage/StorageManager.h/.cpp` | Facade: routing між Tier 0/1/2, graceful degradation logic |
| `src/storage/MockStorageManager.h` | Mock для unit tests |

---

## 16. Матриця виживаємості даних

| Подія | NVS (WiFi, Cal, Settings) | LittleFS_sys (Web UI, Config) | LittleFS_data (Meas, Logs) | SD Card |
|---|---|---|---|---|
| Вимкнення/включення | ✅ Зберігається | ✅ Зберігається | ✅ Зберігається | ✅ Зберігається |
| OTA firmware update | ✅ Зберігається | ✅ Зберігається | ✅ Зберігається | ✅ Зберігається |
| `pio run -t upload` (firmware only) | ✅ | ✅ | ✅ | ✅ |
| `pio run -t uploadfs -e fs-sys` | ✅ | ⚠️ Перезаписується (навмисно) | ✅ НЕ торкається | ✅ |
| Full chip erase | ❌ Знищується | ❌ | ❌ | ✅ Not affected |
| `esp_partition_erase_range()` на LittleFS_data | ✅ | ✅ | ❌ Знищується | ✅ |
| Power failure під час write | ✅ (NVS atomic) | ✅ (LittleFS atomic) | ✅ (LittleFS atomic) | ⚠️ FAT32 не atomic |
| SD видалена фізично | ✅ | ✅ | ✅ | ❌ SD data gone |
| Soft Factory Reset | ❌ WiFi + Settings | ✅ | ✅ Зберігаються (ring overflow = cleanup, [PRE-5]) | ✅ |
| Hard Factory Reset | ❌ Все | ❌ | ❌ | ✅ |

---

## Відкриті питання — Summary

| ID | Питання | Вплив | Для вирішення |
|---|---|---|---|
| **OQ-01** | ~~LDC1101 та SD на одному SPI bus?~~ | ~~SPI mutex architecture~~ | ✅ **Вирішено: shared VSPI bus + spi_vspi_mutex per burst (ADR-ST-008)** |
| **OQ-02** | ~~Одна чи дві LittleFS partitions?~~ | ~~uploadfs safety~~ | ✅ **Вирішено: Option B, дві partitions (ADR-ST-002)** |
| **OQ-03** | ~~Ring buffer size (50 вимірів достатньо?)~~ | ~~UX, storage allocation~~ | ✅ **Вирішено: 250 (ADR-ST-006)** |
| **OQ-04** | ~~Де plugin config.json?~~ | ~~Boot dependency~~ | ✅ **Вирішено: /sys/config/device.json** |
| **OQ-05** | ~~NVS flash encryption до v1 чи після?~~ | ~~Security, one-way decision!~~ | ✅ **Вирішено: без encryption v1 (ADR-ST-005)** |
| **OQ-06** | ~~Session ID для вимірювань?~~ | ~~Community DB трасованість~~ | ✅ **Вирішено: device_id + "_" + meas_count** |
| **OQ-07** | ~~LittleFS log rotation — окремий transport чи periodic dump?~~ | ~~Logger architecture~~ | ✅ **Вирішено: 2×200KB LittleFS hot (Variant A, [PRE-1]) + SD cold archive при ротації (§12.1)** |

---

*Версія 1.2.0 — shared VSPI bus підтверджено схемою P3 (CS=GPIO5). lfs_data_mutex специфіковано. Coredump boot order виправлено. GPIO0 recovery додано. Write order rationale в ADR-ST-006. OQ-07 закрито.*  
*Версія 1.3.0 — [PRE-1]..[PRE-8] закрито: Variant A 2×200KB, ID range validation, mutex scope, GPIO0 TWDT fix, Soft Reset ring semantics, coredump_mc<N>.json, open-once LittleFS.*  
*Версія 1.3.1 — [PRE-10]: incrementMeasCount THREADING constraint. [PRE-11]: log_gen removed from NVS. Cross-doc sync (LOGGER §6.6, CONNECTIVITY HTTP 404).*  
*Версія 1.3.2 — [SA2-1..SA2-10]: second audit fixes — atomic claim residues (ADR-ST-005/006), Hard Reset stop bg task, Boot [5] LittleFSTransport conditional, guard timeout 100→500ms + ok()=false contract, SD write model corrected, lock ordering contract, free space check mutex scope, §16/§10 doc sync.*  
*Версія 1.4.0 — [F-02] RING_SIZE 300→250: LittleFS margin 3%→14%; [F-03] ADR-ST-005 timeout text 100→500ms; [F-04] Wire.begin(SDA=GPIO8, SCL=GPIO9) у boot sequence [2.3]; [F-05] §12.1 SDTransport Модель S — не в Logger dispatch chain; [F-06] CRC32 spec: /data/cache/index_crc32.bin (4B little-endian uint32_t).*  
*Версія 1.4.1 — [FDB-05 Variant G] sd_generation.bin у §8.2 cache tree; boot [7a] generation staleness check після CRC32; buildCache() зберігає sd_generation.bin.*  
*Версія 1.7.0 — §14.3 Soft Shutdown (Fn+Q, deep sleep, [S1]..[S7] sequence, state machine, edge cases, GPIO0 dual-role); §17.2 boot [1.4] deep sleep wakeup check (skip GPIO0 recovery); CONNECTIVITY_ARCHITECTURE §5.3 shutdown_pending/shutdown_complete WebSocket events.*  
*Наступний крок: реалізація P-4 (SDCardManager + FingerprintCache)*

---

## 18. Повернення до заводської прошивки M5Stack (Factory Restore)

> **Чому цей розділ потрібен:** CoinTrace вносить незворотні зміни в flash (partition table, NVS, LittleFS).
> Без цієї процедури користувач, який хоче повернути Cardputer-Adv до оригінального стану, 
> потрапляється з пристроєм у невідомому стані. Відновлення завжди можливе і займає ~5 хвилин.

### 18.1 Що змінюється після встановки CoinTrace

| Компонент | Заводське | CoinTrace | Зворотність |
|---|---|---|---|
| Partition table | `default_8MB.csv` | `cointrace_8MB.csv` (Option B) | Тільки через `erase_flash` |
| NVS content | M5Stack WiFi/UIFlow data | CoinTrace namespaces (wifi, sensor, system) | Через `nvs_flash_erase()` або `erase_flash` |
| LittleFS/SPIFFS | M5Stack demo assets / UIFlow | CoinTrace web UI + plugin config | Через format або `erase_flash` |
| Firmware | M5Stack User Demo / UIFlow2 | CoinTrace firmware | Перезаписується при flash |
| SD карта | Не змінюється | CoinTrace measurements/DB | Відформатувати окремо на ПК |

### 18.2 Метод A — M5Burner (рекомендовано, без командного рядка)

```
1. Завантажити M5Burner: https://docs.m5stack.com/en/uiflow/m5burner/intro
2. Перевести Cardputer-Adv у Download Mode:
   a) Вимкнути (перемикач збоку → OFF)
   b) Натиснути та ТРИМАТИ кнопку G0
   c) Увімкнути (перемикач → ON)
   d) Відпустити G0 — екран порожній (нормально)
3. Підключити USB-C кабель
4. У M5Burner: категорія Cardputer-Adv → "Цardputer-Adv User Demo" → Burn
   M5Burner виконує erase_flash автоматично
5. Після завершення: пристрій у тому самому стані що і з коробки
```

### 18.3 Метод B — esptool.py (повний контроль, командний рядок)

```bash
# Перенести в Download Mode (див. метод A крок 2), потім:

# Повне стирання flash (ОБОВ'ЯЗКОВО при зміні partition table)
esptool.py --chip esp32s3 --port COM3 erase_flash
# Windows: COM3, Linux/macOS: /dev/ttyACM0 або /dev/tty.usbmodem*

# Прошивка заводської firmware (.bin з M5Burner або GitHub Releases)
esptool.py --chip esp32s3 --port COM3 --baud 1500000 \
  write_flash 0x0 Cardputer-Adv-UserDemo.bin

# Вимкнути → увімкнути (без G0)
```

### 18.4 Метод C — Web Flasher (без встановлення ПЗ)

```
1. Відкрити Chrome або Edge (WebSerial API недоступний в Firefox)
   https://web.esphome.io/ або https://adafruit.github.io/Adafruit_WebSerial_ESPTool/
2. Перенести в Download Mode
3. Connect → обрати serial port → занти .bin заводської firmware
4. Flash address: 0x0 → Program
```

### 18.5 SD карта

Procedures above do **not** touch the SD card. If needed, format it separately on a PC (FAT32).

### 18.6 Предуповедження для користувачів (README.md)

> CoinTrace replaces the factory firmware and flash partition layout.
> **Restoring to factory state is always possible** and takes ~5 minutes.
>
> Before installing CoinTrace:
> - If you use UIFlow2 — export your projects
>
> To restore to stock firmware at any time:
> - You need: a USB-C cable and M5Burner (free, https://docs.m5stack.com/en/uiflow/m5burner/intro)
> - Full instructions: `docs/architecture/STORAGE_ARCHITECTURE.md §18`

*Версія 1.5.1 — [F-05] protocol_id плейсхолдер "p1_UNKNOWN_013mm"; P-1 Acceptance Criteria в §15; §18 Factory Restore додано.*
*Версія 1.6.0 — [AUDIT-2026-03-14] F-05/F-01–F-08 опрацьовано.*

---

## 17. Послідовність завантаження (Boot Sequence)

### 17.1 Ordered Init — чому порядок критичний

Між компонентами існують циклічні залежності:
- Logger потрібен для debug output, але LittleFSTransport ще не змонтований
- Plugin System потребує config з LittleFS_sys, але LittleFS_sys монтується після NVS

**Правило:** використовувати SerialTransport для bootstrap-логів, решта транспортів долучаються пізніше.

### 17.2 Послідовність ініціалізації

```
[1] Serial.begin(115200)                     ; bootstrap Serial
[1.4] deep sleep wakeup check               ; захист від помилкового recovery при wakeup
    cause = esp_sleep_get_wakeup_cause()
    if cause != ESP_SLEEP_WAKEUP_UNDEFINED:
        ; пристрій прокинувся з deep sleep (наприклад після soft shutdown §14.3)
        ; GPIO0 recovery check [1.5] не застосовується — це не cold boot
        ; Зараз: cause завжди UNDEFINED (soft shutdown без wakeup source)
        ; Резервування: якщо в майбутньому додається timer/GPIO wakeup — guard тут
        Serial.println("[BOOT] Deep sleep wakeup — skip recovery check")
        goto [2]  ; пропустити [1.5] GPIO0 recovery
[1.5] GPIO0 recovery check                  ; BOOT button (active LOW, підтяжка 10kΩ)
    ; [PRE-4] display() НЕ використовується — LCD не ініціалізований до кроку [9]
    ; [PRE-4] delay(5000) → TWDT race → замінено на 50×100ms loop з wdt reset
    if GPIO0 == LOW:
        Serial.println("[BOOT] GPIO0 HELD — release within 5s to cancel LittleFS_data format")
        released = false
        for i in 0..49:                          ; 50 × 100 ms = 5 seconds
            esp_task_wdt_reset()
            delay(100)
            if digitalRead(GPIO_NUM_0) == HIGH:
                released = true
                break
        if NOT released:
            Serial.println("[BOOT] Formatting LittleFS_data...")
            esp_spiffs_format("littlefs_data")   ; IDF API — не потребує попереднього mount()
            Serial.println("[BOOT] Format done. Restarting.")
            esp_restart()
    ; антидот: TCA8418 fail + LFS_data corrupt = stuck boot без виходу
[2] NVSManager::begin()                      ; NVS open all namespaces
    → якщо FAIL: FATAL MODE (halt + display error)
[2.3] Wire.begin(/*SDA=*/8, /*SCL=*/9)       ; I2C bus init (SDA=GPIO8, SCL=GPIO9 per schematic)
[2.5] TCA8418::begin(0x34, GPIO11_INT)       ; ініціалізація keyboard controller (I2C 0x34)
    → якщо FAIL: DEGRADED MODE — клавіатура недоступна (включно Factory Reset combo!)
      LOG_WARN("TCA8418 init failed — keyboard unavailable")
[3] LittleFSManager::mountSys()              ; монтуємо /sys
    → якщо FAIL: FATAL MODE — без device.json неможливо init plugins
[4] LittleFSManager::mountData()             ; монтуємо /data
    → якщо FAIL: DEGRADED MODE — без вимірів/логів, але alive
      опціонально: prompt user to format LittleFS_data
[5] ; [SA2-3] LittleFSTransport доступний ТІЛЬКИ якщо mountData() [4] успішний
    якщо mountData() успіх:
        Logger::init(SerialTransport,
                     RingBufferTransport,
                     LittleFSTransport)      ; повний transport chain
    інакше (DEGRADED після [4]):
        Logger::init(SerialTransport,
                     RingBufferTransport)    ; без LittleFSTransport — FS не змонтована
        LOG_WARN("LittleFS_data DEGRADED — LittleFSTransport disabled")
; [6] навмисно відсутній — esp_core_dump_image_check() перенесено у [7.5]
;     (coredump потребує SD mount для збереження; [PRE-6])
[7] SDCardManager::tryMount()                ; SD — optional, failure = DEGRADED
    Сценарії кешу при монтуванні:
    a) SD є, cache `/data/cache/index.json` є
       1. Перевірка CRC32 (index_crc32.bin) — цілісність кешу; якщо не збігається → видалити cache і rebuild
       2. CRC32 OK → читати SD:/database/index.json header (~100 B); якщо "generation"
          відрізняється від sd_generation.bin → кеш застарів (SD оновлена) → rebuild
       3. Обидві перевірки OK → використати кеш
    b) SD є, cache відсутній → SDFingerprintLoader::buildCache() → зберегти `/data/cache/index.json` + `index_crc32.bin` + `sd_generation.bin` ; [BOOT-2] buildCache() ітерує файли SD — додати `esp_task_wdt_reset()` в цикл (аналогія PRE-4: запобігає TWDT reset на великих SD)
    c) SD відсутня, cache є → залишити cache; Matching (Quick) працює з кешем
    d) SD відсутня, cache відсутній → Matching (Quick) та Deep недоступні, LOG_WARN
    e) перший boot (LittleFS_data щойно відформатована) → cache відсутній → сценарій (a) або (d)
    → якщо calibration invalid + SD has backup: prompt restore
[7.5] esp_core_dump_image_check()            ; P-05: ПІСЛЯ mount SD — coredump може зберегтись
    → якщо є coredump:
      ; [PRE-7] filename = coredump_mc<meas_count>.json (не <ts> — немає RTC при boot)
      ; meas_count з NVS — монотонний, унікальний, не залежить від RTC
      якщо SD змонтована → SD:/CoinTrace/coredumps/coredump_mc<meas_count>.json
      якщо SD відсутня  → /data/logs/coredump_mc<meas_count>.json (summary < 4 KB)
      якщо і SD і LFS_data недоступні → SerialTransport only, coredump губиться
      очистити partition, LOG_WARN("Crash detected, coredump saved to [sd|lfs|serial]")
[8] PluginManager::loadConfig()              ; читає /sys/config/device.json
    → якщо файл відсутній: loadDefaultConfig() (hardcoded fallback)
[9] PluginManager::initAll()                 ; ініціалізація всіх plugins
[10] WiFiManager::begin()                    ; connect або AP mode
[11] BLEManager::begin()                     ; BLE init
[12] WebServer::begin()                      ; REST API + Web UI
```

### 17.3 FATAL MODE vs DEGRADED MODE

| Подія | Режим | Дія |
|---|---|---|
| NVS fail | FATAL | Halt. Display: "NVS ERROR. Flash chip failure?" |
| LittleFS_sys fail | FATAL | Halt. Display: "SYS partition missing. Re-flash firmware." |
| LittleFS_data fail | DEGRADED | LOG_ERROR + display warning. Виміри тільки в RAM (не зберігаються). |
| SD fail | DEGRADED | LOG_WARN. Матчинг тільки через кеш. |
| Plugin config missing | DEGRADED | Використовується hardcoded default config. LOG_WARN. |
| Calibration invalid | DEGRADED | Display "Calibration needed". Вимірювання недоступні. |
| Coredump detected | — | Зберегти: SD (SD:/CoinTrace/coredumps/coredump_mc<meas_count>.json, пріоритет) → LittleFS /data/logs/coredump_mc<meas_count>.json → Serial. — `meas_count` монотонний ідентифікатор, не залежить від RTC ([PRE-7]). Очистити partition, LOG_WARN, продовжити boot. |
| GPIO0 held at power-on | RECOVERY | `LittleFS_data.format()` + `esp_restart()`. Антидот: TCA8418 fail + LFS_data corrupt → stuck boot loop. |

> ⚠️ **FAT32 + power fail (R-09):** SD writes виконуються тільки batch-операціями (при ротації
> LittleFS лога або при запису в архів), **ніколи** не per-measurement. Зменшує ризик FAT32
> corruption та знижує SD writes з ~72/добу до ~24 (ротація раз на кілька годин).
