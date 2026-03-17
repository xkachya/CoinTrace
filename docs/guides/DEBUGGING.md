# Debugging Guide — CoinTrace Firmware

**Applies to:** CoinTrace firmware, M5Stack Cardputer-Adv (ESP32-S3FN8), Windows  
**Cross-ref:** `docs/guides/development-setup.md`, `docs/guides/UPLOADFS_GUIDE.md`, `docs/guides/UART_DEBUG_SETUP.md`

> **Looking for REST API / heap diagnostics / hw_test.py?**  
> Script is at `scripts/hw_test.py`.  
> See `docs/guides/HW_TESTING.md` — REST API verification, memory diagnostics, automated test pipeline.

---

## Quick Reference

```powershell
# ⚠️ Option A (DTR/RTS reset): ESP32-S3 USB-CDC closes port on any DTR toggle — ReadExisting() may throw
# Prefer: open port first, power-cycle device manually, then read (see §1 Option A for full details)
$ser = New-Object System.IO.Ports.SerialPort "COM3",115200,"None",8,"One"
$ser.Open()
$ser.RtsEnable = $true ; $ser.DtrEnable = $true ; Start-Sleep -Milliseconds 200
$ser.RtsEnable = $false; $ser.DtrEnable = $false; Start-Sleep -Milliseconds 3500
$ser.ReadExisting() ; $ser.Close()

# Live monitor (Ctrl+C to exit)
pio device monitor -e cointrace-dev

# Build only (no upload)
pio run -e cointrace-dev

# Upload firmware
pio run -e cointrace-dev -t upload

# Upload sys partition (web UI + configs)
pio run -e uploadfs-sys -t uploadfs
```

---

## 1. Getting the Boot Log

The most important debugging tool is the UART boot log on COM3 at 115200 baud.

### Option A — PowerShell hardware reset

> ⚠️ **ESP32-S3 USB-CDC known limitation:** On ESP32-S3 with native USB-CDC (no external UART
> bridge chip), any DTR or RTS toggle causes the USB-CDC stack to close the virtual COM port.
> `ReadExisting()` will throw `InvalidOperationException` (port closed before read completes).
> **Option A is unreliable on this hardware.** Use Option B or C to capture boot logs.
> Option A works reliably only on devices with a physical UART bridge chip (CP2102, CH340, etc.).

If you do want to attempt it — triggers a hardware reset via RTS/DTR and captures the boot sequence:

```powershell
$ser = New-Object System.IO.Ports.SerialPort "COM3",115200,"None",8,"One"
$ser.Open()
# Assert reset: pull RTS+DTR high, then release
$ser.RtsEnable = $true ; $ser.DtrEnable = $true ; Start-Sleep -Milliseconds 200
$ser.RtsEnable = $false; $ser.DtrEnable = $false
# Wait for boot to complete (firmware takes ~800 ms to reach "ready")
Start-Sleep -Milliseconds 3500
$log = $ser.ReadExisting()
$ser.Close()
Write-Output $log
```

**Expected output on a healthy device:**
```
ESP-ROM:esp32s3-20210327
rst:0x15 (USB_UART_CHIP_RESET),boot:0xb (SPI_FAST_FLASH_BOOT)
...
[673ms] INFO  System         | CoinTrace 1.0.0-dev starting
[673ms] INFO  System         | CPU: 240 MHz | Heap: 332944 B | PSRAM: 0 MB
[699ms] INFO  NVS            | Ready — meas_count=0 slot=0
[722ms] INFO  LFS            | sys mounted — free: 976 KB
[733ms] INFO  LFS            | /sys/config/device.json found
[768ms] INFO  LFS            | data mounted — free: 1720 KB
[774ms] INFO  System         | CoinTrace ready — 0/1 plugins initialised
```

**Tuning the capture window:** if your boot takes longer (SD mount, large cache build),
increase the `Start-Sleep -Milliseconds 3500` value. 5000–8000 ms is safe for first boot.

**Alternative when Option A fails (USB-CDC port closes):** Use Option B (PIO monitor + physical
reset button on device) or read LittleFS logs directly via `esptool` — see §3 below.

---

### Option B — PlatformIO monitor (interactive, live)

```powershell
pio device monitor -e cointrace-dev
```

Shows log output in real-time. Does **not** reset the device — you see output only from
the moment you connect. To get the full boot log: press the physical reset button
(or power-cycle) while the monitor is running.

Press `Ctrl+C` to exit.

---

### Option C — PlatformIO upload + monitor (build, flash, then watch)

```powershell
pio run -e cointrace-dev -t upload ; pio device monitor -e cointrace-dev
```

