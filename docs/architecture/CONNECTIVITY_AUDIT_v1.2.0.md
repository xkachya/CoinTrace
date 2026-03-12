# Connectivity Architecture — Independent Audit v1.2.0

**Документ що аудитується:** `CONNECTIVITY_ARCHITECTURE.md` v1.2.0  
**Дата аудиту:** 2026-03-12  
**Метод:** Незалежний перегляд без читання попередніх рецензій, перехресна перевірка з STORAGE, LOGGER, FINGERPRINT_DB

---

## Резюме

Документ v1.2.0 — повноцінна connectivity специфікація: Serial JSON-Lines + COBS, HTTP REST з versioning, WebSocket, BLE GATT з зафіксованими UUID, WiFi AP/STA, OTA. Добре опрацьовані bottleneck секції (§8), heap budget (§8.7), Mixed Content HTTPS обмеження (§6.3). Знайдено 7 знахідок. Одна критична (стала посилання RING_SIZE після зміни), одна висока (BLE encoding overflow), одна висока (COBS scale gap), три середніх.

**Загальна оцінка:** 8.0 / 10

---

## Знахідки

---

### CO-01 [CRITICAL] — §5.2 ID range check hardcodes 300 замість RING_SIZE=250

**Файл:** `CONNECTIVITY_ARCHITECTURE.md` §5.2  
**Рядок:** `id < meas_count − 300`

**Опис:**  
STORAGE_ARCHITECTURE.md v1.4.0 (commit `4e13d32`) змінила `RING_SIZE 300 → 250`. CONNECTIVITY §5.2 містить жорстко закодоване значення в умові ID range validation:

```
ID range validation (STORAGE_ARCHITECTURE §12.3, [PRE-2]): firmware повертає HTTP 404 якщо:
  meas_count > RING_SIZE
    AND id < meas_count − 300  ← має бути 250
```

**Наслідок:** Firmware що реалізує `id < meas_count − 300` замість `id < meas_count − 250` допускає запит вимірів 251–300 яких вже немає у ring buffer (overwritten). Відповідь буде HTTP 200 з даними іншого (старого) виміру — це та сама silent wrong data яку PRE-2 мав унеможливити.

**Виправлення:**  
```
meas_count > RING_SIZE
  AND id < meas_count − RING_SIZE  ← завжди використовувати константу, не число
```
Замінити жорстке `300` на `RING_SIZE` (250). Firmware: `#define RING_SIZE 250` у `src/config/fingerprint_config.h`.

---

### CO-02 [HIGH] — BLE RESULT `dl:2u` uint16 overflow для магнітних металів

**Файл:** `CONNECTIVITY_ARCHITECTURE.md` §5.4 (BLE GATT RESULT characteristic)

**Специфіковано:**
```
dl:2u  → uint16, зберігати як round(dL1 * 100); читати / 100
           (dL1=0.18µH → 18; фізичний діапазон 0.00–20.00 µH)
```

**FINGERPRINT_DB §3.3 говорить:**
> «Магнітні (Fe, Ni): 200–2000 µH. `dL1_MAX = 2000 µH`.»

`round(2000 * 100) = 200000 > uint16_max (65535)` — **silent overflow**.  
Сталева монета: `dL1 ≈ 500 µH → round(500*100) = 50000 < 65535` — ще вписується. Але:  
- Нікель: `dL1 → 1200 µH → 120000 > 65535` — overflow ✗  
- Феромагнітний (старий залізний цент): `dL1 → 2000 µH → 200000` — overflow ✗

Заявлений «фізичний діапазон 0.00–20.00 µH» в специфікації охоплює тільки кольорові метали (Ag, Cu, Au, Al). Для залізовмісних монет характеристика тихо передасть truncated значення → client отримає `dL1 = 0.something` замість `dL1 = 12.34 µH`.

**Рекомендація:** Кодувати нормалізований `dL1_n = dL1 / dL1_MAX`:
```
dl:2u  → uint16, зберігати як round(dL1 / dL1_MAX * 10000); читати × dL1_MAX / 10000
          де dL1_MAX = 2000 µH (константа з fingerprint_config.h)
          Range: 0–10000 (= 0.0000–1.0000 dL1_n), uint16 max = 65535 ✓
```
Або позначити що `dl` = кліп до `min(round(dL1*100), 65535)` і клієнт знає що значення > 654.00 µH saturated.

---

### CO-03 [HIGH] — COBS frame: `rp_i32`/`l_i32` — integer encoding, scale не специфіковано

**Файл:** `CONNECTIVITY_ARCHITECTURE.md` §5.1 (COBS binary raw stream)

