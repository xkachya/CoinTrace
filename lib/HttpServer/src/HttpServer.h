// HttpServer.h — REST API HTTP Server (Wave 8 A-2/A-3)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// CONNECTIVITY_ARCHITECTURE.md §5.2, WAVE8_ROADMAP.md §2 A-2/A-3
//
// Registers all /api/v1/ REST routes and serves static files from /sys/web/.
// Non-blocking: AsyncWebServer processes requests on the lwIP thread (not MainLoop).
// ADR-002: all API endpoints exclusively via /api/v1/ prefix.
// CORS: Access-Control-Allow-Origin: * on every /api/v1/ response.
//
// Thread safety notes:
//   Handlers run on lwIP thread — do NOT call Logger::* or NVSManager::* from
//   inside lambda handlers. Use ESP32-native log_i/log_e for any handler-side
//   diagnostics. Reading RingBufferTransport::getEntries() is safe (mutex inside).
//
// Usage (main.cpp):
//   static AsyncWebServer gHttpServer(80);
//   static HttpServer     gHttp;
//   gHttp.begin(gHttpServer, gStorage, gNVS, gWifi, gLFS, gRingTransport,
//               gMeasStore, gFPCache);

#pragma once

#include <ESPAsyncWebServer.h>
#include "IStorageManager.h"
#include "NVSManager.h"
#include "WiFiManager.h"
#include "LittleFSManager.h"
#include "RingBufferTransport.h"
#include "MeasurementStore.h"
#include "FingerprintCache.h"

class HttpServer {
public:
    // Registers all /api/v1/ routes and static web serving on srv.
    // Calls srv.begin() internally — do NOT call it again from main.
    void begin(AsyncWebServer&      srv,
               IStorageManager&    storage,
               NVSManager&         nvs,
               WiFiManager&        wifi,
               LittleFSManager&    lfs,
               RingBufferTransport& ring,
               MeasurementStore&   meas,
               FingerprintCache&   fpCache);

private:
    void registerApiRoutes();
    void registerStaticRoutes();
    void handle404(AsyncWebServerRequest* req);

    // Adds CORS headers to an already-created response.
    static void addCors(AsyncWebServerResponse* resp);

    // Sends a JSON error with standard envelope {\"error\":\"...\"}
    static void sendError(AsyncWebServerRequest* req, int code, const char* error);

    // OTA window state — written by MainLoop ('O' key), read by lwIP handlers.
    // setOtaWindow() must be called once after begin().
    // Both pointers are written exclusively from MainLoop task; reads from lwIP
    // are safe because volatile guarantees visiblity (no torn reads on 32-bit ESP32).
    volatile bool*     otaWindowFlag_   = nullptr;  // true = window open
    volatile uint32_t* otaWindowOpenMs_ = nullptr;  // millis() when window opened
    // Set to true by body chunk handler ONLY after Update.end(true) succeeds.
    // Reset to false at first chunk (index==0). Checked by request handler before
    // committing the NVS metadata and rebooting. Prevents reboot on 0-byte body.
    volatile bool      otaFlashComplete_ = false;

public:
    // Inject OTA window state from main.cpp after begin().
    void setOtaWindow(volatile bool* windowFlag, volatile uint32_t* windowOpenMs) {
        otaWindowFlag_   = windowFlag;
        otaWindowOpenMs_ = windowOpenMs;
    }

private:
    // Dependencies — stored for use inside route handler lambdas.
    AsyncWebServer*      server_  = nullptr;
    IStorageManager*     storage_ = nullptr;
    NVSManager*          nvs_     = nullptr;
    WiFiManager*         wifi_    = nullptr;
    LittleFSManager*     lfs_     = nullptr;
    RingBufferTransport* ring_    = nullptr;
    MeasurementStore*    meas_    = nullptr;
    FingerprintCache*    fp_      = nullptr;
};