Uploads new firmware and immediately attaches the monitor. PlatformIO resets the device
automatically at the end of upload, so you catch the full boot sequence.

---

### Option D — Built-in PlatformIO test monitor (background)

```powershell
pio device monitor -e cointrace-dev --filter default 2>&1 | Select-Object -First 50 &
```

Runs in background (`&`). Useful when you want to capture output while running another
command in the same terminal. Pipe to a file for persistent logs:

```powershell
pio device monitor -e cointrace-dev 2>&1 | Tee-Object -FilePath boot.log &
```

---

## 2. Understanding Boot Log Lines

### Log format

```
[699ms] INFO  NVS            | Ready — meas_count=0 slot=0
 ^^^^^  ^^^^  ^^^              ^^^^^^^^^^^^^^^^^^^^^^^^^
 Time   Level Tag              Message
```

Levels: `DEBUG` `INFO` `WARN` `ERROR` — controlled by `log_level` in NVS (default: `INFO`).  
ESP framework native logs (e.g. `[I][LittleFS.cpp:98]`) appear at `DEBUG` level and are
printed by `log_i()` / `log_e()` directly to Serial, bypassing the CoinTrace Logger.

### Critical boot lines and their meaning

| Line | Meaning | Action if missing/wrong |
|---|---|---|
| `INFO NVS \| Ready` | NVS opened all namespaces | Check flash with `pio run -t erase` + re-upload |
| `INFO LFS \| sys mounted` | `littlefs_sys` at 0x510000 OK | Run `pio run -e uploadfs-sys -t uploadfs` |
| `INFO LFS \| /sys/config/device.json found` | P-2 acceptance criterion | Run uploadfs-sys |
| `INFO LFS \| data mounted` | `littlefs_data` at 0x610000 OK | Power cycle; if persists → full erase |
| `WARN LFS \| sys mount failed` | Sys partition empty or corrupted | `pio run -e uploadfs-sys -t uploadfs` |
| `WARN LFS \| data mount failed` | Data partition corrupted | Full erase + re-flash |
| `ERROR LDC1101 \| CHIP_ID mismatch` | Sensor not connected or SPI fault | Expected when sensor unplugged |
| `E esp_littlefs: Corrupted dir pair` | LittleFS internal corruption | Power cycle; if persists → full erase |

### Normal "degraded" messages (not real errors)

```
[WARN] LFS | sys mount failed — no web UI or plugin config (uploadfs-sys required)
```
Expected on first flash before `uploadfs-sys` is run. Device continues normally.

```
[ERROR] LDC1101 | CHIP_ID mismatch: expected 0xD4, got 0x00
```
Expected when LDC1101 sensor board is not attached. Plugin disables itself gracefully.

---

## 3. Checking Partition State

### Verify partition table is flashed correctly

```powershell
cd d:\GitHub\CoinTrace
python -m esptool --chip esp32s3 --port COM3 read_flash 0x8000 0x1000 partition_backup.bin
python -m esptool --chip esp32s3 partition_table --verify partition_backup.bin
```

Or read back and decode:
```powershell
python -m esptool --chip esp32s3 --port COM3 read_flash 0x8000 0xc00 pt.bin
python -m esptool --chip esp32s3 partition_table --raw-flash pt.bin
```

### Expected partition layout

```
# Offset     Size        Name             Type  SubType
0x009000    0x005000    nvs              data  nvs         (20 KB)
0x00e000    0x002000    otadata          data  ota         (8 KB)
0x010000    0x280000    app0             app   ota_0       (2.5 MB)
0x290000    0x280000    app1             app   ota_1       (2.5 MB)
0x510000    0x100000    littlefs_sys     data  spiffs      (1.0 MB)
0x610000    0x1c0000    littlefs_data    data  spiffs      (1.75 MB)
0x7d0000    0x030000    coredump         data  coredump    (192 KB)
```

If the offsets differ → `pio run -e cointrace-dev -t erase` then re-flash.

### Check free space on both LittleFS partitions (from boot log)

The boot log always reports:
```
[Xms] INFO LFS | sys mounted  — free: 976 KB    ← littlefs_sys  (1024 KB total, ~5 KB used)
[Xms] INFO LFS | data mounted — free: 1720 KB   ← littlefs_data (1792 KB total, empty)
```

If `free` is suspiciously low (e.g. sys < 900 KB) → `data/` folder is too large.
Check size:
```powershell
(Get-ChildItem -Recurse d:\GitHub\CoinTrace\data | Measure-Object -Property Length -Sum).Sum / 1KB
```

### Read LittleFS log files via esptool (workaround when Serial is unavailable)

