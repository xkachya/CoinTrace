# CoinTrace API & CLI Reference

**Дата:** 2026-03-19  
**Актуально для:** v1 (Wave 8, без LDC1101)

---

## Огляд

Цей документ містить детальний перелік REST API endpoint-ів та CLI-команд для розробки, дебагінгу та тестування CoinTrace. Вказано статус реалізації, приклади викликів (curl/PowerShell), очікувані відповіді та типові помилки.

---

## 1. REST API (HTTP)

**Базова адреса:**
- WiFi AP: `http://192.168.4.1/api/v1/`
- mDNS:    `http://cointrace.local/api/v1/`

### 1.1. Device Status
- **GET /status**  
  **Опис:** Системна інформація (heap, uptime, WiFi, версія)
  **Реалізовано:** ✅
  **Приклад:**
  ```sh
  curl "http://$ip/api/v1/status"
  ```
  **Відповідь:**
  ```json
  {"version":"1.0.0","heap":320048,"heap_min":295040,"heap_max_block":8000,"uptime":12345,"wifi":"ap","ip":"192.168.4.1","ble":"off","meas_count":12}
  ```

### 1.2. Log Entries
- **GET /log?n=50&level=DEBUG&since_ms=0**  
  **Опис:** Останні логи (DEBUG/INFO/...) з кільцевого буфера
  **Реалізовано:** ✅
  **Приклад:**
  ```sh
  curl "http://$ip/api/v1/log?n=50&level=DEBUG"
  ```
  **Відповідь:**
  ```json
  {"entries":[{"ms":672,"level":"INFO","comp":"System","msg":"..."},...],"next_ms":13456}
  ```

### 1.3. Sensor State
- **GET /sensor/state**  
  **Опис:** Стан сенсора (stub: IDLE_NO_COIN)
  **Реалізовано:** ✅ (stub, до інтеграції LDC1101)
  **Приклад:**
  ```sh
  curl "http://$ip/api/v1/sensor/state"
  ```
  **Відповідь:** `{ "state": "IDLE_NO_COIN" }`

### 1.4. Fingerprint Database
- **GET /database**  
  **Опис:** Статус кешу відбитків, кількість монет
  **Реалізовано:** ✅
  **Приклад:**
  ```sh
  curl "http://$ip/api/v1/database"
  ```
  **Відповідь:** `{ "count": 12, "ready": true }`

- **POST /database/match**  
  **Опис:** Пошук монети за виміряним вектором
  **Реалізовано:** ✅
  **Приклад:**
  ```sh
  curl -X POST "http://$ip/api/v1/database/match" \
       -H "Content-Type: application/json" \
       -d '{"algo_ver":1,"protocol_id":"p1_UNKNOWN_013mm","vector":{"dRp1":312.4,"k1":0.742,"k2":0.531,"slope_rp_per_mm_lr":-0.128,"dL1":0.18}}'
  ```
  **Відповідь:**
  ```json
  {"match":"Ag925","conf":0.94,"coin_name":"Austrian Maria Theresa Thaler","alternatives":[{"metal_code":"XAG900","coin_name":"US Morgan Dollar 1881","conf":0.71}]}
  ```

### 1.5. Measurements
- **GET /measure/{id}**  
  **Опис:** Отримати вимір за ID (0...meas_count-1)
  **Реалізовано:** ✅
  **Приклад:**
  ```sh
  curl "http://$ip/api/v1/measure/0"
  ```
  **Відповідь:** `{ "id": 0, ... }` або 404 якщо не існує

- **POST /measure/start**  
  **Опис:** Запустити новий вимір (сенсор не підключено)
  **Реалізовано:** 🚧 (stub, повертає 503)
  **Приклад:**
  ```sh
  curl -X POST "http://$ip/api/v1/measure/start"
  ```
  **Відповідь:** `{ "error": "sensor_not_ready" }`

### 1.6. Calibration
- **POST /calibrate**  
  **Опис:** Запустити калібрування (сенсор не підключено)
  **Реалізовано:** 🚧 (stub, повертає 503)
  **Приклад:**
  ```sh
  curl -X POST "http://$ip/api/v1/calibrate"
  ```
  **Відповідь:** `{ "error": "sensor_not_ready" }`

