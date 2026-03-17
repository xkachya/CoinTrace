# UART Debug Setup — FT232RL via EXT 2.54-14P Connector

**Applies to:** CoinTrace firmware, M5Stack Cardputer-Adv (ESP32-S3FN8), Windows  
**Problem solved:** USB-CDC (COM3) resets on DTR toggle → monitor misses full boot log  
**Cross-ref:** `docs/guides/DEBUGGING.md` §1, `src/main.cpp` §17.2

---

## Навіщо це потрібно

M5Stack Cardputer-Adv використовує **Native USB-CDC** (ESP32-S3 GPIO19/20) як Serial.
Будь-яке відкриття COM-порту монітором перемикає DTR → USB-CDC stack закривається →
порт зникає до того як прошивка встигає вивести бут-лог.

```
pio device monitor     ← відкриває COM3
  → DTR toggle         ← ESP32-S3 USB stack закривається
    → COM3 зникає      ← monitor падає з exit code 1
      → бут-лог втрачений
```

**Рішення:** Підключити FT232RL до виділених UART пінів на задньому **EXT 2.54-14P** роз'ємі.
Windows бачить його як окремий COM4, який **ніколи не ресетить** пристрій при відкритті.

---

## Необхідне обладнання

| Компонент | Примітка |
|---|---|
| FT232RL USB-to-TTL адаптер | Наприклад: "FT232RL USB 2.0 to TTL 5V 3.3V" |
| 3× female-female dupont дроти | Для підключення до EXT 2.54-14P роз'єму |

> **Аналоги:** CH340G або CP2102 підійдуть так само. FT232RL — найнадійніший по драйверам на Windows 10/11.

---

## Частина 1 — Налаштування адаптера

### 1.1 Встановити джампер на 3.3V

FT232RL модуль має джампер вибору напруги. ESP32-S3 **не толерує 5V** на GPIO.

```
[VCC SEL]
  ○─────○  ← 5V (НЕ для нас)
  ●─────●  ← 3.3V (встановити сюди)
```

> ⚠️ Якщо встановити 5V — є ризик пошкодити ESP32-S3.

### 1.2 Встановити драйвер (якщо потрібно)

На Windows 10/11 драйвер FT232RL зазвичай встановлюється автоматично.
Якщо ні — завантажити з: https://ftdichip.com/drivers/vcp-drivers/

Після підключення FT232RL → в Device Manager з'явиться `USB Serial Port (COMx)`.
Запам'ятати номер (наприклад COM4).

---

## Частина 2 — Підключення до Cardputer

### 2.1 EXT 2.54-14P — задній роз'єм Cardputer-Adv

Задній 14-пінний роз'єм має **виділені UART піни** (підтверджено по офіційному пінауту):

```
EXT 2.54-14P (задній роз'єм, вид зверху)

  FUNC      PIN   LEFT  RIGHT  PIN    FUNC
  ──────    ───   ────  ─────  ───    ──────
  RESET     G3     1      2    5VIN   (червоний — НЕ чіпати)
  INT       G4     3      4    GND    ← GND (чорний дріт)
  BUSY      G6     5      6    5VOUT  (НЕ чіпати)
  SCK       G40    7      8    G8     I2C_SDA
  MOSI      G14    9     10    G9     I2C_SCL
  MISO      G39   11     12    G13    UART_RX  ← підключити TXD FT232RL
  CS        G5    13     14    G15    UART_TX  ← підключити RXD FT232RL
```

**Наші 3 піни:**
- **Pin 4** = GND
- **Pin 12** = G13 = `UART_RX` (дані з ПК → в Cardputer)
- **Pin 14** = G15 = `UART_TX` (логи з Cardputer → на ПК)

> **Підтвердження коректності:** SPI піни (G40/G14/G39/G5) і I2C (G8/G9) з цього ж
> роз'єму точно збігаються з `main.cpp` — `SPI.begin(40,39,14)` і `Wire.begin(8,9)`. ✅

### 2.2 Схема підключення

```
Cardputer EXT Pin        FT232RL адаптер
─────────────────        ───────────────
  Pin 14  G15 (TX) ─────────→  RXD
  Pin 12  G13 (RX) ←─────────  TXD
  Pin 4   GND      ────────────  GND
  Pin 2   5VIN     ✗  НЕ підключати
  Pin 6   5VOUT    ✗  НЕ підключати
```

> ⚠️ Живлення FT232RL — від USB хоста (ПК). Жодні VCC піни Cardputer **не підключати** до FT232RL.

### 2.3 Фізичне підключення

1. Переконатись що джампер FT232RL на **3.3V**
2. Підключити FT232RL до USB порту ПК
3. Підключити дроти до EXT роз'єму: Pin 4 (GND), Pin 12 (G13→TXD), Pin 14 (G15→RXD)
4. USB-C Cardputer залишається підключеним до ПК (для `pio upload`)

В `Device Manager` → `Ports (COM & LPT)` має бути:
- `USB Serial Device (COM3)` — ESP32-S3 USB-CDC (для `pio upload`)
- `USB Serial Port (COM4)` — FT232RL UART (для `pio device monitor`)

---

## Частина 3 — Зміни в прошивці

Потрібно перенаправити `SerialTransport` з USB-CDC (`Serial`) на UART1 (GPIO13/GPIO15).

### 3.1 Зміна `src/main.cpp`

**Крок A** — Замінити `gSerialTransport` і додати `gUart1` перед ним:

```cpp
// Зараз (рядок ~30):
static SerialTransport     gSerialTransport(Serial, SerialTransport::Format::TEXT, 115200);

// Після змін:
static HardwareSerial      gUart1(1);  // UART1 peripheral — EXT TX=GPIO15, RX=GPIO13
static SerialTransport     gSerialTransport(gUart1, SerialTransport::Format::TEXT, 115200);
```