When USB-CDC port cannot be read (Option A DTR issue, or device crashes before log output),
read the `littlefs_data` partition directly — no device cooperation required:

```powershell
cd d:\GitHub\CoinTrace
python -m esptool --chip esp32s3 --port COM3 read_flash 0x610000 0x20000 lfs_dump.bin

# Extract readable JSONL log entries:
$bytes = [System.IO.File]::ReadAllBytes("lfs_dump.bin")
$text = [System.Text.Encoding]::ASCII.GetString($bytes) -replace '[^\x20-\x7E\n]', '.'
$text -split '\n' | Where-Object { $_ -match '^\{' } | Select-Object -First 30
Remove-Item lfs_dump.bin
```

**Expected JSONL entries when LittleFS contains logs:**
```
{"t":881,"l":1,"c":"LFS","m":"LittleFSTransport started"}
{"t":897,"l":3,"c":"LDC1101","m":"CHIP_ID mismatch..."}
```

**Verify partition is valid:** LittleFS magic bytes at offset 0 of the dump:
`03 04 00 00 ... 6C 69 74 74 6C 65 66 73` (ASCII: `littlefs`).  
If first bytes are `FF FF FF FF` → partition is blank (never formatted).

> **Note:** `BOOT_REASON` log entry will **not** appear in LittleFS — it is logged before
> `LittleFSTransport` is added to the pipeline (boot step [5]). Check Serial via Option B instead.

---

## 4. NVS State

### Read meas_count and boot counter

NVS values are printed at boot:
```
[699ms] INFO NVS | Ready — meas_count=0 slot=0
```

`meas_count` is the total monotonic measurement count (never resets except Hard Reset).  
`slot = meas_count % 250` — the LittleFS ring buffer slot that will be written next.

### Wipe NVS (soft reset all settings, keep calibration)

Implement via firmware Soft Reset (`Fn+Del` 3 s). Not yet available — until then,
the only option is full erase (which also erases calibration):

```powershell
pio run -e cointrace-dev -t erase    # erases ALL flash including NVS and LittleFS
pio run -e cointrace-dev -t upload   # re-flash firmware
pio run -e uploadfs-sys  -t uploadfs # re-flash sys partition
```

---

## 5. Full Erase and Re-Flash Sequence

Required when:
- Changing partition table layout (new CSV)
- Suspected flash corruption (`Corrupted dir pair` in boot log after power cycle)
- Switching from UIFlow/M5Stack firmware back from stock

```powershell
# 1. Erase all flash
pio run -e cointrace-dev -t erase

# 2. Flash firmware
pio run -e cointrace-dev -t upload

# 3. Flash sys partition content (web UI + device.json)
pio run -e uploadfs-sys -t uploadfs

# 4. littlefs_data: no step needed — firmware formats on first boot
#    Boot log: "data mounted — free: 1720 KB"
```

**Note:** `pio run -e cointrace-dev -t upload` alone does NOT touch LittleFS. Step 3
is needed after every full erase to restore `device.json` and web UI.

---

## 6. Build Flags and Debug Levels

### Enable verbose debug output

In `platformio.ini`, add to `[env:cointrace-dev]` / `build_flags`:

```ini
-DCORE_DEBUG_LEVEL=5        # ESP32 framework: show all log_d/log_i/log_e output
-DDEBUG_ESP_LITTLEFS=1      # LittleFS internal debug (very verbose)
```

Remove before committing — these flood the serial output.

### Check build size

```powershell
pio run -e cointrace-dev 2>&1 | Select-String "RAM:|Flash:"
```

Current baseline (2026-03-18, Wave 8 A-2):
```
RAM:   [======    ]  61.5% (used 201676 bytes from 327680 bytes)
Flash: [======    ]  56.0% (used 1468381 bytes from 2621440 bytes)
```

If Flash exceeds ~75% → check for unintended large static arrays or string literals.
For a full size breakdown see `docs/guides/HW_TESTING.md §2`.

---

## 7. Download Mode (Manual Flash)

If PlatformIO cannot auto-reset the device (upload fails with "Connecting..."):

> ⚠️ **Do not confuse with B-3 factory data reset.**
> - **G0 held at power-on** → ROM Download Mode (blank screen, firmware never starts — this section)
> - **G0 held during 3-second splash window** → B-3 factory `LittleFS_data` format (firmware running)
>
> These are two separate mechanisms at different boot stages. If you accidentally enter Download
> Mode instead of triggering factory reset: power off → power on without holding G0.

1. Power **off** the device (side switch → OFF)
2. Hold the **G0 button** (bottom-left of keyboard)
3. Power **on** (side switch → ON)
4. Release G0 — screen stays blank (normal)
5. Run upload command

