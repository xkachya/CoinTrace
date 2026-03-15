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
