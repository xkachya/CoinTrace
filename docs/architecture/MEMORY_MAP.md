# Memory Map — CoinTrace Firmware

**Hardware:** M5Stack Cardputer-Adv · ESP32-S3FN8 · 8 MB Flash · **no PSRAM**  
**Version:** 1.0.0-dev (Wave 8 A-2)  
**Measured:** 2026-03-18 · hw_test.py 9/9 PASS · `ESP.getFreeHeap()` at each boot stage  
**Cross-ref:** `docs/guides/HW_TESTING.md`, `docs/architecture/WAVE8_ROADMAP.md §A-2`

---

## 1. Flash Layout (8 MB)

```
Address       Size       Name              Type           Usage
──────────────────────────────────────────────────────────────────────────
0x000000    ~36 KB      [ROM bootloader]  (ESP32-S3 ROM)  chip ROM, not counted
0x008000     4 KB       [partition table] esp-idf         offset 0x8000, 0xC00 bytes
0x009000    20 KB       nvs               data/nvs        runtime config, meas_count, WiFi creds
0x00E000     8 KB       otadata           data/ota        active OTA slot pointer
0x010000  2560 KB       app0 (ota_0)      app/ota_0       ★ active firmware (current)
0x290000  2560 KB       app1 (ota_1)      app/ota_1       OTA download target (Wave 8 C-3)
0x510000  1024 KB       littlefs_sys      data/spiffs     web UI, device.json, plugin configs
0x610000  1792 KB       littlefs_data     data/spiffs     measurements ring, LFS logs, cache
0x7D0000   192 KB       coredump          data/coredump   post-crash dump (panic handler)
──────────────────────────────────────────────────────────────────────────
Total       8192 KB     8 MB
```

### Firmware partition usage

| Metric | Value |
|--------|-------|
| Firmware size (app0) | 1468381 B = **1.40 MB** of 2.5 MB |
| Flash used | **56.0%** |
| OTA headroom | 1092059 B = 1.04 MB free (OTA update fits if Δ < 1 MB) |

### littlefs_sys usage (~5 KB of 1024 KB)

```
/web/index.html         ~3 KB    placeholder web UI
/config/device.json     ~0.5 KB  { "name": "CoinTrace", ... }
/plugins/ldc1101.json   ~1 KB    LDC1101 plugin config + calibration
```

### littlefs_data usage (grows at runtime)

```
/measurements/m_000.json  …  /m_249.json    ring buffer, NVS_RING_SIZE=250 slots
/logs/log_YYYYMMDD.jsonl                    LittleFSTransport rotation, max 200 KB total
/cache/index.json                           FingerprintCache disk mirror (built from SD)
/cache/index_crc32.bin                      integrity checksum
```

---

## 2. SRAM — Physical Layout (ESP32-S3FN8)

```
 ┌───────────────────────────────────────────────────────┐  ← top of DRAM (approx. 0x3FC00000)
 │  ESP-IDF / ROM reserved                               │  ~63 KB
 │  (ROM functions, DROM cache, interrupt stacks, etc.)  │
 ├───────────────────────────────────────────────────────┤
 │  .rodata / .text in IRAM (ISR code, often-called fns) │  ~few KB
 ├───────────────────────────────────────────────────────┤
 │  .data segment  (initialised globals)                 │  }
 │  .bss  segment  (zero-initialised globals)            │  }  201676 B total
 │    └─ FingerprintCache::entries_[1000]  140 000 B ◄─  │  }  61.5% of 327680 B budget
 │    └─ Framework WiFi/lwIP static buffers ~40 KB       │  }
 │    └─ All other CoinTrace globals         ~22 KB      │  }
 ├───────────────────────────────────────────────────────┤
 │  Heap pool  ≈ 165 KB  (165216 B free at cold boot)    │
 │  (consumed progressively during setup() — see §3)     │
 └───────────────────────────────────────────────────────┘  ← vPortMalloc base
```

**Total usable DRAM reported by PlatformIO:** 327680 B (320 KB)  
**Physical SRAM on die:** 512 KB — remainder reserved for ROM/IDF internal use.

---

## 3. Heap — Allocation Timeline

`ESP.getFreeHeap()` measured at each stage of `setup()`, STA mode, 2026-03-18:

```
165 216 B ████████████████████████████████████  cold boot (firmware entry)
          │
          │  ▼ Logger::init()           ~400 B  (FreeRTOS mutex)
          │  ▼ gRingTransport(20)      4 400 B  (20 × sizeof(LogEntry)=220 B)
          │  ▼ LittleFSTransport queue 14 080 B  (64 × 220 B FreeRTOS queue items)
          │  ▼ LittleFSTransport task   3 240 B  (TCB 164 B + stack 3072 B)
          │  ▼ LittleFS_sys mount      ~12 KB   (LittleFS lookahead/prog/read buffers)
          │  ▼ LittleFS_data mount     ~12 KB   (second partition)
          │  ▼ NVS open (3 namespaces)  ~4 KB
          │  ▼ SDCardManager (mutex)    ~100 B
          │  ▼ FingerprintCache load()  ~7 KB   (JSON parse, temp → freed;
          │                                       entries_ itself is BSS static!)
          │  ▼ StorageManager init      ~400 B
          ▼
107 408 B ██████████████████████████            before WiFi   (−57 808 B = 56.4 KB)
          │
          │  ▼ WiFi STA stack          52 920 B  lwIP (net stack, timers, sockets)
          │                                       WPA2 supplicant (mbedTLS)
          │                                       FreeRTOS WiFi task stacks
          ▼
 54 488 B ████████████████                      after WiFi connect   (−52 920 B = 51.7 KB)
          │
          │  ▼ AsyncWebServer init      ~9 KB   route object pool, connection accept list
          │  ▼ AsyncTCP LwIP pcb pool  ~10 KB   pre-allocated PCB structs
          ▼
 34 844 B ██████████                            HTTP ready — steady state idle
          │
          │  ▼ per-request (GET /status)
          │     ArduinoJson doc          ~800 B  (stack-allocated)
          │     AsyncTCP rx buffer       4 096 B  (freed after Connection:close)
          ▼
~30 052 B █████████                             after first request (some fragmentation)
```

### Steady-state budget

| Window | Free heap | Notes |
|--------|-----------|-------|
| Idle (no active request) | ~30–34 KB | Safe zone |
| During `GET /status` | ~25 KB | ArduinoJson ephemeral |
| During `POST /database/match` | ~29 KB | largest real request; 5 candidates |
| **Hard floor** (OOM threshold) | **~15 KB** | below → AsyncTCP cannot accept new conn |

---

## 4. Static BSS — Dominant Allocations

These are **compile-time** allocations. They consume RAM before `main()` / `setup()` runs.

| Symbol | Type | Size | Location |
|--------|------|------|----------|
| `gFPCache.entries_[1000]` | `CacheEntry[1000]` | **140 000 B** | BSS (zero-init) |
| `gRtcBootReason[32]` | `char[32]` | 32 B | **RTC SLOW MEM** (survives restart) |
| WiFi/lwIP static tables | framework | ~40 KB | BSS (ESP-IDF) |
| BLE stack reservation | framework | ~8 KB | BSS (not used, but linked) |
| Arduino framework buffers | framework | ~8 KB | BSS |
| `gLogger` + transports | objects | ~0.5 KB | BSS |
| All other `g*` globals | objects | ~3 KB | BSS |
| **Total** | | **≈ 202 KB** | |

> **Largest single allocation:** `FingerprintCache::entries_[1000]` at **140 KB**.  
> This is intentional — the array is pre-declared at `MAX_ENTRIES=1000` so no `realloc`
> is needed during cache load (avoids heap fragmentation at startup).  
> At 5 production entries, 995 slots are zero-filled but still reserved in BSS.  
> If the production database grows above ~300 entries, reduce `MAX_ENTRIES` or migrate
> to heap allocation (risk: fragmentation during load).

---

## 5. Heap — Per-Object Detail (steady state)

Objects allocated from heap during setup() and never freed:

| Allocation | Size | Heap consumer |
|------------|------|---------------|
| RingBuffer entries (20 × 220 B) | **4 400 B** | `gRingTransport` ctor |
| LittleFSTransport FreeRTOS queue (64 × 220 B) | **14 080 B** | `gLfsTransport.startTask()` |
| LittleFSTransport FreeRTOS TCB | 164 B | FreeRTOS kernel |
| LittleFSTransport task stack | **3 072 B** | `xTaskCreatePinnedToCore` |
| LittleFS_sys internal buffers | ~12 KB | `esp_littlefs` driver |
| LittleFS_data internal buffers | ~12 KB | `esp_littlefs` driver |
| NVS handles (×3 namespaces) | ~4 KB | `nvs_open()` |
| WiFi STA stack (lwIP + WPA2) | **~53 KB** | `esp_wifi_start()` |
| AsyncWebServer + AsyncTCP | **~19 KB** | `gHttpServer` ctor + `begin()` |
| FreeRTOS system tasks (idle, timer) | ~3 KB | FreeRTOS kernel |
| **Total permanent heap usage** | | **~125 KB** |
| **Free at idle** | | **~30 KB** |

### Per-request ephemeral allocations (freed after response)

