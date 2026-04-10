# CoinTrace — Lessons Learned

**Формат:** Хронологічний лог технічних інцидентів.  
**Призначення:** База знань для debugging і onboarding. Якщо щось зламалось — шукай тут `Ctrl+F`.  
**Правило:** Додавати запис одразу після вирішення проблеми, поки деталі свіжі.

---

## Шаблон запису

```
### [YYYY-MM-DD] Короткий заголовок проблеми
**Середовище:** платформа / бібліотека / інструмент
**Симптом:** що спостерігалось
**Причина:** чому це сталось
**Рішення:** що саме виправило
**Де в коді:** file:line або конфігурація
```

---

## 2026-04-10 — NAU7802: tare()/calibrate() зависали після WiFi startup

**Середовище:** ESP32-S3 (M5Cardputer-Adv), NAU7802Plugin, I2C SDA=8 SCL=9 400kHz  
**Симптом:** Натискання K → wizard → ENTER (порожня платформа) → `tare() capture failed` через ~6-15 секунд, хоча smoke test при завантаженні проходив успішно. Інший запуск: tare пройшов, потім `I2C write failed: reg=0x02 err=5` + `calibrate() capture failed`.

**Причина:** ESP32 WiFi init (`WiFi.begin()`) два незалежних ефекти:
1. **I2C bus hang (PU_CTRL=0xFF):** WiFi використовує GPIO interrupts і DMA, що може спричинити voltage glitch або bit-bang конфлікт на I2C-шині. `Wire.endTransmission()` повертає `err=5` (timeout), `_readReg()` повертає `0xFF`. `_isReady()` завжди false → `_blockingCaptureSamples()` крутить петлю весь deadline (32×200мс = 6400мс) із 0 зразків → повертає false.
2. **Chip register reset (CS=0):** Короткочасний просід 3.3V під час WiFi radio init скидає регістри NAU7802 (зберігає `_initialized=true` у RAM, але CS-bit у PU_CTRL = 0 → мікросхема не запускає конверсії).

**Рішення:** `_ensureConversionsRunning()` pre-flight перед будь-якою blocking операцією:
```cpp
// 1. Якщо PU_CTRL=0xFF → Wire.end() / Wire.begin(sda,scl) / Wire.setClock(hz) / re-probe
// 2. Якщо CS=0 (chip reset) → _startupSequence() + delay(50)
// Викликається на початку tare() і calibrate(float, uint16_t)
```
SDA/SCL/Hz зберігаються в `_sda`, `_scl`, `_i2cHz` при `initialize()` (default 8/9/400000).

**Де в коді:** `lib/NAU7802Plugin/src/NAU7802Plugin.cpp` — `_ensureConversionsRunning()`, `tare()`, `calibrate(float, uint16_t)`  
**Архітектура:** `docs/architecture/NAU7802_ARCHITECTURE.md` §9.10 B-10

**Правило:** Будь-які blocking I2C операції після WiFi.begin() **мусять** перевіряти стан шини та CS-біт перед початком capture-петлі. ESP32 WiFi = потенційний I2C disruptor.

---

## 2026-04-10 — D-12d: `constexpr` у `private` недоступний для зовнішніх callerів

**Середовище:** C++17 / GCC ESP32, NAU7802Plugin.h  
**Симптом:** Помилка компіляції `'constexpr const float NAU7802Plugin::MASS_REF_G' is private within this context` при спробі використати `NAU7802Plugin::MASS_REF_G` в `src/main.cpp` та в `saveDiscoveryDump()`.  
**Причина:** `MASS_REF_G = 33.3f` була у `private:` блоці NAU7802Plugin.h. У C++ `private static constexpr` повністю недоступні зовні класу навіть для читання.  
**Рішення:** Переміщено `MASS_REF_G` до нового `public:` блоку:
```cpp
// NAU7802Plugin.h — public section
static constexpr float MASS_REF_G = 33.3f;   // XUSSR10, heaviest class (ADR-NAU-005)
```
Видалено з `private:` блоку. Всі посилання з main.cpp (`sMassG / NAU7802Plugin::MASS_REF_G`) компілюються нормально.  
**Де в коді:** `lib/NAU7802Plugin/src/NAU7802Plugin.h` — public constants block  
**Правило:** `static constexpr` що виступають частиною публічного API (документовані в архіт. специф.) **мусять** бути в `public:` секції. "Constant as implementation detail" → private. "Constant as interface spec" → public.

---

## 2026-03-11 — ESP32-S3 Boot Loop: три незалежні причини