**Крок B** — Ініціалізувати UART1 в `setup()` **перед** `gLogger.begin()` (перший рядок setup):

```cpp
void setup() {
  // ── 0. EXT UART init (debug serial via FT232RL on EXT 2.54-14P) ─────────
  // EXT Pin 14 = G15 = TX, EXT Pin 12 = G13 = RX
  gUart1.begin(115200, SERIAL_8N1, /*rx=*/13, /*tx=*/15);

  // ── 1. Logger init ────────────────────────────────────────────
  gLogger.begin();
  // ...
```

### 3.2 Чому `SerialTransport` приймає `Print&`

`SerialTransport` оголошений з параметром `Print&`, а не `HardwareSerial&` —
це дозволяє передавати і `HWCDC` (теперішній `Serial`), і `HardwareSerial` (`gUart1`).
Додаткових змін в `SerialTransport.h` / `.cpp` **не потрібно**.

---

## Частина 4 — Налаштування PlatformIO

Додати `monitor_port` для EXT UART в `platformio.ini`:

```ini
[env:cointrace-dev]
; ... існуючі налаштування ...
monitor_port = COM4          ; FT232RL on EXT 2.54-14P G13/G15 (не USB-CDC COM3)
monitor_speed = 115200
```

> COM4 підтверджено в Device Manager як "USB Serial Port (COM4)".

Після цього `pio device monitor` автоматично відкриває EXT UART, не чіпаючи COM3.

---

## Частина 5 — Збірка та тест

### 5.1 Зібрати і прошити

```powershell
cd d:\GitHub\CoinTrace
pio run -e cointrace-dev -t upload   # прошивка через COM3 (USB-CDC, як раніше)
```

### 5.2 Відкрити монітор (Grove UART)

```powershell
pio device monitor -e cointrace-dev  # читає COM4 (FT232RL) — без DTR проблем
```

Або напряму:
```powershell
pio device monitor --port COM4 --baud 115200
```

### 5.3 Очікуваний бут-лог

```
[725ms] INFO  LFS            | LittleFSTransport started
[798ms] INFO  SD             | SD card available (archive tier active)
[1350ms] INFO Cache          | FingerprintCache ready — 5 entries
[1351ms] INFO Storage        | StorageManager ready — NVS:ok LFS:ok SD:ok FP:5 entries
[1642ms] INFO WiFi           | AP mode — SSID: CoinTrace-F974  IP: 192.168.4.1
[1850ms] INFO HTTP           | REST API ready — http://192.168.4.1/api/v1/status
[1851ms] INFO System         | CoinTrace ready — 0/1 plugins initialised
```

> **Примітка:** Рядки ESP-ROM bootloader (`ESP-ROM:esp32s3...`, `rst:0x...`) фізично вшиті
> в ROM і завжди йдуть через UART0 (GPIO43/44), не через Grove. Вони не з'являться.
> Всі логи **прошивки CoinTrace** підуть через Grove UART.

---

## Troubleshooting

| Симптом | Причина | Рішення |
|---|---|---|
| COM4 не з'являється в Device Manager | Немає драйвера FT232RL | Встановити FTDI VCP driver |
| Сміття в моніторі (кракозябри) | Невірна швидкість | Переконатись що 115200 baud |
| Нічого не виводиться | TX/RX переплутані | Поміняти G13 і G15 місцями (Pin 12 ↔ Pin 14) |
| Нічого не виводиться | Забули `gUart1.begin()` | Перевірити що `gUart1.begin(115200,SERIAL_8N1,13,15)` є до `gLogger.begin()` |
| Нічого не виводиться | Джампер FT232RL на 5V | Переставити на 3.3V |
| `InvalidOperationException` в моніторі | Відкрито COM3 замість COM4 | Перевірити `monitor_port` в platformio.ini |

---

## Порівняння підходів до дебагінгу

| Підхід | Бут-лог | DTR reset | Пайка | Складність |
|---|---|---|---|---|
| USB-CDC `pio monitor` | ❌ пропускає | ❌ є | ❌ | Просто |
| LittleFS esptool dump | ⚠️ частково | ✅ | ❌ | Середньо |
| **EXT 2.54-14P + FT232RL** (цей гайд) | ✅ повний | ✅ | ❌ | Просто |
| GPIO43/44 UART0 + адаптер | ✅ + ROM рядки | ✅ | ✅ потрібна | Складно |

---

---

## Підтверджений пінаут EXT 2.54-14P (для довідки)

| PIN L | FUNC L | PIN R | FUNC R | Примітка |
|---|---|---|---|---|
| 1 | G3 RESET | 2 | 5VIN | ⚠️ НЕ чіпати |
| 3 | G4 INT | 4 | GND | ✅ наш GND |
| 5 | G6 BUSY | 6 | 5VOUT | ⚠️ НЕ чіпати |
| 7 | G40 SCK | 8 | G8 I2C_SDA | SPI/I2C (main.cpp) |
| 9 | G14 MOSI | 10 | G9 I2C_SCL | SPI/I2C (main.cpp) |
| 11 | G39 MISO | 12 | **G13 UART_RX** | ✅ наш RX |
| 13 | G5 CS | 14 | **G15 UART_TX** | ✅ наш TX |

**Створено:** 2026-03-17  
**Статус:** ✅ hw-verified 2026-03-17 — COM4 (FT232RL) підтверджено. UART1 (G15/G13) реалізовано в `src/main.cpp`. Бут-лог доступний через `pio device monitor --port COM4`. Детально: `docs/audit/2026-03-17-wave8-a2-hw-session.md` §4.
