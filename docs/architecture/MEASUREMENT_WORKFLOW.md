# Measurement Workflow Specification

**Версія:** 1.0.0  
**Дата:** 2026-03-25  
**Актуально для:** Wave 8, C-4 hw-verified (2026-03-24)  
**Джерело:** `src/main.cpp` — C-2 + C-4 реалізація

---

## 1. Огляд

CoinTrace визначає метал монети через 4-позиційний вимір паразитного опору (RP) датчика LDC1101 при різних відстанях котушка–монета. Результат зберігається в LittleFS_data та відображається на екрані Cardputer.

**Два шляхи запуску:**
1. **HTTP** — POST `/api/v1/measure/start` (Web UI або HTTP-клієнт)
2. **ENTER у Quick Screen** — клавіша ENTER при IDLE + COIN_PRESENT (QUICK_SCREEN_SPEC.md)

> ⚠️ **Автоматичний запуск (edge-детекція) ВИЛУЧЕНО 2026-03-25.**
> `sPrevCoinState` edge-block видалено з `main.cpp`. Монета на котушці більше
> не запускає сесію автоматично — відображається Quick Screen (IDLE sub-mode)
> і чекає на явний ENTER або HTTP trigger.

**Дві передумови запуску (обидві обовʼязкові):**
- `gLDC->getCoinState() == COIN_PRESENT`
- `gLFS.isDataMounted() == true`

---

## 2. Стани вимірювання

```
MeasState (enum class, uint8_t):

 IDLE
  │
  └── HTTP start: POST /measure/start + coin present + LFS  │
                                                             ▼
                                                        STEP_BASE
                                                        (d ≈ 0.6 mm — bare coin on base spacer)
                                                             │ ENTER
                                                             ▼
                                                         STEP_1
                                                        (+1 mm spacer, d ≈ 1.6 mm)
                                                             │ ENTER
                                                             ▼
                                                         STEP_3
                                                        (+2 mm spacer, d ≈ 2.6 mm)
                                                             │ ENTER
                                                             ▼
                                                        STEP_DRIFT
                                                        (прибрати spacers, назад на tray)
                                                             │ ENTER
                                                             ▼
                                                         COMPUTE  ← auto (не потребує ENTER)
                                                             │
                                                             ▼
                                                          IDLE
```

**Аварійне скасування з будь-якого STEP\_\*:**
- клавіша **Backspace** → `sMeas = {}` → `drawMeasIdle()`
- **120-секундний per-step timeout** (гл. 5) → те ж саме

---

## 3. ~~Автозапуск (edge-детекція)~~ — ВИЛУЧЕНО 2026-03-25

> **Статус:** Код видалено з `src/main.cpp`.
> Блок `sPrevCoinState` edge-detection та auto-start повністю вилучено.
>
> **Причина:** автоматичний запуск повного циклу при укладанні монети не влаштовував
> користувача. Замість цього реалізовано **Quick Screen** (QUICK_SCREEN_SPEC.md) —
> IDLE sub-mode що відображає live ΔRp% та ΔL при COIN_PRESENT без запуску сесії.
>
> **Поточна поведінка при COIN_PRESENT у IDLE:**
> → автоматично відображається Quick Screen (live ΔRp%, ΔL, is_ferro, клас металу)
> → ENTER → запускає повний цикл (STEP_BASE)
> → HTTP POST /measure/start → те саме

---

## 4. HTTP-запуск (POST /measure/start)

