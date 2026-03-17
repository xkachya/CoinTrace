"""
CoinTrace hw-test — final phase measurements
Wave 8 A-2 complete regression + heap analysis.
Resets device via COM3, waits for HTTP ready on COM4, runs full API test suite.

Usage (run from project root):
    python scripts/hw_test.py            # full run (reset + boot wait + tests)
    python scripts/hw_test.py --no-reset # skip reset, use existing device_ip
    python scripts/hw_test.py --ip X.X.X.X  # override IP, skip reset+boot
"""
import serial, time, threading, urllib.request, urllib.error, json, sys

COM_MONITOR = "COM4"
COM_RESET   = "COM3"
COM_CRASH   = "COM3"   # Crash/panic output goes to UART0 = USB-CDC = COM3
BAUD        = 115200
TIMEOUT_BOOT_S = 30  # max time to wait for "HTTP" line

SKIP_RESET = "--no-reset" in sys.argv or "--ip" in sys.argv
OVERRIDE_IP = None
for i, a in enumerate(sys.argv):
    if a == "--ip" and i + 1 < len(sys.argv):
        OVERRIDE_IP = sys.argv[i + 1]
        SKIP_RESET  = True

device_ip = OVERRIDE_IP
boot_done = threading.Event()
if OVERRIDE_IP:
    boot_done.set()
log_lines = []
crash_lines = []


def monitor_com_passively(port, output_list, label, duration_s):
    """Read serial port WITHOUT toggling DTR/RTS (no reset triggered)."""
    try:
        s = serial.Serial(port, BAUD, timeout=1, dsrdtr=False, rtscts=False)
        print(f"  [{label} opened (passive, no DTR)]")
        deadline = time.time() + duration_s
        while time.time() < deadline:
            line = s.readline()
            if line:
                txt = line.decode("utf-8", errors="replace").rstrip()
                print(f"  {label}: {txt}")
                output_list.append(txt)
        s.close()
    except Exception as e:
        print(f"  [{label} error: {e}]")


ESPTOOL_PY = r"C:\Users\Yura\.platformio\packages\tool-esptoolpy\esptool.py"


def monitor_com4():
    global device_ip
    try:
        s = serial.Serial(COM_MONITOR, BAUD, timeout=1)
        print(f"  [COM4 opened]")
        deadline = time.time() + TIMEOUT_BOOT_S
        while time.time() < deadline:
            line = s.readline()
            if not line:
                continue
            txt = line.decode("utf-8", errors="replace").rstrip()
            print(f"  LOG: {txt}")
            log_lines.append(txt)
            if "HTTP" in txt and "ready" in txt:
                import re
                m = re.search(r"http://(\d+\.\d+\.\d+\.\d+)", txt)
                if m:
                    device_ip = m.group(1)
                boot_done.set()
        s.close()
    except Exception as e:
        print(f"  [COM4 error: {e}]")
        boot_done.set()


def reset_device():
    """Reset ESP32-S3 via esptool 'run' command — proven reset sequence."""
    import subprocess
    try:
        result = subprocess.run(
            [sys.executable, ESPTOOL_PY,
             "--chip", "esp32s3", "--port", COM_RESET, "run"],
            capture_output=True, text=True, timeout=15
        )
        if result.returncode == 0:
            print("  [reset sent via esptool]")
        else:
            # non-zero but esptool often exits 1 after 'run' — check for key phrase
            out = (result.stdout + result.stderr)
            if "Hard resetting" in out or "Staying in bootloader" in out or "done" in out.lower():
                print("  [reset sent via esptool (ok)]")
            else:
                print(f"  [esptool: {out.strip()[-120:]}]")
    except Exception as e:
        print(f"  [reset error: {e}]")


def http_get(ip, path, timeout=8):
    url = f"http://{ip}{path}"
    try:
        req = urllib.request.Request(url, headers={"Connection": "close"})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            body = r.read().decode()
            return r.status, body
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()
    except Exception as e:
        return 0, str(e)


def http_post(ip, path, body_dict, timeout=8):
    url  = f"http://{ip}{path}"
    data = json.dumps(body_dict).encode()
    try:
        req = urllib.request.Request(url, data=data,
              headers={"Content-Type": "application/json", "Connection": "close"})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()
    except Exception as e:
        return 0, str(e)


