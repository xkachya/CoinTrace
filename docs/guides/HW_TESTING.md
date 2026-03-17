# Hardware Testing Guide — CoinTrace REST API Verification

**Applies to:** CoinTrace firmware ≥ Wave 8 A-2, M5Stack Cardputer-Adv (ESP32-S3FN8), Windows  
**Updated:** 2026-03-18  
**Cross-ref:**
- `docs/guides/DEBUGGING.md` — low-level hardware debug (boot log, partitions, download mode)
- `docs/guides/UART_DEBUG_SETUP.md` — FT232RL wiring (required for boot-log capture)
- `docs/guides/development-setup.md` — initial setup, WiFi provisioning
- `docs/architecture/MEMORY_MAP.md` — детальна карта пам'яті Flash/SRAM/Heap
- `scripts/hw_test.py` — automated test script

---

## Overview

Full verification pipeline for a firmware release or a significant feature branch:

```
1. Native unit tests   (108 tests, no hardware, ~25 s)
2. Firmware build      (RAM/Flash size check)
3. Flash               (upload firmware to device)
4. hw_test.py          (9 REST endpoint tests + memory diagnostics)
```

All four steps must pass before committing. Steps 1–3 run on the PC; step 4 requires the
device to be powered, connected, and joined to the same WiFi network.

All commands below are run from the **project root** (`d:\GitHub\CoinTrace`).

---

## Prerequisites

| Requirement | Detail |
|---|---|
| FT232RL on EXT 2.54-14P | COM4 — needed to capture boot log; see `UART_DEBUG_SETUP.md` |
| Device connected via USB | COM3 — used for flash + esptool reset |
| Device on WiFi (STA mode) | SSID "YuKa", IP 192.168.88.53 (provisioned once via keyboard) |
| Python venv | `d:\GitHub\CoinTrace\.venv` with `pyserial` installed |

### One-time venv setup

```powershell
cd d:\GitHub\CoinTrace
python -m venv .venv
.venv\Scripts\pip install pyserial
```

---

## Step 1 — Native Unit Tests

Run all 108 tests on the host (no device needed):

```powershell
cd d:\GitHub\CoinTrace
pio test -e native-test
```

**Expected:** all suites PASS, zero FAIL/ERROR, ~25 s total.

**If you get `ERRORED — "The process cannot access the file"` on Windows:**
Windows Defender is locking `.pio/build/` object files. Fix (one-time, Admin PowerShell):
```powershell
Add-MpPreference -ExclusionPath "D:\GitHub\CoinTrace\.pio"
```
See `DEBUGGING.md §8` for details.

**If `uxTaskGetStackHighWaterMark was not declared in this scope`:**
The `#ifdef ESP_PLATFORM` guard is missing in `LittleFSTransport.cpp`. That function is
FreeRTOS-only and must be guarded to compile on host. See `lib/Logger/src/LittleFSTransport.cpp`.

---

## Step 2 — Firmware Build (size check)

```powershell
pio run -e cointrace-dev 2>&1 | Select-String "error:|RAM:|Flash:"
```

**Expected healthy ranges:**

| Metric | Budget limit | Current baseline (2026-03-18) |
|---|---|---|
| RAM | < 70% (229376 B) | 61.5% — 201676 B |
| Flash | < 75% (1966080 B) | 56.0% — 1468381 B |
| Errors | 0 | 0 |

If Flash approaches 70%: run `pio run -e cointrace-dev -v 2>&1 | Select-String "\.a\b"` to
identify large static libraries.

---

## Step 3 — Flash

```powershell
pio run -e cointrace-dev -t upload
```

Typical: ~9.5 s, 871 KB compressed, hash verified at end.

`pio run -e uploadfs-sys -t uploadfs` is **only needed** when `data/` (web UI, device.json,
plugin configs) has changed. Not required for pure firmware changes.

After full erase (`-t erase`) you must re-run uploadfs-sys — firmware upload alone does
not restore the sys LittleFS partition.

---

## Step 4 — hw_test.py

### Running the tests

| Command | When to use |
|---|---|
| `python scripts/hw_test.py` | Full run — resets device, waits for boot, runs 9 tests |
| `python scripts/hw_test.py --no-reset` | Skip reset; assumes device is already running |
| `python scripts/hw_test.py --ip 192.168.88.53` | Skip reset + boot-wait entirely (device already up) |

Full path to venv Python:

```powershell
d:\GitHub\CoinTrace\.venv\Scripts\python.exe scripts/hw_test.py
```

### What the script does

1. Optionally resets the device via `esptool run` on COM3 (no DTR toggle — avoids boot loop).
2. Passively listens on COM4 (FT232RL) for the `"HTTP ... ready"` log line → extracts IP.
3. Runs 9 endpoint tests sequentially (see table below).
4. Takes a heap snapshot **after** `POST /database/match` (the most heap-intensive operation).
5. Takes a final `GET /status` and calculates heap drift across the run.
6. Prints a final measurements summary block.