**Специфіковано:**
```
Frame: [type:1][seq:2][rp_i32:4][l_i32:4][ms_u32:4][crc16:2] = 17 байт
```

**Проблема 1 — precision loss:** JSON raw stream у тій самій секції:
```json
{"t":"raw","rp":1250.5,"l":18.2,"ms":1234}
```
`rp` має дробову частину 0.5 Ohm, `l` — 0.1 µH. Але `rp_i32` і `l_i32` є 32-bit integers — при збереженні як int, дробова частина втрачається. Scale factor (наприклад: `rp_i32 = round(rp * 100)` для 0.01 Ohm precision) не визначений.

**Проблема 2 — назва `i32` vs реальний тип:** Rp > 0 завжди (5000–15000 Ohm), L > 0 завжди — тому доцільніший `uint32`, але названо `i32`. Незначно, але вносить confusion.

**Проблема 3 — vs float32:** Поле `rp:4f` в BLE кодується як IEEE 754 float32 (показано явно). В COBS frame — int32 без float. Неузгодженість між двома транспортами без пояснення.

**Виправлення:** Специфікувати scale:
```
rp_i32   = round(rp_ohm * 100)      # 0.01 Ohm precision; макс rp=15000 → 1500000 < INT32_MAX ✓
l_i32    = round(l_uH * 1000)       # 0.001 µH precision;  макс l=2000  → 2000000 < INT32_MAX ✓
CRC вираховується по bytes [0..14], тобто по всьому frame крім 2 байт CRC
```

---

### CO-04 [MEDIUM] — POST /api/v1/measure/start: HTTP re-entrancy response не специфіковано

**Файл:** `CONNECTIVITY_ARCHITECTURE.md` §5.2

**Serial протокол** у §5.1 явно специфікує:
```json
{"t":"ack","seq":5,"status":"err","code":"ALREADY_MEASURING"}
```

**HTTP REST §5.2** для `POST /api/v1/measure/start` показує тільки успішний response:
```
→ {"id":43,"status":"measuring","eta_ms":2000}
```

Нема визначення що повертається якщо вимір вже виконується. Клієнт (Web UI, Python CLI) що викликає measure/start двічі отримає невизначену поведінку — залежно від firmware реалізації: або 200 OK з новим id (racing!), або 500, або 503.

**Рекомендація:** Додати до §5.2:
```
POST /api/v1/measure/start
  → якщо вимір активний: 409 Conflict
    {"error":"already_measuring","current_id":42,"eta_ms":1200}
  → якщо система DEGRADED (LittleFS_data fail): 503 Service Unavailable
    {"error":"storage_unavailable"}
```

---

### CO-05 [MEDIUM] — POST /api/v1/database/match: validation spec відсутня

**Файл:** `CONNECTIVITY_ARCHITECTURE.md` §5.2

**Endpoint** специфікований з body format і success response. Відсутні:

| Випадок | Очікувана поведінка | Специфіковано? |
|---------|---------------------|----------------|
| `algo_ver` не підтримується firmware | 422 Unprocessable / 400? | ❌ |
| `protocol_id` відсутній у локальній базі | 404 або порожній results? | ❌ |
| Поле `vector` відсутнє або null | 400 Bad Request? | ❌ |
| Поля `dRp1`/`k1`/`k2` поза BOUNDS | 400? або match з low confidence? | ❌ |
| Body > X KB (ArduinoJson limit) | AsyncWebServer behavior? | ❌ |

**Наслідок:** Кожен розробник firmware буде вирішувати по-своєму → несумісна поведінка між версіями firmware.

**Рекомендація:** Додати error responses до §5.2 для цього endpoint:
```
POST /api/v1/database/match
  → 400 якщо missing required fields або out-of-range values
  → 422 якщо algo_ver або protocol_ver не підтримується
  → 503 якщо index.json не завантажений (SD відсутня, cache miss)
  → 200 з empty alternatives[] якщо protocol_id не знайдено
     (не 404 — request валідний, просто база не має цього протоколу)
```

---

### CO-06 [MEDIUM] — OTA: firmware integrity check не специфікований

**Файл:** `CONNECTIVITY_ARCHITECTURE.md` §7 ADR-007

**ADR-007 говорить:** «OTA активується тільки після фізичного підтвердження (клавіша `'O'` на пристрої)».  
NVS encryption ризик відзначено: «TODO: NVS Encryption в v2».

