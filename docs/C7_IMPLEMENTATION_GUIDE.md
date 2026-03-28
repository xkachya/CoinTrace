# Wave 8 C-7 — Implementation Guide

**Дата:** 2026-03-28
**Статус:** Готовий до кодингу — всі передумови виконані

Цей документ — швидка довідка для розробника перед початком кодингу C-7.
Всі технічні рішення прийняті та задокументовані у архітектурних специфікаціях.

---

## Що вже зроблено (prereqs — 2026-03-28)

| Артефакт | Файл | Статус |
|----------|------|--------|
| `FingerprintCache::query()` + weights param | `lib/StorageManager/src/FingerprintCache.h/.cpp` | ✅ DONE |
| `LDC1101Plugin::getLiveRp()` | `lib/LDC1101Plugin/src/LDC1101Plugin.h` | ✅ DONE |
| `LDC1101Plugin::getLiveL()` | `lib/LDC1101Plugin/src/LDC1101Plugin.h` | ✅ DONE |
| `LDC1101Plugin::isLDataValid()` | `lib/LDC1101Plugin/src/LDC1101Plugin.h` | ✅ DONE |
| `LDC1101Plugin::recalibrate()` | `lib/LDC1101Plugin/src/LDC1101Plugin.h` | ✅ DONE |
| `MetalMatcher.h` | `lib/StorageManager/src/MetalMatcher.h` | ✅ DONE |
| `MetalMatcher.cpp` | `lib/StorageManager/src/MetalMatcher.cpp` | ✅ DONE |
| `test_metal_matcher.cpp` (13 tests) | `test/test_metal_matcher/` | ✅ DONE |
| `matcher.json` seed | `data/sd_seed/CoinTrace/matcher.json` | ✅ EXISTS |

---

## Що залишилось — C-7b + C-7e

### C-7b: Quick Screen Phase 1 (`src/main.cpp`)

**Нові константи** (додати перед `drawQuickScreen()`):
```cpp
static constexpr float QUICK_NOISE_FLOOR_PCT    =  2.0f;
static constexpr float QUICK_L_NOISE_FLOOR_CT   =  2.0f;
static constexpr float QUICK_SILVER_THRESH_PCT  = 40.0f;  // C-5 updated (було 25)
static constexpr float QUICK_COPPER_THRESH_PCT  = 30.0f;  // C-5 updated (було 14)
static constexpr float QUICK_ALUM_THRESH_PCT    = 15.0f;  // C-5 updated (було 5)
static constexpr float QUICK_FERRO_THRESH_L_RAW = 100.0f; // верифікувати S-5
```

**Нова функція `classifyQuick()`** (~15 рядків):
```cpp
struct QuickClass { const char* label; const char* label_ua; };

static QuickClass classifyQuick(float dRpPct, bool isFerro) {
    if (isFerro)                             return {"STEEL",     "СТАЛЬ ⚠"};
    if (dRpPct > QUICK_SILVER_THRESH_PCT)    return {"SILVER",    "СРІБЛО"};
    if (dRpPct > QUICK_COPPER_THRESH_PCT)    return {"COPPER",    "МІДЬ"  };
    if (dRpPct > QUICK_ALUM_THRESH_PCT)      return {"ALUMINIUM", "АЛЮМІН"};
    return                                          {"?",         "?"     };
}
```

**Нова функція `drawQuickScreen()`** (~60 рядків):
- Приймає: `float liveRp, float liveL, float basRp, float basL`
- Обчислює: `dRpPct`, `dL_raw`, `isFerro = lDataValid && dL_raw > QUICK_FERRO_THRESH`
- Дисплей (240×135): заголовок y=2, ΔRp y=30, ΔL y=46, ferro y=62, клас y=82, hint y=120
- Часткове оновлення: лише рядки ΔRp і ΔL при кожному tick (без повного fillScreen)
- Spec: `QUICK_SCREEN_SPEC.md §5`

**Статична змінна для fresh-redraw:**
```cpp
static bool sQuickScreenFresh = true;  // reset to true on coin removal
```

### C-7e: Інтеграція в `main.cpp`

**1. Includes + global** (вгорі файлу):
```cpp
#include "MetalMatcher.h"
static MetalMatcher gMatcher;
```

**2. `setup()` — після `gFPCache.init()`:**
```cpp
gMatcher.init(gFPCache);
gMatcher.loadConfig(&gSDCard, gCtx.spiMutex);
gLogger.info("Matcher", "ready | sigma=%.2f full_w=[%.1f,%.1f,%.1f,%.1f,%.1f]",
    gMatcher.config().sigma,
    gMatcher.config().full_weights[0], gMatcher.config().full_weights[1],
    gMatcher.config().full_weights[2], gMatcher.config().full_weights[3],
    gMatcher.config().full_weights[4]);
```

