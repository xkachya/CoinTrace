# MetalMatcher Architecture — CoinTrace

**Версія:** 1.3.0
**Дата:** 2026-03-27
**Статус:** Специфікація — очікує реалізації (Wave 8 C-7)
**Cross-ref:** `FINGERPRINT_DB_ARCHITECTURE.md`, `MEMORY_MAP.md`, `WAVE8_ROADMAP.md §C-7`,
`MEASUREMENT_WORKFLOW.md`, `PLUGIN_INTERFACES_EXTENDED.md §5`, `QUICK_SCREEN_SPEC.md`

**Changelog:**
- 1.3.0 (2026-03-27) — C-5 hw-data sync: (A) `full_weights[4]` dL1_n 2.0→1.0 (p3 всі метали |dL1_n|≈2.4, нульова дискримінація); (B) `sigma` 0.3→0.35 (C-5 empirical, відповідає FingerprintCache::CONFIDENCE_SIGMA); (C) `ferro_thresh_dL1_n` 0.05→99.0 (DISABLED — p3 dL1_n всі метали ≈ −2.4 >> 0.05, re-enable після Wave 9 S-5); (D) matcher.json приклад + таблиця defaults оновлені.
- 1.2.0 (2026-03-26) — Pre-implementation sync: (A) §3 arch diagram виправлено: `quickScreen()` → `drawQuickScreen()` з явним Phase 1/2 split; (B) §3 Quick Screen data flow розділено на два блоки Phase 1 (threshold, без MetalMatcher) та Phase 2 (matchQuick, після C-5); (C) §9 Quick Screen handler замінено — Phase 1 передає raw значення у drawQuickScreen() без matchQuick(), Phase 2 описано як внутрішня заміна всередині drawQuickScreen(); (D) виправлено `dL_uH` → `dL_raw` в §9 (unit naming відповідно до QS_SPEC v1.1.0).
- 1.1.0 (2026-03-25) — C-1..C-3 виправлено: alternatives[] в MatchResult; matchQuick приймає raw значення для симетрії API; logTopCandidates без Logger-параметра; W-5: is_ferro через fabsf, знак pending S-5; +§13 unit test spec; HttpServer.cpp у §12; StorageManager naming note у ADR-M1

---

## Зміст