def run_tests(ip):
    PASS = "\033[92m[PASS]\033[0m"
    FAIL = "\033[91m[FAIL]\033[0m"
    WARN = "\033[93m[WARN]\033[0m"
    results = []
    measurements = {}  # collect key metrics for final summary

    # ── 1. GET /status ─────────────────────────────────────────────────────────
    code, body = http_get(ip, "/api/v1/status")
    ok = (code == 200)
    tag = PASS if ok else FAIL
    try:
        s = json.loads(body)
        heap           = s.get("heap", 0)
        heap_min       = s.get("heap_min", 0)
        heap_max_block = s.get("heap_max_block", None)
        uptime         = s.get("uptime", "?")
        wifi_mode      = s.get("wifi", "?")
        ip_reported    = s.get("ip", "?")
        version        = s.get("version", "?")

        measurements["heap_idle"]       = heap
        measurements["heap_min"]        = heap_min
        measurements["heap_max_block"]  = heap_max_block

        frag_ok = True
        frag_note = ""
        if heap_max_block is not None:
            ratio = heap_max_block / heap if heap else 0
            frag_note = f"  max_block={heap_max_block} B ({ratio*100:.0f}% of heap)"
            if ratio < 0.50:
                frag_ok = False
                frag_note += " ← FRAGMENTED"
            else:
                frag_note += " ← healthy"

        print(f"  {tag} GET /api/v1/status → {code}")
        print(f"       version={version}  uptime={uptime}s  wifi={wifi_mode}  ip={ip_reported}")
        print(f"       heap={heap} B  heap_min={heap_min} B" + frag_note)
        if heap_max_block is None:
            print(f"       {WARN} heap_max_block field missing — firmware not updated?")

        results.append((ok and frag_ok, "GET /api/v1/status", code))
    except Exception as e:
        print(f"  {tag} GET /api/v1/status → {code}  parse error: {e}  raw: {body[:80]}")
        results.append((False, "GET /api/v1/status", code))

    # ── 2. GET /sensor/state ───────────────────────────────────────────────────
    code, body = http_get(ip, "/api/v1/sensor/state")
    ok = (code == 200)
    try:
        s = json.loads(body)
        state = s.get("state", "?")
        print(f"  {PASS if ok else FAIL} GET /api/v1/sensor/state → {code}  state={state}")
        measurements["sensor_state"] = state
    except:
        print(f"  {PASS if ok else FAIL} GET /api/v1/sensor/state → {code}  {body[:60]}")
    results.append((ok, "GET /api/v1/sensor/state", code))

    # ── 3. GET /database ───────────────────────────────────────────────────────
    code, body = http_get(ip, "/api/v1/database")
    ok = (code == 200)
    try:
        s = json.loads(body)
        count = s.get("count", "?")
        ready = s.get("ready", "?")
        print(f"  {PASS if ok else FAIL} GET /api/v1/database → {code}  count={count}  ready={ready}")
        measurements["fp_count"] = count
        measurements["fp_ready"] = ready
    except:
        print(f"  {PASS if ok else FAIL} GET /api/v1/database → {code}  {body[:60]}")
    results.append((ok, "GET /api/v1/database", code))

    # ── 4. GET /log?n=5 ────────────────────────────────────────────────────────
    code, body = http_get(ip, "/api/v1/log?n=5")
    ok = (code == 200)
    try:
        s    = json.loads(body)
        ents = s.get("entries", [])
        nxt  = s.get("next_ms", "?")
        # heap diags should NOT appear at INFO level anymore (downgraded to DEBUG)
        heap_diag_in_ring = any(
            ("before WiFi" in e.get("msg","") or "after WiFi" in e.get("msg","") or
             "after HTTP" in e.get("msg",""))
            and e.get("level","") == "INFO"
            for e in ents
        )
        print(f"  {PASS if ok else FAIL} GET /api/v1/log?n=5 → {code}  entries={len(ents)}  next_ms={nxt}")
        for e in ents:
            print(f"       [{e.get('ms','?')}ms] {e.get('level','?')} {e.get('comp','?')}: {e.get('msg','?')[:60]}")
        if heap_diag_in_ring:
            print(f"  {WARN} heap diagnostic still at INFO level — firmware not updated?")
        measurements["log_entries"] = len(ents)
        measurements["log_next_ms"] = nxt
    except Exception as e:
        print(f"  {PASS if ok else FAIL} GET /api/v1/log → {code}  parse error: {e}")
    results.append((ok, "GET /api/v1/log", code))

    # ── 5. GET /measure/0 — expect 200 or 404 ─────────────────────────────────
    code, body = http_get(ip, "/api/v1/measure/0")
    ok = code in (200, 404)
    try:
        s = json.loads(body)
        if code == 404:
            print(f"  {PASS if ok else FAIL} GET /api/v1/measure/0 → {code}  (no measurements stored — correct)")
            measurements["measure_0"] = "not_found"
        else:
            m_id  = s.get("id", "?")
            m_ts  = s.get("ts", "?")
            m_rp  = s.get("rp", [])
            m_conf = s.get("conf", "?")
            print(f"  {PASS if ok else FAIL} GET /api/v1/measure/0 → {code}  id={m_id} ts={m_ts}s rp={m_rp} conf={m_conf}")
            measurements["measure_0"] = f"id={m_id}"
    except:
        print(f"  {PASS if ok else FAIL} GET /api/v1/measure/0 → {code}  {body[:60]}")
    results.append((ok, "GET /api/v1/measure/0", code))

    # ── 6. GET /ota/status ─────────────────────────────────────────────────────
    code, body = http_get(ip, "/api/v1/ota/status")
    ok = (code == 200)
    try:
        s = json.loads(body)
        pending = s.get("pending", "?")
        print(f"  {PASS if ok else FAIL} GET /api/v1/ota/status → {code}  pending={pending}")
    except:
        print(f"  {PASS if ok else FAIL} GET /api/v1/ota/status → {code}  {body[:60]}")
    results.append((ok, "GET /api/v1/ota/status", code))

    # ── 7. POST /database/match ────────────────────────────────────────────────
    match_body = {
        "algo_ver": 1, "protocol_id": "p1_UNKNOWN_013mm",
        "vector": {"dRp1": 312.4, "k1": 0.742, "k2": 0.531,
                   "slope_rp_per_mm_lr": -0.128, "dL1": 0.18}
    }
    code, body = http_post(ip, "/api/v1/database/match", match_body)
    ok = (code == 200)
    try:
        s = json.loads(body)
        match      = s.get("match", None)
        conf       = s.get("conf", 0)
        coin_name  = s.get("coin_name", "?")
        alts       = s.get("alternatives", [])
        print(f"  {PASS if ok else FAIL} POST /api/v1/database/match → {code}")
        print(f"       match={match}  conf={conf:.3f}  coin={coin_name}  alts={len(alts)}")
        measurements["match_result"] = match
        measurements["match_conf"]   = round(conf, 3)
        # heap snapshot after POST (most expensive operation)
        code2, body2 = http_get(ip, "/api/v1/status")
        try:
            s2 = json.loads(body2)
            measurements["heap_after_post"] = s2.get("heap", 0)
            measurements["heap_max_block_after_post"] = s2.get("heap_max_block", None)
            print(f"       heap after POST={s2.get('heap',0)} B  max_block={s2.get('heap_max_block','?')} B")
        except:
            pass
    except Exception as e:
        print(f"  {PASS if ok else FAIL} POST /api/v1/database/match → {code}  {e}  {body[:80]}")
    results.append((ok, "POST /database/match", code))

    # ── 8. POST /measure/start — expect 503 ────────────────────────────────────
    code, body = http_post(ip, "/api/v1/measure/start", {})
    ok = (code == 503)
    try:
        s = json.loads(body)
        err = s.get("error", "?")
        print(f"  {PASS if ok else FAIL} POST /api/v1/measure/start → {code}  error={err}  (expect 503)")
    except:
        print(f"  {PASS if ok else FAIL} POST /api/v1/measure/start → {code}  (expect 503)")
    results.append((ok, "POST /measure/start", code))

    # ── 9. POST /ota/update — expect 403 ──────────────────────────────────────
    code, body = http_post(ip, "/api/v1/ota/update", {})
    ok = (code == 403)
    try:
        s = json.loads(body)
        err = s.get("error", "?")
        print(f"  {PASS if ok else FAIL} POST /api/v1/ota/update → {code}  error={err}  (expect 403)")
    except:
        print(f"  {PASS if ok else FAIL} POST /api/v1/ota/update → {code}  (expect 403)")
    results.append((ok, "POST /ota/update", code))

    # ── 10. GET /log — verify LOG_DEBUG heap entries NOT at INFO ──────────────
    # After firmware update, heap diags are LOG_DEBUG → not in ring at default INFO level
    code, body = http_get(ip, "/api/v1/log?n=50&level=DEBUG")
    ok_extra = (code == 200)
    debug_heap_count = 0
    sizeof_found = False
    watermark_found = False
    try:
        s = json.loads(body)
        all_ents = s.get("entries", [])
        for e in all_ents:
            msg = e.get("msg","")
            lvl = e.get("level","")
            if ("before WiFi" in msg or "after WiFi" in msg or "after HTTP" in msg) and lvl == "DEBUG":
                debug_heap_count += 1
            if "sizeof" in msg and "CacheEntry" in msg:
                sizeof_found = True
                print(f"  [INFO] Mem diagnostic: {msg}")
            if "watermark" in msg.lower() or "LFS task" in msg:
                watermark_found = True
                print(f"  [INFO] Stack watermark: {msg}")
        if debug_heap_count > 0:
            print(f"  [INFO] Heap diags correctly at DEBUG level ({debug_heap_count} entries)")
        if sizeof_found or watermark_found:
            pass  # already printed above
        measurements["debug_heap_in_log"] = debug_heap_count
    except:
        pass

    # ── Final heap stability check (3rd status reading) ────────────────────────
    time.sleep(1)
    code3, body3 = http_get(ip, "/api/v1/status")
    try:
        s3 = json.loads(body3)
        heap3       = s3.get("heap", 0)
        max_block3  = s3.get("heap_max_block", 0)
        heap_drift  = abs(heap3 - measurements.get("heap_idle", heap3))
        measurements["heap_final"]       = heap3
        measurements["heap_max_block_final"] = max_block3
        measurements["heap_drift"]       = heap_drift
        drift_ok = heap_drift < 1024  # < 1 KB drift = no leak
        print(f"\n  [{'STABLE' if drift_ok else 'DRIFT!'}] Heap stability: "
              f"initial={measurements.get('heap_idle',0)} B → final={heap3} B  "
              f"drift={heap_drift} B  max_block={max_block3} B")
    except:
        pass

    # ── Summary ────────────────────────────────────────────────────────────────
    passed = sum(1 for r in results if r[0])
    total  = len(results)
    print(f"\n  {'='*58}")
    print(f"  RESULT: {passed}/{total} passed")
    if passed < total:
        print("  FAILED:")
        for r in results:
            if not r[0]:
                print(f"    - {r[1]} → HTTP {r[2]}")

    print(f"\n  ── Final measurements ──────────────────────────────────")
    print(f"  heap idle:        {measurements.get('heap_idle', '?')} B")
    print(f"  heap_min (boot):  {measurements.get('heap_min', '?')} B")
    print(f"  heap_max_block:   {measurements.get('heap_max_block', '?')} B")
    print(f"  heap after POST:  {measurements.get('heap_after_post', '?')} B")
    print(f"  heap final:       {measurements.get('heap_final', '?')} B")
    print(f"  heap drift:       {measurements.get('heap_drift', '?')} B")
    print(f"  fp count:         {measurements.get('fp_count', '?')}")
    print(f"  match conf:       {measurements.get('match_conf', '?')}")
    print(f"  sensor state:     {measurements.get('sensor_state', '?')}")
    print(f"  log entries:      {measurements.get('log_entries', '?')}")
    print(f"  {'='*58}")

    return passed == total


