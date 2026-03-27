# Discovery Mode Specification — CoinTrace

**Версія:** 1.0.0  
**Дата:** 2026-03-27  
**Статус:** Специфікація — реалізація в Wave 9 Sprint 2  
**Wave:** 9 "Measurement Science"  
**Predecessor:** Wave 8 C-5 Deep Analysis Audit (2026-03-27)  
**Cross-ref:** `LDC1101_ARCHITECTURE.md v1.3.3`, `WAVE8_COMPLETION_WAVE9_DISCOVERY_PLAN.md`, `FINGERPRINT_DB_ARCHITECTURE.md`, `QUICK_SCREEN_SPEC.md`, `METAL_MATCHER_ARCHITECTURE.md`

---

## Зміст

1. [Мета і мотивація](#1-мета-і-мотивація)
2. [Scope і обмеження](#2-scope-і-обмеження)
3. [Активація та конфігурація](#3-активація-та-конфігурація)
4. [Multi-sample capture (D-1)](#4-multi-sample-capture-d-1)
5. [LHR continuous mode (D-2)](#5-lhr-continuous-mode-d-2)
6. [Raw dump to SD (D-3)](#6-raw-dump-to-sd-d-3)
7. [Derived discovery parameters](#7-derived-discovery-parameters)
8. [Інтеграція зі state machine](#8-інтеграція-зі-state-machine)
9. [RAM та timing budget](#9-ram-та-timing-budget)
10. [Display під час capture](#10-display-під-час-capture)
11. [Зміни у файлах](#11-зміни-у-файлах)
12. [HW Session C-6 protocol](#12-hw-session-c-6-protocol)
13. [Offline analysis pipeline](#13-offline-analysis-pipeline)
14. [Архітектурні рішення (ADR)](#14-архітектурні-рішення-adr)
15. [Open questions](#15-open-questions)

---

## 1. Мета і мотивація

### Проблема

C-5 Deep Analysis Audit (2026-03-27) виявив три структурні обмеження поточного вектора `[dRp1_n, k1, k2, slope, dL1_n]`:

| # | Проблема | Масштаб |
|---|----------|---------|
| 1 | rp[2] saturation (0xC000) для 4/5 металів → k2 та slope несуть артефакт | 80% вимірів |
| 2 | dL1_n не розрізняє ферромагнетик (XFE) через скін-ефект Cu при 909 кГц | L-канал фактично "сліпий" |
| 3 | XCU ↔ XZNNIP margin = 0.51σ → ризик при температурному drift > 10°C | Найближча пара |

Ефективна розмірність вектора — 2 (dRp1_n + k1), а не заявлені 5.

### Рішення

Discovery Mode — **паралельний режим збору даних**, який:

1. Збирає **N~900 samples per step** замість одного (multi-sample capture) → статистика + noise profiling
2. Активує **LHR 24-bit continuous** → precise fSENSOR per step → Δf як потенційний новий компонент вектора
3. Зберігає **повний raw dump на SD** для offline аналізу → обґрунтоване рішення про Vector v2

Discovery Mode **не замінює** production measurement flow. Обидва працюють паралельно: production vector обчислюється і зберігається як раніше; discovery raw dump — додатковий файл на SD.

### Чому зараз

Проект на етапі proof-of-concept з 5 монетами. Розширення до 20+ монет без розуміння фізичних обмежень сенсора призведе до невирішуваних проблем класифікації. Discovery Session — одноразова інвестиція (~3 дні), яка формує data-driven foundation для всіх подальших рішень.

---

## 2. Scope і обмеження

### Що Discovery Mode робить

| Функція | Деталі |
|---------|--------|
| Multi-sample capture per step | N~900 пар {RP, L} за 2 секунди → median, σ, min, max |
| LHR continuous accumulation | ~30 LHR readings per step → precise fSENSOR |
| Raw dump JSON на SD | Повна статистика + production vector + derived discovery parameters |
| Progress bar на дисплеі | Countdown bar під час capture window |

### Що Discovery Mode НЕ робить

| Обмеження | Причина |
|-----------|---------|
| Не змінює production vector | Vector v2 — рішення Sprint 3 на основі офлайн аналізу |
| Не змінює MeasState enum | Capture — internal substage, не новий стан |
| Не потребує нових SPI пристроїв | Використовує тільки існуючий LDC1101 |
| Не змінює RESP_TIME runtime | Capture при поточному RESP_TIME=6144 (ADR-D5) |
| Не стрімить raw samples по WebSocket | Raw dump — SD-only; WebSocket отримує production result |
| Не працює без SD карти | Raw dump потребує SD; без SD — Discovery silently disabled |

---

## 3. Активація та конфігурація

### Config: `data/plugins/ldc1101.json`

```json
{
    "name": "LDC1101",
    "enabled": true,
    "spi_cs_pin": 5,
    "resp_time_bits": 7,
    "rp_set": 38,
    "clkin_freq_hz": 16000000,
    "coin_detect_threshold": 0.85,
    "coin_release_threshold": 0.92,
    "detect_debounce_n": 5,
    "release_debounce_m": 3,
    "lhr_rcount": 65535,
    "lhr_continuous": true,
    "high_q_sensor": false,
    "cal_max_age_sec": 120,
    "cal_age_warning_sec": 1800,

    "discovery_enabled": true,
    "discovery_capture_ms": 2000,
    "discovery_settle_ms": 300
}
```

**Нові поля:**

| Поле | Тип | Default | Опис |
|------|-----|---------|------|
| `discovery_enabled` | bool | `false` | Головний вимикач Discovery Mode. `false` = production behavior (single read per step). |
| `discovery_capture_ms` | uint32 | `2000` | Тривалість capture window per step (мс). Рекомендовано: 2000–3000 мс. |
| `discovery_settle_ms` | uint32 | `300` | Час очікування після ENTER перед початком capture (settling time). Перші ~300 мс після натискання — нестабільні через вібрацію покладання/зняття spacer. |
| `lhr_continuous` | bool | `true` | Існуюче поле (LDC1101_ARCHITECTURE.md §7). Для Discovery — обов'язково `true`. |

### Активація при boot

```cpp
// src/main.cpp — setup(), після gLDC->initialize() та gLDC->calibrate()
const bool discoveryEnabled = gCtx.config->getBool("ldc1101", "discovery_enabled", false);
const bool sdReady = gSD.isMounted();

if (discoveryEnabled && !sdReady) {
    gCtx.log->warning("Discovery", "discovery_enabled=true but SD not mounted — Discovery disabled");
}

const bool discoveryActive = discoveryEnabled && sdReady;
```

Discovery Mode деактивується мовчки якщо SD не примонтована — production flow не порушується.

---

## 4. Multi-sample capture (D-1)

### Принцип

Замість одного `readMeasurementBurst()` per step — цикл збору N samples протягом `discovery_capture_ms`, з подальшою статистичною обробкою.

### Timing

При RESP_TIME=6144 cycles, fSENSOR=909.2 kHz:

```
Conversion time = 6144 / (3 × 909200) = 2.253 мс
update() polling @ 50 Hz = 20 мс between reads
Effective sample rate ≈ 1 / 2.253 мс ≈ 443 SPS (sensor limit)
But update() polls at 50 Hz → actual read rate ≈ 50 SPS via update()

Direct read in tight loop (bypass update()): 
  delay(convTimeMs + 1) ≈ 3.3 мс per sample
  2000 мс / 3.3 мс ≈ 606 samples (tight loop)
  
Conservative with delay(5): 
  2000 мс / 5 мс ≈ 400 samples
```

> **ADR-D5:** Discovery capture виконується у tight loop з `delay(convTimeMs() + 2)`, не через `update()` polling. Причина: `update()` при 50 Hz дає лише ~100 samples за 2 секунди; tight loop дає ~400-600. Compromise: `delay()` замість busy-wait щоб watchdog не спрацював.

### CaptureStats struct

```cpp
// src/main.cpp або lib/StorageManager/src/DiscoveryCapture.h

struct CaptureStats {
    // ── RP statistics ─────────────────────────────────────────────
    double   rpSum;                  // running sum (double для precision при N>500)
    double   rpSumSq;               // running sum of squares → σ
    uint16_t rpMin;
    uint16_t rpMax;
    uint16_t rpReservoir[64];       // reservoir sampling → approximate median

    // ── L statistics ──────────────────────────────────────────────
    double   lSum;
    double   lSumSq;
    uint16_t lMin;
    uint16_t lMax;
    uint16_t lReservoir[64];

    // ── LHR statistics (24-bit, async) ────────────────────────────
    uint64_t lhrSum;                // running sum (uint64 для 24-bit × ~30)
    uint32_t lhrMin;
    uint32_t lhrMax;
    uint16_t lhrCount;

    // ── Counters ──────────────────────────────────────────────────
    uint16_t count;                 // successful RP+L reads
    uint16_t failCount;             // failed reads (rpRaw=0 or 0xFFFF)

    // ── Derived (обчислюються після capture loop) ─────────────────
    float    rpMedian;              // з reservoir (sorted, middle element)
    float    rpMean;                // rpSum / count
    float    rpSigma;              // sqrt(rpSumSq/count - rpMean²)
    float    lMedian;
    float    lMean;
    float    lSigma;
    float    lhrMean;               // lhrSum / lhrCount
    float    fSensorHz;             // derived: fCLKIN × LHR_DATA / 2²⁴

    void reset() {
        memset(this, 0, sizeof(*this));
        rpMin = 65535; lMin = 65535; lhrMin = 0xFFFFFF;
    }

    void finalize(uint32_t fClkinHz) {
        if (count == 0) return;
        rpMean  = (float)(rpSum / count);
        rpSigma = sqrtf((float)(rpSumSq / count) - rpMean * rpMean);
        lMean   = (float)(lSum / count);
        lSigma  = sqrtf((float)(lSumSq / count) - lMean * lMean);

        // Reservoir → median (sort + middle)
        uint16_t n = (count < 64) ? count : 64;
        // Simple insertion sort (N≤64, ~2000 comparisons = negligible)
        for (uint16_t i = 1; i < n; i++) {
            uint16_t key = rpReservoir[i];
            int j = i - 1;
            while (j >= 0 && rpReservoir[j] > key) { rpReservoir[j+1] = rpReservoir[j]; j--; }
            rpReservoir[j+1] = key;
        }
        rpMedian = rpReservoir[n / 2];
        // Same for L
        for (uint16_t i = 1; i < n; i++) {
            uint16_t key = lReservoir[i];
            int j = i - 1;
            while (j >= 0 && lReservoir[j] > key) { lReservoir[j+1] = lReservoir[j]; j--; }
            lReservoir[j+1] = key;
        }
        lMedian = lReservoir[n / 2];

        if (lhrCount > 0) {
            lhrMean    = (float)((double)lhrSum / lhrCount);
            fSensorHz  = lhrMean * 2.0f * fClkinHz / 16777216.0f;  // Eq.11: LHR_DATA × 2 × fCLKIN / 2²⁴
        }
    }
};
```

**sizeof(CaptureStats):** 2×(8+8+2+2+128) + (8+4+4+2) + (2+2) + (4×9) ≈ **348 bytes** на стеку. Безпечно для 8 KB main task stack.

### Capture loop

```cpp
// Викликається з measurement state machine при discovery_enabled=true
// Контекст: main task loop(), після натискання ENTER на конкретному step
// Blocking: discovery_settle_ms + discovery_capture_ms ≈ 2.3 секунди

CaptureStats discoveryCaptureStep(LDC1101Plugin* ldc, uint32_t settleMs, uint32_t captureMs) {
    CaptureStats s;
    s.reset();

    // ── Phase 1: Settling (відкидаємо нестабільні зразки) ──────────
    delay(settleMs);

    // ── Phase 2: Data collection ──────────────────────────────────
    const uint32_t start = millis();
    const uint32_t convDelay = ldc->convTimeMs() + 2;  // margin 2ms

    while (millis() - start < captureMs) {
        // ── RP+L read (direct SPI, bypass update() для max throughput) ──
        uint16_t rp = 0, l = 0;

        // Check STATUS.DRDYB first
        uint8_t status = ldc->spiReadPublic(0x20);  // REG_STATUS
        if (status & 0x40) {  // DRDYB=1 → not ready
            delay(1);
            continue;
        }

        if (ldc->readMeasurementBurstPublic(rp, l) && rp > 0 && rp < 0xFFFF) {
            // Accumulate RP
            s.rpSum   += rp;
            s.rpSumSq += (double)rp * rp;
            if (rp < s.rpMin) s.rpMin = rp;
            if (rp > s.rpMax) s.rpMax = rp;

            // Accumulate L
            s.lSum   += l;
            s.lSumSq += (double)l * l;
            if (l < s.lMin) s.lMin = l;
            if (l > s.lMax) s.lMax = l;

            // Reservoir sampling (Vitter Algorithm R)
            if (s.count < 64) {
                s.rpReservoir[s.count] = rp;
                s.lReservoir[s.count]  = l;
            } else {
                uint32_t j = esp_random() % (s.count + 1);
                if (j < 64) {
                    s.rpReservoir[j] = rp;
                    s.lReservoir[j]  = l;
                }
            }
            s.count++;
        } else {
            s.failCount++;
        }

        // ── LHR read (async — check if ready, don't wait) ────────
        if (ldc->isLDataValid()) {
            uint8_t lhrStatus = ldc->spiReadPublic(0x3B);  // REG_LHR_STATUS
            if (!(lhrStatus & 0x01)) {  // LHR_DRDY=0 → data ready
                uint32_t lhrRaw = ldc->readLHRBurstPublic();
                if (lhrRaw > 0 && lhrRaw < 0xFFFFFF) {
                    s.lhrSum += lhrRaw;
                    if (lhrRaw < s.lhrMin) s.lhrMin = lhrRaw;
                    if (lhrRaw > s.lhrMax) s.lhrMax = lhrRaw;
                    s.lhrCount++;
                }
            }
        }

        delay(convDelay);  // Wait for next conversion + yield to watchdog
    }

    // ── Phase 3: Finalize statistics ──────────────────────────────
    s.finalize(ldc->getClkinFreqHz());

    return s;
}
```

### Public SPI access для Discovery

Discovery capture потребує прямого доступу до SPI reads, що зараз є `private` в LDC1101Plugin. Варіанти:

| Варіант | Плюси | Мінуси |
|---------|-------|--------|
| A. Friend function | Мінімальний API change | Зламує інкапсуляцію |
| **B. Public convenience methods** | Чистий API, documented | Розширює public surface |
| C. Capture всередині LDC1101Plugin | Повна інкапсуляція | Плагін отримує knowledge про Discovery |

**Рішення: Варіант B** — додати обмежений набір public methods:

```cpp
// LDC1101Plugin.h — Discovery API extension
#ifdef DISCOVERY_MODE
    uint8_t  spiReadPublic(uint8_t reg);
    bool     readMeasurementBurstPublic(uint16_t& rp, uint16_t& l);
    uint32_t readLHRBurstPublic();
    uint32_t getClkinFreqHz() const { return clkinFreqHz; }
    uint16_t convTimeMs() const;  // вже існує (L-1)
#endif
```

`#ifdef DISCOVERY_MODE` — compile-time guard. В `platformio.ini`:

```ini
build_flags = 
    -D DISCOVERY_MODE    ; Enable for Discovery builds; remove for release
```

> **ADR-D6:** Discovery SPI methods захищені `#ifdef DISCOVERY_MODE`. В release builds вони відсутні — zero Flash overhead, zero public API pollution. Для development — один build flag.

---

## 5. LHR continuous mode (D-2)

### Поточний стан

`lhr_continuous = false` (default). LHR знімається лише при `calibrate()`. В `update()` є TODO коментар (LDC1101_ARCHITECTURE.md §10 задача 9):

```cpp
// TODO (v1.5): lhrContinuous=true path (ADR-LHR-001, §10 задача 9)
// Конфіг `lhr_continuous` вже присутній. При true: 
//   if (!(spiRead(REG_LHR_STATUS) & 0x01)) readLHRBurst()
```

### Реалізація

Замінити TODO блок в `update()` на:

```cpp
// ── LHR continuous (D-2, ADR-LHR-001 §10.9) ──────────────────
if (lhrContinuous) {
    uint8_t lhrStat = spiRead(REG_LHR_STATUS);

    // Check error bits (datasheet §8.6.31)
    if (lhrStat & 0x1E) {  // ERR_ZC | ERR_OR | ERR_UR | ERR_OF
        // Log warning on first occurrence, don't spam
        if (!(diag.lhrErrorLogged)) {
            ctx->log->warning(getName(),
                "LHR error flags: 0x%02X (ZC=%d OR=%d UR=%d OF=%d)",
                lhrStat,
                (lhrStat >> 4) & 1, (lhrStat >> 3) & 1,
                (lhrStat >> 2) & 1, (lhrStat >> 1) & 1);
            diag.lhrErrorLogged = true;
        }
    }

    // Check LHR_DRDY (bit 0, inverted: 0=ready)
    if (!(lhrStat & 0x01)) {
        // Read 3 bytes: LSB first (datasheet §8.6.28 order requirement)
        uint8_t lsb = spiRead(REG_LHR_DATA_LSB);  // must read first
        uint8_t mid = spiRead(REG_LHR_DATA_MID);
        uint8_t msb = spiRead(REG_LHR_DATA_MSB);
        uint32_t lhrRaw = ((uint32_t)msb << 16) | ((uint32_t)mid << 8) | lsb;

        if (lhrRaw > 0 && lhrRaw < 0xFFFFFF) {
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                cached.lhrRaw   = lhrRaw;
                cached.lhrValid = true;
                xSemaphoreGive(dataMutex);
            }
        }
        diag.lhrErrorLogged = false;  // Reset error flag after successful read
    }
}
```

### Timing impact on update()

| Операція | Час | Умова |
|----------|-----|-------|
| `spiRead(REG_LHR_STATUS)` | ~5 μs | Кожен update() при lhr_continuous=true |
| `spiRead(REG_LHR_DATA_*)` × 3 | ~15 μs | Тільки коли LHR_DRDY=0 (~кожні 3-4 update() при RCOUNT=0xFFFF) |
| Mutex acquire/release | ~0.1 μs | При successful read |
| **Worst case total** | **~20 μs** | **0.1% від 20 мс update() бюджету** |

Overhead negligible. Контракт `update() < 10 мс` (PLUGIN_CONTRACT.md §1.2) залишається з 500× запасом.

### LHR error handling

Datasheet §8.6.31 LHR_STATUS містить 4 error bits:

| Bit | Назва | Що означає | Дія |
|-----|-------|------------|-----|
| 4 | ERR_ZC | Zero Count — fSENSOR занизька або sensor error | Log warning |
| 3 | ERR_OR | Over-range — fSENSOR перевищила fCLKIN | Log warning |
| 2 | ERR_UR | Under-range — LHR offset занадто великий | Log warning |
| 1 | ERR_OF | Over-flow — fSENSOR дуже близька до fCLKIN | Log warning |

Всі error bits — informational. Якщо будь-який встановлений: логувати один раз (не spam), не оновлювати кеш, чекати наступну конверсію.

### Нове поле в діагностиці

```cpp
// LDC1101Plugin.h — struct diag {}
bool lhrErrorLogged = false;   // Prevent LHR error log spam
```

---

## 6. Raw dump to SD (D-3)

### Файлова структура

```
SD:\CoinTrace\discovery\
    session_20260401_143022.json    ← один файл per session
    session_20260405_091511.json
```

Назва файлу генерується при першому вимірі сесії з поточного timestamp.

### JSON schema — session header

```json
{
    "schema_version": 1,
    "session": {
        "date": "2026-04-01T14:30:22Z",
        "protocol": "p3_MIKROE3240_b06_012mm",
        "firmware_commit": "abc1234",
        "firmware_version": "1.0.0-discovery",
        "fSensor_baseline_hz": 909200,
        "baseline_rp": 57344,
        "baseline_l": 36200,
        "baseline_lhr": 7654000,
        "config": {
            "resp_time_bits": 7,
            "rp_set": "0x26",
            "lhr_rcount": 65535,
            "lhr_continuous": true,
            "capture_ms": 2000,
            "settle_ms": 300,
            "clkin_freq_hz": 16000000
        }
    },
    "measurements": []
}
```

### JSON schema — one measurement

```json
{
    "index": 0,
    "coin_name": "American Silver Eagle 1oz",
    "metal_code": "XAG999",
    "timestamp": "2026-04-01T14:32:05Z",
    "steps": [
        {
            "step": "base_0.6mm",
            "rp_median": 38271,
            "rp_mean": 38275.4,
            "rp_sigma": 42.3,
            "rp_min": 38102,
            "rp_max": 38440,
            "rp_n": 606,
            "rp_fail": 2,
            "l_median": 36042,
            "l_mean": 36040.8,
            "l_sigma": 3.1,
            "l_min": 36031,
            "l_max": 36055,
            "lhr_mean": 7654123,
            "lhr_min": 7653980,
            "lhr_max": 7654301,
            "lhr_n": 30,
            "fSensor_hz": 909182.4
        },
        {
            "step": "addon_1.6mm",
            "rp_median": 46811,
            "...": "same fields"
        },
        {
            "step": "addon_2.6mm",
            "...": "same fields"
        },
        {
            "step": "drift_0.6mm",
            "...": "same fields"
        }
    ],
    "drift_pct": 0.35,
    "production_vector": {
        "dRp1_n": -10.675,
        "k1": 1.223,
        "k2": 1.355,
        "slope": 0.177,
        "dL1_n": -2.418
    },
    "discovery_derived": {
        "delta_f_base_hz": -18.0,
        "delta_f_1mm_hz": -12.5,
        "delta_f_2mm_hz": -4.2,
        "rp_sigma_base": 42.3,
        "rp_sigma_1mm": 28.1,
        "l_sigma_base": 3.1,
        "dRpPct_baseline": 33.2,
        "baseline_rp_session": 57344
    },
    "match_result": {
        "metal_code": "XAG999",
        "confidence": 0.94,
        "distance": 0.12,
        "algo": "FULL"
    }
}
```

### Реалізація запису

```cpp
// Після doMeasCompute() в STEP_COMPUTE, якщо discoveryActive:

void saveDiscoveryDump(
    SDCardManager& sd, SemaphoreHandle_t spiMutex,
    const char* sessionFile,
    uint16_t measIndex,
    const Measurement& m,
    const CaptureStats steps[4],    // base, 1mm, 2mm, drift
    const MatchResult& matchResult,
    float baselineRp, float baselineL, float baselineLhr,
    uint32_t fClkinHz
) {
    // ArduinoJson — DynamicJsonDocument on heap
    DynamicJsonDocument doc(3072);  // ~3 KB — sufficient for one measurement

    doc["index"] = measIndex;
    doc["coin_name"] = m.coin_name;
    doc["metal_code"] = m.metal_code;

    const char* stepNames[] = {"base_0.6mm", "addon_1.6mm", "addon_2.6mm", "drift_0.6mm"};
    JsonArray stepsArr = doc.createNestedArray("steps");

    for (int i = 0; i < 4; i++) {
        JsonObject s = stepsArr.createNestedObject();
        s["step"]      = stepNames[i];
        s["rp_median"] = steps[i].rpMedian;
        s["rp_mean"]   = round(steps[i].rpMean * 10) / 10.0;
        s["rp_sigma"]  = round(steps[i].rpSigma * 10) / 10.0;
        s["rp_min"]    = steps[i].rpMin;
        s["rp_max"]    = steps[i].rpMax;
        s["rp_n"]      = steps[i].count;
        s["rp_fail"]   = steps[i].failCount;
        s["l_median"]  = steps[i].lMedian;
        s["l_mean"]    = round(steps[i].lMean * 10) / 10.0;
        s["l_sigma"]   = round(steps[i].lSigma * 10) / 10.0;
        s["l_min"]     = steps[i].lMin;
        s["l_max"]     = steps[i].lMax;
        if (steps[i].lhrCount > 0) {
            s["lhr_mean"]    = round(steps[i].lhrMean);
            s["lhr_min"]     = steps[i].lhrMin;
            s["lhr_max"]     = steps[i].lhrMax;
            s["lhr_n"]       = steps[i].lhrCount;
            s["fSensor_hz"]  = round(steps[i].fSensorHz * 10) / 10.0;
        }
    }

    // Production vector
    JsonObject pv = doc.createNestedObject("production_vector");
    pv["dRp1_n"] = round(VectorCompute::dRp1_n(m) * 1000) / 1000.0;
    pv["k1"]     = round(VectorCompute::k1(m) * 10000) / 10000.0;
    pv["k2"]     = round(VectorCompute::k2(m) * 10000) / 10000.0;
    pv["slope"]  = round(VectorCompute::slope(m) * 10000) / 10000.0;
    pv["dL1_n"]  = round(VectorCompute::dL1_n(m) * 10000) / 10000.0;

    // Discovery derived
    JsonObject dd = doc.createNestedObject("discovery_derived");
    float baseFSensor = baselineLhr > 0
        ? baselineLhr * 2.0f * fClkinHz / 16777216.0f
        : 0.0f;
    if (steps[0].fSensorHz > 0 && baseFSensor > 0)
        dd["delta_f_base_hz"] = round((steps[0].fSensorHz - baseFSensor) * 10) / 10.0;
    dd["rp_sigma_base"]     = round(steps[0].rpSigma * 10) / 10.0;
    dd["l_sigma_base"]      = round(steps[0].lSigma * 10) / 10.0;
    dd["dRpPct_baseline"]   = (baselineRp > 1.0f)
        ? round((baselineRp - steps[0].rpMedian) / baselineRp * 1000) / 10.0
        : 0.0;
    dd["baseline_rp_session"] = round(baselineRp);

    // Match result
    JsonObject mr = doc.createNestedObject("match_result");
    mr["metal_code"]  = matchResult.metal_code;
    mr["confidence"]  = round(matchResult.confidence * 1000) / 1000.0;
    mr["distance"]    = round(matchResult.distance * 10000) / 10000.0;

    // ── Write to SD ───────────────────────────────────────────────
    if (xSemaphoreTake(spiMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        File f = SD.open(sessionFile, FILE_APPEND);
        if (f) {
            if (measIndex > 0) f.print(",\n");
            serializeJson(doc, f);
            f.close();
        }
        xSemaphoreGive(spiMutex);
    }
}
```

### RAM budget для запису

| Компонент | Розмір | Тип |
|-----------|--------|-----|
| `DynamicJsonDocument(3072)` | 3 KB | Heap (temporary) |
| `CaptureStats steps[4]` | 4 × 348 = 1392 B | Stack |
| **Total peak** | **~4.4 KB** | Heap+Stack |

Heap idle = ~30 KB → 3 KB temporary = safe (10% utilization).
Stack: 1392 B для steps array. Main task = 8 KB → використання ~17%. Якщо steps зберігаються у file-scope static (не на стеку) — 0% stack impact.

> **Рекомендація:** `static CaptureStats sDiscoverySteps[4];` — file-scope static в main.cpp. Додає ~1.4 KB до BSS (постійно), але гарантує відсутність stack overflow. BSS headroom = 118 KB → negligible.

---

## 7. Derived discovery parameters

Параметри що обчислюються з raw capture data і зберігаються в `discovery_derived`:

| Parameter | Formula | Physical meaning | Potential for vector |
|-----------|---------|------------------|---------------------|
| `delta_f_base_hz` | fSensor(coin@base) − fSensor(baseline) | Frequency shift при покладанні монети. + = L зменшилась (eddy), − = L зросла (ferro) | **HIGH** — бінарний ferro classifier |
| `delta_f_1mm_hz` | fSensor(coin@1mm) − fSensor(baseline) | Frequency shift при 1mm | Spatial decay profile |
| `rp_sigma_base` | σ(RP) при base step | Noise signature metal-specific? | **UNKNOWN** — verify empirically |
| `l_sigma_base` | σ(L_DATA) при base step | L measurement stability | Probably low utility |
| `dRpPct_baseline` | (baseline−rp_median) / baseline × 100% | Quick Screen metric | **Quick Screen Phase 2 centroid** |

### Δf як ferro discriminator — теоретична модель

```
Ag999 (σ=62, μr=1):  eddy currents dominate → L decreases → f increases → Δf > 0
Cu    (σ=58, μr=1):  similar → Δf > 0 (slightly less than Ag)
Al    (σ=37, μr=1):  weaker eddy → smaller Δf > 0
Fe    (σ=10, μr>>1): permeability dominates → L increases → f decreases → Δf < 0
Ni    (σ=14, μr≈600): strong permeability → Δf << 0
```

**Ключова гіпотеза:** Навіть якщо dL1_n (16-bit L_DATA difference) не розрізняє XFE через скін-ефект Cu покриття, **Δf через LHR 24-bit може бути достатньо чутливим** щоб вловити слабкий вплив Fe сердечника. 24-bit = 256× вища роздільність ніж 16-bit L_DATA.

Верифікація: C-6 Discovery Session з XFE монетою.

---

## 8. Інтеграція зі state machine

### Поточний flow (production, без Discovery)

```
STEP_BASE → ENTER → read rp[0], l[0] → prompt STEP_1
```

### Discovery flow

```
STEP_BASE → ENTER → settle(300ms) → capture(2000ms) → stats → read median as rp[0], l[0] → prompt STEP_1
```

**Зміна в measurement handler** (pseudo-code):

```cpp
case MeasState::STEP_BASE:
    if (enterPressed) {
        if (discoveryActive) {
            drawCaptureProgress("Capturing base...", 0);
            sDiscoverySteps[0] = discoveryCaptureStep(gLDC, settleMs, captureMs);
            sMeas.rp[0] = sDiscoverySteps[0].rpMedian;  // median → production
            sMeas.l[0]  = sDiscoverySteps[0].lMedian;
            logCaptureStats("BASE", sDiscoverySteps[0]);
        } else {
            sMeas.rp[0] = gLDC->getLiveRp();  // single read → production
            sMeas.l[0]  = gLDC->getLiveL();
        }
        sMeas.state = MeasState::STEP_1;
        drawMeasStep(sMeas);
    }
    break;
```

**Критична деталь:** Discovery capture **блокує loop()** на ~2.3 секунди per step. Це означає:

- WebSocket frames не надсилаються (~2.3 с gap per step)
- Coin state machine не оновлюється (якщо монету зняти під час capture — виявиться після)
- Watchdog: `delay()` в capture loop забезпечує yield → watchdog не спрацює

Для Discovery use case (manual, controlled) це прийнятно. Production mode (discovery_enabled=false) — zero impact.

---

## 9. RAM та timing budget

### RAM summary

| Component | Type | Size | When |
|-----------|------|------|------|
| `sDiscoverySteps[4]` (file-scope static) | BSS | 1,392 B | Постійно (при `DISCOVERY_MODE` define) |
| `DynamicJsonDocument(3072)` | Heap | 3,072 B | Тільки під час SD write (~50 мс) |
| `CaptureStats` local in capture loop | Stack | 348 B | Тільки під час capture (~2.3 с) |
| `diag.lhrErrorLogged` | BSS | 1 B | Постійно |
| **Total BSS** | | **~1,393 B** | BSS headroom 118 KB → negligible |
| **Peak heap** | | **~3 KB** | Heap idle 30 KB → 10% → safe |

### Timing per measurement (4-step cycle)

| Phase | Duration | Notes |
|-------|----------|-------|
| STEP_BASE capture | 2.3 s | settle 300ms + capture 2000ms |
| User transition | ~5 s | Place +1mm spacer, press ENTER |
| STEP_1 capture | 2.3 s | |
| User transition | ~5 s | Place +2mm spacer, press ENTER |
| STEP_3 capture | 2.3 s | |
| User transition | ~5 s | Remove spacers, press ENTER |
| STEP_DRIFT capture | 2.3 s | |
| COMPUTE + SD write | ~0.2 s | Production save + Discovery dump |
| **Total per coin** | **~24 s** | Discovery vs ~12 s production |

24 секунди на монету — прийнятно для колекціонера в discovery mode.

---

## 10. Display під час capture

### Progress bar

```
┌────────────────────────────────────────┐
│ DISCOVERY CAPTURE                      │
│ Step: BASE (0.6mm)                     │
│                                        │
│ ████████████░░░░░░░░░░  54%           │
│ N = 327 / ~606                         │
│ RP: 38271 ± 42  L: 36042 ± 3         │
│                                        │
│ LHR: 17/30  fS: 909182 Hz            │
└────────────────────────────────────────┘
```

**Оновлення:** Кожні ~100 мс (кожні ~30 samples) — partial redraw рядків N, RP, LHR.

### Реалізація

```cpp
void drawCaptureProgress(const char* stepLabel, const CaptureStats& s,
                         uint32_t elapsedMs, uint32_t totalMs) {
    const int pct = (int)(elapsedMs * 100 / totalMs);
    const int barWidth = 180;
    const int filled = barWidth * pct / 100;

    display.fillRect(0, 48, 240, 14, BLACK);
    display.fillRect(10, 50, filled, 10, GREEN);
    display.fillRect(10 + filled, 50, barWidth - filled, 10, DARKGREY);

    display.fillRect(0, 68, 240, 14, BLACK);
    display.setCursor(4, 76);
    display.setTextColor(WHITE);
    display.printf("N=%u  RP:%.0f+/-%.0f", s.count, s.rpMean, s.rpSigma);

    if (s.lhrCount > 0) {
        display.fillRect(0, 84, 240, 14, BLACK);
        display.setCursor(4, 92);
        display.printf("LHR:%u fS:%.0fHz", s.lhrCount, s.fSensorHz);
    }
}
```

> **Примітка:** Display update всередині capture loop потребує обережності — SPI bus shared з LDC1101. При Strategy A (single task) конфлікту немає, але display.fillRect() займає ~2-5 мс. Оновлювати display кожні 100 мс (не кожен sample).

---

## 11. Зміни у файлах

### Нові файли

| Файл | Scope |
|------|-------|
| `docs/architecture/DISCOVERY_MODE_SPEC.md` | Цей документ |
| `docs/architecture/WAVE9_ROADMAP.md` | Wave 9 roadmap |

### Модифікації

| Файл | Зміна |
|------|-------|
| `lib/LDC1101Plugin/src/LDC1101Plugin.h` | LHR continuous in update(); `#ifdef DISCOVERY_MODE` public SPI methods; `diag.lhrErrorLogged` |
| `lib/LDC1101Plugin/src/LDC1101Plugin.cpp` | LHR read implementation; Discovery SPI methods |
| `src/main.cpp` | `discoveryCaptureStep()`, `saveDiscoveryDump()`, `drawCaptureProgress()`, `sDiscoverySteps[4]`, Discovery integration in STEP handlers |
| `data/plugins/ldc1101.json` | `lhr_continuous`, `discovery_enabled`, `discovery_capture_ms`, `discovery_settle_ms` |
| `platformio.ini` | `-D DISCOVERY_MODE` build flag |
| `LDC1101_ARCHITECTURE.md` | §10: задачі 9, 11, 12 → status update |

---

## 12. HW Session C-6 protocol

### Checklist

- [ ] `ldc1101.json`: `discovery_enabled: true`, `lhr_continuous: true`
- [ ] SD card mounted, `CoinTrace/discovery/` directory exists
- [ ] Serial monitor open (COM port, 115200)
- [ ] Firmware with `-D DISCOVERY_MODE` flashed
- [ ] `isLDataValid()` = true at boot (CLKIN wired)
- [ ] Baseline captured: note RP, L, LHR values from boot log

### Coin set

| # | Монета | Метал | Мета |
|---|--------|-------|------|
| 1 | American Silver Eagle 1oz | XAG999 | C-5 baseline comparison |
| 2 | Russian 5 Kopecks 1870 | XCU | C-5 baseline + XCU/XZNNIP separation |
| 3 | Ukraine 10 UAH 2022 | XZNNIP | C-5 baseline + XCU/XZNNIP separation |
| 4 | Germany 1.5 EUR 1997 | XFE | **Δf ferro test** — key experiment |
| 5 | Germany 50 Pfennig | XAL | C-5 baseline |
| 6+ | New coins (Au, Ag925, Ni, CuNi) | Various | DB expansion |

### Procedure per coin

1. Place coin on base spacer → press ENTER → wait 2.3s capture → done
2. Add +1mm spacer → press ENTER → wait 2.3s → done
3. Add +2mm spacer → press ENTER → wait 2.3s → done
4. Remove spacers, coin on base → press ENTER → wait 2.3s drift check → done
5. Remove coin → device computes + saves production + discovery dump → IDLE
6. Verify Serial: "Discovery dump saved: session_*.json"

### Expected output

Per coin: ~2 KB JSON in session file. 9 coins × 2 KB = ~18 KB per session. Well within SD limits.

---

## 13. Offline analysis pipeline

### Tool: `tools/discovery_analyze.py`

Sprint 3 deliverable. Reads session JSON, produces:

1. **Per-metal boxplots:** RP_median distribution across 5 cycles per metal
2. **Δf analysis:** scatter plot Δf vs metal → is direction consistent? Does it separate ferro?
3. **σ correlation:** Is rp_sigma characteristic of metal type?
4. **LHR vs L_DATA:** Compare fSensor precision: LHR 24-bit vs Eq.6 16-bit
5. **Pairwise distance matrix:** Rebuild with candidate new dimensions
6. **Quick Screen centroids:** Generate `quick_centroid` values for each metal
7. **Recommendation:** Which dimensions to add/remove in Vector v2

Output: `C6_ANALYSIS_REPORT.md` + updated `index.json` + `matcher.json`.

---

## 14. Архітектурні рішення (ADR)

### ADR-D1: Discovery Mode — config flag, not separate firmware

**Рішення:** `discovery_enabled` flag у `ldc1101.json`.  
**Обґрунтування:** Один binary. Toggle через SD config. При `false` — zero runtime overhead.

### ADR-D2: Capture stats via reservoir sampling, not full buffer

**Рішення:** Approximate median через reservoir (size=64).  
**Обґрунтування:** Full buffer = 2.4 KB stack (30% of 8 KB). Reservoir = 256 B. Accuracy: < 2% error при N>400.

### ADR-D3: LHR readings accumulated in capture loop, not separate task

**Рішення:** LHR polling в capture loop.  
**Обґрунтування:** Strategy A (single task). spiMutex не потрібен. ~30 LHR readings per 2s capture — достатньо.

### ADR-D5: Capture at current RESP_TIME, not reduced

**Рішення:** Capture loop використовує RESP_TIME=6144 (production value).  
**Обґрунтування:** Зміна RESP_TIME потребує Sleep→Active transition (~5 мс). Кожен individual sample при RESP_TIME=6144 має максимальний SNR. При N=600 samples → effective SNR = √600 × single_sample_SNR ≈ 24.5× покращення. Якщо зменшити RESP_TIME до 384 (8× більше samples), individual SNR падає в √(6144/384) = 4×, final SNR = √4800 × (SNR/4) = 69.3/4 ≈ 17.3× — **гірше**. Висновок: max RESP_TIME + N averaging = оптимальна стратегія.

### ADR-D6: Discovery SPI methods behind #ifdef

**Рішення:** `spiReadPublic()`, `readMeasurementBurstPublic()`, `readLHRBurstPublic()` під `#ifdef DISCOVERY_MODE`.  
**Обґрунтування:** Release builds — zero API pollution, zero Flash overhead. `-D DISCOVERY_MODE` в platformio.ini.

---

## 15. Open questions

| # | Питання | Коли відповісти | Вплив |
|---|---------|-----------------|-------|
| Q1 | Чи Δf через LHR 24-bit дійсно розрізняє XFE (Fe+Cu) від чистого Cu? | C-6 Session | Визначає чи Δf входить у Vector v2 |
| Q2 | Чи σ(RP) per step корелює з типом металу? | C-6 Session | Визначає чи σ стає компонентом вектора |
| Q3 | Яка оптимальна capture duration? 2s vs 3s vs 5s? | C-6 Session: спробувати 2s і 3s | Баланс accuracy vs UX |
| Q4 | Чи settling time 300ms достатній? | C-6 Session: перевірити перші 100 зразків | Якщо ні — збільшити |
| Q5 | Чи потрібен full raw trace (всі N samples) для окремих монет? | Post C-6 analysis | Якщо reservoir median недостатній — Variant A buffer |
| Q6 | Display update в capture loop — чи SPI contention з LDC1101? | Sprint 2 implementation | Strategy A: немає contention, але verify timing |

---

*Документ створено 2026-03-27. Wave 9 "Measurement Science" — Sprint 2 specification.*  
*Cross-ref: WAVE8_COMPLETION_WAVE9_DISCOVERY_PLAN.md §5*
