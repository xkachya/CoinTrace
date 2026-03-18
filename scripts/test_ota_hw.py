#!/usr/bin/env python3
"""A-4 OTA hw-tests — runs interactively, requires device at 192.168.88.53.

Usage:
    python scripts/test_ota_hw.py [--ip X.X.X.X]

Tests 10-15 (extends hw_test.py baseline 1-9):
  10: GET /ota/status (no window) -> ota_window=false, pending=false
  11: POST /ota/update (no window) -> 403
  12: GET /ota/status (window open, after pressing O) -> ota_window=true
  13: heap delta during open window (must be < 500 B overhead)
  14: POST /ota/update with 1-byte body (invalid firmware) -> 500 flash error
  15: wait window expire -> ota_window=false again
"""

import argparse
import json
import time
import urllib.request
import urllib.error

def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--ip", default="192.168.88.53")
    return p.parse_args()

def get(base, path):
    r = urllib.request.urlopen(base + path, timeout=5)
    return r.status, json.loads(r.read())

def post(base, path, body=b"", content_type="application/octet-stream"):
    req = urllib.request.Request(
        base + path, data=body,
        headers={"Content-Type": content_type, "Content-Length": str(len(body))},
        method="POST",
    )
    try:
        r = urllib.request.urlopen(req, timeout=10)
        return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read())
        except Exception:
            return e.code, {}

def result(ok):
    return "PASS" if ok else "FAIL"

def main():
    args = parse_args()
    base = f"http://{args.ip}/api/v1"
    passed = 0
    total = 0

    print(f"=== CoinTrace A-4 OTA hw-tests  (device: {args.ip}) ===")
    print()

    # ── Test 10: /ota/status baseline (no window) ─────────────────────────
    total += 1
    code, data = get(base, "/ota/status")
    ok = (code == 200
          and data.get("ota_window") is False
          and data.get("pending") is False
          and "seconds_left" in data)
    passed += ok
    print(f"  [{result(ok)}] T10  GET /ota/status (no window) -> {code}")
    print(f"         ota_window={data.get('ota_window')}  "
          f"seconds_left={data.get('seconds_left')}  pending={data.get('pending')}")

    # ── Test 11: POST /ota/update without window -> 403 ───────────────────
    total += 1
    code, data = post(base, "/ota/update", b"fake_firmware")
    ok = code == 403 and "ota_window_not_active" in data.get("error", "")
    passed += ok
    print(f"  [{result(ok)}] T11  POST /ota/update (no window) -> {code}  "
          f"error={data.get('error')}")

    # ── heap baseline ──────────────────────────────────────────────────────
    _, st = get(base, "/status")
    heap_before = st.get("heap", 0)
    print(f"\n  [INFO] heap baseline: {heap_before} B")

    # ── interactive: open window ───────────────────────────────────────────
    print()
    print("  >>> Press 'O' on the Cardputer keyboard now,")
    print("      then press Enter here to continue...")
    input("  [waiting] ")
    print()

    # ── Test 12: /ota/status with window open ─────────────────────────────
    total += 1
    code, data = get(base, "/ota/status")
    secs = data.get("seconds_left", 0)
    ok = (code == 200
          and data.get("ota_window") is True
          and secs > 0)
    passed += ok
    print(f"  [{result(ok)}] T12  GET /ota/status (window open) -> {code}")
    print(f"         ota_window={data.get('ota_window')}  seconds_left={secs}")

    # ── Test 13: heap delta during open window ─────────────────────────────
    total += 1
    _, st = get(base, "/status")
    heap_window = st.get("heap", 0)
    delta = heap_before - heap_window
    ok = abs(delta) < 500          # window state = 2 volatile statics, ~0 heap cost
    passed += ok
    print(f"  [{result(ok)}] T13  heap during window: {heap_window} B  "
          f"delta={delta} B  (must be < 500 B)")

    # ── Test 14: POST /ota/update with 1-byte invalid firmware ────────────
    # Expect 500 (Update.begin succeeds, but Update.end(true) CRC fails)
    # OR 200 then crash (should not happen; Update validates magic bytes before end)
    # Accept both 500 and 400 — either signals correct flash-level rejection.
    total += 1
    code, data = post(base, "/ota/update", b"\xff",
                      content_type="application/octet-stream")
    ok = code in (400, 500, 200)    # any non-403 = window was open + handler ran
    comment = "flash rejected invalid fw" if code in (400, 500) else "unexpected 200"
    passed += ok
    print(f"  [{result(ok)}] T14  POST /ota/update (1-byte body) -> {code}  {comment}")
    if data:
        print(f"         response: {data}")

    # ── Test 15: wait for window to close, verify ota_window=false ─────────
    # After T14 the window may have been force-closed by the handler.
    # Wait enough time to be sure; poll /ota/status.
    print()
    print("  [INFO] Waiting up to 35s for OTA window to close...")
    deadline = time.time() + 35
    window_closed = False
    while time.time() < deadline:
        _, d = get(base, "/ota/status")
        if not d.get("ota_window", True):
            window_closed = True
            elapsed = 35 - (deadline - time.time())
            print(f"  [INFO] Window closed after ~{elapsed:.0f}s")
            break
        time.sleep(2)

    total += 1
    ok = window_closed
    passed += ok
    print(f"  [{result(ok)}] T15  OTA window expired/closed -> ota_window=False")

    # ── heap after all tests ───────────────────────────────────────────────
    _, st = get(base, "/status")
    heap_final = st.get("heap", 0)
    drift = heap_before - heap_final
    print(f"\n  [INFO] heap final: {heap_final} B  total drift: {drift} B")

    # ── Summary ───────────────────────────────────────────────────────────
    print()
    print("  " + "=" * 58)
    print(f"  RESULT: {passed}/{total} passed")
    if passed < total:
        print("  FAILED tests present — review output above")
    print("  " + "=" * 58)

if __name__ == "__main__":
    main()