**Що відсутнє:** Специфікація перевірки firmware image integrity:
- ESP32-IDF OTA має вбудований CRC32 per partition, але **не** криптографічний підпис
- Якщо upload обривається посередині — ESP32 OTA реалізація не boot-ує corrupted partition (rollback) ← це вже є
- Але: навмисно tampered binary (роздатчик у публічному місці) пройде без підпису

**Оцінка ризику:** MEDIUM, не HIGH — бо потребує фізичної присутності і відкритого 30-секундного вікна. Але варто задокументувати явно.

**Рекомендація:** Додати до ADR-007:
```
v1: Захист = фізична кнопка. Прийнято. Ризик = tampered binary при публічній демонстрації.
v2 (backlog): ECDSA підпис із публічним ключем у firmware. Приватний ключ у CI.
POST /api/v1/ota/update: Content-Type: application/octet-stream; Content-Length обов'язковий.
Firmware: esp_ota_handle_t + CRC32 per ESP-IDF rollback mechanism ← вже є, задокументувати явно.
```

---

### CO-07 [LOW] — "ping"/"pong" в WebSocket app messages конфліктують з RFC 6455

**Файл:** `CONNECTIVITY_ARCHITECTURE.md` §5.3

**RFC 6455** визначає WebSocket control frames: Ping (opcode 0x9) і Pong (opcode 0xA) на протокольному рівні. Бібліотека `AsyncWebSocket` обробляє їх автоматично.

Документ використовує `{"v":1,"t":"ping"}` і `{"v":1,"t":"pong"}` як JSON application messages в окремому потоці. Це два конкурентних "ping" механізми з однаковою назвою але різними рівнями.

**Наслідок:** Розробник frontend може переплутати RFC 6455 ping event з application-level `t:ping`. Деякі BLE/WebSocket debugging tools (nRF Connect, wscat) показують RFC ping/pong окремо.

**Рекомендація:** Перейменувати application messages на `"t":"heartbeat"` і `"t":"heartbeat_ack"`, або `"t":"alive"` / `"t":"alive_ack"`. Не блокує v1.

---

## Таблиця знахідок

| ID | Серйозність | Секція | Суть | Блокує |
|----|-------------|--------|------|--------|
| CO-01 | 🔴 CRITICAL | §5.2 | ID range hardcodes 300 (RING_SIZE→250) | silent wrong data |
| CO-02 | 🟠 HIGH | §5.4 BLE RESULT | dl:2u uint16 overflow для Fe/Ni (dL1 2000 µH) | некоректні match для магнітних монет |
| CO-03 | 🟠 HIGH | §5.1 COBS | rp_i32/l_i32 integer, scale не специфіковано | fractional precision loss у stream |
| CO-04 | 🟡 MEDIUM | §5.2 REST | measure/start re-entrancy HTTP response не spec | undefined firmware поведінка |
| CO-05 | 🟡 MEDIUM | §5.2 REST | database/match validation error responses відсутні | несумісна поведінка між firmware vers. |
| CO-06 | 🟡 MEDIUM | ADR-007 OTA | firmware integrity check не spec (прийнятний ризик v1) | tampered binary при публ. демо |
| CO-07 | 🔵 LOW | §5.3 WS | ping/pong naming конфліктує RFC 6455 | developer confusion |

---

## Cross-doc знахідки (також в FINGERPRINT_DB_AUDIT)

| FINGERPRINT_DB | CONNECTIVITY | Суть |
|----------------|--------------|------|
| FDB-01 [CRITICAL] | CO-01 [CRITICAL] | RING_SIZE=300 стале посилання — один і той самий issue |
| FDB-02 [HIGH] | CO-02 [HIGH] | dL1 overflow — похідний наслідок від FDB-02 |

Обидва знахідки потребують одного виправлення:
1. CONNECTIVITY §5.2: `300` → `RING_SIZE` (250)
2. BLE dl encoding: нормалізований uint16 замість абсолютного µH×100

---

## Пріоритет виправлень

**Перед реалізацією Фази 1 (WiFi AP + HTTP REST):**
- CO-01: RING_SIZE fix (1 рядок — CONNECTIVITY §5.2 doc fix + firmware constant)
- CO-04: measure/start 409 spec
- CO-05: database/match 400/422/503 spec

**Перед реалізацією BLE (Фаза 1.5):**
- CO-02: dl encoding fix (BLE RESULT spec)
- CO-03: COBS scale spec

**Backlog:**
- CO-06: ECDSA for v2
- CO-07: rename heartbeat

---

*Аудит: незалежний перегляд, без доступу до попередніх рецензій*  
*Версія документа що аудитувалась: CONNECTIVITY_ARCHITECTURE.md v1.2.0 (2026-03-11)*