```
HTTP клієнт (lwIP thread, core 0)          MainLoop (core 1)
        │                                        │
        │── POST /api/v1/measure/start ──────────│
        │                                        │
        │   measStartFn_() λ:                    │
        │     if (IDLE + COIN_PRESENT):           │
        │       gMeasStartRequested = true        │
        │       return 202 {"started":true}       │
        │     elif (не IDLE):                     │
        │       return 409 {"error":"already_measuring"}
        │     else (немає монети або LFS):        │
        │       return 503 {"error":"sensor_not_ready"}
        │                                        │
        │                                        ▼ (наступний тік loop())
        │                                   if (gMeasStartRequested):
        │                                     gMeasStartRequested = false
        │                                     if (IDLE + COIN_PRESENT + LFS):
        │                                       start STEP_BASE
        │                                     // інакше — прапорець скинуто тихо
        │                                     // (202 вже відправлено; клієнт
        │                                     //  перевіряє GET /sensor/state)
```

**Thread safety:** `gMeasStartRequested` — `volatile bool`. lwIP thread (core 0) пише, MainLoop (core 1) читає та очищає. Достатньо для прапорця типу "set once, clear once" на ESP32-S3.

**Клієнт після 202:** не гарантовано, що сесія стартувала — 202 означає "запит прийнято". Клієнт має polling `GET /sensor/state` і чекати `MEASURING_STEP_BASE` для підтвердження запуску.

---

## 5. Кроки вимірювання та ENTER

На кожному кроці STEP\_BASE … STEP\_DRIFT:

1. Відображається `drawMeasStep_full()` — повний перемалювання екрана
2. Кожну секунду — частковий redraw (без мерехтіння):
   - `y=61` — рядок RP: `fillRect(0,61,155,14,BLACK)` + новий RP
   - `y=103` — відлік timeout: `fillRect(0,103,240,14,BLACK)` + нові секунди
3. ENTER → `gLDC->read()` → якщо `valid && value1 >= 1.0`:
   - зберігає `rp[i]`, `l[i]` у `MeasSession.m`
   - встановлює `stepMs = millis()` (reset timeout для нового кроку)
   - `drawMeasStep_full()` → наступний крок
4. Якщо `!valid` або `value1 < 1.0` → попередження у лог, повтор не ініціюється (користувач має натиснути ENTER знову)

| Клавіша | Дія |
|---|---|
| ENTER (`\r` / `\n`) | Зафіксувати RP поточного кроку → перейти до наступного |
| Backspace (`\b`) | Скасувати всю сесію → IDLE + `drawMeasIdle()` |

---

## 6. STEP_DRIFT → COMPUTE (автоматично)

Після натискання ENTER на STEP\_DRIFT:

```
rp[3] = поточний RP  (drift check — монета знову на tray)
state = MeasState::COMPUTE
doMeasCompute()  ← викликається одразу, не чекає наступного тіку
```

`doMeasCompute()` виконується синхронно в MainLoop:

1. **Drift check:** `driftRatio = |rp[3] - rp[0]| / rp[0]`
   - якщо > `DRIFT_THRESHOLD` (5%) → `driftWarn = true`, `conf = 0.0f`
2. **Обчислення вектора:** `dRp1`, `k1`, `k2`, `slope`, `dL1` через `VectorCompute`
3. **FP query:** `gFPCache.query(...)` → top-N → `metal_code`, `coin_name`, `conf`
   - skip if `driftWarn == true` або `!gFPCache.isReady()`
4. **Збереження:** `gMeasStore.save(sMeas.m)` → LittleFS_data
5. **Відображення:** `drawMeasResult(sMeas)` — залишається на екрані
6. **Reset сесії:** `sMeas = {}` → `state = IDLE`

Екран результату залишається до наступного укладання монети (перехід IDLE → Quick Screen перезаписує екран).

---

## 7. Timeout (120 с на крок)

```cpp
if (millis() - sMeas.stepMs > 120000UL) {
    gLogger.warning("Meas", "Step %u timeout — session aborted", …);
    sMeas = {};
    drawMeasIdle();
}
```

- Timeout — **per-step**, а не загальний на всю сесію
- `stepMs` скидається при кожному переході: запуск STEP\_BASE, ENTER-перехід
- Відлік відображається жовтим/помаранчевим (< 30 с)
- Спрацювання → той самий ефект що й Backspace

