# 🔍 CoinTrace™

> **Open-source coin metal analyzer using inductive sensing**
> Identifies metal alloy composition via attenuation curve fingerprinting

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://github.com/xkachya/CoinTrace/blob/main/LICENSE)
[![License: CERN OHL v2](https://img.shields.io/badge/Hardware-CERN_OHL_v2-orange.svg)](https://github.com/xkachya/CoinTrace/blob/main/LICENSE-HARDWARE)
[![License: CC BY-SA 4.0](https://img.shields.io/badge/Docs-CC_BY--SA_4.0-lightgrey.svg)](https://github.com/xkachya/CoinTrace/blob/main/LICENSE-DOCS)
[![Status](https://img.shields.io/badge/Status-Prototype-yellow.svg)]()
[![Platform](https://img.shields.io/badge/Platform-ESP32--S3-red.svg)]()
[![Prior Art](https://img.shields.io/badge/Prior_Art-2026--03--08-green.svg)](https://github.com/xkachya/CoinTrace/blob/main/docs/concept/prior_art_disclosure.md)

**Author:** Yuriy Kachmaryk | **Location:** Lviv, Ukraine | **First published:** 2026-03-08

---

## 📖 Table of Contents

- [What is CoinTrace™?](#what-is-cointrace)
- [How It Works](#how-it-works)
- [vs Competitors](#vs-competitors)
- [Hardware](#hardware)
- [Wiring](#wiring)
- [Getting Started](#getting-started)
- [Supported Metals](#supported-metals)
- [Fingerprint Database](#fingerprint-database)
- [Project Roadmap](#project-roadmap)
- [Prior Art](#prior-art-disclosure)
- [Contributing](#contributing)
- [License](#license)

---

## ⚡ What is CoinTrace™?

CoinTrace™ measures the **attenuation curve** of a coin's electromagnetic
response across 4 controlled distances using an LDC1101 inductive sensor.
This creates a unique **"fingerprint"** that identifies the coin's metal
composition — without touching, scratching or damaging the coin.

### Why CoinTrace™?

| | Commercial verifiers (~$250–600) | CoinTrace™ (~$40) |
|--|--|--|
| Method | Single resistivity value | **Attenuation curve (4 points)** |
| Frequency sweep | Fixed or single frequency | Single frequency (extensible) |
| Database | Closed, proprietary | **Open community database** |
| Coin identification | Metal type only | **Specific coin fingerprint** |
| Source code | Closed | **GPL v3 open source** |
| Price | $250–600 | **~$40 components** |

---

## 🧬 How It Works

CoinTrace™ takes **4 measurements** at different distances and builds
an attenuation curve unique to each metal:

```
Step 1: Baseline (no coin)     → Rp₀, L₀
Step 2: Coin at 0mm (direct)   → Rp₁, L₁
Step 3: Coin + 1mm spacer      → Rp₂, L₂
Step 4: Coin + 3mm spacer      → Rp₃, L₃
```

**Size-independent normalization** (key innovation):

```
ΔRp₁ = Rp₁ - Rp₀   (absolute change)
ΔRp₂ = Rp₂ - Rp₀
ΔRp₃ = Rp₃ - Rp₀

k1 = ΔRp₂ / ΔRp₁   ← independent of coin size!
k2 = ΔRp₃ / ΔRp₁   ← independent of coin size!
```

**Fingerprint vector:**

```
F = [ΔRp₁, k1, k2, slope, ΔL₁]
```

**Identification** via Euclidean distance to known fingerprints:

```
dist = √((ΔRp₁ - ref_ΔRp₁)² + (k1 - ref_k1)² + (k2 - ref_k2)²)
confidence = 1 - (dist / max_dist)
```

### Physics — Skin Effect

Different metals have different conductivity → different skin depth:

| Metal | Conductivity | Skin depth @ 1MHz | Curve shape |
|-------|-------------|-------------------|-------------|
| Silver 999 | 63 MS/m | ~67 µm | Steepest |
| Copper | 59 MS/m | ~69 µm | Very steep |
| Gold 999 | 45 MS/m | ~80 µm | Medium |
| Aluminum | 37 MS/m | ~88 µm | Medium |
| Steel | 6–10 MS/m | ~200 µm | Flattest |

---

## 🔧 Hardware

### Bill of Materials

| Component | Model | ~Price |
|-----------|-------|--------|
| Main controller | M5Stack Cardputer-Adv (ESP32-S3) | ~$30 |
| Inductive sensor | LDC1101 Click Board (MIKROE-3240) | ~$27 |
| Spacers (1mm, 3mm) | 3D printed or cardboard | free |
| **Total** | | **~$57** |

### Features of Cardputer-Adv

- ESP32-S3FN8 @ 240 MHz dual-core
- 1.14" TFT color display (240×135)
- 56-key keyboard
- 1750 mAh built-in battery
- microSD card slot (fingerprint database storage)
- WiFi + BLE 5.0
- BMI270 IMU (detects level positioning)
- USB-C (Serial + OTA updates)

---

## 🔌 Wiring

### LDC1101 Click Board → Cardputer-Adv EXT Header

```
LDC1101 Click    EXT Header Pin    GPIO
─────────────────────────────────────────
SCK          →   Pin 7          → G40
MOSI         →   Pin 9          → G14
MISO         →   Pin 11         → G39
CS           →   Pin 13         → G5
INT (INTB)   →   Pin 3          → G4
GND          →   Pin 4          → GND
3.3V         →   Stamp-S3A 3V3  → 3V3
```

> ⚠️ Note: G40/G14/G39 are shared with microSD SPI bus.
> Use separate CS pins. Managed automatically in firmware.

### EXT Header Pinout Reference

```
┌─────────────────────────────┐
│  1 (G3/RST)  ○  ○  2 (5VIN)│
│  3 (G4/INT)  ●  ○  4 (GND) │  ← LDC INT + GND
│  5 (G6)      ○  ○  6 (5VOUT│
│  7 (G40/SCK) ●  ○  8 (G8)  │  ← LDC SCK
│  9 (G14/MOSI)●  ○  10(G9)  │  ← LDC MOSI
│  11(G39/MISO)●  ○  12(G13) │  ← LDC MISO
│  13(G5/CS)   ●  ○  14(G15) │  ← LDC CS
└─────────────────────────────┘
```

---

## 🚀 Getting Started

### For Developers (Modify Firmware)

Want to customize CoinTrace firmware or contribute code?

📖 **[Development Setup Guide](docs/guides/development-setup.md)** — Complete guide for setting up VS Code + PlatformIO on Windows. Covers:
- Install development tools (VS Code, PlatformIO, drivers)
- Clone and build CoinTrace firmware
- Upload to M5Stack Cardputer-Adv
- Understand project structure and M5Stack API
- Troubleshooting and debugging

**Beginner-friendly:** Never coded before? The guide walks you through everything step-by-step.

---

### Quick Start (Advanced Users)

Already have PlatformIO? Quick setup:

#### Prerequisites

- [PlatformIO](https://platformio.org/) already installed
- M5Stack Cardputer-Adv board
- LDC1101 Click Board (MIKROE-3240)
- 7 jumper wires
- USB-C cable (data + power)

#### Installation

```bash
# Clone repository
git clone https://github.com/xkachya/CoinTrace.git
cd CoinTrace/firmware

# Build and upload (PlatformIO)
pio run --target upload

# Monitor serial output
pio device monitor --baud 115200
```

### PlatformIO Configuration

```ini
[env:m5stack-cardputer]
platform = espressif32@6.7.0
board = esp32-s3-devkitc-1
framework = arduino
upload_speed = 1500000
build_flags =
    -DESP32S3
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
lib_deps =
    M5Cardputer=https://github.com/m5stack/M5Cardputer
    bogde/HX711
```

### Python Monitor (for data analysis)

```bash
cd tools/
pip install -r requirements.txt
python serial_monitor.py --port COM3 --baud 115200
```

Outputs real-time CSV + live attenuation curve plot.

---

## 📊 Supported Metals

| Metal | Conductivity | Status | Notes |
|-------|-------------|--------|-------|
| Silver 999 | 63 MS/m | ✅ Planned | Steepest curve |
| Silver 925 | ~60 MS/m | ✅ Planned | |
| Silver 900 | ~57 MS/m | ✅ Planned | |
| Copper | 59 MS/m | ✅ Planned | |
| Gold 999 | 45 MS/m | 🔄 Planned | |
| Gold 585 | ~27 MS/m | 🔄 Planned | |
| Aluminum | 37 MS/m | 🔄 Planned | |
| Steel (magnetic) | 6–10 MS/m | ✅ Planned | |
| Nickel silver | ~4 MS/m | 🔄 Planned | |

---

## 🗄️ Fingerprint Database

CoinTrace™ uses a **tiered fingerprint database**:

**🆓 Community tier (free, open source):**
Common metals and popular coins. Schema + basic dataset
published under CC BY-SA 4.0. Works fully offline.

**💎 Premium tier (subscription, future):**
Extended database of 5000+ coins via cloud API.
The _service_ is commercial — the _protocol_ stays open.
GPL v3 requires open code, not free services.

Each fingerprint is a normalized measurement vector
that is **device-independent** — k1 and k2 values are
the same across all CoinTrace™ devices.

### Fingerprint Format

```json
{
  "version": 1,
  "coin": {
    "name": "Austrian Maria Theresa Thaler",
    "year": 1780,
    "country": "Austria",
    "denomination": "1 Thaler",
    "diameter_mm": 39.5,
    "weight_g": 28.06,
    "metal": "Silver 833"
  },
  "fingerprint": {
    "dRp1": 312.4,
    "k1": 0.714,
    "k2": 0.388,
    "slope": -89.2,
    "dL1": 724.1
  },
  "metadata": {
    "device": "CoinTrace™ v1.0",
    "temp_c": 23.4,
    "date": "2026-03-21",
    "contributor": "ykachmaryk"
  }
}
```

### How to Contribute a Fingerprint

1. Measure your coin with CoinTrace™
2. Export via keyboard: `SAVE` → `EXPORT`
3. Copy JSON from microSD
4. Submit via Pull Request to `database/samples/`

---

## 🗺️ Project Roadmap

```
V1 — Basic Prototype (current)
  ✅ LDC1101 + Cardputer-Adv
  ✅ 4-distance measurement
  ✅ Serial data output
  ⬜ Python analysis tools
  ⬜ OLED/TFT display UI
  ⬜ Local fingerprint database (microSD)

V2 — CoinTrace™ Device
  ⬜ Full keyboard UI
  ⬜ WiFi sync with cloud database
  ⬜ OTA firmware updates
  ⬜ Temperature compensation (NTC)
  ⬜ 50+ coin fingerprints

V3 — Community Platform
  ⬜ Web API for fingerprint database
  ⬜ Free tier: common coins (open dataset, CC BY-SA)
  ⬜ Premium tier: 5000+ coins (subscription service)
  ⬜ Community contribution system
  ⬜ Mobile companion app

Future Research
  ⬜ Advanced measurement techniques
  ⬜ Enhanced classification algorithms
  ⬜ Additional sensor capabilities
```

---

## 🛡️ Prior Art Disclosure

**First published:** 2026-03-08
**Repository:** https://github.com/xkachya/CoinTrace

This repository serves as a **formal prior art disclosure**
for the following novel concepts:

1. **Attenuation curve method** — measuring LDC1101 Rp parameter
   at 4 controlled distances to build a distance-attenuation curve
2. **Size-independent coefficients** k1, k2 — normalized ratios
   enabling cross-device comparison without recalibration
3. **Fingerprint vector** [ΔRp₁, k1, k2, slope, ΔL₁] — unique
   identifier compared via Euclidean distance metric
4. **Community fingerprint database** — crowd-sourced normalized
   vectors for coin identification across multiple devices

Full disclosure: [docs/concept/prior_art_disclosure.md](docs/concept/prior_art_disclosure.md)

---

## 🤝 Contributing

Contributions are welcome! See [CONTRIBUTING.md](CONTRIBUTING.md).

### Ways to Help

- 🔬 **Add coin fingerprints** — most valuable contribution!
- 🐛 **Report bugs** — open an issue
- 💡 **Suggest features** — open an issue with `[FEATURE]`
- 🔧 **Submit code** — fork → branch → PR
- ⭐ **Star the repo** — helps others find CoinTrace™

### DCO — Developer Certificate of Origin

By contributing you certify you have the right to submit
under GPL v3. Sign your commits: `git commit -s`

---

## 📄 License

| Component | License |
|-----------|---------|
| Firmware (code) | [GNU GPL v3.0](LICENSE) |
| Hardware (schematics) | [CERN OHL v2](LICENSE-HARDWARE) |
| Documentation | [CC BY-SA 4.0](LICENSE-DOCS) |

Copyright © 2026 **Yuriy Kachmaryk**, Lviv, Ukraine

### Trademark Notice

**CoinTrace™** is a trademark of Yuriy Kachmaryk. The software, hardware designs,
and documentation are open source under GPL v3 / CERN OHL v2 / CC BY-SA 4.0 licenses,
but the CoinTrace™ name and brand are protected. See [LICENSE-BRAND.md](LICENSE-BRAND.md)
for usage guidelines.

---

## 🔗 Links

- 📁 **Repository:** https://github.com/xkachya/CoinTrace
- 🐛 **Issues:** https://github.com/xkachya/CoinTrace/issues
- 💬 **Discussions:** https://github.com/xkachya/CoinTrace/discussions
- 📋 **Prior Art:** https://github.com/xkachya/CoinTrace/blob/main/docs/concept/prior_art_disclosure.md

---

*CoinTrace™ is an independent open-source project not affiliated with
any commercial coin verification company or MikroElektronika.
CoinTrace™ is a trademark of Yuriy Kachmaryk.
LDC1101 is a trademark of Texas Instruments.
All product comparisons are based on publicly available specifications.*
