// WiFiManager.h — WiFi Connectivity (AP + STA mode)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// CONNECTIVITY_ARCHITECTURE.md §4.3 (AP), §4.4 (STA), ADR-005, ADR-006
//
// Boot behaviour (begin()):
//   NVS mode == STA AND credentials stored → connect; on timeout → fall back to AP.
//   Default / no credentials → AP "CoinTrace-XXXX" / "cointrace" / 192.168.4.1
//
// Provisioning (promptSTA()):
//   Reads SSID + pass from M5Cardputer keyboard (blocking), saves to NVS, reconnects.
//   Trigger: 'W' key handler in loop(). ADR-005: keyboard provisioning is unique
//   advantage of Cardputer vs every competing device.
//   TODO(Wave 8 A-2): migrate readLine() to TCA8418-based input (§17.2 [2.5]).
//
// mDNS: "cointrace" → cointrace.local (STA mode, port 80). AP mode: 192.168.4.1.
//
// Thread safety: begin() and promptSTA() must be called from the main Arduino task
// (setup() and loop()). isConnected()/getIP()/getSSID() are read-only, safe to call
// from any context after begin() completes.

#pragma once
#include "NVSManager.h"

class WiFiManager {
public:
    enum class Mode : uint8_t { NONE = 0, AP = 1, STA = 2 };

    WiFiManager()  = default;
    ~WiFiManager() = default;

    /**
     * @brief Start WiFi based on NVS "wifi" namespace config.
     * Must be called once in setup() after nvs.begin() — boot step [10] §17.2.
     * In STA mode: blocks up to 10 s then falls back to AP on timeout.
     * @return true if AP started or STA connected (AP always returns true).
     */
    bool begin(NVSManager& nvs);

    /**
     * @brief Interactive STA provisioning via M5Cardputer keyboard (blocking).
     * Shows full-screen prompt, reads SSID + masked password, saves to NVS,
     * reconnects. On failure falls back to AP mode.
     * Call from loop() on 'W' key. Returns true if STA connection succeeded.
     * TODO(Wave 8 A-2): migrate readLine() to TCA8418 keyboard when initialised.
     */
    bool promptSTA(NVSManager& nvs);

    // ── State (read-only after begin()) ──────────────────────────────────────

    /** @return true while AP is up or STA is associated. */
    bool isConnected() const;

    bool isAP()  const { return mode_ == Mode::AP;  }
    bool isSTA() const { return mode_ == Mode::STA; }
    Mode getMode() const { return mode_; }

    /** @return IP string: "192.168.4.1" in AP, DHCP address in STA. */
    const char* getIP()   const { return ip_;   }

    /** @return Active SSID: "CoinTrace-XXXX" in AP, configured SSID in STA. */
    const char* getSSID() const { return ssid_; }

private:
    bool startAP (const char* ssid);
    bool startSTA(const char* ssid, const char* pass);

    /** Build "CoinTrace-XXXX" from last 4 hex digits of MAC address. */
    static void buildAPSsid(char* out, size_t len);

    Mode mode_     = Mode::NONE;
    char ip_[16]   = {};   // max "255.255.255.255" + NUL = 16 bytes
    char ssid_[33] = {};   // 802.11 SSID max 32 chars + NUL
};
