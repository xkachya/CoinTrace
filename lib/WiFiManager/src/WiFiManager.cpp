// WiFiManager.cpp — WiFi Connectivity (AP + STA mode)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// CONNECTIVITY_ARCHITECTURE.md §4.3, §4.4, ADR-005, ADR-006

#include "WiFiManager.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <M5Cardputer.h>
#include <esp_mac.h>   // esp_efuse_mac_get_default() — reads eFuse, no WiFi driver required
#include <esp_wifi.h>  // esp_wifi_set_ps() — P1.2: disable power save to reduce I2C DMA interference

// ── Constants ──────────────────────────────────────────────────────────────────
namespace {
    constexpr char     kAPPass[]    = "cointrace";     // ADR-005: fixed AP password
    constexpr char     kMDNSHost[]  = "cointrace";     // → cointrace.local
    constexpr uint32_t kTimeoutMs   = 10000;           // STA connect timeout
    constexpr uint32_t kPollMs      = 250;             // STA status poll interval
    constexpr uint32_t kKeyPollMs   = 50;              // Keyboard poll in promptSTA
}

// ── Private helpers ────────────────────────────────────────────────────────────

/*static*/ void WiFiManager::buildAPSsid(char* out, size_t len) {
    // Read from eFuse directly (ADR-005). WiFi.macAddress() calls esp_wifi_get_mac()
    // which requires the WiFi driver to be initialised — not the case at this point
    // since buildAPSsid() is called BEFORE WiFi.mode() in begin().
    // esp_efuse_mac_get_default() reads the base MAC from eFuse with no driver dependency.
    // MeasurementStore uses the same approach for device_id (test/mocks/esp_mac.h).
    uint8_t mac[6] = {};
    esp_efuse_mac_get_default(mac);
    snprintf(out, len, "CoinTrace-%02X%02X", mac[4], mac[5]);
}

bool WiFiManager::startAP(const char* ssid) {
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(ssid, kAPPass)) return false;

    // P1.2: disable WiFi power save — eliminates DTIM beacon burst I2C collisions.
    // Without this, WiFi wakes every 100ms (DTIM=1) causing I2C bus contention.
    // Trade-off: +~30mA idle current vs. stable I2C at 80SPS (ADR-NAU-010).
    esp_wifi_set_ps(WIFI_PS_NONE);

    // Explicitly set 192.168.4.1 — default on ESP32 but set for clarity.
    WiFi.softAPConfig(
        IPAddress(192, 168, 4, 1),   // local IP
        IPAddress(192, 168, 4, 1),   // gateway
        IPAddress(255, 255, 255, 0)  // subnet
    );

    strlcpy(ssid_, ssid, sizeof(ssid_));
    strlcpy(ip_, "192.168.4.1", sizeof(ip_));
    mode_ = Mode::AP;
    return true;
}

bool WiFiManager::startSTA(const char* ssid, const char* pass) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    uint32_t elapsed = 0;
    while (WiFi.status() != WL_CONNECTED && elapsed < kTimeoutMs) {
        delay(kPollMs);
        elapsed += kPollMs;
    }

    if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect(true);
        return false;
    }

    strlcpy(ssid_, ssid, sizeof(ssid_));
    strlcpy(ip_, WiFi.localIP().toString().c_str(), sizeof(ip_));
    mode_ = Mode::STA;

    // P1.2: disable WiFi power save — eliminates DTIM beacon burst I2C collisions.
    // Must be called after WL_CONNECTED (driver fully up). Same rationale as AP mode.
    esp_wifi_set_ps(WIFI_PS_NONE);

    // mDNS: disabled to preserve heap on ESP32-S3FN8 (no PSRAM).
    // MDNS.begin() uses ~15 KB — too expensive when free heap after WiFi is ~29 KB.
    // Will re-enable in A-3 once PSRAM variant is available or heap budget reviewed.
    return true;
}

// ── Public API ─────────────────────────────────────────────────────────────────

