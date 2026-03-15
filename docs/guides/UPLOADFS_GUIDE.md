# UPLOADFS Guide — Dual LittleFS Partition Workflow

**Applies to:** CoinTrace firmware, M5Stack Cardputer-Adv (ESP32-S3FN8, 8 MB flash)  
**Cross-ref:** `STORAGE_ARCHITECTURE.md` §8, ADR-ST-002, `scripts/upload_littlefs_sys.py`

---

## Огляд: як влаштовано флеш і що чим аплоадиться

Flash ESP32-S3 (8 MB) поділено на незалежні регіони. Кожен регіон аплоадиться **окремою командою** і **не впливає на сусідів**:

```
Регіон              Команда                                    Хто керує
─────────────────── ────────────────────────────────────────── ─────────────────────
Firmware (app0)     pio run -e cointrace-dev -t upload         Розробник
littlefs_sys        pio run -e uploadfs-sys  -t uploadfs       Розробник
littlefs_data       (немає команди uploadfs)                   Тільки firmware
```

**Firmware upload** (`-t upload`) записує скомпільований `.elf` у регіон `app0` (`0x010000`).
Він **ніколи не торкається** `littlefs_sys` чи `littlefs_data` — виміри та web UI
залишаються після кожного перепрошивання firmware.

**`littlefs_sys` uploadfs** записує вміст папки `data/` (web UI, конфіги) у регіон `0x510000`.
Він **ніколи не торкається** `littlefs_data` — виміри в безпеці.

**`littlefs_data`** ніколи не аплоадиться з ПК. Firmware форматує її сам при першому старті
і далі записує туди виміри, логи, кеш відбитків. Розробник читає дані звідти через
web UI або майбутній USB Mass Storage (Wave 8+).

---

## Background: Why This Is Needed

CoinTrace uses **two independent LittleFS partitions** (ADR-ST-002):

| Partition (CSV name) | Mount | Offset | Size | Content | Who writes |
|---|---|---|---|---|---|
| `littlefs_sys` | `/sys` | `0x510000` | 1 MB | Web UI, device config | Developer (uploadfs) |
| `littlefs_data` | `/data` | `0x610000` | 1.75 MB | Measurements, logs, cache | Firmware (runtime) |

**The problem:** PlatformIO's built-in `uploadfs` command always targets the **last** partition with subtype `spiffs` in the CSV. With two such partitions, it invariably writes to `littlefs_data` — silently erasing user measurements.

**The fix:** `scripts/upload_littlefs_sys.py` is an extra_scripts hook that:
1. Parses `partitions/cointrace_8MB.csv` to find the target partition by label
2. Builds the LittleFS image via `mklittlefs` with the correct partition size
3. Flashes to the **correct offset** via `esptool.py` directly

`board_build.littlefs_partition_label` — the "official" PlatformIO option for this — does not work in PlatformIO 6.x with espressif32 platform. The script is the reliable solution.

---

## Quick Reference

```
# Flash firmware only (never touches flash partitions)
pio run -e cointrace-dev -t upload

# Flash web UI + system configs → littlefs_sys (SAFE for user data)
pio run -e uploadfs-sys -t uploadfs

# First flash (new device or after full erase):
pio run -e cointrace-dev -t erase     # erase all flash
pio run -e cointrace-dev -t upload    # firmware
pio run -e uploadfs-sys -t uploadfs   # sys partition
# littlefs_data: no uploadfs needed — firmware formats it on first boot
```

---

## What Each Command Touches

```
Flash layout:
  0x000000  bootloader         ← touched by: pio run -t upload (always)
  0x008000  partition table    ← touched by: pio run -t upload (always)
  0x00e000  otadata            ← touched by: pio run -t upload (always)
  0x010000  app0               ← touched by: pio run -t upload
  0x290000  app1               ← touched by: OTA update only
  0x510000  littlefs_sys  ─────────────── pio run -e uploadfs-sys -t uploadfs
  0x610000  littlefs_data ─── NEVER by uploadfs ─── formatted by firmware on first boot
  0x7D0000  coredump           ← not touched by normal workflow
```

**`littlefs_data` is NEVER written by any uploadfs command** — only by the running firmware at runtime. This is by design: user measurements must survive firmware reflash and web UI updates.

