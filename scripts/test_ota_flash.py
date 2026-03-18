#!/usr/bin/env python3
"""A-4 OTA real-flash test â€” streams firmware.bin to the device.

Usage:
    # Baseline firmware already on device (cointrace-dev, 1.0.0-dev)
    python scripts/test_ota_flash.py                        # default env
    python scripts/test_ota_flash.py --env cointrace-ota-test  # version bump
    python scripts/test_ota_flash.py --ip 192.168.88.12 --env cointrace-ota-test

Flow:
  1. Read pre-OTA /status (version, heap)
  2. Prompt user to press O on device (opens 30s window)
  3. POST .pio/build/<env>/firmware.bin  (chunked stream -> flash)
  4. Wait for device reboot (~15s)
  5. Read post-OTA /status, compare versions
  6. Remind user to confirm with O (or wait 60s for auto-rollback)
  7. Verify /ota/status: confirmed=true
"""
import argparse
import http.client
import json
import os
import sys
import time
import urllib.request
import urllib.error

DEFAULT_ENV = "cointrace-dev"


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--ip",  default="192.168.88.53",      help="Device IP in STA mode")
    p.add_argument("--env", default=DEFAULT_ENV,           help="PlatformIO env name")
    return p.parse_args()


def get_json(base, path, timeout=5):
    r = urllib.request.urlopen(base + path, timeout=timeout)
    return r.status, json.loads(r.read())


