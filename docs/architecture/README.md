# Архітектура CoinTrace

**Документація професійної архітектури з системою плагінів**

---

## 📚 Документи в цій папці

### 1. 🏗️ [PLUGIN_ARCHITECTURE.md](./PLUGIN_ARCHITECTURE.md) - **Головний документ**
**Для кого:** Архітектори, технічні ліди, досвідчені розробники  
**Що містить:**
- Повний опис Plugin System архітектури
- Як працює система плагінів
- Структура проекту
- Приклади конфігураційних файлів
- Міграція між hardware платформами
- Roadmap імплементації

**Прочитати першим якщо ви:** Плануєте структуру проекту, хочете зрозуміти "чому саме так"

---

### 2. 🔌 [CREATING_PLUGINS.md](./CREATING_PLUGINS.md) - **Практичний гайд**
**Для кого:** Розробники, контрибутори, студенти  
**Що містить:**
- Покроковий туторіал "Ваш перший плагін за 15 хвилин"
- Шаблони плагінів (I2C, SPI, IMU)
- Checklist перед публікацією
- Debugging поради
- Приклади складних випадків

**Прочитати першим якщо ви:** Хочете додати свій датчик/модуль, контрибутите в проект

---

### 3. 📊 [COMPARISON.md](./COMPARISON.md) - **Чому Plugin System?**
**Для кого:** Менеджери проектів, інвестори, керівники команд  
**Що містить:**
- Порівняльні таблиці (Традиційний vs Plugin)
- Економічне обґрунтування (-89% часу розробки)
- Реальні приклади з Git workflow
- ROI розрахунки

**Прочитати першим якщо ви:** Приймаєте рішення "яку архітектуру використовувати"

---

### 4. 🔧 [PLUGIN_INTERFACES_EXTENDED.md](./PLUGIN_INTERFACES_EXTENDED.md) - **Розширені інтерфейси**
**Для кого:** Архітектори, розробники плагінів, технічні ліди  
**Що містить:**
- Повний список інтерфейсів для ВСЬОГО заліза (не тільки сенсори!)
- **ISensorPlugin** - 15+ типів сенсорів (вага, індуктивність, магнетизм, діаметр, звук...)
- **IDisplayPlugin** - підтримка різних екранів (ST7789V2, ILI9341, OLED)
- **IInputPlugin** - клавіатури, тачскрін, енкодери
- **IAudioPlugin** - аудіо кодеки, buzzer, мікрофони
- **IConnectivityPlugin**, **IPowerPlugin**, **IPeripheralPlugin**
- Приклади реальних плагінів для кожного типу
- Комплексна система ідентифікації монет (вага + індуктивність + магнетизм + діаметр)

**Прочитати першим якщо ви:** Створюєте плагін для нестандартного заліза, хочете зрозуміти повний scope системи

---

### 5. 🏥 [PLUGIN_DIAGNOSTICS.md](./PLUGIN_DIAGNOSTICS.md) - **Система self-diagnostics**
**Для кого:** Розробники плагінів, DevOps, QA тестувальники  
**Що містить:**
- **КРИТИЧНО:** Чому базові `canInitialize()` та `isReady()` недостатні
- **HealthStatus enum** - 15+ детальних станів (OK, DEGRADED, TIMEOUT, NOT_FOUND, SENSOR_FAULT...)
- **Коди помилок** - точна діагностика "ЩО не так" (не просто "не працює")
- **Self-test** - автоматична перевірка при старті (Device ID, R/W test, stability)
- **Health monitoring** - періодична перевірка стану в loop()
- **Автоматичне відновлення** - restart при TIMEOUT/COMM_ERROR
- **Статистика** - success rate, failed reads, degradation detection
- **Production-ready features** - crash reports, remote diagnostics, predictive maintenance
- Повний приклад LDC1101 з діагностикою

**Прочитати першим якщо ви:** Створюєте production систему, потрібна надійність 99.9%, хочете зрозуміти "чому плагін не працює"