---

## Partition Safety: No Overlap & Independent Testing

### Flash layout is mathematically non-overlapping

```
Partition       Offset      End (excl.)   Size
app1            0x290000    0x510000      2.5 MB
littlefs_sys    0x510000    0x610000      1.0 MB   ← sys ends exactly where data starts
littlefs_data   0x610000    0x7D0000      1.75 MB
coredump        0x7D0000    0x800000      192 KB
```

`lfs_sys` end = `0x510000 + 0x100000 = 0x610000` = `lfs_data` start.  
Contiguous, zero gap, zero overlap — writing to one partition cannot corrupt the other.

### How each partition is tested

| Partition | How initialized | Verified by |
|---|---|---|
| `littlefs_sys` | `pio run -e uploadfs-sys -t uploadfs` | Boot log: `sys mounted — free: 976 KB` + `/sys/config/device.json found` |
| `littlefs_data` | Firmware auto-formats on first boot (`format_on_fail=true`) | Boot log: `data mounted — free: 1720 KB` |

`littlefs_data` has **no uploadfs command** by design (see [Why NOT `uploadfs-data`](#why-not-uploadfs-data)). Verification is always through the boot log.

### Sequential test run (both partitions in one session)

```powershell
# 1. Flash sys partition
pio run -e uploadfs-sys -t uploadfs

# 2. Monitor boot log — all three lines must appear:
pio device monitor -e cointrace-dev
# [INFO] LFS | sys mounted  — free: 976 KB
# [INFO] LFS | /sys/config/device.json found
# [INFO] LFS | data mounted — free: 1720 KB
```

`uploadfs-sys` only touches flash range `0x510000–0x60FFFF`. The data partition at `0x610000`
retains its contents across every `uploadfs-sys` run.

---

## Developer Workflow: Updating Web UI or System Config

This is the most common operation for a developer working on the web UI or `device.json`:

```powershell
# 1. Edit files in data/web/ and/or data/config/
# 2. Flash — firmware is NOT re-uploaded, only the sys partition
pio run -e uploadfs-sys -t uploadfs
# 3. Monitor to confirm sys mounted
pio device monitor -e cointrace-dev
# Expected: [INFO] LFS | sys mounted — free: ... KB
#           [INFO] LFS | /sys/config/device.json found
```

**user measurements in `littlefs_data` are untouched by step 2.**

---

## Developer Workflow: Full First Flash (New Device)

```powershell
# 1. Erase all flash (required only when changing partition table layout)
pio run -e cointrace-dev -t erase

# 2. Flash firmware
pio run -e cointrace-dev -t upload

# 3. Flash sys partition (web UI + configs)
pio run -e uploadfs-sys -t uploadfs

# 4. littlefs_data is automatically formatted + directories created on first boot:
#    /measurements/   (ring buffer for m_000.json .. m_249.json)
#    /cache/          (fingerprint index)
#    /logs/           (log.0.jsonl, log.1.jsonl)
# No manual uploadfs-data step needed.
```

Step 1 (`erase`) is only required when the **partition table layout changes** (offsets or sizes). For normal firmware + web UI updates, skip step 1 entirely.

---

## `data/` Directory Structure

The `data/` directory is what gets packed into the `littlefs_sys` image:

```
data/
  config/
    device.json        ← plugin list and device identity (schema_ver: 1)
    protocols/
      registry.json    ← coin protocol registry (future)
  web/
    index.html         ← web UI entry point (future)
    app.js
    style.css
  logger.json          ← logger transport configuration
  plugins/
    ldc1101.json       ← LDC1101 plugin defaults
```

**Size budget:** `littlefs_sys` = 1 MB. Current content is ~5 KB. Plenty of headroom for the web UI.

The `data/` directory is **never uploaded to `littlefs_data`**. That partition is managed exclusively by the firmware.

---

## How the Script Works

`scripts/upload_littlefs_sys.py` is loaded as a PlatformIO **`post:`** extra_script. It:

1. Reads `custom_partition_label` from `platformio.ini` (default: `littlefs_sys`)
2. Parses `partitions/cointrace_8MB.csv` — finds the partition entry by name
3. Calls `env.Replace(FS_START=offset, FS_SIZE=size)` to patch two SCons variables
4. PlatformIO's own `mklittlefs` + `esptool.py` pipeline then runs with the patched values

**Why `post:` not `pre:`:** the espressif32 platform sets `FS_START`/`FS_SIZE` inside its own
`main.py`. A `pre:` hook runs before that, so the platform overwrites the values immediately
after. A `post:` hook runs after, so the `Replace()` sticks.

**Why not `UPLOADFSCMD` override or `AddCustomTarget`:** overriding `UPLOADFSCMD` fixes only
the flash address but not `FS_SIZE`, so `mklittlefs` still builds a wrong-size image.
`AddCustomTarget("uploadfs")` raises `AssertionError` — the name is already registered by
the platform before `post:` scripts run. Patching `FS_START` + `FS_SIZE` is the minimal
correct fix.

---

## Troubleshooting

### `sys mount failed — no web UI` in boot log

```
[WARN] LFS | sys mount failed — no web UI or plugin config (uploadfs-sys required)
```

`littlefs_sys` partition is empty (first flash or after full erase). Run:
```powershell
pio run -e uploadfs-sys -t uploadfs
```
This is expected on a freshly erased device. The firmware continues without crashing (graceful degradation).

---

### `data mounted — free: 1720 KB` but `sys mount failed`

Normal when `uploadfs-sys` has not been run yet. `littlefs_data` formats itself on first boot (`format_on_fail=true`). `littlefs_sys` never auto-formats (`format_on_fail=false`) — production content must be explicitly uploaded.

---

### Script writes to wrong address

Verify in the terminal output:
```
[uploadfs] Partition : 'littlefs_sys'
[uploadfs] FS_START  : 0x00610000 -> 0x00510000
[uploadfs] FS_SIZE   : 0x1C0000 -> 0x100000 (1024 KB)
```
The `->` shows the rewrite from PlatformIO's default to the correct value.  
If these lines are missing or the right side still shows `0x00610000`, the script is not
being invoked — check `extra_scripts = post:scripts/upload_littlefs_sys.py` is present in
`[env:uploadfs-sys]` in `platformio.ini`.

---

### `mklittlefs: image exceeds partition size`

`data/` contents are too large for the 1 MB sys partition. Check:
```powershell
(Get-ChildItem -Recurse d:\GitHub\CoinTrace\data | Measure-Object -Property Length -Sum).Sum / 1KB
```
Budget: stay under ~900 KB (LittleFS overhead ~100 KB).

---

## Why NOT `uploadfs-data`

A separate `uploadfs-data` environment to initialize `littlefs_data` is **deliberately not provided** in this project. Reasons:

1. **Firmware handles it:** `LittleFSManager::mountData()` uses `format_on_fail=true`. On first boot, the partition is formatted and `measurements/`, `cache/`, `logs/` directories are created automatically.
2. **Safety:** A developer accidentally running `uploadfs-data` on a device with real measurement data would permanently erase it with no recovery path.
3. **Hard reset workflow:** The in-firmware Hard Reset (`Fn+Del` 10 sec) calls `LittleFS_data.format()` + `esp_restart()` as the intentional "factory reset" mechanism.

### How to load test fixture data (future)

For automated testing or seeding a device with a known set of measurements,
three approaches are planned for future waves:

| Approach | Wave | Notes |
|---|---|---|
| `[env:uploadfs-data-test]` — explicit test-only env | on demand | Intentionally separate; never in default workflow; requires deliberate invocation |
| Web UI file upload endpoint | Wave 8 | Upload JSON files via browser |
| USB Mass Storage | Wave 8+ | ESP32-S3 native USB; mounts `/data` as a removable drive on Windows/macOS |

Until then: the only way to populate `littlefs_data` is to **use the device normally** —
measure real coins, or trigger test measurements via the serial/web API.

---

## ADR Reference

| Decision | Location |
|---|---|
| Two LittleFS partitions | `STORAGE_ARCHITECTURE.md` ADR-ST-002 |
| `format_on_fail=true` for data, `false` for sys | `lib/StorageManager/src/LittleFSManager.cpp` |
| `uploadfs` offset bug | `docs/lessons-learned.md` — "PlatformIO uploadfs dual LittleFS" |