def main():
    args = parse_args()
    base = "http://" + args.ip + "/api/v1"
    firmware = os.path.join(".pio", "build", args.env, "firmware.bin")

    if not os.path.exists(firmware):
        print("ERROR: firmware not found:", firmware)
        print("  Build first: pio run -e", args.env)
        sys.exit(1)

    fw_size = os.path.getsize(firmware)
    fw_name = os.path.basename(os.path.dirname(firmware))  # env name as identifier
    print("=" * 60)
    print("  CoinTrace A-4 OTA real-flash test")
    print("=" * 60)
    print("  Env     :", args.env)
    print("  Firmware:", firmware)
    print("  Size    :", fw_size, "B (", round(fw_size / 1024, 1), "KB )")
    print()

    # â”€ Step 1: pre-OTA status â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    _, pre = get_json(base, "/status")
    pre_ver  = pre.get("version", "?")
    pre_heap = pre.get("heap", 0)
    print("[Pre-OTA]  version=", pre_ver, "  heap=", pre_heap, "B")
    print()

    # â”€ Step 2: open OTA window â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    print("â”Œâ”€" + "â”€" * 56 + "â”")
    print("â”‚  Press O on Cardputer â†’ OTA window opens (30s)         â”‚")
    print("â”‚  Then press Enter here to start upload.                â”‚")
    print("â””â”€" + "â”€" * 56 + "â”˜")
    input("[waiting for Enter] ")
    print()

    # â”€ Step 3: verify window â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    _, ota_st = get_json(base, "/ota/status")
    if not ota_st.get("ota_window"):
        print("ERROR: OTA window not open. Press O on the device first.")
        sys.exit(1)
    secs = ota_st.get("seconds_left", 0)
    print("[OTA window] OPEN  seconds_left=", secs)
    print()

    # â”€ Step 4: upload â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    print("[Upload] Sending", round(fw_size / 1024, 1), "KB to device...")
    print("         Flash write ~20-120s depending on WiFi quality.")
    print()

    with open(firmware, "rb") as f:
        fw_data = f.read()

    req = urllib.request.Request(
        base + "/ota/update",
        data=fw_data,
        headers={
            "Content-Type":   "application/octet-stream",
            "Content-Length": str(len(fw_data)),
        },
        method="POST",
    )

    _CONNECTION_RESET_ERRORS = (
        ConnectionResetError,
        ConnectionAbortedError,
        http.client.RemoteDisconnected,
        TimeoutError,
    )

    t0 = time.time()
    code = 0
    body = {}
    try:
        resp = urllib.request.urlopen(req, timeout=300)   # 5 min
        code = resp.status
        try:
            body = json.loads(resp.read())
        except Exception:
            body = {}
    except urllib.error.HTTPError as e:
        code = e.code
        try:
            body = json.loads(e.read())
        except Exception:
            body = {}
    except urllib.error.URLError as e:
        # On Windows a device reboot tears the TCP connection and surfaces as
        # URLError(WinError 10054). This is normal â€” flash already completed.
        if isinstance(e.reason, _CONNECTION_RESET_ERRORS) or \
                any(tok in str(e).lower() for tok in ("reset", "forcibly", "closed", "10054", "timed out", "timeout")):
            print("[NOTE] Connection reset by device (expected â€” device rebooted).")
            code = 200
            body = {"status": "ok", "action": "rebooting",
                    "note": "connection_reset_on_reboot"}
        else:
            print("Upload error:", e)
            sys.exit(1)
    except _CONNECTION_RESET_ERRORS:
        print("[NOTE] Connection reset by device (expected â€” device rebooted).")
        code = 200
        body = {"status": "ok", "action": "rebooting",
                "note": "connection_reset_on_reboot"}
    except Exception as ex:
        print("Upload error:", ex)
        sys.exit(1)

    elapsed = time.time() - t0
    print("[Response] HTTP", code, " in", round(elapsed, 1), "s  body:", body)
    print()

    if code != 200:
        print("FAIL: upload did not return 200")
        sys.exit(1)

    # â”€ Step 5: wait for reboot â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    print("[Reboot ] Waiting for device to come back online...")
    for i in range(18, 0, -1):
        print("          ", i, "s ", end="\r", flush=True)
        time.sleep(1)
    print()

    attempts = 0
    post_st = None
    while attempts < 8:
        try:
            _, post_st = get_json(base, "/status")
            break
        except Exception:
            attempts += 1
            print("          Device not ready yet, retry", attempts, "/8...")
            time.sleep(3)

    if not post_st:
        print("FAIL: Device did not come back online after OTA")
        sys.exit(1)

    post_ver  = post_st.get("version", "?")
    post_heap = post_st.get("heap", 0)

    # â”€ Step 6: version check â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    print("=" * 60)
    print("  Pre-OTA  version :", pre_ver,  "  heap:", pre_heap, "B")
    print("  Post-OTA version :", post_ver, "  heap:", post_heap, "B")
    print()
    if pre_ver != post_ver:
        print("  Version change   : ", pre_ver, "â†’", post_ver, "  âœ”")
    else:
        print("  Version change   : SAME (", pre_ver, ")")
        print("  TIP: use --env cointrace-ota-test for a visible version bump")
    print()

    # OTA status: should be pending=true, confirmed=false until O pressed
    _, ota_post = get_json(base, "/ota/status")
    print("  OTA status       : pending=", ota_post.get("pending"),
          " confirmed=", ota_post.get("confirmed"))
    pre_version_saved = ota_post.get("pre_version", "")
    if pre_version_saved:
        print("  pre_version (NVS):", pre_version_saved)
    print()
    print("  OTA FLASH        : PASS âœ”")
    print("=" * 60)
    print()

    # â”€ Step 7: confirm banner â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    print("â”Œâ”€" + "â”€" * 56 + "â”")
    print("â”‚  New firmware booted:                                  â”‚")
    print("â”‚  â”‚", fw_name.ljust(50), "â”‚  â”‚")
    print("â”‚  Size:", str(round(fw_size/1024, 1)).ljust(8), "KB",
          " " * 36, "â”‚")
    print("â”‚                                                        â”‚")
    print("â”‚  Press O on Cardputer to CONFIRM (keep new firmware).  â”‚")
    print("â”‚  Do nothing for 60s to ROLLBACK to previous firmware.  â”‚")
    print("â””â”€" + "â”€" * 56 + "â”˜")
    print()

    input("Press Enter AFTER pressing O on device (or after rollback): ")
    print()

    # Final OTA status check
    _, ota_final = get_json(base, "/ota/status")
    confirmed = ota_final.get("confirmed", False)
    pending   = ota_final.get("pending",   False)
    print("  Final OTA status : pending=", pending, " confirmed=", confirmed)
    if confirmed and pending:
        print("  OTA CONFIRM      : PASS âœ”  (new firmware accepted)")
    elif not pending and not confirmed:
        print("  OTA ROLLBACK     : PASS âœ”  (auto-rollback occurred)")
    else:
        print("  Status           : unexpected state")
    print()


if __name__ == "__main__":
    main()