bool WiFiManager::begin(NVSManager& nvs) {
    // Clean up any previous WiFi state (e.g. after promptSTA() retry).
    WiFi.disconnect(/*wifioff=*/true);
    delay(50);

    char    ssid[33]  = {};
    char    pass[64]  = {};
    uint8_t nvsMode   = 0;

    const bool hasCreds = nvs.isReady() &&
                          nvs.loadWifi(ssid, sizeof(ssid), pass, sizeof(pass), nvsMode);

    // Attempt STA if mode == 1 and credentials are present.
    if (hasCreds && nvsMode == 1 && ssid[0] != '\0') {
        if (startSTA(ssid, pass)) return true;
        // Fall through: timeout or auth failure → AP fallback.
    }

    // Default: AP mode.
    char apSsid[33] = {};
    buildAPSsid(apSsid, sizeof(apSsid));
    return startAP(apSsid);
}

bool WiFiManager::isConnected() const {
    if (mode_ == Mode::AP)  return true;                  // AP always "up"
    if (mode_ == Mode::STA) return WiFi.status() == WL_CONNECTED;
    return false;
}

// ── Interactive STA provisioning ──────────────────────────────────────────────

// Read a line of text from M5Cardputer keyboard with a prompt drawn at the
// bottom of the splash screen. Returns true on Enter (non-empty), false on Escape.
// masked=true: shows '*' instead of typed characters.
//
// TODO(Wave 8 A-2): replace with TCA8418-backed readLine() when keyboard
// controller is initialised (§17.2 [2.5]). The display coordinate contract
// (y=88..135 = provisioning area) remains the same.
static bool readLine(const char* prompt, char* buf, size_t maxLen, bool masked) {
    constexpr int16_t kAreaY    = 88;
    constexpr int16_t kAreaH    = 47;
    constexpr int16_t kPromptY  = 93;
    constexpr int16_t kInputY   = 107;

    size_t pos = 0;
    buf[0] = '\0';

    M5Cardputer.Display.fillRect(0, kAreaY, 240, kAreaH, BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(YELLOW);
    M5Cardputer.Display.setCursor(5, kPromptY);
    M5Cardputer.Display.print(prompt);

    auto redraw = [&]() {
        M5Cardputer.Display.fillRect(5, kInputY, 230, 12, BLACK);
        M5Cardputer.Display.setTextColor(WHITE);
        M5Cardputer.Display.setCursor(5, kInputY);
        for (size_t i = 0; i < pos; ++i)
            M5Cardputer.Display.print(masked ? '*' : buf[i]);
        M5Cardputer.Display.print('_');  // text cursor indicator
    };
    redraw();

    while (true) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            const auto& ks = M5Cardputer.Keyboard.keysState();
            // M5Cardputer KeysState: special keys are in dedicated bool fields,
            // NOT in ks.word. ks.word contains only printable ASCII chars.
            if (ks.enter) {
                return pos > 0;                    // Enter: confirm (must be non-empty)
            } else if (ks.fn) {
                return false;                      // Fn key alone: cancel/escape
            } else if (ks.del && pos > 0) {
                buf[--pos] = '\0';
                redraw();
            } else if (!ks.word.empty()) {
                const char c = ks.word[0];
                if (c >= 0x20 && c < 0x7f && pos < maxLen - 1u) {
                    buf[pos++] = c;
                    buf[pos]   = '\0';
                }
                redraw();
            }
        }
        delay(kKeyPollMs);
    }
}