---

### 6. ⚖️ [PLUGIN_CONTRACT.md](./PLUGIN_CONTRACT.md) - **Контракт система ↔ плагін**
**Для кого:** Розробники плагінів (ОБОВ'ЯЗКОВО), архітектори, QA тестувальники  
**Що містить:**
- **КРИТИЧНО:** Формальний контракт між системою і плагіном
- **Що система гарантує плагіну:**
  - Порядок викликів методів (canInitialize → initialize → update → shutdown)
  - Wire та SPI вже ініціалізовані (❌ заборонено викликати Wire.begin() в плагіні!)
  - PluginContext з доступом до ресурсів (I2C, SPI, config, logger)
  - Thread safety гарантії (update() single-threaded, read() може бути з будь-якого task)
  - Memory management (система керує lifecycle плагіна)
- **Що плагін зобов'язаний гарантувати системі:**
  - Performance обмеження (update() max 10ms, read() max 5ms)
  - Thread safety (read() має бути thread-safe через mutex)
  - No exceptions (ESP32 не має повноцінної підтримки exceptions)
  - Коректний shutdown() навіть якщо initialize() провалився
  - Memory обмеження (max 8KB RAM на плагін)
  - Валідний HealthStatus завжди
- **Санкції за порушення:** що станеться якщо не дотриматись (crash, corrupted data, watchdog reset)
- **Checklist перед публікацією** - як перевірити дотримання контракту
- **Повний приклад** правильного плагіна з mutex та діагностикою
- **Versioning** - як змінюється контракт (patch/minor/major)

**Прочитати першим якщо ви:** Створюєте свій перший плагін (ОБОВ'ЯЗКОВО перед кодуванням!), хочете зрозуміти "що я повинен/можу/не можу робити"

---

### 7. 📋 [LOGGER_ARCHITECTURE.md](./LOGGER_ARCHITECTURE.md) - **Архітектура сервісу логування**
**Для кого:** Розробники (ОБОВ'ЯЗКОВО перед імплементацією логера), архітектори  
**Що містить:**
- Logger — перший сервіс що ініціалізується (до Wire, SPI, плагінів)
- `dispatch()` зі stack-local буфером — без heap allocation, без deadlock
- **ILogTransport** контракт: `write()` НІКОЛИ не викликає Logger методи
- 5 транспортів: Serial (sync), WebSocket (async), RingBuffer (in-memory), SD (async + spiMutex), BLE (async)
- Thread Safety: mutex з 5ms timeout + drop замість `portMAX_DELAY`
- SDTransport: `spiMutex` обов'язковий — SD і LDC1101 на одній SPI шині (Cardputer-Adv)
- Повний розподіл пам'яті (байт по байту), logger.json конфігурація
- `main.cpp` ініціалізація з `PluginContext` — Logger підключається до системи плагінів
- **Порядок імплементації:** Фаза 1 (Serial + RingBuffer, 1 день) → Фаза 2 (WS + SD) → Фаза 3 (BLE)

**Прочитати першим якщо ви:** Починаєте імплементацію Logger, хочете зрозуміти як замінити всі `Serial.printf()` на `ctx->log->`

---

## 🎯 Швидкий старт: Що читати?

### Якщо ви **Менеджер проекту:**
1. [COMPARISON.md](./COMPARISON.md) - економіка і переваги (10 хв)
2. [PLUGIN_ARCHITECTURE.md](./PLUGIN_ARCHITECTURE.md) - розділ "Висновок" (5 хв)

**Час:** 15 хвилин  
**Результат:** Розумієте чому Plugin System економить 89% часу

---

### Якщо ви **Розробник** (хочете додати датчик):
1. **[PLUGIN_CONTRACT.md](./PLUGIN_CONTRACT.md)** - ОБОВ'ЯЗКОВО! Контракт система ↔ плагін (15 хв)
2. [CREATING_PLUGINS.md](./CREATING_PLUGINS.md) - розділ "Quick Start" (15 хв)
3. [PLUGIN_DIAGNOSTICS.md](./PLUGIN_DIAGNOSTICS.md) - як додати self-test (10 хв)
4. [PLUGIN_INTERFACES_EXTENDED.md](./PLUGIN_INTERFACES_EXTENDED.md) - типи сенсорів (5 хв)
5. [LOGGER_ARCHITECTURE.md](./LOGGER_ARCHITECTURE.md) - як використовувати `ctx->log->` (10 хв)
6. Практика: створити свій плагін з діагностикою (45 хв)

**Час:** 1 година 40 хвилин  
**Результат:** Ваш перший production-ready плагін з self-diagnostics що дотримується контракту

---

### Якщо ви **QA/DevOps** (потрібна надійність):
1. [PLUGIN_CONTRACT.md](./PLUGIN_CONTRACT.md) - контракт, санкції (10 хв)
2. [PLUGIN_DIAGNOSTICS.md](./PLUGIN_DIAGNOSTICS.md) - повний документ (20 хв)
3. [PLUGIN_ARCHITECTURE.md](./PLUGIN_ARCHITECTURE.md) - розділ “PluginSystem” (10 хв)
4. Тестування: запустити diagnostics екран

**Час:** 30 хвилин  
**Результат:** Розумієте як діагностувати проблеми, читати логи, налаштувати моніторинг

---

### Якщо ви **Архітектор** (проектуєте систему):
1. [PLUGIN_ARCHITECTURE.md](./PLUGIN_ARCHITECTURE.md) - повний документ (30 хв)
2. [PLUGIN_CONTRACT.md](./PLUGIN_CONTRACT.md) - контракт і гарантії (15 хв)
3. [CREATING_PLUGINS.md](./CREATING_PLUGINS.md) - API reference (15 хв)
4. [COMPARISON.md](./COMPARISON.md) - як пояснити команді (10 хв)

**Час:** 1 година  
**Результат:** Повне розуміння архітектури + можете пояснити команді

---

## 🔑 Ключові концепції

### 1. Plugin System
```
Плагін = окрема папка з кодом датчика/модуля
Додавання плагіна = створити папку + 1 рядок у конфігу
НЕ треба редагувати main.cpp або інші файли!
```

### 2. Configuration-Driven
```
Вся поведінка системи керується через JSON файли
Змінити hardware = змінити 1 строку в config.json
Система адаптується автоматично
```

### 3. Hardware Abstraction
```
Код не знає на якому hardware він працює
Всі специфічні деталі в JSON профілях
Легко мігрувати між платформами
```

### 4. Interface-Based
```
ISensorPlugin, IIMUPlugin, IStoragePlugin
Всі плагіни реалізують стандартні інтерфейси
Легко замінити одну реалізацію на іншу
```

---

## 📖 Глосарій

**Plugin (Плагін)** - незалежний модуль коду в окремій папці, реалізує специфічну функціональність (датчик, сховище, IMU, тощо)

**Plugin System** - ядро системи, яке завантажує та управляє плагінами

**Hardware Profile** - JSON файл з описом конкретної плати (піни, периферія, можливості)

**Configuration-Driven** - підхід коли поведінка системи визначається конфігураційними файлами, не хардкод

**Service Locator** — ⚠️ антипатерн в цьому проекті. Замінений `PluginContext` (DI). Описаний в глосарії лише для розуміння чому від нього відмовились.

**Dependency Injection** - паттерн передачі залежностей в об'єкти (не створюють самі)

**Graceful Degradation** - здатність системи працювати навіть якщо частина hardware не доступна

**Interface** - абстракція (контракт) який визначає які методи повинен мати клас

---

## 🎨 Діаграми

### Архітектура високого рівня

```
┌─────────────────────────────────────────┐
│           main.cpp (Bootloader)         │
│  ├─ Читає config.json                   │
│  ├─ Завантажує плагіни                  │
│  └─ Запускає систему                    │
└─────────────────────────────────────────┘
                  ↓
┌─────────────────────────────────────────┐
│         Plugin System (Core)            │
│  ├─ PluginRegistry                      │
│  ├─ PluginLoader                        │
│  └─ ConfigManager                       │
└─────────────────────────────────────────┘
                  ↓
┌─────────────────────────────────────────┐
│           Plugins (Модулі)              │
│  ├─ LDC1101Plugin                       │
│  ├─ BMI270Plugin                        │
│  ├─ SDCardPlugin                        │
│  └─ [Ваш плагін]                        │
└─────────────────────────────────────────┘
```

### Життєвий цикл плагіна

```
         canInitialize()
┌──────┐       ↓        ┌─────────┐
│ New  │ ─────────────→ │ Testing │
└──────┘                └─────────┘
                             ↓ (hardware OK)
                        initialize()
                             ↓
                        ┌────────┐
                    ┌──→│ Ready  │←──┐
                    │   └────────┘   │
                    │       ↓         │
                    │   update()      │
                    └─────────────────┘
                             ↓
                        shutdown()
                             ↓
                        ┌────────┐
                        │ Closed │
                        └────────┘
```

---

## 🛠️ Інструменти розробки

### Рекомендовані IDE:
- **VS Code** + PlatformIO (primary)
- Arduino IDE 2.0 (для простих плагінів)
- CLion (для C++ гуру)

### Debug tools:
- Serial Monitor (115200 baud)
- PlatformIO Debugger (ESP32-S3 JTAG)
- Logic Analyzer (для I2C/SPI)

### Testing:
- Unity Test Framework (unit tests)
- Hardware-in-the-loop (HIL)
- Mock plugins (без реального hardware)

---

## 📞 Підтримка

**Питання по архітектурі?**
- 💬 [GitHub Discussions](https://github.com/xkachya/CoinTrace/discussions) - обговорення архітектури
- 📝 [GitHub Issues](https://github.com/xkachya/CoinTrace/issues) - баги та пропозиції
- 📖 [Main README](../../README.md) - загальна документація

**Баги в документації?**
- 🐛 [GitHub Issues](https://github.com/xkachya/CoinTrace/issues)
- ✏️ [Edit on GitHub](https://github.com/xkachya/CoinTrace/edit/main/docs/architecture/)

**Хочете контрибутити?**
- 🤝 [GitHub Issues](https://github.com/xkachya/CoinTrace/issues) - знайти задачі
- 📖 [README.md](../../README.md) - як почати

---

## 📜 Версіонування документації

| Версія | Дата | Зміни |
|--------|------|-------|
| 1.0.0 | 2026-03-09 | Початкова версія архітектури |
| | | - Plugin System базова концепція |
| | | - Configuration-Driven підхід |
| | | - Hardware Abstraction Layer |
| 1.1.0 | 2026-03-10 | Додано `PLUGIN_CONTRACT.md` |
| | | - Формальний контракт система ↔ плагін |
| | | - `PluginContext`, Thread Safety, Memory limits |
| | | - Переведено `initialize(PluginContext* ctx)` |

---

## 📚 Додаткові ресурси

### Patterns & Best Practices:
- [Plugin Architecture Pattern](https://en.wikipedia.org/wiki/Plug-in_(computing))
- [Service Locator Pattern](https://martinfowler.com/articles/injection.html)
- [Dependency Injection](https://www.martinfowler.com/articles/injection.html)
- [Interface-Based Programming](https://en.wikipedia.org/wiki/Interface-based_programming)

### ESP32 Development:
- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/)
- [PlatformIO Docs](https://docs.platformio.org/)
- [M5Stack Docs](https://docs.m5stack.com/)

### C++ Best Practices:
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/)
- [Modern C++ Features](https://github.com/AnthonyCalandra/modern-cpp-features)

---

**Статус проекту:** 📝 Архітектура запроектована, очікує імплементації  
**Останнє оновлення:** 9 березня 2026