---

## 8. Передумови запуску

| Перевірка | Де | Наслідок відмови |
|---|---|---|
| `gLDC != nullptr` | loop() — guard `if (gLDC && gLDC->isReady())` | auto + HTTP старт ігноруються |
| `gLDC->isReady()` | те ж | те ж |
| `coinState == COIN_PRESENT` | HTTP-start | HTTP: 503 |
| `gLFS.isDataMounted()` | HTTP-start | HTTP: 503 |
| `sMeas.state == IDLE` | HTTP-start | HTTP: 409 |

---

## 9. HTTP statuses — GET /sensor/state

Endpoint повертає поточний `MeasState`, відображаючи стан реального вимірювання:

| MeasState | HTTP відповідь `{ "state": "…" }` |
|---|---|
| IDLE (монети немає) | `IDLE_NO_COIN` |
| IDLE (монета є, не вимірюємо) | `IDLE_COIN_PRESENT` |
| STEP_BASE | `MEASURING_STEP_BASE` |
| STEP_1 | `MEASURING_STEP_1` |
| STEP_3 | `MEASURING_STEP_3` |
| STEP_DRIFT | `MEASURING_STEP_DRIFT` |
| COMPUTE | `MEASURING_COMPUTE` |

---

## 10. Display Rendering Summary

| Функція | Треба | Час виклику |
|---|---|---|
| `drawMeasIdle()` | full screen | boot / session end / abort / timeout |
| `drawMeasStep_full()` | full screen | при кожному переході між STEP\_\* |
| Partial RP update (`y=61`) | 1 рядок | щосекунди під час STEP\_\* |
| Partial timeout update (`y=103`) | 1 рядок | щосекунди під час STEP\_\* |
| `drawMeasResult()` | full screen | після `doMeasCompute()` |

Часткові оновлення (`fillRect` + друк) запобігають мерехтінню при постійному polling RP.

---

## 11. UART Log — ключові повідомлення

| Component | Message | Значення |
|---|---|---|
| `Meas` | `HTTP start: session started (STEP_BASE)` | HTTP-start |
| `Meas` | `Step 1/4 (0.6mm): RP=XXXX  L=YYYY` | ENTER на STEP_BASE |
| `Meas` | `Step 2/4 (1.6mm): RP=XXXX  L=YYYY` | ENTER на STEP_1 |
| `Meas` | `Step 3/4 (2.6mm): RP=XXXX  L=YYYY` | ENTER на STEP_3 |
| `Meas` | `Step 4/4 drift (0.6mm): RP=XXXX  L=YYYY` | ENTER на STEP_DRIFT |
| `Meas` | `Drift X.X% > 5% — conf forced=0` | driftWarn |
| `Meas` | `Vec: dRp1=…  k1=…  k2=…  slope=…  dL1=…` | вектор |
| `Meas` | `Match: <coin>  conf=X.XX  dist=X.XXXX` | FP match |
| `Meas` | `Saved #N — 4pos [r0,r1,r2,r3]  conf=X.XX` | збережено |
| `Meas` | `Step N timeout — session aborted` | timeout |
| `Meas` | `Session aborted (Bksp)` | Backspace |

---

## 12. Пов'язані файли

| Файл | Роль |
|---|---|
| `src/main.cpp` | State machine + triggers + display + keyboard |
| `lib/StorageManager/src/VectorCompute.h` | Алгоритм вектора (`DRIFT_THRESHOLD=0.05`) |
| `lib/StorageManager/src/FingerprintCache.h` | FP query (`CONFIDENCE_SIGMA=0.3`) |
| `lib/HttpServer/src/HttpServer.cpp` | HTTP `/sensor/state` + `/measure/start` |
| `docs/api/COINTRACE_API_CLI_REFERENCE.md` | API reference |
| `docs/architecture/WAVE8_ROADMAP.md` | §C-2, §C-4 — commit history |
