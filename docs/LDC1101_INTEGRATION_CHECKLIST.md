# LDC1101 Integration Checklist

Дата: 2026-03-19

Мета: покрокова, перевірена інструкція для безпечного підключення та валідації LDC1101 до CoinTrace.

Короткий висновок
- Програмна частина (`LDC1101Plugin`) реалізована в `lib/LDC1101Plugin/src/LDC1101Plugin.h`.
- Потрібне фізичне підключення платки сенсора та верифікація RP_SET / RESP_TIME на реальному обладнанні.

Передумови
- Мати під рукою: USB кабель, серійний доступ до пристрою, мультиметр, тестову монету (еталон), тепло/холодні умови для перевірки drift.
- Скомпільований і зафлешений firmware (PlatformIO `cointrace-dev` environment).

1) Аппаратне підключення
- Підключіть LDC1101 breakout: VCC → 3.3V, GND → GND, SCK → MCU SCK, MOSI → MCU MOSI, MISO → MCU MISO, CS → GPIO, загальний GND.
- Перевірте напруги 3.3V і відсутність коротких замикань мультиметром.

2) Flash firmware та відкриття логів
- Flash: `pio run -e cointrace-dev -t upload`
- Серіал монітор: `pio device monitor -e cointrace-dev`

3) Базова перевірка зв'язку
- У серіал-лозі шукайте: `Ready. CS=%d, RESP_TIME=0x%02X, RP_SET=0x%02X`.
- Curl статус системи:
```sh
curl "http://<device_ip>/api/v1/status"
```
- Лог: відсутність помилок SPI/`CHIP_ID` mismatch.

4) Self-test / CHIP_ID
- У серіалі або через вбудовані команди (CLI/API) запустіть self-test (якщо API підтримує):
```sh
# перегляньте останні логи
curl "http://<device_ip>/api/v1/log?n=200&level=INFO"
```
- Очікуваний рядок: `CHIP_ID OK (0xD4)` та `Self-test PASSED`.
- Якщо отримуєте `CHIP_ID mismatch` або `POR timeout` — перевірити wiring, CS pin та живлення.

5) Перевірка осциляції (NO_OSC)
- У логах або при `runSelfTest()` сигналізується `NO_SENSOR_OSC` / `Coil not oscillating`.
- Якщо це трапляється — перевірте котушку/схему і RP_SET (має задовольняти §9.1.4: `RPD∞ ≤ RPMAX ≤ 2×RPD∞`).

6) Калібрування
- Запустіть калібрування (через API, CLI або trigger в коді):
```sh
curl -X POST "http://<device_ip>/api/v1/calibrate"
```
- Очікування в логах: `Calibration OK. Baseline RP=XXXX (N samples)`.
- Критерій: `calibrationRpBaseline_ > 100 && < 60000`.

7) Перевірка детекції монети
- Покладіть еталонну монету на котушку. Стежте логи: очікується `Coin PRESENT (RP=..., baseline=..., ratio=...)`.
- Зняти монету — очікується `Coin REMOVED` (переходить на один цикл update()).

8) Статистика та health
- Викликати діагностику / статистику (якщо є API):
```sh
curl "http://<device_ip>/api/v1/sensor/state"
curl "http://<device_ip>/api/v1/log?n=200&level=DEBUG"
```
- Переконатися: `successRate >= 90%` у нормальних умовах, `getHealthStatus()` → `OK` або `OK_WITH_WARNINGS`.

9) Верифікація TC1/TC2 по TI Datasheet (обов'язково при новій котушці)

> ⚠️ **КРИТИЧНО:** MikroE SDK/приклади для MIKROE-3240 мають TC1=0x1F (τ=15.8 ns) та TC2=0x3F (τ=91.5 ns) — *некоректні* MikroE legacy defaults (×52 та ×11 помилка). Це спричиняє SAT-lock артефакти: rp_raw=39321/46811/52428 з σ=0.

Розрахунок по TI §9.1.5/9.1.6:
```
τ₁_target = 0.75026 / fSENSOR_baseline
τ₂_target = 1.0 / (RPMIN × CSENSOR)
```
Для MIKROE-3240 (CSENSOR=330 pF, fSENSOR=811 kHz, RPMIN=1.5 kΩ):
- TC1=0xD5 (τ₁=893 ns), TC2=0xFE (τ₂=1039 ns)

Конфіг: встановити `tc1_val` та `tc2_val` в `data/plugins/ldc1101.json`.
Flash обов'язково двома кроками:
```sh
pio run -e cointrace-dev -t upload       # firmware
pio run -e uploadfs-sys -t uploadfs      # LittleFS (ldc1101.json)
```
Перевірити в boot log: `TC1=0xD5, TC2=0xFE` та відсутність SAT-lock у вимірах.
Деталі: `docs/architecture/LDC1101_ARCHITECTURE.md` ADR-LDC-002, `docs/lessons-learned.md` 2026-04-01.

10) Оптимізація RP_SET та RESP_TIME
- Якщо `NO_OSC` або погана якість — змініть `ldc1101.rp_set` у конфігу та повторіть кроки 3–8.
- RP_SET має задовольняти §9.1.4: `RPD∞ ≤ RPMAX ≤ 2×RPD∞`. Поточне значення MIKROE-3240: `rp_set=54` (0x36, RPMAX=12 kΩ).
- Для вищого SNR використовувати `resp_time_bits=0x07` (default в коді). Для вищого throughput — зменшити (тестувати SNR).

11) Температурний тест
- Обов'язково перевірити drift baseline при зміні температури. Якщо drift значний, планувати періодичну перекалібровку або temperature compensation.

Типові лог-рядки для верифікації
- `Ready. CS=%d, RESP_TIME=0x%02X, RP_SET=0x%02X`
- `CHIP_ID OK (0xD4)` / `CHIP_ID mismatch: expected 0xD4, got 0x%02X`
- `Coil not oscillating — check wiring or RP_SET`
- `Calibration OK. Baseline RP=%.0f (%u samples)`
- `Coin PRESENT (RP=%.0f, baseline=%.0f, ratio=%.2f)`

Базові діагностичні команди (копіпаст):
```sh
# Системний статус
curl "http://<device_ip>/api/v1/status"

# Останні логи
curl "http://<device_ip>/api/v1/log?n=200&level=DEBUG"

# Запустити калібрування
curl -X POST "http://<device_ip>/api/v1/calibrate"

# Перевірити сенсорний стан
curl "http://<device_ip>/api/v1/sensor/state"

# UART монітор (PlatformIO)
pio device monitor -e cointrace-dev
```

Потенційні наступні кроки (рекомендації)
- Автоматизувати smoke tests: `runSelfTest()` x3, `calibrate()`, `coin detect` sequence.
- Створити невеликий Python-скрипт у `scripts/` для автоматичної перевірки HW (serial+HTTP) та генерації report.
- Документувати пікові значення RP_SET для кожної використовуваної котушки (N = 3) у `docs/hardware/`.

Контакти та посилання
- Архітектура: [docs/architecture/LDC1101_ARCHITECTURE.md](docs/architecture/LDC1101_ARCHITECTURE.md#L1)
- Реалізація плагіна: [lib/LDC1101Plugin/src/LDC1101Plugin.h](lib/LDC1101Plugin/src/LDC1101Plugin.h#L1)

---
Файл згенеровано автоматично інструментом інтеграційного аудиту CoinTrace.