**3. `doMeasCompute()` — замінити `gFPCache.query()` на `gMatcher.matchFull()`:**

Поточний код (рядки ~264-279):
```cpp
// ЗАМІНИТИ ЦЕ:
QueryResult results[FingerprintCache::QUERY_TOP_N];
const uint8_t n = gFPCache.query(...);
if (n > 0 && results[0].confidence > 0.01f) { ... }
```
На це:
```cpp
// НА ЦЕ:
MatchResult mr = gMatcher.matchFull(sMeas.m);
if (mr.valid) {
    strlcpy(sMeas.m.metal_code, mr.metal_code, sizeof(sMeas.m.metal_code));
    strlcpy(sMeas.m.coin_name,  mr.coin_name,  sizeof(sMeas.m.coin_name));
    sMeas.m.conf = mr.confidence;
    gLogger.info("Meas", "Match: %s  conf=%.2f  dist=%.4f  ferro=%s",
        mr.metal_code, mr.confidence, mr.distance, mr.is_ferro ? "YES" : "no");
    gMatcher.logTopCandidates(mr);
} else if (!gMatcher.isReady()) {
    gLogger.info("Meas", "FP cache not ready — skipping match");
} else {
    gLogger.info("Meas", "No match (conf=%.2f)", mr.confidence);
}
```

**4. `loop()` — IDLE handler з Quick Screen:**

Знайти місце де `drawMeasIdle()` викликається в IDLE стані і додати перевірку монети:
```cpp
// В loop(), IDLE branch (після state machine, перед gPluginSystem.update()):
if (sMeas.state == MeasState::IDLE && gLDC && gLDC->isReady()) {
    if (gLDC->isCoinPresent()) {
        drawQuickScreen(gLDC->getLiveRp(), gLDC->getLiveL(),
                        gLDC->getBaseline(), gLDC->getLBaseline());
    } else {
        if (!sQuickScreenFresh) {
            sQuickScreenFresh = true;
            drawMeasIdle();  // refresh idle screen after coin removed
        }
    }
}
```

**5. `loop()` — keyboard: додати 'R' handler** (перед або після 'O' handler):
```cpp
} else if (key == 'r' || key == 'R') {
    if (sMeas.state == MeasState::IDLE) {
        M5Cardputer.Display.fillRect(0, 50, 240, 40, BLACK);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(CYAN);
        M5Cardputer.Display.setCursor(5, 60);
        M5Cardputer.Display.print("Recalibrating...");
        gLDC->recalibrate();
        sQuickScreenFresh = true;
        drawMeasIdle();
    }
}
```

---

## Порядок виконання

```
1. Запустити тести: pio test -e native-test -f test_metal_matcher
   → Очікується: 13/13 PASS

2. Перевірити що існуючі тести не зламались:
   pio test -e native-test
   → Очікується: всі PASS (FingerprintCache, VectorCompute, Logger, etc.)

3. Реалізувати C-7b: drawQuickScreen() + classifyQuick() + константи в main.cpp
4. Реалізувати C-7e: gMatcher global + setup() init + doMeasCompute() + loop() IDLE + 'R' key

5. Збірка: pio run -e cointrace-dev
   → Очікується: компіляція без помилок

6. HW тест:
   a) Покласти монету → Quick Screen має з'явитись за ~2с
   b) ENTER → повний цикл запускається
   c) 'R' без монети → "Recalibrating..." → базова лінія оновлена
   d) Провести 5 монет × 1 цикл кожна → порівняти metal_code з очікуваним
```

---

## Відомі ризики (не блокують початок)

| Ризик | Вплив | Мітигація |
|-------|-------|-----------|
| Quick Screen пороги — оцінки (PENDING HW-QS-6) | Хибна класифікація | Скоригувати QUICK_*_THRESH після першого hw тесту |
| XCU ↔ XZNNIP margin = 0.51σ | Можливий swap при T > +10°C | Очікується; зафіксувати в hw тесті |
| dL1_n не розрізняє XFE від XCU (p3 protocol) | is_ferro=false для сталі | ferro_thresh_dL1_n=99 вимкнено; Wave 9 S-5 виправить |

---

## Посилання

- Повна специфікація: `docs/architecture/METAL_MATCHER_ARCHITECTURE.md` v1.3.0
- Quick Screen spec: `docs/architecture/QUICK_SCREEN_SPEC.md` v1.3.0
- Sprint план: `docs/external/2026-03-27.WAVE8_COMPLETION_WAVE9_DISCOVERY_PLAN.md`
- C-5 audit (пороги, ризики): `docs/external/2026-03-27.C5_DEEP_ANALYSIS_AUDIT.md`
