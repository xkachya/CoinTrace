# CoinTrace — Залежності між модулями

**Тип документа:** Архітектурна довідка
**Версія:** 1.0.0
**Дата:** 28 березня 2026
**Статус:** Актуальний

---

## Зміст

1. [Карта бібліотек](#1-карта-бібліотек)
2. [Дерево залежностей](#2-дерево-залежностей)
3. [Матриця залежностей](#3-матриця-залежностей)
4. [Модулі всередині StorageManager](#4-модулі-всередині-storagemanager)
5. [Архітектурні рівні](#5-архітектурні-рівні)

---

## 1. Карта бібліотек

### Розташування директорій

```
d:\GitHub\CoinTrace\
├── include/                    # Системні інтерфейси (plugIn contract)
│   ├── IPlugin.h
│   ├── ISensorPlugin.h
│   ├── IDiagnosticPlugin.h
│   ├── PluginContext.h
│   ├── PluginSystem.h
│   ├── IStorageManager.h
│   └── ConfigManager.h
│
├── lib/
│   ├── Logger/                 # Логування: Serial / RingBuffer / LittleFS
│   ├── StorageManager/         # 3-рівнева збережка + FP DB + MetalMatcher
│   ├── LDC1101Plugin/          # Плагін LDC1101 (SPI, header-only)
│   ├── WiFiManager/            # Wi-Fi + NVS credentials
│   └── HttpServer/             # Async HTTP API
│
├── src/
│   └── main.cpp                # Точка входу — оркеструє всі модулі
│
└── test/
    ├── test_metal_matcher/
    ├── test_fingerprint_cache/
    ├── test_vector_compute/
    └── ...
```

---

## 2. Дерево залежностей

```
main.cpp
├── [sys] PluginSystem (include/)
│   ├── IPlugin.h
│   └── PluginContext.h  ──── Wire.h, SPI.h, FreeRTOS semphr
│
├── [lib] Logger
│   ├── SerialTransport.h
│   ├── RingBufferTransport.h
│   └── LittleFSTransport.h
│       └── ──────────────────── LittleFSManager.h  (StorageManager)
│
├── [lib] StorageManager
│   ├── StorageManager.h  (facade)
│   │   ├── NVSManager.h
│   │   ├── LittleFSManager.h
│   │   ├── SDCardManager.h
│   │   │   └── LittleFSManager.h
│   │   ├── FingerprintCache.h
│   │   │   ├── LittleFSManager.h
│   │   │   └── SDCardManager.h
│   │   └── MeasurementStore.h
│   │       ├── Measurement.h
│   │       ├── LittleFSManager.h
│   │       ├── NVSManager.h
│   │       └── SDCardManager.h
│   │
│   ├── MetalMatcher.h          ← Wave 8 C-7a (2026-03-28)
│   │   ├── FingerprintCache.h
│   │   ├── SDCardManager.h
│   │   ├── Measurement.h
│   │   └── VectorCompute.h
│   │
│   └── VectorCompute.h         ← pure math, no HW deps
│       └── Measurement.h
│
├── [lib] WiFiManager
│   └── NVSManager.h  (StorageManager)
│
├── [lib] HttpServer
│   ├── LogEntry.h / LogLevel.h  (Logger)
│   ├── IStorageManager.h
│   ├── NVSManager.h / LittleFSManager.h  (StorageManager)
│   ├── MeasurementStore.h  (StorageManager)
│   ├── FingerprintCache.h  (StorageManager)
│   └── WiFiManager.h
│
└── [lib] LDC1101Plugin
    ├── ISensorPlugin.h  (include/)
    ├── IDiagnosticPlugin.h  (include/)
    └── PluginContext.h  (include/)
```

---

## 3. Матриця залежностей

Позначення: **●** = пряма залежність, **○** = транзитивна, ` ` = немає

| Залежний ↓ \ Від →       | Logger | NVSMgr | LittleFSMgr | SDCardMgr | FPCache | MeasStore | MetalMatcher | VectorCompute | WiFiMgr | PluginSystem |
|---------------------------|:------:|:------:|:-----------:|:---------:|:-------:|:---------:|:------------:|:-------------:|:-------:|:------------:|
| **Logger**                |   —    |        |      ●      |           |         |           |              |               |         |              |
| **NVSManager**            |        |   —    |             |           |         |           |              |               |         |              |
| **LittleFSManager**       |        |        |      —      |           |         |           |              |               |         |              |
| **SDCardManager**         |        |        |      ●      |     —     |         |           |              |               |         |              |
| **FingerprintCache**      |        |        |      ●      |     ●     |    —    |           |              |               |         |              |
| **MeasurementStore**      |        |   ●    |      ●      |     ●     |         |     —     |              |               |         |              |
| **MetalMatcher**          |        |        |             |     ●     |    ●    |           |      —       |       ●       |         |              |
| **VectorCompute**         |        |        |             |           |         |           |              |       —       |         |              |
| **WiFiManager**           |        |   ●    |             |           |         |           |              |               |    —    |              |
| **HttpServer**            |   ●    |   ●    |      ●      |           |    ●    |     ●     |              |               |    ●    |              |
| **LDC1101Plugin**         |        |        |             |           |         |           |              |               |         |      ●       |
| **main.cpp**              |   ●    |   ●    |      ●      |     ●     |    ●    |     ●     |      ●       |       ●       |    ●    |      ●       |

---

## 4. Модулі всередині StorageManager

`lib/StorageManager/src/` містить кілька логічно окремих компонентів в одній бібліотеці:

| Файл | Роль | Залежить від |
|------|------|-------------|
| `NVSManager.h/.cpp` | NVS key-value store | `IStorageManager.h`, Preferences.h |
| `LittleFSManager.h/.cpp` | LittleFS файлова система | Arduino.h, FreeRTOS |
| `SDCardManager.h/.cpp` | SD-карта (SPI) | `LittleFSManager.h` (ротація логів) |
| `FingerprintCache.h/.cpp` | FP індекс в RAM; CRC32 | `LittleFSManager.h`, `SDCardManager.h` |
| `MeasurementStore.h/.cpp` | Зберігання вимірювань | `Measurement.h`, NVS+LittleFS+SD |
| `MetalMatcher.h/.cpp` | Матчінг металу (5D Euclidean) | `FingerprintCache.h`, `VectorCompute.h`, SD |
| `VectorCompute.h/.cpp` | Нормалізація вектора вимірювання | `Measurement.h` (чиста математика) |
| `Measurement.h` | Структура даних вимірювання | — (немає залежностей) |
| `StorageManager.h/.cpp` | Facade для NVS+LittleFS+SD+FP+Meas | всі вище |

### Чому MetalMatcher в StorageManager, а не окремо

ADR-M1 (задокументовано в `METAL_MATCHER_ARCHITECTURE.md`):
- MetalMatcher не є плагіном — не реалізує `IPlugin`
- MetalMatcher читає FingerprintCache і SD — природно сусідить з ними
- Уникаємо нової бібліотеки PlatformIO для класу ~200 рядків

---

## 5. Архітектурні рівні

```
┌─────────────────────────────────────────────────────────────┐
│                    РІВЕНЬ ЗАСТОСУНКУ                        │
│                       main.cpp                              │
│     (display, UI state machine, IDLE handler, keyboard)     │
└──────────────────────────┬──────────────────────────────────┘
                           │
          ┌────────────────┼────────────────┐
          ▼                ▼                ▼
┌──────────────┐  ┌──────────────┐  ┌──────────────┐
│  HttpServer  │  │  WiFiManager │  │ PluginSystem  │
│  (Web API)   │  │  (Wi-Fi NVS) │  │ (lifecycle)   │
└──────┬───────┘  └──────┬───────┘  └──────┬───────┘
       │                 │                 │
       └─────────────────┼─────────────────┘
                         ▼
┌─────────────────────────────────────────────────────────────┐
│                   РІВЕНЬ СЕРВІСІВ                           │
│                    StorageManager                           │
│  NVSMgr │ LittleFSMgr │ SDCardMgr │ FPCache │ MeasStore    │
│                   MetalMatcher                              │
│                   VectorCompute                             │
└──────────────────────────┬──────────────────────────────────┘
                           │
          ┌────────────────┼────────────────┐
          ▼                ▼                ▼
┌──────────────┐  ┌──────────────┐  ┌──────────────┐
│    Logger    │  │ LDC1101Plugin│  │  Interfaces  │
│  (транспорт) │  │ (SPI сенсор) │  │  (include/)  │
└──────────────┘  └──────────────┘  └──────────────┘
          │
          ▼
┌─────────────────────────────────────────────────────────────┐
│               ЗОВНІШНІ ЗАЛЕЖНОСТІ (ESP32-S3)                │
│  Arduino.h │ M5Cardputer.h │ ArduinoJson │ AsyncWebServer  │
│  FreeRTOS  │ LittleFS │ SD │ Preferences │ SPI │ Wire       │
└─────────────────────────────────────────────────────────────┘
```

### Ключові архітектурні принципи

| Принцип | Реалізація |
|---------|-----------|
| **Залежності вниз** | main.cpp → сервіси → апаратура (ніколи навпаки) |
| **Без циклічних залежностей** | VectorCompute ← MetalMatcher ← main (ланцюг одностронній) |
| **Plugin isolation** | LDC1101Plugin знає тільки про `include/` — не про StorageManager |
| **Storage facade** | HttpServer використовує `IStorageManager.h`, не конкретні класи |
| **No PSRAM** | FingerprintCache обмежений 256 записами (~4.1 KB RAM) — ніяких heap-heavy структур |

---

*Документ згенеровано за аналізом вихідного коду станом на 28 березня 2026.*
*Автоматичного підтримання не передбачено — оновлювати при зміні `#include` залежностей між бібліотеками.*