**Середовище:** ESP32-S3FN8 (M5Stack Cardputer-Adv), PlatformIO, espressif32 6.13.0  
**Симптом:** Пристрій циклічно перезавантажується (boot loop) відразу після прошивки. Serial Monitor показує кілька рядків bootloader, потім reset.

### Причина 1: `xSemaphoreCreateMutex()` в конструкторі статичного глобального об'єкта

**Деталі:** Статичні C++ глобали (`static Logger gLogger;` в `main.cpp`) ініціалізуються **до** виклику `app_main()` / `setup()`, тобто **до** старту FreeRTOS scheduler. `xSemaphoreCreateMutex()` всередині конструктора викликає FreeRTOS API на не ініціалізованій системі → паніка.

**Рішення:** Порожній конструктор. Mutex створюється тільки в `Logger::begin()`, який викликається першим рядком `setup()`.
```cpp
// ❌ НЕПРАВИЛЬНО — constructor викликається до FreeRTOS
Logger::Logger() { mutex_ = xSemaphoreCreateMutex(); }

// ✅ ПРАВИЛЬНО
Logger::Logger() {}  // порожній
bool Logger::begin() { mutex_ = xSemaphoreCreateMutex(); return mutex_ != nullptr; }
```
**Де в коді:** `lib/Logger/src/Logger.cpp` — `Logger::begin()`  
**Правило:** Будь-який FreeRTOS API (`xSemaphoreCreate*`, `xQueueCreate`, `xTaskCreate`) — ЗАБОРОНЕНИЙ в конструкторах глобальних/статичних об'єктів.

---

### Причина 2: Неправильний flash_mode і розмір партиційної таблиці

**Деталі:** `flash_mode = dio` і `board_build.partitions = default_16MB.csv` на чіпі ESP32-S3FN8 (8MB flash, потребує QIO). Bootloader з режимом DIO на QIO-flash читає сміття. Партиційна таблиця 16MB описує регіони за межами 8MB чіпа → bootloader читає за межами flash → паніка/reset.

**Рішення:** Кастомний board JSON `boards/m5cardputer-adv.json`:
```json
"flash_mode": "qio",
"maximum_size": 8388608,
"default_partitions": "default_8MB.csv"
```
**Де в коді:** `boards/m5cardputer-adv.json`  
**Правило:** Завжди перевіряй маркування чіпа. `ESP32-S3FN8` = 8MB flash, QIO. Не довіряй generic `esp32-s3-devkitc-1` — він override'ить flash налаштування.

---

### Причина 3: `-mfix-esp32-psram-cache-issue` не компілюється на ESP32-S3

**Деталі:** Цей прапор компілятора існує тільки для ESP32 (Xtensa LX6). ESP32-S3 використовує LX7 — прапор не підтримується, компілятор видає error → build падає або прошивається некоректний образ.

**Рішення:** Видалити прапор. На ESP32-S3 він не потрібен — PSRAM cache у S3 працює правильно без workaround.  
**Де в коді:** `platformio.ini` — видалено з `build_flags`  
**Примітка:** `BOARD_HAS_PSRAM` теж видалено — ESP32-S3FN8 взагалі не має PSRAM (підтверджено в офіційній таблиці M5Stack Cardputer comparison).

---

## 2026-03-11 — ESP32-S3: `Serial` має тип `HWCDC`, не `HardwareSerial`

**Середовище:** ESP32-S3 з `ARDUINO_USB_CDC_ON_BOOT=1`, Arduino framework  
**Симптом:** Помилка компіляції при передачі `Serial` в параметр типу `HardwareSerial&`:
```
error: cannot bind non-const lvalue reference of type 'HardwareSerial&' to an 
rvalue of type 'HWCDC'
```

**Причина:** На ESP32-S3 з увімкненим USB CDC (`ARDUINO_USB_CDC_ON_BOOT=1`) об'єкт `Serial` має тип `HWCDC` (клас-обгортка навколо USB CDC). `HardwareSerial` — окремий клас для UART. Обидва успадковують `Print`, але **не пов'язані ієрархією**.

**Рішення:** Змінити тип параметра з `HardwareSerial&` на `Print&`:
```cpp
// ❌ Не компілюється на ESP32-S3 з USB CDC
SerialTransport(HardwareSerial& serial = Serial, ...);

// ✅ Працює з HWCDC, HardwareSerial, і будь-яким Print-сумісним об'єктом
SerialTransport(Print& serial = Serial, ...);
```
**Де в коді:** `lib/Logger/src/SerialTransport.h`, `SerialTransport.cpp`  
**Де в архітектурі:** `docs/architecture/LOGGER_ARCHITECTURE.md` §6.1 — виправлено сигнатуру