1. [Мета і контекст](#1-мета-і-контекст)
2. [Аналіз пам'яті](#2-аналіз-памяті)
3. [Архітектурний огляд](#3-архітектурний-огляд)
4. [Розміщення модуля](#4-розміщення-модуля)
5. [API — MetalMatcher](#5-api--metalmatcher)
6. [Алгоритм: Full 5D та Quick 2D](#6-алгоритм-full-5d-та-quick-2d)
7. [Конфігурація: matcher.json](#7-конфігурація-matcherjson)
8. [Зміни у FingerprintCache](#8-зміни-у-fingerprintcache)
9. [Інтеграція з main.cpp](#9-інтеграція-з-maincpp)
10. [Debug та logging](#10-debug-та-logging)
11. [Архітектурні рішення (ADR)](#11-архітектурні-рішення-adr)
12. [Пов'язані файли](#12-повязані-файли)
13. [Unit test specification](#13-unit-test-specification)

---

## 1. Мета і контекст

### Проблема

`FingerprintCache::query()` — поточна точка входу для ідентифікації металу — містить алгоритм
пошуку жорстко в тілі класу бази даних:

```cpp
// FingerprintCache.cpp:159 — рівні ваги захардкоджено
const float dist = sqrtf(d0*d0 + d1*d1 + d2*d2 + d3*d3 + d4*d4);
```

Це означає:
- **Зміна ваги будь-якого виміру** (посилити роль dL1 як ferro-дискримінатора) → рекомпіляція
- **Quick Screen** (2D пошук лише по dRp1_n + dL1_n) не підтримується — немає способу передати
  нульові ваги для k1/k2/slope
- **σ = 0.3 захардкоджено** — неможливо підібрати без rebuild після реальних hw-даних
- **Клас бази даних змішує дві відповідальності**: storage lifecycle (завантаження, кеш, CRC)
  + класифікаційний алгоритм

### Рішення

Розділити відповідальності: ввести `MetalMatcher` — тонкий алгоритмічний шар над
`FingerprintCache`, що:

1. Отримує ваги та параметри з **`matcher.json` на SD карті** (без рекомпіляції)
2. Реалізує **Full 5D** (повний цикл, всі вектори) та **Quick 2D** (лише STEP_BASE, для Quick
   Screen)
3. Повертає **top-1 + 3 alternatives** з покомпонентними дистанціями для debug
4. Залишає `FingerprintCache` чистим storage-класом

> **Пов'язаний модуль:** Quick Screen state machine специфікується окремо в
> `QUICK_SCREEN_SPEC.md` (Wave 8 C-7). MetalMatcher надає лише метод `matchQuick()`;
> управління станами, дисплей та user flow — поза межами цього документа.

---

## 2. Аналіз пам'яті

### Поточний бюджет (MEMORY_MAP.md §4, hw-verified 2026-03-18)

| Ресурс | Стан | Headroom |
|--------|------|----------|
| BSS (.bss + .data) | 202 KB з 320 KB | 118 KB |
| Heap (idle) | ~30 KB | 15 KB до hard floor |
| Flash (firmware) | 1.40 MB з 2.5 MB | 1.10 MB |
| main task stack | 8 KB Arduino default | не вимірювалось |

Домінуючий BSS-споживач — `FingerprintCache::entries_[1000]` = **140 KB** (незмінно).

### Вплив MetalMatcher

| Компонент | Тип | Розмір | Коментар |
|-----------|-----|--------|----------|
| `MetalMatcher` об'єкт (`gMatcher` global) | BSS | **~48 B** | Config (8×float=32B) + ptr (4B) + bool (1B) + padding |
| `MatchResult` на стеку при виклику | stack | **~240 B** | struct + 3×Alternative = 60B+3×60B |
| `loadConfig()` тимчасовий JsonDocument | heap | **~256 B** | звільняється одразу після парсингу |
| Новий код (matchFull + matchQuick + loadConfig) | Flash | **~5 KB** | оцінка |
| **Разом постійно** | **BSS** | **~48 B** | |

### Висновок

MetalMatcher є **практично безкоштовним** з точки зору пам'яті. `MatchResult` (включаючи
`alternatives[3]`) живе на стеку під час виклику — жодного heap allocation. Flash headroom
залишається >1 MB.

Єдиний реальний бюджетний ризик — майбутнє розширення `CacheEntry` при рості BSS через
`entries_[1000]` — задокументовано MEMORY_MAP.md §8.

---

## 3. Архітектурний огляд

```
╔══════════════════════════════════════════════════════════════╗
║  main.cpp                                                    ║
║                                                              ║
║  doMeasCompute()    →  gMatcher.matchFull(m)      // повний цикл  ║
║  drawQuickScreen()  →  (Phase 1) classifyQuick()  // threshold   ║
║                        (Phase 2) gMatcher.matchQuick(           ║
║                                    rpLive, rpBase,              ║
║                                    lLive,  lBase)               ║
╚══════════════════════════╦═══════════════════════════════════╝
                           │  MatchResult { top1 + alternatives[3] }
╔══════════════════════════▼═══════════════════════════════════╗
║  MetalMatcher  (NEW — lib/StorageManager/src/)               ║
║                                                              ║
║  · Config: weights[5]×2, sigma, min_conf, ferro_thresh       ║
║  · matchFull(Measurement&) → MatchResult                     ║
║  · matchQuick(rpLive, rpBase, lLive, lBase) → MatchResult    ║
║  · loadConfig(SDCardManager*, spiMutex)                      ║
║  · logTopCandidates(MatchResult&)  — log_i dump              ║
╚══════════════════════════╦═══════════════════════════════════╝
                           │  query(…, weights[5])
╔══════════════════════════▼═══════════════════════════════════╗
║  FingerprintCache  (existing — мінімальне розширення)        ║
║                                                              ║
║  · query(…, const float* weights)  ← новий параметр          ║
║    (weights==nullptr → рівні ваги 1.0, backward-compatible)  ║
║  · все інше — без змін                                       ║
╚══════════════════════════════════════════════════════════════╝
```

**Потік даних — повний цикл:**
```
Measurement{rp[4], l[4]}
    │
    ▼ VectorCompute::dRp1_n(), k1(), k2(), slope(), dL1_n()
    │   (нормалізація всередині matchFull — caller не знає констант)
    ▼ MetalMatcher::matchFull()
    │   · calls cache.query(..., full_weights)
    │   · is_ferro = fabsf(dL1_n) > cfg.ferro_thresh  (знак: pending S-5)
    │   · returns MatchResult{top1, alternatives[3], dist_components[5]}
    │
    ▼ MatchResult → drawMeasResult() + logTopCandidates()
```

**Потік даних — Quick Screen Phase 1 (threshold-based, реалізується зараз):**
```
LDC1101Plugin: getLiveRp(), getLiveL(), getBaseline(), getLBaseline()
    │
    ▼ drawQuickScreen(liveRp, liveL, basRp, basL)   ← main.cpp
    │   · обчислює dRpPct, dL_raw, lDataValid, isFerro (QUICK_SCREEN_SPEC.md §3)
    │   · classifyQuick(dRpPct, isFerro) → QuickClass  (вбудовані пороги)
    │   · MetalMatcher НЕ викликається у Phase 1 (quick_centroid у DB відсутній)
    │
    ▼ QuickClass → відображення на дисплеї
```

**Потік даних — Quick Screen Phase 2 (matchQuick, після C-5 hw-сесії):**
```
LDC1101Plugin: getLiveRp(), getLiveL(), getBaseline(), getLBaseline()
    │
    ▼ drawQuickScreen(liveRp, liveL, basRp, basL)   ← main.cpp (сигнатура незмінна)
    │   · обчислює dRpPct, dL_raw, lDataValid (QUICK_SCREEN_SPEC.md §3)
    │   · gMatcher.matchQuick(rpLive, rpBase, lLive, lBase)
    │       · нормалізує: dRp1_n = (rpBase−rpLive)/800, dL1_n = (lLive−lBase)/2000
    │       · cache.query(dRp1_n, 0,0,0, dL1_n, quick_weights) над quick_centroid entries
    │       · returns MatchResult (algo=ALGO_QUICK)
    │
    ▼ MatchResult → QuickClass (mapped від metal_code) → відображення на дисплеї
```

> **Symmetry note:** обидва методи `matchFull` і `matchQuick` приймають **raw** значення і
> виконують нормалізацію всередині. Caller ніколи не маніпулює constants 800.0f / 2000.0f.
> Це ADR-M6.
>
> **Phase 1 → Phase 2 transition:** сигнатура `drawQuickScreen()` та виклик з loop() **не
> змінюються** при переході. Phase 2 змінює лише внутрішню логіку drawQuickScreen() (заміна
> `classifyQuick()` на `gMatcher.matchQuick()`) та index.json (додавання `quick_centroid`).
> QUICK_SCREEN_SPEC.md §4.2.

---

## 4. Розміщення модуля

### Варіанти розглядались

| Варіант | Плюси | Мінуси |
|---------|-------|--------|
| A. Окрема `lib/MetalMatcher/` | явне розділення | нова library.json, inter-lib deps у platformio.ini |
| **B. Додати до `lib/StorageManager/`** | ✅ немає нових залежностей, FingerprintCache поруч | StorageManager стає ширшим (tech debt — §ADR-M1) |
| C. Плагін (IPlugin interface) | формально "правильно" | MetalMatcher не hardware component |

### Рішення: **варіант B**

MetalMatcher розміщується в `lib/StorageManager/src/` поряд з `FingerprintCache`:
- MetalMatcher є **standalone service class** (як FingerprintCache, MeasurementStore) — не
  hardware component, тому не є плагіном (`PLUGIN_INTERFACES_EXTENDED.md §5`, `IPlugin.h:22`
  _"Every hardware component … is a plugin"_)
- FingerprintCache і MetalMatcher мають тісне зчеплення (MetalMatcher викликає розширений
  `cache.query()`)
- `lib/StorageManager/library.json` вже декларує всі потрібні залежності (ArduinoJson, SD)
- Нульові зміни у `platformio.ini`

**Нові файли:**
```
lib/StorageManager/src/MetalMatcher.h
lib/StorageManager/src/MetalMatcher.cpp
```

---

## 5. API — MetalMatcher

### Alternative (вкладений struct)

```cpp
// lib/StorageManager/src/MetalMatcher.h

// One runner-up candidate (ranked #2..#4 after the top match).
struct Alternative {
    char  metal_code[8];   // "XAG833"
    char  coin_name[48];   // full name
    float confidence;      // exp(−dist²/σ²)
    float distance;        // weighted Euclidean distance
};
```

### MatchResult

```cpp
struct MatchResult {
    // ── Top match ──────────────────────────────────────────────
    char    metal_code[8];      // "XAG925" | "" if no match
    char    coin_name[48];      // "Test Silver 925 (synthetic)"
    float   confidence;         // exp(−dist²/σ²), [0.0 .. 1.0]
    float   distance;           // weighted Euclidean distance

    // ── Classification meta ────────────────────────────────────
    bool    is_ferro;           // fabsf(dL1_n) > cfg.ferro_thresh (⚠️ sign: pending S-5)
    bool    valid;              // false if cache not ready or confidence < min_conf
    uint8_t algo;               // ALGO_FULL=0, ALGO_QUICK=1

    // ── Debug / tuning ─────────────────────────────────────────
    // Per-axis weighted components: √(wi · Δi²)
    // Non-zero even for zero-weight axes (raw Δ stored) — для post-analysis
    float   dist_components[5]; // [dRp1_n, k1, k2, slope, dL1_n]

    // ── Alternatives (top-2 .. top-4) ─────────────────────────
    Alternative alternatives[3];
    uint8_t     alt_count;      // 0..3 valid entries in alternatives[]
};
// sizeof(MatchResult) ≈ 8+48+4+4+1+1+1+20+3×60+1 ≈ 248 B — lives on stack, not heap
```

### MetalMatcher::Config

```cpp
struct Config {
    // Ваги для 5D-вектора: [dRp1_n, k1, k2, slope, dL1_n]
    float full_weights[5]    = {1.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    // Quick Screen: k1/k2/slope = 0.0 → ці осі не впливають на пошук
    float quick_weights[5]   = {1.5f, 0.0f, 0.0f, 0.0f, 2.5f};
    // Gaussian confidence transform: conf = exp(−dist²/σ²)
    float sigma              = 0.35f;  // C-5 empirical (відповідає FingerprintCache::CONFIDENCE_SIGMA)
    // Поріг впевненості: нижче → valid=false
    float min_confidence     = 0.3f;
    // Поріг ferro-детекції по |dL1_n|.
    // ⚠️ C-5 hw-data: p3 dL1_n ВСІ метали (Ag/Cu/Zn/Fe/Al) ≈ −2.4 (діапазон [−2.3,−2.6]).
    // Значення 0.05 (архівне) → is_ferro=true для ВСІХ металів → DISABLED.
    // Встановлено 99.0 (практично вимкнено) до Wave 9 S-5 ferro HW-сесії.
    // Після S-5: якщо ferro збільшує |dL1_n| суттєво відносно non-ferro → оновити.
    float ferro_thresh_dL1_n = 99.0f;
};
```

### MetalMatcher клас

```cpp
class MetalMatcher {
public:
    // Ініціалізація: передати posилання на готовий FingerprintCache.
    // Викликати ПІСЛЯ FingerprintCache::init() у setup().
    void init(FingerprintCache& cache, const Config& cfg = Config{});

    // Завантажити Config з SD:/CoinTrace/matcher.json.
    // Якщо файл відсутній або не парситься — Config без змін (defaults).
    // [SPI-3] timeout 50 ms, [SD-A-03] no Logger::* під час IO.
    // Повертає true якщо файл знайдено та успішно завантажено.
    bool loadConfig(SDCardManager* sd, SemaphoreHandle_t spiMutex);

    // ── Matching ────────────────────────────────────────────────────────────

    // Full 5D match: приймає сирий Measurement (rp[0..2], l[0..1]).
    // Нормалізація виконується всередині через VectorCompute.
    // Вимагає повного циклу вимірювання (STEP_BASE + STEP_1 + STEP_2).
    MatchResult matchFull(const Measurement& m) const;

    // Quick 2D match: приймає сирі показники LDC1101 (raw units, ті ж що Measurement::rp/l).
    //   rpLive, lLive — поточні live-показники (монета на базовій позиції)
    //   rpBase, lBase — базові показники (getBaseline(), getLBaseline())
    // Нормалізація виконується всередині: dRp1_n = (rpBase−rpLive)/800, dL1_n = (lLive−lBase)/2000
    // [Symmetry ADR-M6] — caller ніколи не знає констант нормалізації.
    MatchResult matchQuick(float rpLive, float rpBase,
                           float lLive,  float lBase) const;

    // ── Debug ───────────────────────────────────────────────────────────────

    // Виводить top-1 + alternatives з покомпонентними дистанціями через log_i.
    // Викликати після matchFull/matchQuick для tuning/debug.
    // [SD-A-03] Використовує log_i (не Logger::*) — консистентно з StorageManager.
    void logTopCandidates(const MatchResult& r) const;

    // ── Config access ───────────────────────────────────────────────────────

    void          resetConfig()        { cfg_ = Config{}; }
    const Config& config()       const { return cfg_; }
    bool          isReady()      const { return cache_ != nullptr && cache_->isReady(); }

private:
    FingerprintCache* cache_ = nullptr;
    Config            cfg_;

    // Internal: normalizes inputs, calls cache.query(), builds MatchResult.
    // algo = ALGO_FULL (0) or ALGO_QUICK (1)
    MatchResult doMatch(float dRp1_n, float k1, float k2, float slope,
                        float dL1_n, const float* weights, uint8_t algo) const;
};
```

---

## 6. Алгоритм: Full 5D та Quick 2D

### Зважена Euclidean дистанція

Поточний `FingerprintCache::query()` — рівні ваги (w=1.0):
```
dist = √(Δ0² + Δ1² + Δ2² + Δ3² + Δ4²)
```

MetalMatcher передає ваги в розширений `cache.query()`:
```
dist = √(w0·Δ0² + w1·Δ1² + w2·Δ2² + w3·Δ3² + w4·Δ4²)
```

де `Δi = input_i − centroid_i`, а `wi` беруться з `full_weights[]` або `quick_weights[]`.

Поле `dist_components[i]` в MatchResult зберігає `√(wi·Δi²)` — зважений внесок кожної осі.
Це ключ до ручного tuning: видно яка вісь "тягне" результат неправильно.

### Gaussian confidence transform

```
confidence = exp(−dist² / σ²)
```
- dist=0.0 → conf=1.0 (ідеальний збіг)
- dist=σ=0.3 → conf=e⁻¹ ≈ 0.37
- dist=0.6 → conf≈0.135
- dist>1.0 → conf<0.07 (практично no match)

σ задається в `matcher.json` — підбирається після накопичення реальних hw-даних без rebuild.

### Quick 2D як проекція 5D

Quick Screen — той самий 5D пошук з `quick_weights = [w0, 0, 0, 0, w4]`:

```
dist_quick = √(w0·Δ_dRp1_n² + w4·Δ_dL1_n²)
             (k1, k2, slope — вага 0.0 → не впливають на дистанцію)
```

**Нормалізація всередині matchQuick:**
```cpp
const float dRp1_n = (rpBase - rpLive) / 800.0f;   // dRp1_MAX = 800 Ω
const float dL1_n  = (lLive  - lBase)  / 2000.0f;  // dL1_MAX  = 2000 µH
```

> **Примітка про знакову конвенцію:** `dRp1_n` в Quick Screen (rpBase−rpLive) та у Full scan
> (VectorCompute::dRp1 = rp[0]−rp[1]) є **різними вимірами**: Quick Screen порівнює live з
> baseline, Full scan порівнює step[0] з step[1]. Числові значення подібні але не ідентичні.
> Це прийнятно для Quick (попередній результат), але ваги `quick_weights` можуть
> відрізнятись від `full_weights` саме через цю різницю.

**Фізичний зміст:** при Quick Screen маємо лише STEP_BASE — одну точку без просторової кривої.
dRp1 (провідність при d≈0.6mm, p3 bare coin) + dL1 (магнітне сприйняття) — мінімально достатній набір для
rozriznennia класів металів.

### is_ferro детекція

```cpp
// ⚠️ ЗНАК НЕВИЗНАЧЕНИЙ — pending hw-сесії S-5 (spacer 0.6/0.8mm).
//
// Синтетичні дані: XNI:  l[0]=9500, l[1]=9680 → dL1 = l[0]−l[1] = −180 µH (від'ємний)
//                  XCU:  dL1 ≈ 0 (діамагнетик)
//
// Quick Screen: dL1_n = (lLive−lBase)/2000
//   Якщо ferro збільшує L при наближенні: lLive > lBase → dL1_n > 0
//   Якщо ferro зменшує L при наближенні: lLive < lBase → dL1_n < 0
//
// Рішення для phase-1: використовуємо |dL1_n| — не залежить від знаку.
//   is_ferro = (fabsf(dL1_n) > cfg_.ferro_thresh_dL1_n)
//
// Після S-5 уточнити:
//   a) якщо знак стабільно від'ємний для ferro: is_ferro = (dL1_n < −thresh)
//   b) якщо знак додатній: is_ferro = (dL1_n > thresh)
//   c) якщо знак різний на різних відстанях: залишити fabsf
result.is_ferro = (fabsf(dL1_n) > cfg_.ferro_thresh_dL1_n);
```

### Fallback при недоступному cache

Якщо `!cache_->isReady()`:
```cpp
return MatchResult{ .valid = false, .confidence = 0.0f,
                    .metal_code = "", .alt_count = 0 };
```
Caller (main.cpp) відображає "DB N/A" замість краша.

---

## 7. Конфігурація: matcher.json

### Розміщення та lifecycle

```
SD:/CoinTrace/matcher.json   ← єдине джерело (редагується на ПК)
```

Файл **не кешується** в LittleFS. Він малий (~250 B), читається один раз при boot, і ніколи
не потрібен offline (при відсутності SD використовуються compile-time defaults).
Додавати cache-infrastructure (CRC32, generation) — непропорційна складність.

### Формат

```json
{
  "_comment": "CoinTrace MetalMatcher — edit weights without rebuild. Order: [dRp1_n, k1, k2, slope, dL1_n]",
  "full_weights":  [1.0, 1.0, 1.0, 0.0, 1.0],
  "quick_weights": [1.5, 0.0, 0.0, 0.0, 2.5],
  "sigma": 0.35,
  "min_confidence": 0.3,
  "ferro_thresh_dL1_n": 99.0
}
```

> **Примітка:** поле `version` навмисно відсутнє. `matcher.json` — приватний config одного
> пристрою, не community-shared формат як `index.json`. Версіонування не потрібне.

### Дефолтні значення (якщо файл відсутній)

| Параметр | Дефолт | Обґрунтування |
|----------|--------|---------------|
| `full_weights[0]` dRp1_n | 1.0 | базовий дискримінатор провідності |
| `full_weights[1]` k1 | 1.0 | перша просторова нормалізація |
| `full_weights[2]` k2 | 1.0 | друга просторова нормалізація |
| `full_weights[3]` slope | **0.0** | p2 протокол (x={0,1,2}): slope=(k2−1)/2, тобто slope є лінійною функцією k2 → нульова незалежна інформація. Деталі: VectorCompute.h ⚠️ TODO, WAVE8_ROADMAP §C-5 |
| `full_weights[4]` dL1_n | **1.0** | C-5: dL1_n ≈ −2.4 для ВСІХ 5 металів → нульова дискримінація. Рівна вага. Переоцінити після Wave 9 S-5. |
| `quick_weights[0]` dRp1_n | 1.5 | підсилено: єдина провідна вісь в Quick |
| `quick_weights[1..3]` | 0.0 | k1/k2/slope недоступні в Quick Screen |
| `quick_weights[4]` dL1_n | 2.5 | підсилено: ferro-детекція критична |
| `sigma` | **0.35** | C-5 empirical: 25/25 correct at σ=0.35 (відповідає FingerprintCache::CONFIDENCE_SIGMA) |
| `min_confidence` | 0.3 | нижче → `valid=false` |
| `ferro_thresh_dL1_n` | **99.0** | ⚠️ DISABLED — p3 |dL1_n| всі метали ≈ 2.4 >> 0.05. Re-enable після Wave 9 S-5. |

### Workflow для tuning без rebuild

```
1. Зробити вимір → logTopCandidates() показує dist_components[]
2. Визначити яка вісь "тягне" в неправильну сторону
3. Відредагувати matcher.json на ПК (текстовий редактор)
4. Перезавантажити Cardputer → loadConfig() підхоплює нові ваги (~1 s)
5. Повторний вимір → порівняти
```

---

## 8. Зміни у FingerprintCache

Єдина зміна — розширення `query()` для підтримки зовнішніх ваг.

### Нова сигнатура

```cpp
// weights: масив [w_dRp1_n, w_k1, w_k2, w_slope, w_dL1_n]
// nullptr → рівні ваги 1.0 (backward-compatible — всі існуючі виклики без змін)
uint8_t query(float dRp1_n, float k1, float k2, float slope, float dL1_n,
              QueryResult* results, uint8_t maxResults = QUERY_TOP_N,
              const float* weights = nullptr) const;
```

### Зміна в тілі query()

```cpp
const float w0 = weights ? weights[0] : 1.0f;
const float w1 = weights ? weights[1] : 1.0f;
const float w2 = weights ? weights[2] : 1.0f;
const float w3 = weights ? weights[3] : 1.0f;
const float w4 = weights ? weights[4] : 1.0f;

const float dist = sqrtf(w0*d0*d0 + w1*d1*d1 + w2*d2*d2 + w3*d3*d3 + w4*d4*d4);
```

Confidence transform — без змін.

> **Зауваження про σ:** `FingerprintCache::CONFIDENCE_SIGMA = 0.3f` (hardcoded const)
> використовується тільки у прямих викликах `cache.query()` (наприклад через HTTP
> `POST /api/v1/database/match`). При виклику через MetalMatcher σ береться з `cfg_.sigma`
> (з matcher.json) — `doMatch()` застосовує власний expf. Перетин відсутній.

> **W-QS1 (tech debt — Phase 2):** При виклику через MetalMatcher `cache.query()` внутрішньо
> обчислює confidence з `CONFIDENCE_SIGMA`, а потім `doMatch()` ігнорує це значення і
> перераховує confidence з `cfg_.sigma` — **2 expf() на запис**. Для `matchQuick()` яке
> викликається при кожному loop() тіку (~50 Hz) з 1000 entries: 2000 expf × 50 = 100 k expf/s
> ≈ 7.5 ms/s CPU overhead. Не критично для v1 (< 1% CPU). Для Phase 2 оптимізація:
> додати режим `cache.query(…, return_distances_only=true)` що пропускає внутрішній expf →
> MetalMatcher рахує confidence один раз з `cfg_.sigma` → 50% економія expf calls.

---

## 9. Інтеграція з main.cpp

### Глобальний об'єкт

```cpp
// main.cpp — секція globals
MetalMatcher gMatcher;   // sizeof ≈ 48 B (BSS)
```

### Setup — ланцюжок ініціалізації

```cpp
// setup() — після FingerprintCache::init() і SDCardManager::tryMount()
gMatcher.init(gFPCache);                         // defaults
gMatcher.loadConfig(gSDCard, gSpiMutex);         // перезаписати з SD якщо є
log_i("Matcher", "ready | sigma=%.2f full_w=[%.1f,%.1f,%.1f,%.1f,%.1f]",
    gMatcher.config().sigma,
    gMatcher.config().full_weights[0],
    gMatcher.config().full_weights[1],
    gMatcher.config().full_weights[2],
    gMatcher.config().full_weights[3],
    gMatcher.config().full_weights[4]);
```

### doMeasCompute() — заміна прямого cache.query()

```cpp
// ── Було: ────────────────────────────────────────────────────────
QueryResult qr[FingerprintCache::QUERY_TOP_N];
uint8_t n = gFPCache.query(dRp1_n, k1, k2, slope, dL1_n, qr);
// (рівні ваги, немає alternatives, немає is_ferro, немає dist_components)

// ── Стане: ───────────────────────────────────────────────────────
MatchResult mr = gMatcher.matchFull(sMeas.m);
if (mr.valid) {
    strlcpy(sMeas.m.metal_code, mr.metal_code, sizeof(sMeas.m.metal_code));
    sMeas.m.conf = mr.confidence;
    log_i("Meas", "Match: %s  conf=%.2f  dist=%.4f  ferro=%s",
        mr.metal_code, mr.confidence, mr.distance,
        mr.is_ferro ? "YES" : "no");
    gMatcher.logTopCandidates(mr);   // друкує top-1 + alternatives
} else {
    log_w("Meas", "No match (conf=%.2f, db_ready=%d)",
        mr.confidence, (int)gMatcher.isReady());
}
```

### Quick Screen — обробник в loop()

**Phase 1 (реалізується зараз — threshold-based, без MetalMatcher):**

```cpp
// В loop() при IDLE + COIN_PRESENT — передати raw значення у drawQuickScreen().
// Всі обчислення (dRpPct, dL_raw, lDataValid, isFerro, classifyQuick) — всередині.
// QUICK_SCREEN_SPEC.md §2, §3, §5.
const float liveRp = gLDC->getLiveRp();
const float liveL  = gLDC->getLiveL();
const float basRp  = gLDC->getBaseline();
const float basL   = gLDC->getLBaseline();
drawQuickScreen(liveRp, liveL, basRp, basL);
```

**Phase 2 (після C-5 — matchQuick всередині drawQuickScreen, main.cpp не змінюється):**

```cpp
// Виклик з loop() залишається ідентичним Phase 1.
// drawQuickScreen() внутрішньо замінює classifyQuick() на gMatcher.matchQuick():
//   MatchResult qm = gMatcher.matchQuick(liveRp, basRp, liveL, basL);
//   // Нормалізація 800.0f / 2000.0f — всередині matchQuick (ADR-M6).
// Умова: quick_centroid entries у index.json (C-5 hw-сесія + quick_centroid_gen.py).
// QUICK_SCREEN_SPEC.md §4.2.
```

### HTTP endpoint — оновлення

`POST /api/v1/database/match` (`HttpServer.cpp`) наразі викликає `gFPCache.query()` напряму
(hw-verified A-3). Після появи MetalMatcher цей endpoint повинен йти через
`gMatcher.matchFull()` — інакше HTTP-клієнт матиме рівні ваги навіть коли matcher
налаштований. Це вимагає рефакторингу `HttpServer.cpp` (задокументовано у §12).

---

## 10. Debug та logging

### Log-повідомлення — стандартний вивід

| Місце | Рівень | Повідомлення |
|-------|--------|-------------|
| `loadConfig()` OK | `log_i` | `Matcher: SD config loaded — sigma=0.35 full_w=[1.0,1.0,1.0,0.0,1.0]` |
| `loadConfig()` не знайдено | `log_w` | `Matcher: matcher.json not found — using defaults` |
| `loadConfig()` parse error | `log_e` | `Matcher: JSON parse error: <error>` |
| `matchFull()` OK | `log_i` | `Meas: Match: XAG925  conf=0.71  dist=0.1420  ferro=no` |
| `matchFull()` no match | `log_w` | `Meas: No match  conf=0.18  dist=0.6210` |
| `matchQuick()` OK | `log_i` | `Quick: XAG925  conf=0.64  algo=QUICK` |
| `logTopCandidates()` | `log_i` | (детально нижче) |

> **[SD-A-03] compliance:** всі log-виклики в MetalMatcher використовують `log_i/log_w/log_e`
> (ESP32 native), не `Logger::*`. Це консистентно зі StorageManager-класами
> (FingerprintCache, MeasurementStore) і уникає re-entrancy issues під час SD IO в
> `loadConfig()`.

### logTopCandidates()

```cpp
void MetalMatcher::logTopCandidates(const MatchResult& r) const {
    log_i("Meas", "#1 %-8s conf=%.2f dist=%.4f [%.3f|%.3f|%.3f|%.3f|%.3f] ferro=%s",
        r.metal_code, r.confidence, r.distance,
        r.dist_components[0], r.dist_components[1],
        r.dist_components[2], r.dist_components[3], r.dist_components[4],
        r.is_ferro ? "Y" : "n");
    for (uint8_t i = 0; i < r.alt_count; ++i) {
        log_i("Meas", "#%u %-8s conf=%.2f dist=%.4f",
            i + 2, r.alternatives[i].metal_code,
            r.alternatives[i].confidence,
            r.alternatives[i].distance);
    }
}
```

Приклад виводу після правильного визначення:
```
[Meas] #1 XAG925   conf=0.71 dist=0.1420 [0.041|0.021|0.018|0.009|0.003] ferro=n
[Meas] #2 XAG833   conf=0.58 dist=0.1980
[Meas] #3 XCU      conf=0.21 dist=0.4110
```

Компоненти `[a|b|c|d|e]` — зважені внески `√(wi·Δi²)` по осях dRp1_n/k1/k2/slope/dL1_n.
Одразу видно яка вісь домінує і чи dL1 вносить шум.

---

## 11. Архітектурні рішення (ADR)

### ADR-M1: MetalMatcher — standalone service class, не плагін і не окремий lib

**Рішення:** `lib/StorageManager/src/MetalMatcher.h/.cpp`

**Чому не плагін:**
`IPlugin.h:22` визначає: _"Every hardware component (sensor, display, input, storage) is a
plugin."_ MetalMatcher — чистий класифікаційний алгоритм без hardware access, без SPI/I2C,
без lifecycle tick. `PLUGIN_INTERFACES_EXTENDED.md §5` встановлює точний прецедент:
`LittleFSManager`, `NVSManager`, `MeasurementStore`, `FingerprintCache` — це standalone
класи, що не реалізують жодного IPlugin-інтерфейсу. MetalMatcher потрапляє в ту саму
категорію.

**Чому в StorageManager, а не окремий lib:**
FingerprintCache і MetalMatcher мають тісне зчеплення (MetalMatcher викликає розширений
`cache.query()`). Окрема бібліотека означала б inter-lib dependency у `platformio.ini` без
практичної вигоди для ~150 рядків коду.

**Tech debt — naming drift:**
`lib/StorageManager/` тепер містить: LFS/SD/NVS/Measurement (storage ✅),
FingerprintCache (identification data ⚠️), VectorCompute (pure math ⚠️), MetalMatcher NEW
(classification ❌). Назва більше не відповідає повному вмісту. Рекомендований шлях:
в Wave 9+ перейменувати на `lib/CoinTraceCore/` або `lib/Identification/` і виокремити
storage-шар. На поточному етапі — нульова вигода від рефакторингу.

### ADR-M2: Ваги через matcher.json, не compile-time constants

**Рішення:** ваги завантажуються з SD при boot.
**Причина:** підбір вагів — емпіричний процес після накопичення hw-даних. Рекомпіляція для
кожного експерименту критично гальмує ітерацію. SD-файл редагується на ПК і підхоплюється
після reboot (~1 s).
**Trade-off:** якщо SD відсутня — дефолтні ваги з `Config{}`. Функціональність не втрачається.

### ADR-M3: Quick 2D як проекція 5D через нульові ваги

**Рішення:** `quick_weights[1..3] = 0.0` замість окремого алгоритму.
**Причина:** єдина кодова база → єдина поведінка → менше багів. `doMatch()` — один internal
helper для обох режимів. Нульова вага осі математично коректна і еквівалентна "не
використовувати цю вісь".
**Альтернатива відхилена:** `queryQuick()` у FingerprintCache — дублювання distance logic.

### ADR-M4: `alternatives[3]` inline у MatchResult замість зовнішньої структури

**Рішення:** MatchResult містить 3 вкладених `Alternative` struct (inline, stack-allocated).
**Причина:** `logTopCandidates()` мав би запускати query вдруге (double compute) або
MetalMatcher тримав би `mutable lastCandidates_[]` (порушення const + потенційна
thread-unsafety). Inline struct — clean, zero-overhead поза стеком виклику.
**Розмір overhead:** 3 × sizeof(Alternative) = 3 × 64 = 192 B на стеку. Прийнятно в
контексті main task (8 KB stack) і відповідає PLUGIN_CONTRACT.md §2.4 spirit (8 KB per
component).

### ADR-M5: matcher.json не кешується у LittleFS

**Рішення:** читати напряму з SD при кожному boot.
**Причина:** файл ~250 B, читається один раз при boot, ніколи не потрібен offline. Додавати
cache-infrastructure (CRC32, generation counter) як для FingerprintCache —
непропорційна складність. На відміну від index.json (до 80 KB, community-shared, потребує
integrity check) — matcher.json є приватним device config.

### ADR-M6: matchQuick приймає raw значення (API symmetry)

**Рішення:** `matchQuick(float rpLive, float rpBase, float lLive, float lBase)` — raw units.
**Причина:** обидва match-методи (`matchFull`, `matchQuick`) тепер приймають **raw**
значення і виконують нормалізацію всередині. Caller не маніпулює константами (800.0f,
2000.0f). Це запобігає розповзанню magic numbers по main.cpp і виключає клас помилок де
caller нормалізує неправильно.
**Альтернатива відхилена:** `matchQuick(float dRp1_n, float dL1_n)` (pre-normalized) —
асиметрично щодо matchFull і змушує caller знати normalization constants.

---

## 12. Пов'язані файли

### Нові файли

| Файл | Роль |
|------|------|
| `lib/StorageManager/src/MetalMatcher.h` | клас MetalMatcher, Config, MatchResult, Alternative |
| `lib/StorageManager/src/MetalMatcher.cpp` | реалізація matchFull/matchQuick/loadConfig/logTopCandidates |
| `data/sd_seed/CoinTrace/matcher.json` | дефолтний конфіг SD (seed разом з index.json) |

### Модифікації

| Файл | Зміна |
|------|-------|
| `lib/StorageManager/src/FingerprintCache.h` | додати `const float* weights = nullptr` до query() |
| `lib/StorageManager/src/FingerprintCache.cpp` | реалізація зваженої дистанції (5 рядків) |
| `lib/StorageManager/src/VectorCompute.h` | виправити сталі коментарі: "3mm spacer" → "2mm spacer"; `x={0,1,3}` → `{0,1,2}` (p2 protocol, separate commit) |
| `lib/LDC1101Plugin/src/LDC1101Plugin.h` | додати `getLiveRp()`, `getLiveL()`, `recalibrate()` |
| `src/main.cpp` | init gMatcher; замінити doMeasCompute(); додати Quick Screen handler |
| `lib/HttpServer/src/HttpServer.cpp` | `POST /api/v1/database/match` → через `gMatcher.matchFull()` замість прямого `gFPCache.query()` |

### Документи

| Файл | Зміна |
|------|-------|
| `docs/architecture/MEMORY_MAP.md` | §4: додати `gMatcher` (~48 B) у BSS table; §7: sizeof(MatchResult)≈248 B |
| `docs/architecture/WAVE8_ROADMAP.md` | додати §C-7: MetalMatcher + Quick Screen |
| `docs/architecture/QUICK_SCREEN_SPEC.md` | ✅ СТВОРЕНО (2026-03-25) — state machine, display layout, user flow |

---

## 13. Unit test specification

MetalMatcher є **чистою математикою** (немає SPI/I2C/FreeRTOS/SD) і **повністю
тестується на native (PC)** без hardware — аналогічно FingerprintCache (6/6), VectorCompute
(9/9), NVSManager (9/9).

### Розміщення

```
test/test_metal_matcher/
└── test_metal_matcher.cpp
```

### Обов'язкові тест-кейси

| ID | Назва | Що перевіряє |
|----|-------|-------------|
| MM-01 | `matchFull_perfectHit` | Exact centroid → confidence ≈ 1.0, distance ≈ 0 |
| MM-02 | `matchFull_noCache` | `cache.isReady()=false` → `valid=false`, no crash |
| MM-03 | `matchFull_topN_sorted` | alternatives[] відсортовані за зростанням distance |
| MM-04 | `matchQuick_normalizes` | rpLive=rpBase → dRp1_n≈0, no crash |
| MM-05 | `matchQuick_zeroWeights` | quick_weights[1..3]=0 → k1/k2/slope не впливають на dist |
| MM-06 | `weights_alter_distance` | full_weights[4]=10.0 → dist збільшується при ненульовому dL1 |
| MM-07 | `sigma_alters_confidence` | sigma=0.1 vs sigma=0.5 для однієї dist → різний conf |
| MM-08 | `is_ferro_threshold` | dL1_n=0.04 → false; dL1_n=0.06 → true; dL1_n=−0.06 → true (fabsf) |
| MM-09 | `min_confidence_filter` | якщо best dist > threshold → `valid=false` |
| MM-10 | `loadConfig_missing_file` | відсутній файл → defaults незмінні, повертає false |
| MM-11 | `loadConfig_partial_json` | файл без `sigma` → тільки присутні поля перезаписуються |
| MM-12 | `matchFull_dist_components` | `dist_components[i]` = `sqrt(wi * Δi²)` для кожної осі |

### Тестова стратегія

```cpp
// Аналогічно test_fingerprint_cache — ін'єкція через loadTestEntry():
FingerprintCache cache;
cache.loadTestEntry({ .id="test/xag925", .metal_code="XAG925",
                      .dRp1_n=0.231f, .k1=0.750f, .k2=0.611f,
                      .slope=-0.097f, .dL1_n=0.004f });
cache.beginTest();

MetalMatcher matcher;
matcher.init(cache);

Measurement m;
m.rp[0] = 57344; m.rp[1] = 43008; m.rp[2] = 34999;  // → k1≈0.75, k2≈0.61
m.l[0] = 100;    m.l[1] = 92;

MatchResult r = matcher.matchFull(m);
TEST_ASSERT_TRUE(r.valid);
TEST_ASSERT_FLOAT_WITHIN(0.1f, 1.0f, r.confidence);  // near-perfect
TEST_ASSERT_EQUAL_STRING("XAG925", r.metal_code);
```

**Acceptance criterion:** MM-01..MM-12 PASS на native target (Unity framework) **до** інтеграції
в main.cpp. Фіксується у WAVE8_ROADMAP.md §C-7.
