// HttpServer.cpp — REST API HTTP Server Implementation (Wave 8 A-2/A-3)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// CONNECTIVITY_ARCHITECTURE.md §5.2, WAVE8_ROADMAP.md §2 A-2/A-3
//
// IMPORTANT: All route handlers run on the lwIP task thread.
// Do NOT call Logger::*, NVSManager::*, or MeasurementStore::save() from
// inside any handler lambda — those methods are [PRE-10] MainLoop-only.
// RingBufferTransport::getEntries() and FingerprintCache::query() are safe
// (both are read-only after init(), with internal mutexes where needed).
//
// Memory pattern for POST body:
//   _tempObject   — malloc'd buffer accumulated per body fragment;
//   AsyncWebServerRequest destructor calls free(_tempObject) automatically.

#include "HttpServer.h"
#include <Arduino.h>
#include <Update.h>          // Wave 8 A-4 — ESP32 OTA flash writer
#include <ArduinoJson.h>
#include "LogEntry.h"    // LogEntry::levelToString()
#include "LogLevel.h"    // LogLevel enum

void HttpServer::begin(AsyncWebServer&       srv,
                       IStorageManager&     storage,
                       NVSManager&          nvs,
                       WiFiManager&         wifi,
                       LittleFSManager&     lfs,
                       RingBufferTransport& ring,
                       MeasurementStore&    meas,
                       FingerprintCache&    fpCache)
{
    server_  = &srv;
    storage_ = &storage;
    nvs_     = &nvs;
    wifi_    = &wifi;
    lfs_     = &lfs;
    ring_    = &ring;
    meas_    = &meas;
    fp_      = &fpCache;

    registerApiRoutes();
    registerStaticRoutes();

    server_->onNotFound([this](AsyncWebServerRequest* req) {
        handle404(req);
    });

    server_->begin();
}

// ── Helpers ──────────────────────────────────────────────────────────────────

