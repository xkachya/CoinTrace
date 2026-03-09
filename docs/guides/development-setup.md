# CoinTrace Development Environment Setup

**Setting up VS Code and PlatformIO for CoinTrace firmware development**

---

## 🎯 Who is this guide for?

This guide helps you set up a complete development environment for building and modifying CoinTrace firmware. Choose your path:

| Your Experience | Time Needed | Start Here |
|----------------|-------------|------------|
| **Never coded before** | 30-45 minutes | [Part 1: Complete Setup](#part-1-complete-beginner-setup-windows) |
| **Have VS Code installed** | 10-15 minutes | [Part 2: Project Setup](#part-2-cointrace-project-setup) |
| **Have PlatformIO installed** | 5 minutes | [Part 2.2: Open Project](#22-open-project-in-platformio) |
| **Want to customize firmware** | Ongoing | [Part 4: Development](#part-4-cointrace-firmware-development) |

**What you'll learn:**
- ✅ Install VS Code and PlatformIO
- ✅ Clone and open CoinTrace project
- ✅ Build and upload firmware to M5Stack Cardputer-Adv
- ✅ Understand project structure and make modifications
- ✅ Debug and troubleshoot common issues

---

## 📋 Prerequisites

**Hardware:**
- ✅ M5Stack Cardputer-Adv (ESP32-S3)
- ✅ USB-C cable (must support data transfer, not just power)
- ✅ Windows PC (this guide is Windows-specific)

**Software:**
- Internet connection (for downloading tools and libraries)
- Administrator access (for driver installation)

---

## Part 1: Complete Beginner Setup (Windows)

### 1.1 Install Visual Studio Code

**What is VS Code?**
VS Code is a free code editor from Microsoft. It's lightweight, powerful, and supports embedded development through extensions.

**Installation Steps:**

1. **Download VS Code:**
   - Visit: https://code.visualstudio.com/
   - Click **"Download for Windows"** (blue button)
   - File size: ~100 MB

2. **Run the installer:**
   - Open `VSCodeUserSetup-x64-X.XX.X.exe`
   - ✅ Check: "Add to PATH" (important!)
   - ✅ Check: "Create a desktop icon" (optional, but convenient)
   - Click **Install**

3. **Verify installation:**
   - Open VS Code from Start Menu
   - You should see the Welcome screen

**✅ Checkpoint:** VS Code is running, showing the welcome page.

---

### 1.2 Install PlatformIO Extension

**What is PlatformIO?**
PlatformIO is a professional ecosystem for embedded development. It handles:
- Compiler toolchains (for ESP32-S3)
- Library management
- Board configurations
- Upload utilities

**Installation Steps:**

1. **Open Extensions panel:**
   - In VS Code, click Extensions icon (left sidebar) or press `Ctrl+Shift+X`

2. **Search for PlatformIO:**
   - Type: `platformio ide` in search box
   - Find: **"PlatformIO IDE"** by PlatformIO
   - Click **Install**

3. **Wait for installation:**
   - This takes 5-10 minutes (downloads ~500 MB of tools)
   - Progress shown in bottom-right corner
   - ☕ Good time for coffee!

4. **Restart VS Code:**
   - Click **Reload** button when prompted
   - PlatformIO icon appears in left sidebar (alien logo)

**✅ Checkpoint:** PlatformIO icon visible in sidebar, "PIO Home" opens when clicked.

---

### 1.3 Install Git (Optional but Recommended)

**What is Git?**
Git is version control software. It helps you:
- Clone the CoinTrace repository
- Track your changes
- Update to latest code
- Contribute improvements back to project

**Installation Steps:**

1. **Download Git for Windows:**
   - Visit: https://git-scm.com/download/win
   - Download: 64-bit installer

2. **Run installer:**
   - Accept default settings (just click Next)
   - Important: Keep "Git from command line" selected

3. **Verify installation:**
   - Open PowerShell: Press `Win+X`, choose "PowerShell"
   - Type: `git --version`
   - Should show: `git version 2.XX.X`

**✅ Checkpoint:** `git --version` command works in PowerShell.

**Alternative (if you skip Git):**
You can download CoinTrace as a ZIP file instead (see Part 2.1 Option B).

---

## Part 2: CoinTrace Project Setup

### 2.1 Get CoinTrace Source Code

Choose one method:

#### **Option A: Clone with Git** (recommended)

1. **Choose location:**
   ```powershell
   # Example: clone to your Documents folder
   cd $HOME\Documents
   ```

2. **Clone repository:**
   ```powershell
   git clone https://github.com/xkachya/CoinTrace.git
   cd CoinTrace
   ```

3. **Verify:**
   ```powershell
   dir
   # You should see: firmware/, docs/, hardware/, README.md, etc.
   ```

#### **Option B: Download ZIP** (if no Git)

1. **Download:**
   - Visit: https://github.com/xkachya/CoinTrace
   - Click green **"Code"** button → **"Download ZIP"**

2. **Extract:**
   - Right-click downloaded ZIP → **"Extract All..."**
   - Choose location (e.g., `C:\Users\YourName\Documents\CoinTrace`)

3. **Verify:**
   - Folder should contain: `firmware/`, `docs/`, `README.md`, etc.

**✅ Checkpoint:** CoinTrace folder exists with project files inside.

---

### 2.2 Open Project in PlatformIO

1. **Open VS Code**

2. **Open CoinTrace folder:**
   - `File` → `Open Folder...`
   - Navigate to CoinTrace directory
   - Click **"Select Folder"**

3. **PlatformIO detection:**
   - PlatformIO automatically detects `platformio.ini` file
   - Bottom toolbar shows PlatformIO buttons (Build, Upload, etc.)
   - If not visible: Click PlatformIO icon in sidebar → **"Open Project"**

**✅ Checkpoint:** Bottom toolbar shows: Home, Build, Upload, Clean buttons.

---

### 2.3 Understanding Project Structure

```
CoinTrace/
├── platformio.ini          # Project configuration (already set up!)
├── src/                    # Main source code
│   └── main.cpp            # Entry point
├── lib/                    # Custom libraries
│   └── LDC1101/            # Sensor driver
├── include/                # Header files
├── test/                   # Unit tests
├── data/                   # SPIFFS/LittleFS data
├── docs/                   # Documentation
└── hardware/               # Schematics, PCB files
```

**Key files:**
- `platformio.ini` — Board settings, libraries (don't modify unless you know what you're doing)
- `src/main.cpp` — Main firmware code (safe to explore and modify)
- `lib/LDC1101/` — LDC1101 sensor driver

---

### 2.4 Library Dependencies (Automatic)

PlatformIO will automatically download required libraries:
- **M5Cardputer** — M5Stack hardware support
- **TFT_eSPI** — Display driver
- **Wire** — I2C communication (for LDC1101)

**First time:**
When you first build the project, PlatformIO downloads these libraries (~5 minutes).

**✅ Checkpoint:** No action needed — PlatformIO handles this automatically!

---

## Part 3: Build and Upload Firmware

### 3.1 Connect M5Stack Cardputer-Adv

1. **Connect USB cable:**
   - Plug USB-C cable into M5Stack Cardputer
   - Plug other end into your PC
   - Device should power on (screen lights up)

2. **Install USB driver (if needed):**
   - M5Stack uses **CP2104** USB-to-serial chip
   - Windows usually installs driver automatically
   - If not, download from: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers
   - Choose: "CP210x Windows Drivers" → Download → Install

3. **Check COM port:**
   - Open Device Manager: `Win+X` → "Device Manager"
   - Expand: **"Ports (COM & LPT)"**
   - Look for: **"Silicon Labs CP210x USB to UART Bridge (COMX)"**
   - Note the COM port number (e.g., COM3, COM5)

**Troubleshooting:**
- ❌ Device not detected → Try different USB cable (must support data!)
- ❌ "Unknown device" → Install CP210x driver (see step 2)
- ❌ Multiple COM ports → Unplug/replug to identify correct one

**✅ Checkpoint:** Device Manager shows COM port for M5Stack.

---

### 3.2 Build Firmware

1. **Click Build button:**
   - Bottom toolbar → Click **checkmark icon** (Build)
   - Or press: `Ctrl+Alt+B`

2. **Wait for compilation:**
   - First build takes 5-10 minutes (compiles all libraries)
   - Subsequent builds: ~30 seconds (only changed files)
   - Output window shows progress

3. **Success indicators:**
   - Green "SUCCESS" message
   - Shows memory usage:
     ```
     RAM:   [==        ]  23.4% (used 76800 of 327680 bytes)
     Flash: [====      ]  45.2% (used 748288 of 1654784 bytes)
     ```

**Common errors:**
- ❌ "Library not found" → Wait, PlatformIO is downloading libraries
- ❌ "Python not found" → Restart VS Code, PlatformIO installs Python automatically
- ❌ Syntax errors → Check your code modifications

**✅ Checkpoint:** Build succeeds with green SUCCESS message.

---

### 3.3 Upload to Device

1. **Click Upload button:**
   - Bottom toolbar → Click **arrow icon** (Upload)
   - Or press: `Ctrl+Alt+U`

2. **Automatic upload process:**
   - PlatformIO compiles code (if changed)
   - Detects COM port automatically
   - Uploads firmware (~15 seconds)
   - Resets device

3. **Monitor upload:**
   ```
   Writing at 0x00010000... (10 %)
   Writing at 0x00020000... (25 %)
   ...
   Writing at 0x000b0000... (100 %)
   Wrote 748288 bytes (485632 compressed) at 0x00010000
   Hash of data verified.
   
   Leaving...
   Hard resetting via RTS pin...
   ```

4. **Verify:**
   - M5Stack screen should show CoinTrace interface
   - If blank: Press reset button (on side of device)

**Troubleshooting:**
- ❌ "No serial port detected" → Check USB connection, try different port
- ❌ "Failed to connect" → Hold BOOT button while uploading
- ❌ Upload stuck → Unplug/replug device, try again

**✅ Checkpoint:** Firmware uploaded, CoinTrace UI displays on device screen.

---

### 3.4 Serial Monitor (Debugging)

**View device output:**

1. **Open Serial Monitor:**
   - Bottom toolbar → Click **plug icon** (Serial Monitor)
   - Or: `Ctrl+Alt+S`

2. **View logs:**
   ```
   CoinTrace v1.0.0
   Initializing LDC1101...
   LDC sensor OK
   Display initialized
   Ready for measurement
   ```

3. **Close monitor:**
   - Click same plug icon again
   - Or close the terminal tab

**Use cases:**
- 🐛 Debugging: Print values with `Serial.println()`
- 📊 Monitoring: Watch sensor readings in real-time
- ⚠️ Error diagnosis: See crash messages, stack traces

---

## Part 4: CoinTrace Firmware Development

### 4.1 Understanding Firmware Structure

**Entry point: `src/main.cpp`**

```cpp
#include <M5Cardputer.h>
#include "ldc1101.h"

void setup() {
    // Runs once at startup
    M5Cardputer.begin();        // Initialize M5Stack hardware
    Serial.begin(115200);        // USB serial communication
    initLDC1101();               // Initialize sensor
    initDisplay();               // Setup display
}

void loop() {
    // Runs continuously
    M5Cardputer.update();        // Update button/keyboard state
    
    if (measureButtonPressed()) {
        performMeasurement();     // Measure coin attenuation
        displayResults();         // Show on screen
    }
    
    delay(10);                   // Small delay
}
```

**Key concepts:**
- `setup()` — Runs once at boot (initialization)
- `loop()` — Runs forever (main logic)
- `M5Cardputer.update()` — Must call before reading keyboard/buttons

---

### 4.2 CoinTrace Measurement Flow

**How CoinTrace works:**

1. **Initialize LDC1101:**
   ```cpp
   initLDC1101();  // Configure sensor with fixed frequency
   ```

2. **Measure at 4 distances:**
   ```cpp
   float k0 = measureAtDistance(0);   // 0mm (surface contact)
   float k1 = measureAtDistance(2);   // 2mm spacer
   float k2 = measureAtDistance(4);   // 4mm spacer
   float k3 = measureAtDistance(8);   // 8mm spacer
   ```

3. **Create fingerprint vector:**
   ```cpp
   Fingerprint fp = {k0, k1, k2, k3};
   ```

4. **Compare with database:**
   ```cpp
   CoinMatch result = matchFingerprint(fp);
   displayResult(result);  // "Canadian Silver Dollar (1967)"
   ```

**Attenuation Curve:**
The curve shape uniquely identifies metal composition — copper drops fast, silver slower, steel stays high.

---

### 4.3 M5Stack Cardputer API Basics

**Display:**
```cpp
// Text
M5.Display.setTextSize(2);
M5.Display.setTextColor(TFT_WHITE);
M5.Display.println("CoinTrace");

// Clear screen
M5.Display.fillScreen(TFT_BLACK);

// Draw shapes
M5.Display.drawRect(10, 10, 100, 50, TFT_GREEN);
M5.Display.fillCircle(60, 35, 20, TFT_RED);
```

**Keyboard:**
```cpp
M5Cardputer.update();  // Must call first!

if (M5Cardputer.Keyboard.isKeyPressed('m')) {
    Serial.println("Measure button pressed");
}

// Check specific keys
if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
    startMeasurement();
}
```

**Battery:**
```cpp
int batteryLevel = M5.Power.getBatteryLevel();  // 0-100%
bool isCharging = M5.Power.isCharging();

M5.Display.printf("Battery: %d%%", batteryLevel);
```

**SD Card (optional):**
```cpp
#include <SD.h>

if (SD.begin()) {
    File file = SD.open("/measurements.csv", FILE_APPEND);
    file.println("timestamp,k0,k1,k2,k3");
    file.close();
}
```

---

### 4.4 Making Modifications

**Example: Add custom welcome message**

1. **Edit `src/main.cpp`:**
   ```cpp
   void setup() {
       M5Cardputer.begin();
       M5.Display.setTextSize(2);
       M5.Display.println("CoinTrace v1.1");
       M5.Display.println("by YourName");  // ← Your custom text
       delay(2000);  // Show for 2 seconds
   }
   ```

2. **Build:**
   - `Ctrl+Alt+B` or click checkmark icon

3. **Upload:**
   - `Ctrl+Alt+U` or click arrow icon

4. **Verify:**
   - Device shows your custom message on startup

**That's it!** You just modified CoinTrace firmware. 🎉

---

### 4.5 LDC1101 Sensor Driver

**Library location:** `lib/LDC1101/`

**Basic usage:**
```cpp
#include "ldc1101.h"

// Initialize sensor
LDC1101 sensor;
sensor.begin();

// Configure (fixed frequency)
sensor.setRpMin(0x01);
sensor.setRpMax(0x1F);
sensor.setFrequency(LDC1101_DEFAULT_FREQ);

// Read impedance
float impedance = sensor.readImpedance();
Serial.printf("Impedance: %.2f\n", impedance);
```

**Key functions:**
- `begin()` — Initialize I2C communication
- `setFrequency()` — Set measurement frequency (fixed)
- `readImpedance()` — Get current impedance value
- `calibrate()` — Perform baseline calibration

**Wiring (already done on M5Stack Cardputer):**
- LDC1101 SDA → GPIO 2
- LDC1101 SCL → GPIO 1
- LDC1101 VCC → 3.3V
- LDC1101 GND → GND

---

## Part 5: Troubleshooting

### Build Errors

**Error: "Library XYZ not found"**
```
Library Manager: Installing XYZ
```
**Solution:** Wait, PlatformIO is downloading. If stuck >5 min, try:
```powershell
pio pkg install
```

---

**Error: "Python not found"**
**Solution:** 
- Restart VS Code
- PlatformIO installs Python automatically
- Check: `python --version` in terminal

---

**Error: "Compilation failed"**
**Solution:**
- Check for syntax errors (missing semicolons, brackets)
- Read error message — shows file and line number
- Undo recent changes: `Ctrl+Z`

---

### Upload Errors

**Error: "Serial port not found"**
**Solution:**
1. Check USB cable (data cable, not power-only)
2. Verify COM port in Device Manager
3. Try different USB port on PC
4. Restart device (unplug/replug)

---

**Error: "Failed to connect to ESP32"**
**Solution:**
1. Hold **BOOT** button on M5Stack while clicking Upload
2. Release BOOT when upload starts
3. Or: Press RESET while holding BOOT, release BOOT, click Upload

---

**Error: "Upload timeout"**
**Solution:**
- Upload speed too high → Edit `platformio.ini`:
  ```ini
  upload_speed = 115200  ; Slower but more reliable
  ```
- Or: Try different USB cable/port

---

### Runtime Issues

**Display blank/frozen:**
```cpp
// Add to setup():
M5.Display.setBrightness(200);  // Increase brightness
M5.Display.fillScreen(TFT_BLACK);  // Clear screen
```

---

**Sensor not responding:**
```cpp
// Check I2C connection:
if (!sensor.begin()) {
    Serial.println("LDC1101 not found!");
    while(1);  // Halt
}
```

---

**Serial monitor garbled text:**
- Baud rate mismatch
- Set both sides to 115200:
  ```cpp
  Serial.begin(115200);  // In code
  ```
  And in `platformio.ini`:
  ```ini
  monitor_speed = 115200
  ```

---

## Part 6: Next Steps

### 📚 Further Learning

**Documentation:**
- [M5Cardputer Docs](https://docs.m5stack.com/en/core/M5Cardputer)
- [PlatformIO Docs](https://docs.platformio.org/)
- [ESP32-S3 Reference](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/)

**Tutorials:**
- Read other guides in `docs/guides/`:
  - `calibration-guide.md` — Device calibration
  - `measuring-coins.md` — Best practices for measurements
  - `interpreting-results.md` — Understanding fingerprints

**Community:**
- GitHub Issues: Report bugs, request features
- GitHub Discussions: Ask questions, share projects
- Pull Requests: Contribute your improvements!

---

### 🛠️ Advanced Topics

**Custom fingerprints:**
- Measure your own coins
- Add to database (`data/fingerprints.json`)
- Calibrate for your device's specific characteristics

**Hardware modifications:**
- Different LDC sensor configurations
- Additional spacers for finer resolution
- Custom enclosure designs (in `hardware/3d-models/`)

**Firmware features:**
- WiFi connectivity (OTA updates)
- Cloud database sync
- Multi-language UI
- Export measurements to CSV/JSON

---

### 🤝 Contributing to CoinTrace

We welcome contributions!

1. **Fork the repository** on GitHub
2. **Create a branch** for your feature:
   ```bash
   git checkout -b feature/my-improvement
   ```
3. **Make changes** and test thoroughly
4. **Commit with clear message:**
   ```bash
   git commit -m "Add support for coin thickness detection"
   ```
5. **Push to your fork:**
   ```bash
   git push origin feature/my-improvement
   ```
6. **Open Pull Request** on GitHub

**Contribution ideas:**
- 🐛 Fix bugs
- 📖 Improve documentation
- 🌍 Add translations
- ✨ Add features (new coin detection modes, UI improvements)
- 🧪 Write unit tests

---

## Appendices

### A: PlatformIO Command Line Interface

If you prefer terminal over GUI:

```bash
# Build
pio run

# Upload
pio run -t upload

# Serial monitor
pio device monitor

# Clean build
pio run -t clean

# List devices
pio device list

# Install library
pio pkg install -l "M5Cardputer"

# Update dependencies
pio pkg update
```

---

### B: Useful VS Code Extensions

While PlatformIO IDE is all you need, these extensions enhance development:

- **C/C++ IntelliSense** (Microsoft) — Better code completion
- **GitLens** — Enhanced Git integration
- **Error Lens** — Inline error messages
- **Better Comments** — Colorful comment highlighting
- **Bracket Pair Colorizer** — Easier to read nested code

Install: `Ctrl+Shift+X` → Search → Install

---

### C: Debugging with Serial.println()

**Basic debugging:**
```cpp
void measureCoin() {
    Serial.println("=== Starting measurement ===");
    
    float k0 = measureAtDistance(0);
    Serial.printf("k0 = %.3f\n", k0);
    
    float k1 = measureAtDistance(2);
    Serial.printf("k1 = %.3f\n", k1);
    
    Serial.println("=== Measurement complete ===");
}
```

**Conditional debugging:**
```cpp
#define DEBUG 1

#if DEBUG
    #define DEBUG_PRINT(x) Serial.println(x)
#else
    #define DEBUG_PRINT(x)
#endif

// Use in code:
DEBUG_PRINT("Debug: entered loop");
```

---

### D: Memory Optimization Tips

ESP32-S3 has 8MB PSRAM, but firmware size matters:

**Reduce flash usage:**
```cpp
// Store strings in flash memory (not RAM)
const char* msg = "Hello";  // Uses RAM ❌
const char msg[] PROGMEM = "Hello";  // Uses Flash ✅

// Print from flash:
Serial.println(FPSTR(msg));
```

**Monitor memory:**
```cpp
Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
Serial.printf("PSRAM: %d bytes\n", ESP.getFreePsram());
```

---

### E: platformio.ini Explained

```ini
[env:m5cardputer]
platform = espressif32          ; ESP32 platform
board = esp32-s3-devkitc-1      ; Generic ESP32-S3 board
framework = arduino              ; Use Arduino framework

; Board configuration
board_build.mcu = esp32s3
board_build.f_cpu = 240000000L   ; CPU frequency: 240MHz
board_build.flash_mode = dio
board_build.flash_size = 16MB    ; M5Cardputer has 16MB
board_build.partitions = default_16MB.csv

; Upload/Monitor settings
upload_speed = 921600            ; Fast upload (can reduce if issues)
monitor_speed = 115200           ; Serial monitor baud rate

; Build flags
build_flags = 
    -DCORE_DEBUG_LEVEL=3         ; Debug verbosity (0-5)
    -DBOARD_HAS_PSRAM            ; Enable 8MB PSRAM
    
; Library dependencies
lib_deps = 
    m5stack/M5Cardputer @ ^1.0.0
    bodmer/TFT_eSPI @ ^2.5.0
```

---

## 📞 Getting Help

**Documentation:**
- CoinTrace README: [README.md](../../README.md)
- Hardware docs: [docs/hardware/](../hardware/)
- API reference: [docs/api/](../api/)

**Community:**
- GitHub Issues: [Report bugs](https://github.com/xkachya/CoinTrace/issues)
- GitHub Discussions: [Ask questions](https://github.com/xkachya/CoinTrace/discussions)

**Maintainer:**
- Yuriy Kachmaryk
- Lviv, Ukraine
- GitHub: [@xkachya](https://github.com/xkachya)

---

**License:** CC BY-SA 4.0 (Documentation)  
**Last Updated:** 2026-03-09  
**Version:** 1.0

---

🎉 **Congratulations!** You've successfully set up CoinTrace development environment. Happy coding! 🚀
