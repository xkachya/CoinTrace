# Connectivity Architecture — CoinTrace

**Статус:** 📐 Запроектовано, очікує імплементації  
**Версія:** 1.0.0  
**Дата:** 2026-03-11  
**Автор:** Yuriy Kachmaryk

---

## Зміст

1. [Навіщо цей документ](#1-навіщо-цей-документ)
2. [Інвентар hardware можливостей](#2-інвентар-hardware-можливостей)
3. [Інструменти для раннього дебагінгу](#3-інструменти-для-раннього-дебагінгу)
4. [Шар 1 — Фізичні транспорти](#4-шар-1--фізичні-транспорти)
5. [Шар 2 — Прикладні протоколи](#5-шар-2--прикладні-протоколи)
6. [Шар 3 — Клієнтські додатки](#6-шар-3--клієнтські-додатки)
7. [Архітектурні рішення (ADR)](#7-архітектурні-рішення-adr)
8. [Аналіз потенційних боттлнеків](#8-аналіз-потенційних-боттлнеків)
9. [Фазовий роадмап](#9-фазовий-роадмап)

---

## 1. Навіщо цей документ

CoinTrace — це embedded пристрій, що має взаємодіяти з:
- **PC** (розробник, debugging, batch processing)
- **Телефоном** (кінцевий користувач, field use)
- **Хмарою** (community fingerprint database, future)

Рішення, прийняті на етапі **першого датчика**, жорстко закладають foundation.  
Якщо вибрати не той протокол або не закласти versioning — після появи першого зовнішнього користувача будь-яка зміна стає breaking change.

Цей документ:
- фіксує всі варіанти, що розглядались, і чому кожен обраний або відхилений
- визначає інструменти для дебагінгу до появи UI
- встановлює 6 архітектурних рішень (ADR) що треба прийняти до написання коду
- аналізує де можуть виникнути боттлнеки при масштабуванні

---

## 2. Інвентар hardware можливостей

M5Stack Cardputer-Adv (ESP32-S3FN8) надає такі connectivity канали **без додаткового hardware**:

| Канал | Протокол | Поточний стан | Примітки |
|-------|----------|---------------|----------|
| USB-C (HWCDC) | Virtual Serial | ✅ Працює | `ARDUINO_USB_CDC_ON_BOOT=1` |
| USB-C (MSC) | Mass Storage | ❌ Не реалізовано | Конкурує з HWCDC за USB контролер |
| WiFi 802.11 b/g/n | TCP/IP | ❌ Не реалізовано | 2.4 GHz, AP або STA |
| BLE 5.0 | GATT | ❌ Не реалізовано | DLE: до 244 байт/packet |
| Display 240×135 | QR Code | ❌ Не реалізовано | `lgfx_qrcode.h` вже в libdeps |
| Keyboard 56-key | Input | ✅ Працює (з фіксом) | Унікальна перевага для provisioning |

**Ключове обмеження ESP32-S3:** один USB PHY контролер. Одночасно — або HWCDC Serial, або USB MSC. Перемикання потребує reboot або динамічного usb_mode switching (ESP-IDF рівень, складно).

---

## 3. Інструменти для раннього дебагінгу

Це критична секція: **що треба встановити/налаштувати до написання будь-якого connectivity коду**, щоб не дебажити вручну.

### 3.1 Вже є / вже налаштовано

| Інструмент | Використання |
|-----------|-------------|
| PlatformIO Serial Monitor | Базовий serial output. `pio device monitor` |
| `xtensa-esp32s3-elf-addr2line` | Декодування crash backtrace (вже використали) |
| Logger + RingBuffer | 100 останніх записів доступні в пам'яті |
| `scripts/pre-commit-test.ps1` | 40 unit тестів без hardware |

### 3.2 Потрібно додати: Python serial client

**Навіщо:** PlatformIO Monitor виводить сирий текст. Як тільки Serial протокол стане JSON — потрібен parser що показує тільки потрібне, форматує, пише в файл для аналізу.

```python
# tools/monitor.py (приклад)
import serial, json
with serial.Serial("COM3", 115200) as s:
    for line in s:
        msg = json.loads(line)
        if msg["t"] == "result":
            print(f"Match: {msg['match']} ({msg['conf']:.0%})")
```

**Встановлення:** `pip install pyserial`  
**Де покласти:** `tools/monitor.py`

### 3.3 Потрібно додати: nRF Connect (телефон)

**Навіщо:** BLE GATT debugging без написання жодного рядка клієнтського коду.  
Дозволяє: бачити всі сервіси/характеристики, читати/писати вручну, підписуватись на Notify.

**Де взяти:** nRF Connect for Mobile (Nordic Semiconductor) — безкоштовно, Android та iOS.

**Коли знадобиться:** відразу як тільки BLE GATT service буде реалізовано.

### 3.4 Потрібно додати: curl / httpie для REST API

**Навіщо:** Тестувати HTTP endpoints без UI. Краще ніж браузер — зберігаємо history, scriptable.

```bash
# httpie (зручніший ніж curl)
pip install httpie

http GET http://cointrace.local/api/v1/status
http POST http://cointrace.local/api/v1/measure/start
```

**Коли знадобиться:** відразу як тільки WiFi AP + HTTP server будуть реалізовані.

### 3.5 Потрібно додати: wscat для WebSocket тестування

**Навіщо:** Дебажити WebSocket streaming до появи UI.

```bash
npm install -g wscat
wscat -c ws://cointrace.local/api/v1/stream
# > {"t":"sensor","rp":1250.5,"ts":123456}
```

**Коли знадобиться:** при реалізації WebSocket streaming.

### 3.6 Потрібно додати: Wireshark + WiFi monitor

**Навіщо:** Бачити реальні TCP/HTTP пакети. Діагностика "чому не підключається", "яка затримка", "чи дійшов пакет".  
**Особливо:** Якщо ESP32 AP mode не працює — Wireshark бачить чи пристрій взагалі відповідає на ARP.

**Встановлення:** https://www.wireshark.org — Windows, безкоштовно.

### 3.7 Потрібно додати: LittleFS filesystem tools

**Навіщо:** Для упаковки HTML/CSS/JS у LittleFS образ і заливки на пристрій.

```bash
# PlatformIO вбудована підтримка:
pio run --target uploadfs   # заливає data/ папку в LittleFS
pio run --target buildfs    # тільки будує образ
```

**Структура:** All web assets у `data/` → PlatformIO автоматично пакує.

### 3.8 Mock ESP32 Server для frontend розробки

**Навіщо:** Розробляти Web UI без підключеного пристрою. Декілька рядків Python що повертають mock JSON.

```python
# tools/mock_server.py
from flask import Flask, jsonify
app = Flask(__name__)

@app.get("/api/v1/status")
def status():
    return jsonify({"version": "1.0.0-dev", "heap": 320000, "bat": 87})

@app.get("/api/v1/measure")
def measure():
    return jsonify({"match": "Ag925", "conf": 0.94,
                    "vector": [1250.5, 0.742, 0.531, 0.0923, 0.18]})
```

```bash
pip install flask ; python tools/mock_server.py
# Web UI розробляється проти http://localhost:5000
```

### 3.9 Зведена таблиця: що встановити і коли

| Інструмент | Фаза | Платформа | Команда встановлення |
|-----------|------|-----------|---------------------|
| `pyserial` | Зараз | PC | `pip install pyserial` |
| `httpie` | Перед WiFi фазою | PC | `pip install httpie` |
| `flask` | Перед WiFi фазою | PC | `pip install flask` |
| `wscat` | Перед WebSocket фазою | PC | `npm install -g wscat` |
| nRF Connect | Перед BLE фазою | Телефон | App Store / Play Store |
| Wireshark | Перед WiFi фазою | PC | wireshark.org |
| LittleFS (вбудовано) | Перед Web UI фазою | PlatformIO | вже є в PIO |

---

## 4. Шар 1 — Фізичні транспорти

### 4.1 USB Serial (HWCDC) — базовий, вже працює

**Характеристики:**
- Швидкість: до 921600 baud (~100 KB/s)
- Латентність: ~1–5 ms
- Надійність: 100% (провідний зв'язок)
- Потребує: USB-C кабель + PC

**Роль в архітектурі:** Primary transport для розробки і debugging. Завжди доступний паралельно з WiFi/BLE. НЕ замінюється іншими транспортами — залишається насавжди для `pio device monitor`.

**Поточний стан:** ✅ Працює. Відсутній структурований протокол — тільки лог рядки.

---

### 4.2 USB Mass Storage (MSC)

**Сценарій:** microSD маппиться як USB-диск без жодного додатку. Windows Explorer бачить "CoinTrace". Файли `measurements.json`, `2026-03-11.csv` відразу відкриваються в Excel.

**Trade-off:**

| Плюс | Мінус |
|------|-------|
| Нульова інсталяція ПЗ | Один USB PHY → або serial, або MSC |
| Знайомо всім (Explorer) | Потрібен reboot для перемикання |
| Прямий доступ до CSV/JSON | Немає real-time streaming |

**Рішення:** MSC реалізувати як окремий boot mode. При старті з натиснутою клавішею (наприклад `M`) — USB MSC mode; інакше — HWCDC Serial. Відображати поточний режим на дисплеї.

**Пріоритет:** Низький. Корисно для non-developer end users. Не блокує core functionality.

---

### 4.3 WiFi — AP Mode

Пристрій стає точкою доступу:
```
SSID:     "CoinTrace-A1B2"   (suffix = last 4 hex of MAC)
Password: "cointrace"         (або OPEN для прототипу)
IP:       192.168.4.1
```

**Сценарій:** Піднести телефон → підключитись до WiFi → браузер → `http://192.168.4.1` → повноцінний UI.

**Trade-off:**

| Плюс | Мінус |
|------|-------|
| Нульова конфігурація пристрою | Телефон втрачає інтернет |
| Працює в полі (без роутера) | Тільки 1 клієнт одночасно (reliably) |
| Простий IP: 192.168.4.1 | 2.4GHz конкуренція в місцях скупчення |

**Пріоритет:** Високий. **Перша WiFi фаза** = AP mode + HTTP server. Проста і надійна.

---

### 4.4 WiFi — STA Mode (Station)

Пристрій підключається до домашнього роутера:
```
cointrace.local → (mDNS/Bonjour) → 192.168.1.XXX (DHCP)
```

**Provisioning проблема і рішення:**  
Як ввести SSID/password? Стандартний підхід — BLE provisioning або captive portal. Але у нас є **56-key QWERTY keyboard** — можна просто набрати на самому пристрої! Зберегти в NVS flash.

```
[CoinTrace] Enter WiFi SSID:
> HomeNetwork_5G █
[CoinTrace] Enter Password:
> **********
[CoinTrace] Connecting... OK
[CoinTrace] IP: 192.168.1.47  /  cointrace.local
```

**Це унікальна перевага Cardputer що немає в жодного конкурента.**

**Trade-off:**

| Плюс | Мінус |
|------|-------|
| Телефон зберігає інтернет | Треба знати SSID/password |
| Доступний з ноутбука і телефона одночасно | Залежить від роутера |
| `cointrace.local` зручніший ніж IP | mDNS іноді ненадійний (Windows) |

**Пріоритет:** Середній. Реалізувати після AP mode.

---

### 4.5 BLE 5.0

ESP32-S3 підтримує BLE 5.0 з DLE (Data Length Extension): до 244 байт payload за packet.

**Два незалежних use case:**

#### Use Case A: BLE як основний transport (телефон ↔ пристрій)
- GATT сервіс з характеристиками (measure, result, status, raw stream)
- Fingerprint вектор [5 × float32] = 20 байт → один BLE пакет
- Підходить для: field use без WiFi, низьке споживання батареї
- **Web Bluetooth API:** Chrome на Android/desktop підтримує без додатку

#### Use Case B: BLE як provisioning канал для WiFi
- Телефон передає SSID/password через BLE (зашифровано)
- Пристрій підключається до WiFi, повідомляє IP назад через BLE
- BLE відключається — далі спілкування через WiFi
- Стандартний підхід Espressif (ESP-Provisioning SDK)

**Важливо:** BLE і WiFi можуть працювати **одночасно** на ESP32-S3 (coexistence). Але heap обмежений (~337 KB), тому одночасно BLE + WiFi + великий буфер WebSocket = обережно з алокаціями.

**Пріоритет:** Середній. Use Case A — після WiFi. Use Case B — тільки якщо keyboard provisioning виявиться недостатнім.

---

### 4.6 QR Code на дисплеї

`lgfx_qrcode.h` вже присутня в `.pio/libdeps`. M5GFX включає QR рендерер без додаткових бібліотек.

**Ємність QR на дисплеї 240×135:**
- При масштабі 4 пікс/модуль: Version 3 (29×29) → до 47 байт ASCII
- При масштабі 3 пікс/модуль: Version 5 (37×37) → до 77 байт ASCII

**Сценарії використання:**
1. **URL результату:** `http://192.168.4.1/r/42` → телефон відкриває деталі
2. **Стислий результат:** `{"m":"Ag925","c":94}` → ~22 байт → Version 2
3. **Fingerprint share:** URL до cloud API з параметрами вимірювання

**Trade-off:**

| Плюс | Мінус |
|------|-------|
| Нульова інсталяція | Односторонній (device → phone тільки) |
| Будь-яке залізо з камерою | Фіксована ємність |
| `lgfx_qrcode.h` вже є | Вимагає WiFi/Cloud для корисного payload |

**Пріоритет:** Низький для v1. Корисний як "Share" функція в v1.5+.

---

### 4.7 Порівняльна таблиця транспортів

| Транспорт | Швидкість | Відстань | Інсталяція | Телефон | PC | Офлайн | Пріоритет |
|-----------|----------|----------|------------|---------|-----|--------|-----------|
| USB Serial | 100 KB/s | 2 м (кабель) | Ніщо | ❌ | ✅ | ✅ | v0 ✅ |
| USB MSC | 5 MB/s | 2 м (кабель) | Ніщо | ❌ | ✅ | ✅ | v2 |
| WiFi AP | 5 Mbit/s | ~30 м | Ніщо | ✅ | ✅ | ✅ | v1 |
| WiFi STA | 10 Mbit/s | Мережа | SSID/pass | ✅ | ✅ | ✅ | v1.5 |
| BLE | 500 KB/s | ~50 м | Ніщо* | ✅ | ✅ | ✅ | v1.5 |
| QR Code | ~77 байт | очі 📷 | Ніщо | ✅ | ❌ | ✅ | v2 |

\* Web Bluetooth в Chrome (Android/Desktop) — без додатку.

---

## 5. Шар 2 — Прикладні протоколи

### 5.1 Serial Protocol: JSON-Lines + COBS Binary

**Формат:** Один JSON об'єкт на рядок (`\n` як delimiter). Тип повідомлення → поле `"t"`.

#### Message types (device → host):

```jsonc
// Лог рядок — forwarded з Logger
{"t":"log","level":"INFO","comp":"System","msg":"CoinTrace ready","ms":672}

// Результат вимірювання
{"t":"result","id":42,"match":"Ag925","conf":0.94,
 "vector":[1250.5,0.742,0.531,0.0923,0.18],"ms":2341}

// Статус пристрою
{"t":"status","version":"1.0.0-dev","heap":320048,
 "uptime":12345,"bat":87,"wifi":"off","ble":"off"}

// Один сирий відлік сенсора (під час streaming)
{"t":"raw","rp":1250.5,"l":18.2,"ms":1234}

// Помилка/виняток
{"t":"err","code":"SENSOR_TIMEOUT","comp":"LDC1101","ms":800}
```

#### Message types (host → device):

```jsonc
// Запустити вимірювання
{"t":"measure","start":true}

// Розпочати/зупинити raw streaming
{"t":"stream","on":true}
{"t":"stream","on":false}

// Запит статусу
{"t":"ping"}

// Калібрування
{"t":"calibrate","steps":4}
```

**Чому JSON-Lines, а не бінарний:**
- Читається в будь-якому Serial Monitor без декодера
- `cat /dev/ttyUSB0 | jq .` вже корисно
- Python: `json.loads(line)` — одна рядок коду
- Для частоти вимірювання CoinTrace (~1 на 5 сек) overhead незначний

**Вийняток — raw sensor stream:** При включеному streaming можливе до 400+ Hz. Тут — COBS binary:
- Trigger JSON: `{"t":"stream","on":true,"format":"cobs"}`
- Frame: `[type:1][seq:2][rp_i32:4][l_i32:4][ms_u32:4][crc16:2]` = 17 байт
- COBS overhead: max +1 байт на 254 байти → ~0% для 17 байт frame
- `0x00` = frame end delimiter

---

### 5.2 HTTP REST API

**Base URL:** `http://cointrace.local/api/v1/` (AP mode: `http://192.168.4.1/api/v1/`)

**Версіонування:** `/api/v1/` prefix — **критично закласти зараз**. Після появи зовнішніх клієнтів будь-яка зміна схеми без версіонування = breaking change.

#### Endpoints:

```
GET  /api/v1/status
     → {"version":"1.0.0","heap":320048,"bat":87,"uptime":12345,
        "wifi":"ap","ip":"192.168.4.1","ble":"off"}

POST /api/v1/measure/start
     → {"id":43,"status":"measuring","eta_ms":2000}

GET  /api/v1/measure/{id}
     → {"id":43,"status":"done","match":"Ag925","conf":0.94,
        "vector":[1250.5,0.742,0.531,0.0923,0.18]}
     або {"id":43,"status":"measuring"}
     або {"id":43,"status":"error","code":"SENSOR_TIMEOUT"}

GET  /api/v1/log?n=50&level=DEBUG
     → {"entries":[{"ms":672,"level":"INFO","comp":"System","msg":"..."},...]}
     (дані з RingBufferTransport — 100 записів безкоштовно)

GET  /api/v1/database
     → {"count":12,"coins":[{"name":"Ag925","fingerprints":3},...]

POST /api/v1/database/match
     body: {"vector":[1250.5,0.742,0.531,0.0923,0.18]}
     → {"match":"Ag925","conf":0.94,"alternatives":[...]}

POST /api/v1/calibrate
     → {"status":"started"}

GET  /api/v1/ota/status
     → {"version":"1.0.0","latest":"1.0.1","update_available":true}

POST /api/v1/ota/update
     → {"status":"downloading"} + потім reboot
```

**Library:** `AsyncWebServer` (бібліотека `ESP Async WebServer`) — не блокуюча, підходить для ESP32. Альтернатива: вбудований `WebServer.h` — простіший, але блокуючий (не підходить якщо одночасно потрібен BLE або інші задачі).

---

### 5.3 WebSocket — Real-Time Streaming

**Endpoint:** `ws://cointrace.local/api/v1/stream`

**Навіщо WebSocket, а не HTTP polling:**
- HTTP poll: 500+ байт headers кожного запиту. При 10 Hz → 5 KB/s overhead.
- WebSocket: handshake один раз → потім 2–10 байт overhead/frame.
- Критично для UX: **live attenuation curve** — користувач бачить як крива будується в реальному часі.

**Messages (server → client):**
```json
{"t":"sensor","rp":1250.5,"l":18.2,"ts":1234567}
{"t":"result","match":"Ag925","conf":0.94,"vector":[...]}
{"t":"status","heap":320000}
```

**Messages (client → server):**
```json
{"t":"measure","start":true}
{"t":"stream","on":true}
```

**Library:** `AsyncWebServer` включає WebSocket підтримку (`AsyncWebSocket`).

---

### 5.4 BLE GATT Service

**UUID стратегія:** 128-bit UUID генерується **один раз** і фіксується назавжди. Зміна UUID після публікації = всі старі клієнти ламаються.

```
Service UUID:  cointrace-0001-0000-0000-000000000000  (генерувати через uuidgen)

Characteristics:
  MEASURE   UUID: ...0001  Properties: Write
                  Value: 0x01 = start measurement
  
  RESULT    UUID: ...0002  Properties: Notify
                  20 bytes: [match:1][conf:1][rp:4f][k1:4f][k2:4f][slope:4f][dl:2u]
                  match = index в таблиці металів (0=Ag999, 1=Ag925, ...)
  
  STATUS    UUID: ...0003  Properties: Read + Notify
                  1 byte: 0=IDLE, 1=MEASURING, 2=DONE, 3=ERROR, 4=CALIBRATING
  
  LOG       UUID: ...0004  Properties: Notify
                  20 bytes: [level:1][comp:7][msg:12] (стиснуто)
  
  RAW       UUID: ...0005  Properties: Notify
                  8 bytes: [rp:4f][l:4f] — для real-time curve
```

**Web Bluetooth API** (Chrome на Android 6.0+ і Chrome Desktop):
```javascript
const device = await navigator.bluetooth.requestDevice({
    filters: [{name: "CoinTrace-A1B2"}],
    optionalServices: ["cointrace-0001-..."]
});
// Далі — повноцінний GATT клієнт без жодного native додатку
```

Це дозволяє PWA на `cointrace.io` (або GitHub Pages) підключатись до пристрою через BLE без App Store.

---

### 5.5 Data Format: Fingerprint Vector

```json
{
  "version": 1,
  "device": "CoinTrace-A1B2",
  "fw_version": "1.0.0",
  "measured_at": "2026-03-11T14:23:45Z",
  "vector": {
    "delta_rp1":  1250.5,
    "k1":         0.7420,
    "k2":         0.5310,
    "slope":      0.0923,
    "delta_l1":   0.1800
  },
  "match": {
    "name":       "Silver 925",
    "confidence": 0.94,
    "distance":   0.0312
  }
}
```

**Чому JSON, а не MessagePack/CBOR/Protobuf для v1:**
- Fingerprint = 1 вимірювання на ~5 секунд → bandwidth незначний
- JSON: людиночитаємий, `jq .`, Excel, Python без декодера
- ArduinoJson тут достатньо, версіонування через поле `"version": 1`
- MessagePack: оптимізація для v2 якщо BLE bandwidth стане критичним (малоймовірно)

---

## 6. Шар 3 — Клієнтські додатки

### 6.1 Web UI в LittleFS — "нульова інсталяція"

ESP32-S3 має 8 MB flash. Якщо розподілити: 3.3 MB firmware + 4.5 MB LittleFS → ~4 MB для Web UI.

**Структура:**
```
data/                     ← PlatformIO пакує в LittleFS образ
  index.html              ← Single Page Application
  app.js                  ← ~100-200 KB після мінімізації
  style.css               ← ~20 KB
  fonts/
  favicon.ico
```

**Заливка:** `pio run --target uploadfs` (окремо від firmware).

**Сценарій:** Відкрив браузер → `http://cointrace.local/` → повноцінний UI. Ніякого хостингу, ніякого бекенду.

**OTA оновлення UI:** `POST /api/v1/ota/fs/update` з binary LittleFS image. Або окрема `pio run --target uploadfs`.

**Технологічний стек Web UI (рекомендація):**
- Vanilla HTML/CSS/JS (без build step) — найменший розмір, проста дебагіка
- Або: Preact (~3 KB) замість React (~130 KB) — менше ніж LittleFS обмеження
- **Не:** React/Vue/Angular — після bundling 300+ KB, повільний старт на мобільному

---

### 6.2 Python CLI / Monitor

```python
# tools/coinTrace.py
class CoinTrace:
    def __init__(self, port: str, baud: int = 115200): ...
    def measure(self) -> MeasurementResult: ...
    def stream(self) -> Iterator[SensorReading]: ...
    def get_log(self, n: int = 50) -> List[LogEntry]: ...
    def calibrate(self) -> bool: ...
```

**Два режими роботи:**
1. **Serial transport:** `CoinTrace(port="COM3")`
2. **HTTP transport:** `CoinTrace(url="http://cointrace.local")` — той самий API

Спільний інтерфейс = скрипти аналізу не залежать від транспорту.

**Місце коду:** `tools/coinTrace.py` + `tools/monitor.py` + `tools/batch_analyze.py`

---

### 6.3 PWA (Progressive Web App)

**Хостинг:** GitHub Pages (`https://xkachya.github.io/CoinTrace/`) — безкоштовно.

**Підключення:**
- **WiFi:** `fetch("http://cointrace.local/api/v1/...")` — але CORS! ESP32 має повертати `Access-Control-Allow-Origin: *`
- **BLE:** Web Bluetooth API — Chrome Android + Chrome Desktop

**Встановлення:** "Add to Home Screen" в Chrome → виглядає як native app. Без App Store.

**Service Worker:** PWA кешує UI → працює офлайн (тільки UI; дані — живі з пристрою).

**Обмеження:** Safari (iOS) — Web Bluetooth не підтримується. iPhone потребує або Safari-only UI (без BLE) або dedicated iOS app.

---

### 6.4 Порівняння клієнтських підходів

| Рішення | Інсталяція | iOS | Android | PC | BLE | Зусилля |
|---------|-----------|-----|---------|-----|-----|---------|
| Web UI (на пристрої) | Ніщо | ✅ Safari | ✅ Chrome | ✅ | ❌ | Мало |
| PWA (GitHub Pages) | 1 клік | ⚠️ Safari | ✅ Chrome | ✅ | ✅ Chrome | Мало |
| Python CLI | pip install | ✅ | ✅ | ✅ | ❌ | Мало |
| React Native App | App Store | ✅ | ✅ | ❌ | ✅ | Багато |
| Flutter App | App Store | ✅ | ✅ | ✅ | ✅ | Багато |
| Electron/Tauri App | Install | ✅ | ❌ | ✅ | Tauri✅ | Середній |

**Рекомендація v1:** Web UI на пристрої + Python CLI. Покриває 90% випадків без App Store.

---

## 7. Архітектурні рішення (ADR)

Це рішення які потрібно прийняти **до початку реалізації**. Після появи зовнішніх клієнтів — змінити без breaking change майже неможливо.

---

### ADR-001: Serial Protocol Format

**Статус:** ✅ Прийнято  
**Рішення:** JSON-Lines для control messages + COBS binary для raw sensor stream (активується окремою командою).  
**Причина:** Людиночитаємість в розробці переважує bandwidth для нашої частоти вимірювань. COBS для stream — тільки коли дійсно потрібен.  
**Альтернативи розглядались:** Pure binary (відхилено — нечитаємо), Pure JSON (відхилено — overhead при 400 Hz stream).

---

### ADR-002: HTTP API Versioning

**Статус:** ✅ Прийнято  
**Рішення:** Всі endpoints починаються з `/api/v1/`. При breaking change — новий prefix `/api/v2/`, стара версія підтримується мінімум 6 місяців.  
**Причина:** Після першого публічного release неможливо змінити URL без поламки клієнтів.  
**Наслідок:** Треба закласти в `AsyncWebServer` routing від початку, не "додати пізніше".

---

### ADR-003: Web UI Location

**Статус:** ✅ Прийнято  
**Рішення:** Web UI живе в LittleFS на самому пристрої (`data/` → `pio uploadfs`). Дублюється на GitHub Pages як PWA.  
**Причина:** "Відкрив браузер — і є UI" без залежності від інтернету чи хостингу.

---

### ADR-004: BLE UUID Permanence

**Статус:** ⏳ Очікує: треба згенерувати UUIDs до початку BLE реалізації  
**Рішення:** Згенерувати 128-bit UUID для сервісу і кожної з 5 характеристик через `uuidgen`. Зафіксувати в `lib/BLE/include/ble_uuids.h`. **Ніколи не змінювати після першого публічного release.**  
**Причина:** Зміна UUID = всі BLE клієнти (PWA, nRF Connect збережені конфіги, сторонні інтеграції) ламаються.  
**Дія:** Додати `TODO` в GitHub Issues: "Generate and lock BLE UUIDs before BLE phase".

---

### ADR-005: WiFi Provisioning UX

**Статус:** ✅ Прийнято  
**Рішення:** Вводити SSID/password напряму на Cardputer клавіатурі. Зберігати в ESP32 NVS flash.  
**Причина:** 56-key QWERTY keyboard — унікальна перевага нашого hardware. BLE provisioning (ESP-Touch) значно складніший у реалізації і не дає UX переваги.  
**Fallback:** AP Captive Portal як альтернативний метод якщо keyboard UX виявився незручним.

---

### ADR-006: Одночасність WiFi + BLE

**Статус:** ✅ Прийнято  
**Рішення:** WiFi і BLE можуть працювати одночасно (ESP32-S3 coexistence) але **не активувати обидва за замовчуванням**. BLE вмикається тільки у "pairing mode" (клавіша `B` на клавіатурі або перша хвилина після boot).  
**Причина:** WiFi + BLE + AsyncWebServer + Logger buffers одночасно → heap ~180–200 KB зайнято з 337 KB. Залишає мало для fingerprint database в RAM.  
**Моніторинг:** `GET /api/v1/status` завжди показує `heap` → легко помітити деградацію.

---

## 8. Аналіз потенційних боттлнеків

### 8.1 USB Controller Conflict (HWCDC vs MSC)

**Проблема:** Один USB PHY. Serial (HWCDC) і MSC не можуть бути активні одночасно.  
**Коли стане болючим:** Якщо end user хоче одночасно бачити лог в Serial Monitor і копіювати файли.  
**Митигація:** Boot mode selection (клавіша при старті). Чіткий індикатор режиму на дисплеї.  
**Не блокує v1:** Serial mode використовується тільки розробниками. End user — WiFi.

---

### 8.2 Heap Fragmentation при Довгій Роботі

**Проблема:** ESP32 не має MMU. `malloc`/`free` + `std::string` + ArduinoJson + WiFi stack → фрагментація heap після годин роботи. `heap` може показувати 100 KB вільних але `malloc(50KB)` fails.  
**Коли стане болючим:** При continuous operation 24/7 або великій fingerprint database в RAM.  
**Митигація v1:** Статичні буфери де можливо. `StaticJsonDocument<512>` замість `DynamicJsonDocument`. Logger RingBuffer — фіксований розмір, вже правильно зроблено.  
**Моніторинг:** `ESP.getMinFreeHeap()` (мінімум з моменту boot) — більш показовий ніж поточний `getFreeHeap()`.

---

### 8.3 LittleFS Size Limit

**Проблема:** 8 MB flash total. Partition: ~3.3 MB firmware + ~4.5 MB LittleFS (з default_8MB.csv).  
**Коли стане болючим:** Web UI + fingerprint database + logs одночасно у LittleFS.  
**Відповіді:**
- Web UI: ~500 KB після мінімізації → добре вписується
- Fingerprint DB: microSD (є на Cardputer!) → окремий filesystem
- Logs: тільки RAM RingBuffer, не пишемо в flash
- **Архітектурне правило:** Fingerprint database — завжди на microSD, не в LittleFS.

---

### 8.4 mDNS Ненадійність на Windows

**Проблема:** `cointrace.local` через mDNS/Bonjour ненадійно на Windows (firewall, Bonjour service стан). Mac/iOS/Android — надійно.  
**Митигація:** Завжди показувати IP-адресу на дисплеї паралельно з `cointrace.local`. QR-код з `http://192.168.4.1` в AP mode.  
**Не блокує v1:** AP mode використовує фіксований IP `192.168.4.1` без mDNS.

---

### 8.5 AsyncWebServer Memory під Навантаженням

**Проблема:** `ESP Async WebServer` алоцює heap для кожного HTTP request/response. При багатьох паралельних клієнтах → потенційний heap exhaustion.  
**Реальний сценарій для CoinTrace:** 1–2 одночасних клієнти (1 телефон + 1 ноутбук) → не проблема.  
**Невирішений сценарій:** Публічна демонстрація, 10+ людей підключаються одночасно через AP mode. ESP32 AP підтримує max 4–5 connected clients без деградації.  
**Митигація:** Connection limit = 4. Документувати обмеження в UI.

---

### 8.6 Backward Compatibility Fingerprint Database

**Проблема:** Якщо змінити формат fingerprint vector між v1 і v2 — вся community database стає несумісною. Особливість: це **мовчазна** несумісність — firmware не впаде з помилкою, а тихо видасть неправильний результат (confidence 0.6 там де має бути 0.95).

**Чотири незалежних виміри поламки:**
1. `"version": 1` покриває три різних речі (schema / algo / protocol) — потрібні три окремих поля
2. k1 і k2 залежать від `freq_hz` і `steps_mm` — вони мають бути частиною fingerprint, не конфіга
3. `slope` в README.md семантично невизначений (одиниці? метод обчислення?)
4. `dRp1` ненормалізований → домінує в Euclidean distance → скасовує size-independence

**Детальний аналіз і всі ADR:** 📄 **[FINGERPRINT_DB_ARCHITECTURE.md](./FINGERPRINT_DB_ARCHITECTURE.md)**

**Коротка митигація (§10 Чек-лист в FINGERPRINT_DB_ARCHITECTURE.md):**
- Три версійних поля: `schema_ver`, `algo_ver`, `protocol_ver`
- Секція `conditions` (freq_hz, steps_mm, coil_model) — обов'язкова, ключ сумісності
- Секція `raw` (rp0..rp3, l0, l1) — source of truth для майбутніх міграцій
- CI validation (GitHub Actions) для всіх PR в `database/`
- `SINGLE_FREQUENCY` → `config/measurement_protocol_v1.h` (не в `platformio.ini`)

---

## 9. Фазовий роадмап

### Фаза 0 — Зараз (без датчика)

**Мета:** Підготувати інфраструктуру і переконатись що протоколи правильно визначені.

| Задача | Що робити | Тестування |
|--------|-----------|-----------|
| Serial Protocol spec | Визначити всі JSON message schemas, задокументувати | Unit test парсера |
| Mock serial server | Python скрипт що емулює пристрій | Тестувати monitor.py |
| WiFi AP skeleton | AP mode + HTTP `/api/v1/status` з mock даними | Browser + curl |
| LittleFS setup | `data/index.html` placeholder, `pio uploadfs` workflow | Browser |

---

### Фаза 1 — Є датчик (v1.0)

**Мета:** Повноцінний WiFi AP + Web UI + Serial + базовий Python CLI.

| Задача | Деталі |
|--------|--------|
| Serial structured output | Logger → JSON-Lines через SerialTransport |
| WiFi AP + HTTP server | AsyncWebServer, всі `/api/v1/` endpoints |
| WebSocket stream | Real-time attenuation curve в UI |
| Web UI (LittleFS) | Vanilla JS SPA: кнопка Measure, результат, live curve |
| Python CLI | `tools/coinTrace.py` serial + http transport |
| CORS headers | `Access-Control-Allow-Origin: *` для PWA |

---

### Фаза 1.5 — Post-v1 stabilization

| Задача | Деталі |
|--------|--------|
| WiFi STA + keyboard provisioning | NVS зберігання SSID/password |
| mDNS `cointrace.local` | mdns library |
| BLE GATT service | UUID генерація + характеристики + Web Bluetooth тест |
| PWA на GitHub Pages | Service Worker, offline cache, Web Bluetooth |
| OTA firmware update | ArduinoOTA або ESP-IDF OTA через HTTP |

---

### Фаза 2 — Community Features

| Задача | Деталі |
|--------|--------|
| microSD fingerprint database | JSON files на SD, index у RAM |
| Community DB sync | WiFi STA → HTTPS API → download нові fingerprints |
| USB MSC mode | Boot selection, Explorer access до SD |
| QR Code share | lgfx_qrcode, URL з результатом |
| OTA LittleFS update | `POST /api/v1/ota/fs/update` |

---

## Приклад: діаграма шарів

```
┌─────────────────────────────────────────────────────────┐
│                    Client Layer                          │
│  Browser (Web UI) │ Python CLI │ PWA │ nRF Connect BLE  │
└──────────────────────────────────────────────────────────┘
                            ↕
┌─────────────────────────────────────────────────────────┐
│                   Protocol Layer                         │
│  JSON-Lines Serial │ HTTP REST │ WebSocket │ BLE GATT   │
└──────────────────────────────────────────────────────────┘
                            ↕
┌─────────────────────────────────────────────────────────┐
│                  Transport Layer                         │
│    USB HWCDC    │    WiFi AP/STA    │    BLE 5.0        │
└──────────────────────────────────────────────────────────┘
                            ↕
┌─────────────────────────────────────────────────────────┐
│                   Firmware Layer                         │
│  Logger+RingBuffer │ Plugin System │ LDC1101 Plugin     │
└──────────────────────────────────────────────────────────┘
```

---

## Статус і наступні кроки

| # | Дія | Відповідальний | Коли |
|---|-----|----------------|------|
| 1 | ✅ ADR-001: Serial format визначено | — | Цей документ |
| 2 | ✅ ADR-002: API versioning `/api/v1/` | — | Цей документ |
| 3 | ✅ ADR-003: LittleFS Web UI | — | Цей документ |
| 4 | ⏳ ADR-004: Згенерувати BLE UUIDs | До BLE фази | GitHub Issue |
| 5 | ✅ ADR-005: Keyboard provisioning | — | Цей документ |
| 6 | ✅ ADR-006: WiFi+BLE не одночасно | — | Цей документ |
| 7 | ⏳ Встановити debugging tools | Розробник | Зараз |
| 8 | ⏳ Serial Protocol unit tests | Розробник | Фаза 0 |
| 9 | ⏳ WiFi AP skeleton + mock API | Розробник | Фаза 0 |