---

## 2026-03-11 — PlatformIO native tests: `mock_impl.cpp` не підхоплюється з підпапки

**Середовище:** PlatformIO `platform = native`, Unity test framework  
**Симптом:** Linker error при `pio test -e native-test`:
```
undefined reference to `g_mock_millis'
```
Хоча `mock_impl.cpp` (який визначає `g_mock_millis`) існує в `test/mocks/mock_impl.cpp`.

**Причина:** PlatformIO компілює shared test файли тільки з кореня `test/`. Підпапки `test/mocks/`, `test/helpers/` та ін. — ігноруються для compilation, якщо вони не є test suite директоріями (тобто не мають `test_` префіксу).

**Рішення:** Перемістити `mock_impl.cpp` безпосередньо в `test/`:
```
test/mock_impl.cpp          ← компілюється для всіх сюїтів
test/mocks/Arduino.h        ← заголовки — нормально в підпапці
test/mocks/freertos/...     ← заголовки — OK
```
**Де в коді:** `test/mock_impl.cpp`  
**Правило PlatformIO:** `test/*.cpp` → shared між всіма сюїтами. `test/mocks/*.cpp` → НЕ компілюється автоматично. Тільки headers можна тримати в підпапках.

---

## 2026-03-11 — FreeRTOS headers: порядок включення

**Середовище:** PlatformIO, ESP32-S3, FreeRTOS  
**Симптом:** Помилка компіляції:
```
error: 'SemaphoreHandle_t' was not declared in this scope
```
Або:
```
freertos/semphr.h: No such file or directory
```

**Причина:** `freertos/semphr.h` залежить від типів визначених у `freertos/FreeRTOS.h`. Якщо `semphr.h` включається першим або без `FreeRTOS.h` — типи не визначені.

**Рішення:** Завжди включати в правильному порядку:
```cpp
#include <freertos/FreeRTOS.h>  // ← ПЕРШИМ — визначає базові типи
#include <freertos/semphr.h>    // ← тільки після FreeRTOS.h
```
**Де в коді:** `lib/Logger/src/Logger.h`, `lib/Logger/src/RingBufferTransport.h`

---

## 2026-03-11 — Scoop/MinGW потрібен для `platform = native` на Windows

**Середовище:** Windows, PlatformIO `platform = native`  
**Симптом:** `pio test -e native-test` падає з:
```
'gcc' is not recognized as an internal or external command
'g++' is not recognized as an internal or external command
```

**Причина:** PlatformIO `platform = native` використовує системний GCC/G++. На Windows GCC не входить до складу системи — потрібна окрема установка.

**Рішення:** Встановити MinGW-w64 через Scoop:
```powershell
scoop install mingw   # ~150 MB, GCC 15.2
```
Після цього `gcc` і `g++` з'являються в PATH автоматично.  
**Примітка:** Scoop вже був на системі (`C:\Users\Yura\scoop`). Якщо немає — спочатку [встановити Scoop](https://scoop.sh).

---

## 2026-03-11 — LoadProhibited при натисканні кнопки: `KeysState` copy crash

**Середовище:** ESP32-S3, M5Cardputer, M5Cardputer бібліотека, `loop()`  
**Симптом:** `Guru Meditation Error: Core 1 panic'ed (LoadProhibited)`. `EXCVADDR=0x00000000`, `A2=0x00000000`. Трапляється лише при натисканні кнопки. Після перезавантаження — нормальна робота.

**Діагностика:** Декодовано через `xtensa-esp32s3-elf-addr2line -e firmware.elf -f -C -i 0x42002641 0x4200b11d`:
```
loop() [main.cpp:107]
  → KeysState::KeysState(const&)   ← copy-ctor
    → std::vector<uint8_t>::vector(const&)
      → std::uninitialized_copy → std::copy
        CRASH: read from 0x00000000
```

**Причина (два баги):**  
1. `Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState()` — **копіює** struct по значенню. `keysState()` повертає `KeysState&` (reference). `KeysState` містить три `std::vector` (`word`, `hid_keys`, `modifier_keys`). При копіюванні копіюється вектор зі станом `size > 0`, `data() == nullptr` — race condition з keyboard scanner або стан при натисканні лише фізичної кнопки (без ASCII символу).  
2. `status.word[0]` без перевірки `empty()` → UB + nullptr deref коли `word` порожній (натиснуто лише Fn/modifier/physical button).

**Рішення:**
```cpp
// БУЛО (crash):
Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
LOG_DEBUG(&gLogger, ..., status.word[0], status.word[0]);

// СТАЛО (fix):
const Keyboard_Class::KeysState& status = M5Cardputer.Keyboard.keysState();
if (!status.word.empty()) {
    const char key = status.word[0];
    LOG_DEBUG(&gLogger, ..., key, (uint8_t)key);
}
```

**Де в коді:** `src/main.cpp:107` → `loop()`  
**Правило:** `keysState()` повертає reference — ніколи не копіювати. Завжди перевіряти `word.empty()` перед `word[0]`.

---

## 2026-03-15 — PlatformIO: custom partition table boot loop через hardcoded esptool addresses

**Середовище:** ESP32-S3FN8 (8MB Flash), M5Stack Cardputer, PlatformIO espressif32 6.13.0, Arduino framework  
**Симптом:** Після заливки прошивки з кастомною partition table пристрій не завантажується — boot loop. Serial показує `rst:0x...` і відразу reset без жодного лога з firmware.

**Причина:** PlatformIO при `pio run -t upload` (esptool backend) **hardcode-ує адреси** незалежно від вмісту partition CSV:

| Бінарник         | Hardcoded адреса |
|------------------|------------------|
| `bootloader.bin` | `0x0000`         |
| `partitions.bin` | `0x8000`         |
| `boot_app0.bin`  | `0xe000`         |
| `firmware.bin`   | `0x10000`        |

Оригінальна схема мала `otadata @ 0x18000` і `app0 @ 0x20000`. esptool залив `firmware.bin` на `0x10000` (середина NVS/otadata зони), а bootloader шукав app0 на `0x20000` — там порожньо → boot loop.

```
0x9000  -> nvs        (correct)
0x10000 -> FIRMWARE   <- esptool always writes here!
0x18000 -> otadata    (overwritten by firmware)
0x20000 -> app0       <- bootloader looks here, nothing found -> reset
```

**Рішення:** Привести partition table до стандартних адрес, які esptool очікує:

```csv
nvs,     data, nvs,   0x9000,  0x5000   # 20 KB (fits between 0x9000 and 0xe000)
otadata, data, ota,   0xe000,  0x2000   # standard: esptool writes boot_app0.bin here
app0,    app,  ota_0, 0x10000, 0x280000 # standard: esptool writes firmware.bin here
```

`nvs` зменшено з 60 KB до 20 KB — необхідно, щоб помістилось між `0x9000` і `0xe000`. Для NVSManager з ~25 реальними entries це більш ніж достатньо (запас 14x).

**Альтернатива (якщо треба `app0 != 0x10000`):** передати кастомні адреси через `upload_flags` у `platformio.ini`. Але це ламає OTA workflow.

**Де в коді:** `partitions/cointrace_8MB.csv`, `platformio.ini`, `boards/m5cardputer-adv.json`  
**Правило:** У PlatformIO Arduino-ESP32 завжди тримай `otadata @ 0xe000` і `app0 @ 0x10000`.

---

## 2026-03-15 — PlatformIO: Unicode в partition CSV падає на Windows (cp1252)

**Середовище:** Windows 10/11, PlatformIO, Python cp1252 locale  
**Симптом:** Build успішний, але `checkprogsize` падає з `UnicodeDecodeError: 'charmap' codec can't decode byte 0x8f`.

**Причина:** PlatformIO читає partition `.csv` через стандартний Python file open без explicit encoding — на Windows це `cp1252`. Символи `⚠️`, `✅`, `—` (em-dash), `§` тощо не входять в cp1252.

**Рішення:** Тільки ASCII в `.csv` файлах партицій. Замінити:
- `⚠️` → `NOTE:` або `IMPORTANT:`
- `✅` → `(OK)` або `(verified)`
- `—` (em-dash) → `-`
- `§` -> `section`

**Де в коді:** `partitions/cointrace_8MB.csv`  
**Правило:** partition `.csv` = strict ASCII. Коментарі можна, але тільки 7-bit символи.

---

## 2026-03-15 — PlatformIO: `board_build.littlefs_partition_label` ігнорується при uploadfs

**Середовище:** PlatformIO espressif32 6.13.0, ESP32-S3, два LittleFS partition в CSV (`littlefs_sys`, `littlefs_data`)  
**Симптом:** `pio run -e uploadfs-sys -t uploadfs` пише на адресу `littlefs_data` (0x610000) замість очікуваної `littlefs_sys` (0x510000). Розмір образу = 1.75 MB (data partition) замість 1 MB (sys partition).

```
# Очікувалось:
Flash will be erased from 0x00510000 to 0x0060ffff...  # littlefs_sys

# Отримали:
Flash will be erased from 0x00610000 to 0x007cffff...  # littlefs_data !!!
```

**Причина:** PlatformIO espressif32 6.13.0 завжди ставить `FS_START`/`FS_SIZE` з ОСТАННЬОЇ partition з subtype=`spiffs` у CSV. З двома LittleFS partition (`littlefs_sys` ПЕРША, `littlefs_data` ОСТАННЯ) — завжди вибирає data. `board_build.littlefs_partition_label` впливає лише на IDE metadata, але **не** на uploadfs pipeline.

**Точні змінні (знайдено через debug print в post: скрипті):**
```python
FS_START = 6356992      # = 0x610000 (data partition offset) -- WRONG
FS_SIZE  = 1835008      # = 0x1C0000 = 1.75 MB (data partition size) -- WRONG
# FS_START є останнім елементом UPLOADERFLAGS -- саме він передається esptool
```

**Рішення:** `post:` extra_scripts hook, який патчить ці дві змінні після того як платформа їх встановлює:
```python
# scripts/upload_littlefs_sys.py (post:, НЕ pre:)
# 1. Парсить partition CSV -> знаходить partition за label -> offset + size
# 2. env.Replace(FS_START=offset, FS_SIZE=size)
# Вся решта uploadfs pipeline (mklittlefs + esptool) працює без змін.
```

**Чому `post:`, а не `pre:`:** `pre:` запускається ДО того як `platform/main.py` встановлює `FS_START`/`FS_SIZE`. Наш `Replace` одразу перезаписується платформою. `post:` запускається ПІСЛЯ -> наш override стабільний.

**Чому `AddCustomTarget("uploadfs")` не підходить:** `platformio/builder/tools/piotarget.py` має `assert name not in env["__PIO_TARGETS"]` -- uploadfs вже зареєстровано платформою -> `AssertionError`.

**Результат після fix:**
```
[uploadfs] FS_START  : 0x00610000 -> 0x00510000
[uploadfs] FS_SIZE   : 0x1C0000 -> 0x100000 (1024 KB)
Flash will be erased from 0x00510000 to 0x0060ffff...
```

**Де в коді:** `scripts/upload_littlefs_sys.py`, `platformio.ini` секція `[env:uploadfs-sys]`  
**Документація:** `docs/guides/UPLOADFS_GUIDE.md`  
**Правило:** `board_build.littlefs_partition_label` НЕ працює для uploadfs target. Для multi-partition LittleFS завжди використовуй `post:` скрипт з `env.Replace(FS_START=..., FS_SIZE=...)`.

---
## 2026-03-17 — ESP32-S3 USB-CDC: `pio device monitor` пропускає бут-лог

**Середовище:** ESP32-S3FN8 (M5Stack Cardputer-Adv), Native USB-CDC (`ARDUINO_USB_CDC_ON_BOOT=1`), Windows  
**Симптом:** `pio device monitor` завершується з exit code 1 одразу після підключення. Бут-лог недоступний — весь вивід від `setup()` пропущений.

**Причина:** На ESP32-S3 Native USB-CDC будь-яке відкриття COM-порту перемикає DTR → USB-CDC stack у ESP32-S3 закривається → COM-порт зникає до того як прошивка встигає вивести перший рядок. Це фундаментальна поведінка USB-CDC, не баг PlatformIO.

**Рішення:** Підключити FT232RL (або CH340G/CP2102) до заднього **EXT 2.54-14P** роз'єму:
- `Pin 14 = G15 = UART_TX` → `RXD` адаптера
- `Pin 12 = G13 = UART_RX` → `TXD` адаптера
- `Pin 4 = GND` → `GND` адаптера
- Джампер адаптера: обов'язково **3.3V** (не 5V)

Зміни в прошивці:
```cpp
// main.cpp — перед gLogger.begin()
static HardwareSerial  gUart1(1);
static SerialTransport gSerialTransport(gUart1, SerialTransport::Format::TEXT, 115200);
// ...
gUart1.begin(115200, SERIAL_8N1, /*rx=*/13, /*tx=*/15);
```

Зміна в `platformio.ini`:
```ini
monitor_port = COM4   ; FT232RL (не USB-CDC COM3)
```

**Документація:** `docs/guides/UART_DEBUG_SETUP.md` — повний wiring + налаштування  
**Де в коді:** `src/main.cpp` рядки ~30-33, `platformio.ini` `monitor_port`  
**Правило:** На ESP32-S3 з Native USB-CDC ніколи не використовуй `Serial` для debug UART якщо потрібен повний бут-лог. Використовуй окремий UART peripheral через зовнішній адаптер.

---

## 2026-04-01 — LDC1101: TC1/TC2 MikroE SDK defaults — ×52 та ×11 помилки

**Середовище:** LDC1101 Click Board MIKROE-3240 (CSENSOR=330 pF), LSENSOR≈116.7 μH, fSENSOR=811.5 kHz  
**Симптом:** Дві срібні монети (Kangaroo Ag999, Olympic Ag900) повертали `rp_raw = 39321` (0x9999) з `σ = 0.0` через всі 5 вимірів. Також 14/20 монет насичувались на кроках addon_1.6mm/2.6mm з hex-паліндромами: `0xB6DB`=46811, `0xCCCC`=52428.  
**Причина:** MikroE Arduino SDK та готові приклади для MIKROE-3240 містять `TC1=0x1F` (τ=15.8 ns) та `TC2=0x3F` (τ=91.5 ns) — значення *не розраховані* за формулами TI Datasheet §9.1.5/9.1.6, а просто MikroE legacy defaults. Для CSENSOR=330 pF, fSENSOR=811 kHz, RPMIN=1.5 kΩ правильні значення:
```
τ₁_target = 0.75026 / 811500 = 925 ns  →  TC1=0xD5 (τ=893 ns, −3.5%)
τ₂_target = 1.0 / (1500 × 330e-12) = 2020 ns  →  TC2=0xFE (τ=1039 ns)
```
Занадто малі TC1/TC2 → петля AGC нестабільна → DAC застрягає ("SAT-lock") на hex-паліндромних точках рівноваги.  
**Рішення:**
1. Розрахувати TC1/TC2 по TI Datasheet §9.1.5/9.1.6 для конкретного CSENSOR і fSENSOR своєї схеми
2. Змінити `data/plugins/ldc1101.json`: `tc1_val`, `tc2_val`, `rp_set`
3. Flash двома кроками: `pio run -e cointrace-dev -t upload` + `pio run -e uploadfs-sys -t uploadfs`

**SAT-lock діагностика** (характерні ознаки в session NDJSON):
- `rp_raw = 39321` (0x9999) — hex-паліндром, `σ = 0.0` фізично неможливий
- `rp_raw = 46811` (0xB6DB), `52428` (0xCCCC) — інші точки SAT-lock
- Однакове значення через всі N=5 вимірів → регулятор застряг

**Побічний ефект:** fSENSOR з неправильними TC1/TC2 показує артефактне значення. У даному проекті 909 kHz → 811.5 kHz після виправлення. Будь-які розрахунки LSENSOR на основі fSENSOR до виправлення є неправильними (92.9 μH vs реальних 116.7 μH).  
**Де в коді:** `lib/LDC1101Plugin/src/LDC1101Plugin.h` — `tc1Val_`, `tc2Val_`, `configure_()`; `data/plugins/ldc1101.json`  
**Документація:** `docs/architecture/LDC1101_ARCHITECTURE.md` — ADR-LDC-002; `docs/external/2026-04-01.D4_SENSOR_CALIBRATION_PLAN.md`  
**Правило:** При будь-якій зміні котушки/конденсатора — *обов'язково* перерахувати TC1/TC2 по TI §9.1.5/9.1.6. Ніколи не довіряти SDK defaults без перевірки по схемі та формулах. SAT-lock ознака: σ=0 + hex-паліндром у rp_raw.

---

## 2026-04-01 — LDC1101: min_freq_nibble потрібно коригувати при зміні fSENSOR

**Середовище:** LDC1101Plugin, `data/plugins/ldc1101.json`, D-4 після виправлення TC1/TC2  
**Симптом:** Після D-4 flash перший boot пройшов нормально, але fSENSOR змістився з 909 kHz → 811.5 kHz. Nibble=6 залишав лише 9.8 kHz запас до LOW_LIMIT (800 kHz) — при будь-якій важкій монеті або тепловому зсуві міг тригернутись NO_OSC fault.  
**Причина:** min_freq_nibble встановлюється виходячи з fSENSOR baseline. При зміні TC1/TC2 або схеми fSENSOR може суттєво змінитись, і старий nibble стає небезпечним.  
**Рішення:** Nibble=4 → threshold=667 kHz → margin=143 kHz (17.6%) — безпечний запас для даного fSENSOR=811.5 kHz.  

| nibble | threshold | margin при 811.5 kHz |
|--------|-----------|---------------------|
| 6 | 800 kHz | 9.8 kHz ← небезпечно |
| 5 | 727 kHz | 84 kHz ← мінімально |
| **4** | **667 kHz** | **143 kHz ✅** |

**Де в коді:** `lib/LDC1101Plugin/src/LDC1101Plugin.h` `minFreqNibble_`; `data/plugins/ldc1101.json` `min_freq_nibble`  
**Правило:** Після будь-якої зміни TC1/TC2 — повірити fSENSOR baseline по boot log і перерахувати min_freq_nibble. Цільовий margin: ≥ 15% від fSENSOR.

---

## 2026-04-10 — NAU7802: BYPASS_EN=1 — тихий зрив підсилення (silent gain failure)

**Середовище:** NAU7802 24-bit ADC, `lib/NAU7802Plugin`, D-12a startup sequence (v1.4.0 і старіше)  
**Симптом:** Чутливість 151 counts/g замість очікуваних ~21,474 counts/g при GAINS=128x. `raw ADC ≈ -21,000` (від'ємний, повільно дрейфує). sigma=14,258 counts при порожній платформі = ~94g RMS. Самодіагностика: самотест NOISY/DRIFTING навіть після механічного усунення вібрацій.

**Причина (X-01 CRITICAL):** Стара startup sequence читала регістр `0x15` (помилково названий `REG_OTP_B1`), потім записувала `(value & 0x38) | 0x30` у `REG_PGA` (0x1B). Оскільки `REG_ADC_CTRL(0x15)` = `0x00` після reset → результат запису: `0x30`. Але `0x30 = OUT_EN(bit5) | BYPASS_EN(bit4)` → PGA обходиться. Ефективний gain = 1x замість 128x — **без жодного error**, чіп працює «нормально».

**Діагноз:** Порівняння моделей чутливості:
```
Виміряна:         151 counts/g  (відповідає gain=1x: 168 counts/g ± load cell tolerance)
Очікувана 128x:   21,474 counts/g  (у 142 рази більше)
```

**Рішення:**
```cpp
// ❌ НЕПРАВИЛЬНО (v1.4.0): читає ADC_CTRL, а не OTP; пише BYPASS_EN=1
uint8_t otp_b1 = _readReg(REG_ADC_CTRL);       // 0x15 = ADC Control, NOT OTP !
_writeReg(REG_PGA, (otp_b1 & 0x38) | 0x30);    // 0x30 = OUT_EN | BYPASS_EN ← БАГИ!