| Request | Peak alloc | Freed by |
|---------|------------|---------|
| `GET /status, /database, /sensor/state, /ota/status` | ~800 B (JsonDocument) + 4 KB (TCP rx buf) | `Connection: close` + scope exit |
| `GET /log?n=5` | 5 × 220 B (LogEntry[]) + ~1 KB JsonDoc | `delete[] buf` + scope exit |
| `POST /database/match` | 512 B (body buf) + ~2 KB JsonDoc × 2 + 220 B (FPMatch[5]) | `free(_tempObject)` + scope exit |

---

## 6. FreeRTOS Tasks

| Task name | Core | Priority | Stack allocated | Measured usage | Free watermark |
|-----------|------|----------|----------------|----------------|----------------|
| `main` (setup/loop) | 1 | 1 | 8 192 B (Arduino default) | not sampled | — |
| `lfs_log` | 0 | 2 | **3 072 B** | 2 764 B | **1 332 B** ✅ |
| `wifi_task` | 0 | 23 | ~8 KB (framework) | — | — |
| `async_tcp` | 0 | 3 | ~4 KB (framework) | — | — |
| `IDLE0` / `IDLE1` | 0/1 | 0 | 1 024 B each | — | — |
| `timerT` | 0 | 1 | varies | — | — |

**`lfs_log` watermark** — measured 10 s after boot (after processing all boot-log entries):  
→ 1332 B free = **43% headroom** — safe to keep at 3072 B.  
→ If watermark drops below 512 B: increase stack in `LittleFSTransport.cpp startTask()`.

---

## 7. Key Struct Sizes (hw-confirmed, 2026-03-18)

| Struct | Fields summary | sizeof | Note |
|--------|---------------|--------|------|
| `LogEntry` | `uint32_t + LogLevel + char[20] + char[192]` | **220 B** | 3 B tail padding (Xtensa ABI) |
| `CacheEntry` | `char[32+8+48+24] + float×6 + uint16_t` | **140 B** | hw-confirmed |
| FPMatch result (5 candidates) | `const CacheEntry* + float + float` | ~60 B | ephemeral |
| `Measurement` | `rp[4] + l[4] + ts + protocol_id + ...` | ~64 B | stack in handlers |

---

## 8. Risk Register & Budgets

### Heap floor risk

Minimum safe free heap: **15 KB**.  
Current idle: ~30 KB → margin = **15 KB**.  
This margin covers:
- 1 concurrent HTTP request (4 KB TCP buf + up to 2 KB ArduinoJson)
- FreeRTOS message queues during burst logging

**Risk: A-3 features** (BLE, OTA download buffer, file upload handler) will each consume
2–8 KB of permanent heap. At A-3 entry the heap budget must be re-measured.

### FingerprintCache BSS risk

`entries_[1000]` = 140 KB in BSS.  
Production DB currently has **5 entries** → 995 slots wasted in BSS (but not in heap).  
If struct grows (new field added): 1000 × `sizeof(CacheEntry)` recalculates automatically.  
**Guard:** `sizeof(CacheEntry)` logged at boot via `LOG_DEBUG("Mem", "sizeof(CacheEntry)=%u")`.  
**Trigger:** if `sizeof(CacheEntry) × MAX_ENTRIES > 120 000 B` → reduce `MAX_ENTRIES` or
switch to `new CacheEntry[actual_count]` heap allocation.

### LittleFS data growth

`/logs/` is rotation-capped at `maxLogKB=200` KB in `LittleFSTransport` ctor.  
`/measurements/` = ring of 250 × ~1 KB JSONL files = max ~250 KB.  
Total data partition usage at full load: ~450 KB of 1792 KB → **25%**, safe.

---

## 9. Memory Events Timeline (cold boot, STA mode)

```
   0 ms  Firmware entry         heap=165 216 B
  ~50 ms Logger + ring init     heap=146 000 B (−19 KB: ring buf + queue + task)
 200 ms  LittleFS mounts        heap=120 000 B (−26 KB: LFS driver buffers × 2)
 500 ms  NVS + storage init     heap=113 000 B (−7 KB)
1073 ms  NVS ready              —
1095 ms  LFS sys mounted        —
1484 ms  FingerprintCache (5)   heap=107 408 B (−5 KB: JSON parse temp)
1642 ms  [DEBUG] before WiFi    heap=107 408 B  ← LOG_DEBUG checkpoint
2059 ms  WiFi STA up            heap= 54 488 B  ← LOG_DEBUG checkpoint (−52.9 KB)
2092 ms  HTTP ready             heap= 34 844 B  ← LOG_DEBUG checkpoint (−19.6 KB)
~4000 ms First request          heap= 30 052 B  (−4.8 KB: TCP buf + JsonDoc, then freed)
10005 ms [DEBUG] LFS watermark  lfs_log: 1 332 B free of 3 072 B stack
```