void HttpServer::addCors(AsyncWebServerResponse* resp) {
    resp->addHeader("Access-Control-Allow-Origin",  "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    // ESP32-S3FN8 has no PSRAM — force connection close to free AsyncTCP
    // buffers immediately after each request instead of keeping them alive.
    resp->addHeader("Connection", "close");
}

void HttpServer::sendError(AsyncWebServerRequest* req, int code, const char* error) {
    // Fixed-size stack buffer — no heap allocation for error responses.
    char buf[80];
    snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", error);
    AsyncWebServerResponse* resp = req->beginResponse(code, "application/json", buf);
    addCors(resp);
    req->send(resp);
}

// ── Route registration ────────────────────────────────────────────────────────

void HttpServer::registerApiRoutes() {
    // ── CORS preflight (OPTIONS) ─────────────────────────────────────────────
    // Handles preflight for all /api/v1/ routes (required for POST from browser).
    server_->on("^/api/v1/.*$", HTTP_OPTIONS,
        [](AsyncWebServerRequest* req) {
            AsyncWebServerResponse* resp = req->beginResponse(204);
            resp->addHeader("Access-Control-Allow-Origin",  "*");
            resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
            req->send(resp);
        }
    );

    // ── GET /api/v1/status ───────────────────────────────────────────────────
    // Returns device health: heap, uptime, WiFi mode/IP, BLE state.
    // CONNECTIVITY_ARCHITECTURE.md §5.2
    server_->on("/api/v1/status", HTTP_GET,
        [this](AsyncWebServerRequest* req) {
            JsonDocument doc;
            doc["version"]       = COINTRACE_VERSION;
            doc["heap"]          = (uint32_t)ESP.getFreeHeap();
            doc["heap_min"]      = (uint32_t)ESP.getMinFreeHeap();
            // Largest contiguous allocatable block — detects heap fragmentation.
            // If heap=34000 but heap_max_block=8000 → fragmented; large allocs will fail.
            doc["heap_max_block"] = (uint32_t)ESP.getMaxAllocHeap();
            doc["uptime"]        = (uint32_t)(millis() / 1000UL);
            doc["wifi"]          = wifi_->isAP() ? "ap" : (wifi_->isSTA() ? "sta" : "off");
            doc["ip"]            = wifi_->getIP();
            doc["ble"]           = "off";  // BLE not implemented yet (Wave 8 future)

            String body;
            serializeJson(doc, body);
            AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", body);
            addCors(resp);
            req->send(resp);
        }
    );

    // ── GET /api/v1/sensor/state ─────────────────────────────────────────────
    // Stub until C-2 integrates LDC1101 state machine via PluginContext.
    server_->on("/api/v1/sensor/state", HTTP_GET,
        [this](AsyncWebServerRequest* req) {
            AsyncWebServerResponse* resp = req->beginResponse(
                200, "application/json", "{\"state\":\"IDLE_NO_COIN\"}"
            );
            addCors(resp);
            req->send(resp);
        }
    );

    // ── GET /api/v1/database ─────────────────────────────────────────────────
    // Reports FingerprintCache load status and entry count.
    server_->on("/api/v1/database", HTTP_GET,
        [this](AsyncWebServerRequest* req) {
            JsonDocument doc;
            doc["count"] = fp_->entryCount();
            doc["ready"] = fp_->isReady();

            String body;
            serializeJson(doc, body);
            AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", body);
            addCors(resp);
            req->send(resp);
        }
    );

    // ── GET /api/v1/ota/status ───────────────────────────────────────────────
    // Reports OTA window state (open/closed + countdown) and NVS pending flag.
    server_->on("/api/v1/ota/status", HTTP_GET,
        [this](AsyncWebServerRequest* req) {
            const bool windowOpen = (otaWindowFlag_ && *otaWindowFlag_);
            uint32_t secsLeft = 0;
            if (windowOpen && otaWindowOpenMs_) {
                const uint32_t elapsed = millis() - *otaWindowOpenMs_;
                secsLeft = (elapsed < 30000UL) ? (30000UL - elapsed) / 1000UL : 0;
            }

            NVSManager::OtaMeta meta;
            const bool hasMeta = nvs_->loadOtaMeta(meta);

            JsonDocument doc;
            doc["ota_window"]  = windowOpen;
            doc["seconds_left"] = secsLeft;
            doc["pending"]     = hasMeta && meta.pending;
            doc["confirmed"]   = hasMeta && meta.confirmed;
            if (hasMeta && meta.pre_version[0] != '\0') {
                doc["pre_version"] = meta.pre_version;
            }

            String body;
            serializeJson(doc, body);
            AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", body);
            addCors(resp);
            req->send(resp);
        }
    );

    // ── POST /api/v1/ota/update ────────────────────────────────────────────────
    // Accepts raw firmware binary (Content-Type: application/octet-stream).
    // ADR-007: requires physical OTA window active ('O' key, 30-second window).
    // Memory-safe: each body chunk (~1–4 KB) is written directly to flash and
    // discarded — the full ~1.4 MB binary NEVER lives in heap simultaneously.
    //
    // On success: NVS pending=true, confirmed=false → esp_restart().
    // On error:   503 with Update.errorString() detail, flash state reset.
    //
    // [ThreadSafety] nvs_->saveOtaMeta(): write-only NVS calls — safe from lwIP
    // task (no get+put race; "ota" namespace is written only from this handler).
    server_->on("/api/v1/ota/update", HTTP_POST,
        // ─ request handler — all body chunks already written to flash ───────────
        [this](AsyncWebServerRequest* req) {
            if (!otaWindowFlag_ || !(*otaWindowFlag_)) {
                sendError(req, 403, "ota_window_not_active");
                return;
            }
            // Guard: body chunk handler must have completed Update.end(true) successfully.
            // Without this check, a 0-byte body (no Content-Length / empty stream)
            // would skip Update.begin() entirely, yet Update.hasError() returns false,
            // causing a spurious reboot with no firmware written to flash.
            if (!otaFlashComplete_) {
                sendError(req, 400, "no_firmware_received");
                Update.abort();
                return;
            }
            if (Update.hasError()) {
                // Flash write failed during body chunks — report error and clean up.
                char buf[96];
                snprintf(buf, sizeof(buf),
                         "{\"error\":\"flash_write_failed\",\"detail\":\"%s\"}",
                         Update.errorString());
                AsyncWebServerResponse* resp = req->beginResponse(
                    500, "application/json", buf);
                addCors(resp);
                req->send(resp);
                Update.abort();
                otaFlashComplete_ = false;
                return;
            }
            // Flash complete — save OTA metadata before reboot.
            nvs_->saveOtaMeta(COINTRACE_VERSION);
            // Close the OTA window immediately to block concurrent requests.
            if (otaWindowFlag_) *otaWindowFlag_ = false;
            otaFlashComplete_ = false;  // reset for next attempt

            AsyncWebServerResponse* resp = req->beginResponse(
                200, "application/json",
                "{\"status\":\"ok\",\"action\":\"rebooting\"}");
            addCors(resp);
            req->send(resp);
            // 1500 ms: gives AsyncTCP time to flush response to client TCP buffer
            // before esp_restart() tears down all connections.
            // urllib with 90s timeout will receive the 200 before the connection drops.
            delay(1500);
            ESP.restart();
        },
        nullptr,   // upload handler — not used (raw octet-stream, not multipart)
        // ─ body chunk handler — streams directly to flash ──────────────────
        [this](AsyncWebServerRequest* req,
               uint8_t* data, size_t len,
               size_t index, size_t total) {
            if (!otaWindowFlag_ || !(*otaWindowFlag_)) {
                // Window closed or never opened — abort silently.
                Update.abort();
                return;
            }
            if (total == 0) {
                // No Content-Length — cannot size the OTA partition write.
                Update.abort();
                return;
            }
            if (index == 0) {
                // First chunk: reset the completion flag and open flash partition.
                otaFlashComplete_ = false;
                // U_FLASH: write to OTA app slot (app1 when running app0, vice versa).
                if (!Update.begin(total, U_FLASH)) {
                    log_e("OTA Update.begin failed: %s", Update.errorString());
                }
            }
            if (!Update.isRunning()) return;   // begin() failed — no-op remaining chunks
            Update.write(data, len);
            if (index + len >= total) {
                // Last chunk: finalise and verify CRC.
                if (Update.end(true)) {
                    otaFlashComplete_ = true;  // mark success — request handler may now reboot
                } else {
                    log_e("OTA Update.end failed: %s", Update.errorString());
                }
            }
        }
    );

    // ── GET /api/v1/measure/{id} ─────────────────────────────────────────────
    // Loads a ring-buffer slot by measurement ID.
    // [PRE-2] Valid ID range: [max(0, N - NVS_RING_SIZE), N)
    //   where N = measurementCount(). Slot = id % NVS_RING_SIZE.
    // Requires ASYNCWEBSERVER_REGEX=1 build flag (added to platformio.ini).
    //
    // [PRE-10] NOTE: MeasurementStore::load() contract recommends MainLoop-only calls.
    // This handler runs on the lwIP thread. The risk is accepted for Phase 1 because:
    //   a) load() is read-only — no NVS writes, no external mutation
    //   b) save() fires ~1/5 sec only on COIN_REMOVED; race window is <1 ms
    //   c) [ADR-ST-006] "complete" sentinel prevents partial-read data corruption
    // Phase 2 mitigation: route load requests through a MainLoop request queue.
    server_->on("^/api/v1/measure/([0-9]+)$", HTTP_GET,
        [this](AsyncWebServerRequest* req) {
            const uint32_t totalCount = storage_->measurementCount();
            const uint32_t id         = (uint32_t)req->pathArg(0).toInt();

            // [PRE-2]: reject IDs outside the live ring window
            const uint32_t minValid = (totalCount > NVS_RING_SIZE)
                                      ? (totalCount - NVS_RING_SIZE) : 0;
            if (totalCount == 0 || id >= totalCount || id < minValid) {
                sendError(req, 404, "not_found");
                return;
            }

            const uint16_t slot = (uint16_t)(id % NVS_RING_SIZE);
            Measurement m = {};
            if (!meas_->load(slot, m)) {
                sendError(req, 404, "not_found");
                return;
            }

            JsonDocument doc;
            doc["id"]         = id;
            doc["slot"]       = slot;
            doc["ts"]         = m.ts;
            doc["pos_count"]  = m.pos_count;
            doc["metal_code"] = m.metal_code;
            doc["coin_name"]  = m.coin_name;
            doc["conf"]       = m.conf;
            JsonArray rp = doc["rp"].to<JsonArray>();
            JsonArray l  = doc["l"].to<JsonArray>();
            const uint8_t nPos = (m.pos_count > 0 && m.pos_count <= 4) ? m.pos_count : 1;
            for (uint8_t i = 0; i < nPos; ++i) {
                rp.add(m.rp[i]);
                l.add(m.l[i]);
            }

            String body;
            serializeJson(doc, body);
            AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", body);
            addCors(resp);
            req->send(resp);
        }
    );

    // ── GET /api/v1/log ──────────────────────────────────────────────────────
    // Query params: n (max entries, default 50), level (DEBUG/INFO/WARNING/ERROR/FATAL),
    //               since_ms (filter entries older than this timestamp, default 0).
    server_->on("/api/v1/log", HTTP_GET,
        [this](AsyncWebServerRequest* req) {
            uint16_t maxN   = 50;
            LogLevel minLvl = LogLevel::DEBUG;
            uint32_t sinceMs = 0;

            if (req->hasParam("n")) {
                const int v = req->getParam("n")->value().toInt();
                if (v > 0 && v <= 200) maxN = (uint16_t)v;
            }
            if (req->hasParam("level")) {
                const String lvl = req->getParam("level")->value();
                if      (lvl.equalsIgnoreCase("INFO"))    minLvl = LogLevel::INFO;
                else if (lvl.equalsIgnoreCase("WARNING") ||
                         lvl.equalsIgnoreCase("WARN"))    minLvl = LogLevel::WARNING;
                else if (lvl.equalsIgnoreCase("ERROR"))   minLvl = LogLevel::ERROR;
                else if (lvl.equalsIgnoreCase("FATAL"))   minLvl = LogLevel::FATAL;
            }
            if (req->hasParam("since_ms")) {
                sinceMs = (uint32_t)req->getParam("since_ms")->value().toInt();
            }

            // Heap allocation — avoid blowing the lwIP task stack (typically 8 KB).
            LogEntry* buf = new LogEntry[maxN];
            if (!buf) {
                sendError(req, 503, "out_of_memory");
                return;
            }
            const uint16_t fetched = ring_->getEntries(buf, maxN, minLvl);

            JsonDocument doc;
            JsonArray arr = doc["entries"].to<JsonArray>();
            uint32_t lastMs = 0;
            for (uint16_t i = 0; i < fetched; ++i) {
                if (buf[i].timestampMs < sinceMs) continue;
                JsonObject e = arr.add<JsonObject>();
                e["ms"]    = buf[i].timestampMs;  // §5.2: "ms" (not "ts")
                e["level"] = LogEntry::levelToString(buf[i].level);
                e["comp"]  = buf[i].component;    // §5.2: "comp" (not "component")
                e["msg"]   = buf[i].message;      // §5.2: "msg"  (not "message")
                lastMs = buf[i].timestampMs;
            }
            doc["next_ms"] = lastMs;  // §5.2: pagination cursor for next since_ms
            delete[] buf;

            String body;
            serializeJson(doc, body);
            AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", body);
            addCors(resp);
            req->send(resp);
        }
    );

    // ── POST /api/v1/database/match ──────────────────────────────────────────
    // Identifies a coin from a 5-component measurement vector.
    // Request body (JSON):
    //   { "algo_ver": 1,
    //     "protocol_id": "p1_UNKNOWN_013mm",
    //     "vector": { "dRp1": 312.4, "k1": 0.742, "k2": 0.531,
    //                 "slope_rp_per_mm_lr": -0.128, "dL1": 0.18 } }
    // Response: { "match": "Ag925", "conf": 0.94, "coin_name": "...",
    //             "alternatives": [...] }
    //         | { "match": null, "alternatives": [] }  — no match for protocol_id
    //
    // Body is accumulated in a small heap buffer via _tempObject.
    // AsyncWebServerRequest::~AsyncWebServerRequest() frees _tempObject automatically.
    constexpr size_t kMaxMatchBodyB = 512;
    server_->on("/api/v1/database/match", HTTP_POST,
        // ─ request handler (runs after all body chunks are received) ─────────
        [this](AsyncWebServerRequest* req) {
            if (!req->_tempObject) {
                sendError(req, 400, "missing_body");
                return;
            }
            const char* bodyStr = static_cast<const char*>(req->_tempObject);

            JsonDocument parsed;
            if (deserializeJson(parsed, bodyStr) != DeserializationError::Ok) {
                sendError(req, 400, "invalid_json");
                return;
            }

            if (!parsed["algo_ver"].is<int>()) {
                sendError(req, 400, "missing_algo_ver");
                return;
            }
            if (parsed["algo_ver"].as<int>() != 1) {
                sendError(req, 422, "unsupported_algo_ver");
                return;
            }
            if (!parsed["vector"].is<JsonObject>()) {
                sendError(req, 400, "missing_vector");
                return;
            }
            JsonObject vec = parsed["vector"].as<JsonObject>();
            if (!vec["dRp1"].is<float>() || !vec["k1"].is<float>()  ||
                !vec["k2"].is<float>()   || !vec["slope_rp_per_mm_lr"].is<float>() ||
                !vec["dL1"].is<float>()) {
                sendError(req, 400, "missing_vector_fields");
                return;
            }

            // Normalize per IStorageManager::queryFingerprint() contract:
            //   dRp1_n = measured_dRp1 / 800.0  (dRp1_MAX = 800 Ohm)
            //   dL1_n  = measured_dL1  / 2000.0 (dL1_MAX  = 2000 µH)
            const float dRp1_n = vec["dRp1"].as<float>() / 800.0f;
            const float dL1_n  = vec["dL1"].as<float>()  / 2000.0f;
            const float k1     = vec["k1"].as<float>();
            const float k2     = vec["k2"].as<float>();
            const float slope  = vec["slope_rp_per_mm_lr"].as<float>();

            IStorageManager::FPMatch matches[5];
            const uint8_t n = storage_->queryFingerprint(
                dRp1_n, k1, k2, slope, dL1_n, matches, 5
            );

            JsonDocument respDoc;
            if (n == 0) {
                respDoc["match"] = nullptr;
                respDoc["conf"]  = 0.0f;
                respDoc["alternatives"].to<JsonArray>();
            } else {
                respDoc["match"]      = matches[0].id;
                respDoc["conf"]       = matches[0].confidence;
                respDoc["coin_name"]  = matches[0].coin_name;
                respDoc["metal_code"] = matches[0].metal_code;
                JsonArray alts = respDoc["alternatives"].to<JsonArray>();
                for (uint8_t i = 1; i < n; ++i) {
                    JsonObject a = alts.add<JsonObject>();
                    a["match"]      = matches[i].id;
                    a["conf"]       = matches[i].confidence;
                    a["coin_name"]  = matches[i].coin_name;
                    a["metal_code"] = matches[i].metal_code;
                }
            }

            String body;
            serializeJson(respDoc, body);
            AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", body);
            addCors(resp);
            req->send(resp);
        },
        nullptr,  // upload handler — not used
        // ─ body chunk handler ─────────────────────────────────────────────────
        [kMaxMatchBodyB](AsyncWebServerRequest* req,
                          uint8_t* data, size_t len,
                          size_t index, size_t /*total*/) {
            if (index == 0) {
                req->_tempObject = malloc(kMaxMatchBodyB);
                if (req->_tempObject) {
                    static_cast<char*>(req->_tempObject)[0] = '\0';
                }
            }
            if (!req->_tempObject) return;
            char*        buf       = static_cast<char*>(req->_tempObject);
            const size_t already   = strlen(buf);
            const size_t remaining = kMaxMatchBodyB - already - 1;
            const size_t toCopy    = (len < remaining) ? len : remaining;
            memcpy(buf + already, data, toCopy);
            buf[already + toCopy] = '\0';
        }
    );

    // ── POST /api/v1/measure/start — stub (sensor not ready until C-2) ───────
    server_->on("/api/v1/measure/start", HTTP_POST,
        [this](AsyncWebServerRequest* req) {
            sendError(req, 503, "sensor_not_ready");
        }
    );

    // ── POST /api/v1/calibrate — stub (sensor not ready until C-2) ──────────
    server_->on("/api/v1/calibrate", HTTP_POST,
        [this](AsyncWebServerRequest* req) {
            sendError(req, 503, "sensor_not_ready");
        }
    );
}

void HttpServer::registerStaticRoutes() {
    // Serve web UI from /web/ on the read-only LittleFS_sys partition.
    // isSysMounted() guards against the case where uploadfs-sys has not been
    // run yet — the REST API remains fully functional without web files.
    if (lfs_->isSysMounted()) {
        server_->serveStatic("/", lfs_->sys(), "/web/")
                .setDefaultFile("index.html");
    }
}

void HttpServer::handle404(AsyncWebServerRequest* req) {
    AsyncWebServerResponse* resp = req->beginResponse(
        404, "application/json", "{\"error\":\"not_found\"}"
    );
    addCors(resp);
    req->send(resp);
}
