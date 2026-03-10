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