### Test suite reference

| # | Endpoint | Expected HTTP status | What it validates |
|---|---|---|---|
| 1 | `GET /api/v1/status` | 200, `heap_max_block ≥ 50%` | System health + heap fragmentation |
| 2 | `GET /api/v1/sensor/state` | 200, `state=IDLE_NO_COIN` | Sensor state machine (no sensor attached) |
| 3 | `GET /api/v1/database` | 200, `count=5`, `ready=true` | FingerprintCache loaded correctly |
| 4 | `GET /api/v1/log?n=5` | 200, `entries=5` | RingBuffer transport + logging pipeline |
| 5 | `GET /api/v1/measure/0` | 200 **or** 404 | MeasurementStore ring-buffer index check |
| 6 | `GET /api/v1/ota/status` | 200, `pending=false` | NVS OTA key present |
| 7 | `POST /api/v1/database/match` | 200, `conf ≥ 0.8` | Fingerprint matching engine (synthetic vector) |
| 8 | `POST /api/v1/measure/start` | 503 `sensor_not_ready` | Correct pre-sensor stub behavior |
| 9 | `POST /api/v1/ota/update` | 403 `ota_window_not_active` | OTA security guard |
| — | Heap stability | `drift < 1024 B` | No memory leak across the test run |

---

## Memory Diagnostics Reference

### `heap` and `heap_min`

`heap` — free heap at query time (from `ESP.getFreeHeap()`).
Must remain above **25 KB** for reliable operation — AsyncTCP allocates ~4 KB per active
connection from internal heap (no PSRAM on ESP32-S3FN8).

`heap_min` — lowest `heap` has ever reached since boot (from `ESP.getMinFreeHeap()`).
A very low `heap_min` (< 15 KB) indicates a transient allocation spike; watch for OOM on
next cold boot or under higher load.

### `heap_max_block` — fragmentation indicator

`heap_max_block` = largest contiguous allocatable block = `ESP.getMaxAllocHeap()`.

**Why it matters:** the reported free heap can be 30 KB, but if heavily fragmented the
largest block may be only 5 KB. ArduinoJson document allocation and AsyncTCP accept buffers
both require contiguous blocks — they will fail-allocate and crash even with "plenty of heap."

**Fragmentation ratio** = `heap_max_block / heap`:

| Ratio | Status | hw_test.py output |
|---|---|---|
| ≥ 50% | Healthy | `← healthy` |
| 30–50% | Watch | No specific warning yet |
| < 30% | Fragmented | `← FRAGMENTED` — investigate immediately |

**Reference measurements (2026-03-18, STA mode):**

| Condition | heap | heap_max_block | Ratio |
|---|---|---|---|
| Idle (after boot) | 30052 B | 21492 B | **72%** |
| After POST /database/match | 29104 B | 18420 B | **63%** |

### Heap drift

Difference between heap at test #1 and heap at final `GET /status` after all 9 tests.

| Drift | Interpretation |
|---|---|
| < 1024 B | ✅ Stable — no memory leak |
| 1024–2048 B | ⚠️ Marginal — re-run to confirm; may be normal startup amortization |
| > 2048 B | ❌ Leak likely — a response handler is not freeing a buffer |

Reference (2026-03-18): **948 B drift across 9 tests** → stable.

---

## DEBUG-Level Log Diagnostics via REST

Some diagnostics are logged at `DEBUG` level; they are invisible to the default `INFO` log
stream but stored in the RingBufferTransport and accessible via REST.

```powershell
$ip = "192.168.88.53"
curl "http://$ip/api/v1/log?n=50&level=DEBUG"
```

### Expected DEBUG entries after a cold boot

| Entry | Appears at | Meaning |
|---|---|---|
| `DEBUG Mem \| sizeof(LogEntry)=220 sizeof(CacheEntry)=140 (MAX=1000 entries)` | ~1484 ms | Struct layout check |
| `DEBUG Heap \| before WiFi: 107408 B free` | ~1642 ms | Pre-WiFi heap baseline |
| `DEBUG Heap \| after WiFi:  54488 B free` | ~2059 ms | WiFi STA stack cost (~−53 KB) |
| `DEBUG Heap \| after HTTP:  34844 B free` | ~2092 ms | HTTP route registration (~0 KB) |
| `DEBUG Stack \| LFS task watermark: 1332 B free (of 3072 B stack)` | ~10005 ms | LFS task stack health |

### sizeof(CacheEntry) — memory budget check

`FingerprintCache::MAX_ENTRIES = 1000`. At `sizeof(CacheEntry) = 140 B`:
→ maximum load = 1000 × 140 B = **140 KB** of SRAM.

This exceeds total available SRAM (165 KB usable) if the cache were ever fully populated.
In practice the production database has ~30–50 entries; the limit is a safety cap.

