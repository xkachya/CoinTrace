#!/usr/bin/env python3
"""LDC1101 smoke test utility

Usage:
  python scripts/ldc1101_smoke_test.py --ip 192.168.4.1 [--serial COM3] [--baud 115200]

Performs a sequence of HTTP checks against CoinTrace firmware to verify basic
LDC1101 readiness: status, logs, sensor state, calibration. Optionally tails
serial output for live logs.

This script is safe to run on a developer workstation; it does not require
physical hardware interaction besides following prompts for placing/removing a coin.
"""
import argparse
import time
import sys
import json

try:
    import requests
except Exception:
    print("Missing dependency 'requests'. Install: pip install -r scripts/requirements.txt")
    sys.exit(2)

try:
    import serial
except Exception:
    serial = None


def http_get(base, path, params=None, timeout=5):
    url = f"http://{base}{path}"
    try:
        r = requests.get(url, params=params, timeout=timeout)
        return r.status_code, r.text
    except Exception as e:
        return None, str(e)


def http_post(base, path, data=None, timeout=20):
    url = f"http://{base}{path}"
    try:
        r = requests.post(url, json=data, timeout=timeout)
        return r.status_code, r.text
    except Exception as e:
        return None, str(e)


def tail_serial(port, baud, duration=10):
    if serial is None:
        print("pyserial not installed; skipping serial tail")
        return
    try:
        with serial.Serial(port, baud, timeout=1) as s:
            print(f"Tailing serial {port} @ {baud} for {duration}s...")
            t0 = time.time()
            while time.time() - t0 < duration:
                line = s.readline()
                if line:
                    print(line.decode('utf-8', errors='replace').rstrip())
    except Exception as e:
        print(f"Serial error: {e}")


def main():
    p = argparse.ArgumentParser(description="LDC1101 smoke test for CoinTrace")
    p.add_argument('--ip', required=True, help='device IP or host (e.g. 192.168.4.1)')
    p.add_argument('--serial', help='optional serial port (COM3, /dev/ttyUSB0)')
    p.add_argument('--baud', type=int, default=115200)
    p.add_argument('--tail', type=int, default=8, help='serial tail seconds (if --serial)')
    args = p.parse_args()

    base = f"{args.ip}/api/v1"

    print('\n1) System status')
    sc, body = http_get(base, '/status')
    print('HTTP', sc)
    try:
        print(json.dumps(json.loads(body), indent=2))
    except Exception:
        print(body)

    print('\n2) Recent logs (INFO)')
    sc, body = http_get(base, '/log', params={'n': 200, 'level': 'INFO'})
    print('HTTP', sc)
    print(body[:4000])

    print('\n3) Sensor state')
    sc, body = http_get(base, '/sensor/state')
    print('HTTP', sc)
    try:
        print(json.dumps(json.loads(body), indent=2))
    except Exception:
        print(body)

    print('\n4) Trigger calibration (remove any coin first)')
    if input('Proceed with calibration now? [y/N]: ').lower() != 'y':
        print('Skipping calibration')
    else:
        sc, body = http_post(base, '/calibrate')
        print('HTTP', sc)
        try:
            print(json.dumps(json.loads(body), indent=2))
        except Exception:
            print(body)

    print('\n5) Re-check sensor state')
    sc, body = http_get(base, '/sensor/state')
    print('HTTP', sc)
    try:
        print(json.dumps(json.loads(body), indent=2))
    except Exception:
        print(body)

    if args.serial:
        tail_serial(args.serial, args.baud, duration=args.tail)

    print('\n6) Coin detect interactive check')
    if input('Place coin on coil and press Enter (or skip with N): ').lower() == 'n':
        print('Skipping coin check')
    else:
        print('Waiting 3s for conversion...')
        time.sleep(3)
        sc, body = http_get(base, '/sensor/state')
        print('HTTP', sc)
        try:
            print(json.dumps(json.loads(body), indent=2))
        except Exception:
            print(body)

    print('\nSmoke test complete')


if __name__ == '__main__':
    main()