// ✅ ПРАВИЛЬНО (v1.5.0): тільки LDOMODE bit6 очищується
uint8_t pga = _readReg(REG_PGA);
_writeReg(REG_PGA, pga & ~PGA_LDOMODE_BIT);     // PGA_LDOMODE_BIT = 0x40
```

**Апаратне підтвердження після виправлення:**
```
Startup OK: PU_CTRL=0x9E REG_PGA=0x00  ← BYPASS_EN=0 ✅
raw ADC: ~+113,000  (vs ~-21,000 раніше)  ← 128x підтверджено
sigma=96 counts = 4.5 mg ✅
```

**Де в коді:** `lib/NAU7802Plugin/src/NAU7802Plugin.cpp` — `_startupSequence()` step 6; `NAU7802Plugin.h` — `PGA_LDOMODE_BIT = 0x40`  
**Де задокументовано:** `docs/architecture/NAU7802_ARCHITECTURE.md §9.9 B-09`, `ADR-NAU-001`  
**Правило:** Перед будь-яким `_writeReg(REG_PGA, ...)` — звірятись з datasheet. `REG_PGA(0x1B)`: bit4=BYPASS_EN ПОВИНЕН залишатись 0. Reference libs (Adafruit, SparkFun) тільки `&= ~0x40`.

---

## 2026-04-10 — NAU7802: три помилки в initialization (audit X-01/X-02/X-03)

**Середовище:** NAU7802 startup sequence, `lib/NAU7802Plugin` v1.4.0  
**Симптом:** Описано окремо для кожного:

### X-02 [CRITICAL]: CLK_CHP не вимкнений

**Причина:** REG 0x15 неправильно ідентифікований як `REG_OTP_B1`. Насправді — `REG_ADC_CTRL`. Bits[5:4]=CLK_CHP після reset = 00 (максимальний chopper noise). Reference libs (Adafruit, SparkFun) записують `reg[0x15] |= 0x30` як один з перших кроків init.

**Рішення:** Додати крок до startup: `_writeReg(REG_ADC_CTRL, adc_ctrl | ADC_CHP_DIS)` де `ADC_CHP_DIS = 0x30`.

### X-03 [HIGH]: PGA_CAP_EN не активований

**Причина:** `PGA_CAP_EN = bit7 = 0x80`. Старе `PGA_PWR_VAL = 0x30` встановлювало `MSTR_BIAS_CURR = 0b011` (bits[6:4]). Внутрішній 330pF конденсатор PGA (§9.14 datasheet) не підключався.

**Рішення:** `PGA_PWR_VAL = 0x80` (тільки bit7=PGA_CAP_EN).

**Де в коді:** `lib/NAU7802Plugin/src/NAU7802Plugin.h` — `REG_ADC_CTRL`, `ADC_CHP_DIS`, `PGA_PWR_VAL`  
**Правило:** При ініціалізації нового I²C/SPI сенсора — завжди порівнювати свій init sequence з двома незалежними open-source реалізаціями (Adafruit + SparkFun). Якщо кроки відрізняються — перечитати datasheet того регістра.

---

## 2026-04-10 — Load cell: апаратне налагодження (broken wire + EMI + vibration)

**Середовище:** 1kg load cell (wheatstone bridge), 4-wire (E+/E-/A+/A-), NAU7802 Adafruit breakout #4538, M5Stack Cardputer на столі  
**Симптом (сесія C-13):** Серія несподіваних результатів:

| Симптом | Причина | Рішення |
|---|---|---|
| `err=2 NACK` при initialize() | Обрив I²C дроту | Перепаяти |
| sigma=1,460,000, range=23,839 DRIFTING | Обрив аналогового дроту A+ | Перепаяти |
| sigma=6,997 (после ремонту) | Механічна вібрація від вентилятора ПК через стіл | Підняти пристрій на пінополістирол |
| sigma=2,000 | LDC1101 котушка + металевий корпус load cell поряд (< 5 см) | Розмістити load cell ≥ 10 см від котушки |
| sigma=17 (momentary) | Механічне повзання після монтажу | Дати 30-60 хв settling |
| sigma=96 (стабільно) | Незахищений 4-wire кабель поряд з блоком живлення ПК | Відсунути кабель |

**Підсумок:** Від `sigma=1.46M → 96 counts` (4.5 mg) виключно через апаратне усунення проблем, без змін коду.

**Де задокументовано:** `docs/architecture/NAU7802_ARCHITECTURE.md §3.4 EMI shielding`, `§9.9 B-09`  
**Правило:** Якщо sigma >> 100 counts при gain=128x — спочатку перевірити (1) цілісність дротів (омметром), (2) механічну ізоляцію від поверхні, (3) відстань від LDC1101 котушки та corе PSU.

---

## 2026-04-10 — NAU7802: 80 SPS значно краще ніж 10 SPS (при вібрації навколишнього середовища)

**Середовище:** NAU7802, ESP32-S3, ambient mechanical noise (вентилятор ПК ~50 Hz, будинкова вібрація ~1-3 Hz, open desk)  
**Симптом початковий:** При 10 SPS для tare(): 32 семпли за ~3.2 секунди → sigma катастрофічна (тисячі counts), вимір нестабільний навіть після ремонту проводів.  
**При 80 SPS:** 32 семпли за ~0.4 секунди → sigma=96 counts (4.5 mg) ✅

**Причина:** При 10 SPS вікно 3.2 с захоплює *повні цикли* механічного шуму (1-3 Hz). При 80 SPS вікно 0.4 с потрапляє *всередину* одного циклу шуму — averaging works. `Δt = N/SPS`: при N=32 → 10 SPS: 3.2 с >> 1/3 Hz (333 ms) → шум не усереднюється. 80 SPS: 0.4 с < 1/3 Hz → мінімум варіації.

**Де задокументовано:** `docs/architecture/NAU7802_ARCHITECTURE.md §2.3`, ADR-NAU-003  
**Правило:** У середовищі з низькочастотною вібрацією (1-10 Hz) — використовуй максимальний доступний SPS (80 або 320), не мінімальний. Averaging window < 0.2 × T_noise_period — для NAU7802 на відкритому столі: SPS ≥ 80.

---