**If `sizeof(CacheEntry)` increases above ~180 B** (struct padding changed, new fields):
→ reduce `FingerprintCache::MAX_ENTRIES` to keep the theoretical max below 120 KB.

### LFS task stack watermark

The watermark is logged once, ≥10 s after boot, giving the task time to process all
boot-log entries (the most intensive burst of writes).

| Watermark (B free) | Action |
|---|---|
| ≥ 512 B | ✅ Safe |
| 256–511 B | ⚠️ Narrow margin — do not increase LFS task workload |
| < 256 B | ❌ Stack overflow risk — increase `/*stack=*/` in `LittleFSTransport.cpp` `startTask()` |

Current (2026-03-18): **1332 B free** of 3072 B allocated → safe, 43% stack unused.

---

## Manual REST API Testing (curl)

Quick, targeted checks without running the full hw_test.py suite.

```powershell
$ip = "192.168.88.53"

# System health (heap + uptime + wifi)
curl "http://$ip/api/v1/status"

# Last 20 INFO log entries
curl "http://$ip/api/v1/log?n=20"

# Last 50 DEBUG log entries (heap diags, sizeof, stack watermark)
curl "http://$ip/api/v1/log?n=50&level=DEBUG"

# Sensor state
curl "http://$ip/api/v1/sensor/state"

# Fingerprint database info
curl "http://$ip/api/v1/database"

# Run a fingerprint match (synthetic test vector — matches xcu/synthetic at ~0.919)
curl -X POST "http://$ip/api/v1/database/match" `
     -H "Content-Type: application/json" `
     -d '{"algo_ver":1,"protocol_id":"p1_UNKNOWN_013mm","vector":{"dRp1":312.4,"k1":0.742,"k2":0.531,"slope_rp_per_mm_lr":-0.128,"dL1":0.18}}'

# Latest measurement by ring-buffer index (404 if nothing stored yet)
curl "http://$ip/api/v1/measure/0"

# OTA status
curl "http://$ip/api/v1/ota/status"
```

---

## Troubleshooting

### `heap_max_block field missing` warning

`heap_max_block` absent from `GET /status` response → outdated firmware (pre-2026-03-18).
Fix: flash the current firmware.

### `← FRAGMENTED` warning in hw_test.py

`heap_max_block < 50%` of `heap`. Possible causes:
- Device has been running for a long time with many requests (normal fragmentation)
- A handler is allocating and not freeing in-order (buddy allocator can't coalesce)

First step: power-cycle and re-run. If fragmentation persists after a cold boot → check
recent ArduinoJson `DynamicJsonDocument` scoping and `AsyncResponseStream` usage.

### `drift > 1 KB` warning

Heap growing across the test run → memory leak.
Check: `new`/`malloc` in request handlers without matching `delete`/`free`; JSON document
not going out of scope before `response->send()` is called; `String` temporaries inside
handlers accumulating in the FreeRTOS heap.

### Boot timeout — COM4 never saw "HTTP ready"

hw_test.py exits after 30 s without seeing the ready line.

1. Verify FT232RL is wired correctly to G15 (TX→RXD) / G13 (RX→TXD) — see `UART_DEBUG_SETUP.md`.
2. Run `pio device monitor -e cointrace-dev` (COM4) manually and power-cycle: do you see any output?
3. If device is in AP mode instead of STA → WiFi provisioning needed (`DEBUGGING.md` or `development-setup.md`).
4. To skip boot-wait entirely when IP is known: `python scripts/hw_test.py --ip 192.168.88.53`.

### POST /database/match returns 503

`FingerprintCache` is not ready (still loading, or LittleFS mount failed).
Check boot log: `INFO Cache | FingerprintCache ready — N entries`. If absent → `uploadfs-sys`
may need to be re-run to restore `data/plugins/ldc1101.json`.

---

## Baseline Reference (Wave 8 A-2, 2026-03-18)

| Metric | Value |
|---|---|
| PlatformIO env | `cointrace-dev` |
| Build — RAM | 61.5% (201676 / 327680 B) |
| Build — Flash | 56.0% (1468381 / 2621440 B) |
| Native tests | 108/108 PASS |
| Hardware tests | 9/9 PASS |
| Device IP | 192.168.88.53 (SSID: YuKa) |
| boot → HTTP ready | 2092 ms |
| heap idle | 30052 B |
| heap_min | 30044 B |
| heap_max_block idle | 21492 B (72% of heap) |
| heap_max_block under POST load | 18420 B (63% of heap) |
| heap drift (9 tests) | 948 B |
| sizeof(LogEntry) | 220 B |
| sizeof(CacheEntry) | 140 B |
| LFS task stack allocated | 3072 B |
| LFS task stack watermark | 1332 B free (43% unused) |
| POST /database/match confidence | 0.919 (xcu/synthetic) |
