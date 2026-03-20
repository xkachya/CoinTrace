# LDC1101 smoke test

Simple utility to perform a smoke-test sequence against a CoinTrace device with LDC1101.

Prerequisites
- Python 3.9+
- Install deps:

```sh
python -m pip install -r scripts/requirements.txt
```

Usage

```sh
python scripts/ldc1101_smoke_test.py --ip 192.168.4.1
# optional: --serial COM3 --baud 115200 --tail 10
```

What it does
- Queries `/api/v1/status`, `/api/v1/log`, `/api/v1/sensor/state`.
- Optionally triggers `/api/v1/calibrate` (if firmware supports it).
- Optionally tails serial output via `pyserial`.