```powershell
pio run -e cointrace-dev -t upload
```

To exit Download Mode: power off → power on (without holding G0).

---

## 8. Common Problems and Quick Fixes

### Upload fails — "A fatal error occurred: Failed to connect"

```
Device is not in Download Mode or wrong COM port.
```
→ Check Device Manager: should show "USB-Enhanced-SERIAL CH343 (COM3)"  
→ Enter Download Mode manually (§7 above)  
→ Verify `upload_port = COM3` in `platformio.ini`

---

### Build error — partition CSV not found

```
[uploadfs] ERROR: partition CSV not found: partitions/cointrace_8MB.csv
```
→ Confirm `partitions/cointrace_8MB.csv` exists in project root  
→ Run from project directory: `cd d:\GitHub\CoinTrace`

---

### `sys mount failed` after uploading new firmware

```
[WARN] LFS | sys mount failed — no web UI or plugin config
```
→ Firmware upload does NOT touch `littlefs_sys`. This message appears after full erase.  
→ Fix: `pio run -e uploadfs-sys -t uploadfs`

---

### `E esp_littlefs: Corrupted dir pair at {0x0, 0x1}`

LittleFS metadata blocks are corrupted. Usually caused by interrupted upload or power loss
during first format.

→ Power cycle once (sometimes self-heals via LittleFS journal)  
→ If persists: full erase + re-flash (§5)

---

### uploadfs flashes to wrong address

```
Flash will be erased from 0x00610000 to 0x007cffff   ← WRONG (data partition)
```
vs correct:
```
[uploadfs] FS_START  : 0x00610000 -> 0x00510000       ← script patched correctly
Flash will be erased from 0x00510000 to 0x0060ffff    ← CORRECT
```

→ Verify `extra_scripts = post:scripts/upload_littlefs_sys.py` is present in `[env:uploadfs-sys]`  
→ See `docs/guides/UPLOADFS_GUIDE.md` — "Troubleshooting" section

---

### Blank screen when powering on with G0 held

```
Device enters ROM Download Mode — blank screen, no Serial output, COM port still enumerates.
```

This is **not a defect** — ESP32-S3 ROM bootloader intercepts GPIO0=LOW at power-on before
firmware starts, entering UART/USB DFU download mode.

→ To exit: power off → power on without holding G0  
→ **Do not confuse with B-3 factory reset** (§7 above) — B-3 activates during the 3-second
  splash window in running firmware, not at power-on  
→ After accidental Download Mode: device is unaffected, just exit and re-boot normally

---

### Native tests `ERRORED` — "The process cannot access the file" (Windows only)

```
Error: *** [test_logger.o] The process cannot access the file because it is
being used by another process
native-test:test_logger [ERRORED]
```

**Cause:** Windows Defender real-time protection scans every `.o`/`.exe` file written to
`.pio\build\`. When VS Code Test Explorer runs suites back-to-back, Defender holds a read
lock on the previous suite's object file while the next suite tries to overwrite it.

Does **not** reproduce in terminal (`pio test`) because there the build directories are
separate per suite and the gap between suites is longer.

**Fix — add `.pio` folder to Defender exclusions (one-time, requires admin):**

Open PowerShell **as Administrator** and run:
```powershell
Add-MpPreference -ExclusionPath "D:\GitHub\CoinTrace\.pio"
```

Or via GUI: **Windows Security → Virus & threat protection → Manage settings →
Exclusions → Add an exclusion → Folder → `D:\GitHub\CoinTrace\.pio`**

After adding the exclusion, run tests again from VS Code Test Explorer — all suites should
pass without `ERRORED`.

> ℹ️ This exclusion is safe: `.pio` contains only compiled build artefacts (no source code,
> no sensitive data). The actual source files in `src/`, `lib/`, `test/` remain protected.

---

## 9. Useful One-Liners

```powershell
# Show only errors and size summary from build
pio run -e cointrace-dev 2>&1 | Select-String "error:|RAM:|Flash:"

# Tail the monitor output to a file (background)
pio device monitor -e cointrace-dev 2>&1 | Tee-Object -FilePath .\boot.log &

# Check COM port is available
[System.IO.Ports.SerialPort]::GetPortNames()

# Read 2 seconds of live serial output without resetting
$s = New-Object System.IO.Ports.SerialPort "COM3",115200
$s.Open() ; Start-Sleep 2 ; $s.ReadExisting() ; $s.Close()

# Check git status before committing
git status --short
git log --oneline -5

# Dump littlefs_data partition and extract JSONL log entries (when Serial unavailable)
python -m esptool --chip esp32s3 --port COM3 read_flash 0x610000 0x20000 lfs_dump.bin
```