bool WiFiManager::promptSTA(NVSManager& nvs) {
    // Show header bar for the provisioning UI.
    M5Cardputer.Display.fillRect(0, 75, 240, 13, DARKGREEN);
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(5, 78);
    M5Cardputer.Display.print("WiFi  [Enter=retry saved  Fn=new]");

    char ssid[33] = {};
    char pass[64] = {};
    uint8_t savedMode = 0;

    // If saved credentials exist, offer one-key retry before asking keyboard input.
    // Enter → retry saved SSID/pass (no typing required).
    // Fn    → fall through to manual SSID/pass entry.
    const bool hasSaved = nvs.isReady() &&
                          nvs.loadWifi(ssid, sizeof(ssid), pass, sizeof(pass), savedMode) &&
                          ssid[0] != '\0';
    if (hasSaved) {
        M5Cardputer.Display.fillRect(0, 88, 240, 47, BLACK);
        M5Cardputer.Display.setTextColor(YELLOW);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(5, 93);
        M5Cardputer.Display.printf("Saved: %.28s", ssid);  // truncate to fit display
        M5Cardputer.Display.setCursor(5, 107);
        M5Cardputer.Display.print("Enter=retry  Fn=new creds  Esc=cancel");

        bool useNew = false;
        bool cancelled = false;
        while (true) {
            M5Cardputer.update();
            if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
                const auto& ks = M5Cardputer.Keyboard.keysState();
                if (ks.enter) { break; }           // retry saved
                if (ks.fn)    { useNew = true; break; }  // enter new
                // Treat any visible char as "enter new" too (user started typing SSID)
                if (!ks.word.empty()) { useNew = true; break; }
                // Fn key alone or Esc = cancel
                if (ks.del)   { cancelled = true; break; }
            }
            delay(kKeyPollMs);
        }
        if (cancelled) {
            M5Cardputer.Display.fillRect(0, 75, 240, 60, BLACK);
            return false;
        }
        if (!useNew) {
            // Retry saved credentials — skip keyboard prompts below.
            M5Cardputer.Display.fillRect(0, 88, 240, 47, BLACK);
            M5Cardputer.Display.setTextColor(CYAN);
            M5Cardputer.Display.setCursor(5, 93);
            M5Cardputer.Display.printf("Connecting to: %s", ssid);
            goto connect;  // jump past keyboard prompts to the connection block
        }
        // useNew=true: clear saved fields and fall through to keyboard prompts
        ssid[0] = '\0';
        pass[0] = '\0';
    }

    M5Cardputer.Display.fillRect(0, 75, 240, 13, DARKGREEN);
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.setCursor(5, 78);
    M5Cardputer.Display.print("WiFi provisioning   [Fn=cancel]");

    if (!readLine("SSID:", ssid, sizeof(ssid), /*masked=*/false)) {
        M5Cardputer.Display.fillRect(0, 75, 240, 60, BLACK);
        return false;
    }
    if (!readLine("Password:", pass, sizeof(pass), /*masked=*/true)) {
        M5Cardputer.Display.fillRect(0, 75, 240, 60, BLACK);
        return false;
    }

    // Connecting feedback.
    M5Cardputer.Display.fillRect(0, 88, 240, 47, BLACK);
    M5Cardputer.Display.setTextColor(CYAN);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(5, 93);
    M5Cardputer.Display.printf("Connecting to: %s", ssid);

    connect:

    if (!startSTA(ssid, pass)) {
        // Connection failed — restore AP mode and inform user.
        M5Cardputer.Display.fillRect(0, 88, 240, 47, BLACK);
        M5Cardputer.Display.setTextColor(RED);
        M5Cardputer.Display.setCursor(5, 93);
        M5Cardputer.Display.print("Connection failed");
        M5Cardputer.Display.setCursor(5, 107);
        M5Cardputer.Display.print("Returning to AP mode...");
        delay(2500);

        char apSsid[33] = {};
        buildAPSsid(apSsid, sizeof(apSsid));
        startAP(apSsid);   // restore AP

        M5Cardputer.Display.fillRect(0, 75, 240, 60, BLACK);
        return false;
    }

    // Success — persist credentials and briefly confirm.
    nvs.saveWifi(ssid, pass, /*mode=*/1);

    M5Cardputer.Display.fillRect(0, 88, 240, 47, BLACK);
    M5Cardputer.Display.setTextColor(GREEN);
    M5Cardputer.Display.setCursor(5, 93);
    M5Cardputer.Display.print("Connected!");
    M5Cardputer.Display.setCursor(5, 107);
    M5Cardputer.Display.printf("IP: %s  (%s)", ip_, kMDNSHost);
    delay(2000);

    M5Cardputer.Display.fillRect(0, 75, 240, 60, BLACK);
    return true;
}