# ── Main ──────────────────────────────────────────────────────────────────────
print("\n=== CoinTrace hw-test — Wave 8 A-2 final measurements ===")

if not SKIP_RESET:
    print("Step 1: Starting COM4 monitor...")
    t_mon = threading.Thread(target=monitor_com4, daemon=True)
    t_mon.start()

    print("Step 2: Sending reset via esptool (COM3 must be free)...")
    reset_device()

    print(f"Step 3: Waiting up to {TIMEOUT_BOOT_S}s for HTTP ready...")
    boot_done.wait(timeout=TIMEOUT_BOOT_S + 2)

    if not device_ip:
        print("\n[ERROR] HTTP ready not detected — check COM4/wiring")
        sys.exit(1)

    # After HTTP ready, open COM3 passively to catch any crash/panic output.
    # Start AFTER esptool is done (esptool already released COM3).
    t_crash = threading.Thread(target=monitor_com_passively,
                               args=(COM_CRASH, crash_lines, "COM3-CRASH", 30),
                               daemon=True)
    t_crash.start()

    print(f"\nStep 4: Device ready at {device_ip} — running REST API tests in 2s...")
    time.sleep(2)  # let WiFi stabilize and crash monitor open
else:
    print(f"Step 1-3: Skipped (--no-reset / --ip). Using IP: {device_ip}")
    print(f"Step 4: Running REST API tests directly...")

print()
success = run_tests(device_ip)

print("\nStep 5: Monitoring 5s for crash output on COM3/COM4...")
time.sleep(5)

if crash_lines:
    print("\n--- COM3 (panic/crash) output ---")
    for l in crash_lines:
        print(f"  {l}")

sys.exit(0 if success else 1)