### 1.7. OTA Update
- **GET /ota/status**  
  **Опис:** OTA-статус, вікно, pending/confirmed
  **Реалізовано:** ✅
  **Приклад:**
  ```sh
  curl "http://$ip/api/v1/ota/status"
  ```
  **Відповідь:** `{ "ota_window": false, "pending": false, ... }`

- **POST /ota/update**  
  **Опис:** OTA-оновлення (тільки через API, потрібна фізична активація)
  **Реалізовано:** ✅
  **Приклад:**
  ```sh
  curl -X POST --data-binary @firmware.bin -H "Content-Type: application/octet-stream" http://$ip/api/v1/ota/update
  ```
  **Відповідь:** `{ "status": "ok", "action": "rebooting" }` або помилка

### 1.8. Settings
- **GET /settings**  
  **Опис:** Прочитати налаштування (ім'я, мова, яскравість, log_level)
  **Реалізовано:** ✅
  **Приклад:**
  ```sh
  curl "http://$ip/api/v1/settings"
  ```
  **Відповідь:** `{ "dev_name": "CoinTrace", ... }`

- **POST /settings**  
  **Опис:** Оновити налаштування (JSON, часткове оновлення)
  **Реалізовано:** ✅
  **Приклад:**
  ```sh
  curl -X POST "http://$ip/api/v1/settings" \
       -H "Content-Type: application/json" \
       -d '{"dev_name":"TestDevice","brightness":180}'
  ```
  **Відповідь:** `{ "ok": true, "needs_restart": false }`

---

## 2. WebSocket API

- **ws://cointrace.local/api/v1/stream**  
  **Опис:** Live-стрім даних (сенсор, статус, логи)
  **Реалізовано:** ✅
  **Протокол:** JSON frames, поле `"t"` визначає тип ("sensor", "result", "status", "log", ...)

---

## 3. CLI-команди (PlatformIO, PowerShell, Python)

### 3.1. Build & Flash
- **Зібрати firmware:**
  ```powershell
  pio run -e cointrace-dev
  ```
- **Залити firmware:**
  ```powershell
  pio run -e cointrace-dev -t upload
  ```
- **Залити sys partition (web UI):**
  ```powershell
  pio run -e uploadfs-sys -t uploadfs
  ```

### 3.2. OTA Update (скрипт)
- **Автоматизований OTA upload:**
  ```powershell
  python scripts/test_ota_flash.py --ip <device_ip> --env cointrace-dev --fw firmware.bin
  ```
- **Ручний OTA через curl:**
  ```sh
  curl -X POST --data-binary @firmware.bin -H "Content-Type: application/octet-stream" http://<device_ip>/api/v1/ota/update
  ```

### 3.3. Серіал/моніторинг
- **Live UART monitor:**
  ```powershell
  pio device monitor -e cointrace-dev
  ```
- **Читати лог через PowerShell:**
  ```powershell
  $ser = New-Object System.IO.Ports.SerialPort "COM3",115200
  $ser.Open(); Start-Sleep 2; $ser.ReadExisting(); $ser.Close()
  ```

### 3.4. Partition/Flash Tools
- **Перевірити partition table:**
  ```powershell
  python -m esptool --chip esp32s3 --port COM3 read_flash 0x8000 0x1000 partition_backup.bin
  python -m esptool --chip esp32s3 partition_table --verify partition_backup.bin
  ```
- **Зробити повний erase:**
  ```powershell
  pio run -e cointrace-dev -t erase
  ```

---

## 4. Типові помилки та відповіді

- 404: Not Found (невірний ID виміру)
- 400: Bad Request (некоректний JSON, відсутні поля)
- 403: Forbidden (OTA window не активне)
- 503: Service Unavailable (сенсор не підключено, storage недоступний)
- 422: Unprocessable Entity (непідтримуваний algo_ver)

---

## 5. Додаткові ресурси
- [docs/guides/HW_TESTING.md](../guides/HW_TESTING.md) — приклади curl, діагностика
- [docs/guides/DEBUGGING.md](../guides/DEBUGGING.md) — UART, PowerShell, PlatformIO
- [docs/OTA_UPDATE_FOR_DEVELOPERS.md](../OTA_UPDATE_FOR_DEVELOPERS.md) — OTA-процедура
- [lib/HttpServer/src/HttpServer.cpp](../../lib/HttpServer/src/HttpServer.cpp) — реалізація API

---

**Оновлено:** 2026-03-19
