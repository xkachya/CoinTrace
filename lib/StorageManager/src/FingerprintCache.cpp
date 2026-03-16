// FingerprintCache.cpp — Fingerprint Index Cache (Wave 7 P-4)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// STORAGE_ARCHITECTURE.md §17.2 boot [7], FINGERPRINT_DB_ARCHITECTURE.md §6.3

#include "FingerprintCache.h"

#include <ArduinoJson.h>
#include <Arduino.h>         // log_i / log_e / log_w / strlcpy
#include <esp32s3/rom/crc.h>  // crc32_le() — ESP32-S3 ROM CRC32
#include <esp_task_wdt.h>    // esp_task_wdt_reset() [BOOT-2]
#include <math.h>            // sqrtf, expf

// ── init() ───────────────────────────────────────────────────────────────────

bool FingerprintCache::init(LittleFSManager& lfs, SDCardManager* sdCard,
                            SemaphoreHandle_t spiMutex) {
    ready_ = false;
    count_ = 0;

    const bool sdAvail  = (sdCard != nullptr && sdCard->isAvailable());
    const bool lfsReady = lfs.isDataMounted();

    if (!lfsReady) {
        log_e("FPCache: LittleFS data not mounted — cache unavailable");
        return false;
    }

    bool cacheExists = false;
    {
        LittleFSDataGuard g(lfs.lfsDataMutex());
        if (g.ok()) {
            cacheExists = lfs.data().exists(LFS_CACHE_INDEX);
        }
    }

    // ── Scenario a/b: SD + cache exists ───────────────────────────────────
    if (sdAvail && cacheExists) {
        log_i("FPCache: SD + cache found — validating CRC32 + generation");

        bool crcOk  = false;
        bool genOk  = false;

        // CRC32 check
        {
            LittleFSDataGuard g(lfs.lfsDataMutex());
            if (g.ok()) {
                uint32_t stored = 0, computed = 0;
                if (readU32LE(lfs.data(), LFS_CACHE_CRC32, stored)) {
                    computed = computeLFSCrc32(lfs.data(), LFS_CACHE_INDEX);
                    crcOk = (computed == stored);
                }
                if (!crcOk) {
                    log_w("FPCache: CRC32 mismatch — cache invalidated");
                    invalidateCache(lfs.data());
                }
            }
        }

        // Generation check (only if CRC passed)
        if (crcOk) {
            uint32_t cachedGen = 0, sdGen = 0;
            {
                LittleFSDataGuard g(lfs.lfsDataMutex());
                if (g.ok()) {
                    readU32LE(lfs.data(), LFS_CACHE_GENERATION, cachedGen);
                }
            }
            if (readSDGeneration(*sdCard, spiMutex, sdGen)) {
                genOk = (cachedGen == sdGen);
                if (!genOk) {
                    log_w("FPCache: generation mismatch (cache=%u SD=%u) — rebuild",
                          (unsigned)cachedGen, (unsigned)sdGen);
                    LittleFSDataGuard g(lfs.lfsDataMutex());
                    if (g.ok()) invalidateCache(lfs.data());
                }
            } else {
                // Can't read SD generation → treat as mismatch, rebuild
                log_w("FPCache: cannot read SD generation — rebuild");
                genOk = false;
                LittleFSDataGuard g(lfs.lfsDataMutex());
                if (g.ok()) invalidateCache(lfs.data());
            }
        }

        if (crcOk && genOk) {
            // Scenario a: fast path — load from LittleFS cache
            log_i("FPCache: CRC32 OK + generation match — loading from cache");
            LittleFSDataGuard g(lfs.lfsDataMutex());
            if (g.ok() && loadFromLFS(lfs.data())) {
                log_i("FPCache: loaded %u entries from LittleFS cache", (unsigned)count_);
                ready_ = true;
                return true;
            }
            // loadFromLFS failed unexpectedly — fall through to rebuild
            log_w("FPCache: loadFromLFS failed — rebuilding from SD");
        }

        // Scenario b: rebuild
        return buildFromSD(lfs, *sdCard, spiMutex);
    }

    // ── Scenario c: SD + no cache ─────────────────────────────────────────
    if (sdAvail && !cacheExists) {
        log_i("FPCache: no cache — building from SD");
        return buildFromSD(lfs, *sdCard, spiMutex);
    }

    // ── Scenario d: no SD + cache exists ─────────────────────────────────
    if (!sdAvail && cacheExists) {
        log_i("FPCache: SD absent — loading existing cache (offline mode)");
        LittleFSDataGuard g(lfs.lfsDataMutex());
        if (g.ok() && loadFromLFS(lfs.data())) {
            log_i("FPCache: %u entries loaded (offline mode)", (unsigned)count_);
            ready_ = true;
            return true;
        }
        log_e("FPCache: offline cache load failed");
        return false;
    }

    // ── Scenario e: no SD + no cache ─────────────────────────────────────
    log_w("FPCache: no SD and no cache — matching unavailable");
    return false;
}

// ── query() ──────────────────────────────────────────────────────────────────
//
// Weighted Euclidean distance, equal weights w=1.0 for v1.
// FINGERPRINT_DB_ARCHITECTURE.md §3.3: adjust weights after validation set.
//
// Input MUST be pre-normalized by caller:
//   dRp1_n = measured_dRp1 / 800.0f
//   dL1_n  = measured_dL1  / 2000.0f

uint8_t FingerprintCache::query(float dRp1_n, float k1, float k2, float slope, float dL1_n,
                                QueryResult* results, uint8_t maxResults) const {
    if (!ready_ || count_ == 0 || results == nullptr || maxResults == 0) return 0;

    const uint8_t topN = (maxResults < QUERY_TOP_N) ? maxResults : QUERY_TOP_N;

    // Initialise results with sentinel values
    for (uint8_t i = 0; i < topN; ++i) {
        results[i].entry      = nullptr;
        results[i].distance   = 1e9f;
        results[i].confidence = 0.0f;
    }

    uint8_t found = 0;

    for (uint16_t i = 0; i < count_; ++i) {
        const CacheEntry& e = entries_[i];

        const float d0 = dRp1_n - e.dRp1_n;
        const float d1 = k1     - e.k1;
        const float d2 = k2     - e.k2;
        const float d3 = slope  - e.slope;
        const float d4 = dL1_n  - e.dL1_n;
        const float dist = sqrtf(d0*d0 + d1*d1 + d2*d2 + d3*d3 + d4*d4);

        // Insert into sorted results (ascending distance)
        const uint8_t slots = (topN < count_) ? topN : (uint8_t)count_;
        for (uint8_t j = 0; j < slots; ++j) {
            if (dist < results[j].distance) {
                // Shift tail down
                for (uint8_t k = slots - 1; k > j; --k) {
                    results[k] = results[k-1];
                }
                const float sigma2 = CONFIDENCE_SIGMA * CONFIDENCE_SIGMA;
                results[j].entry      = &e;
                results[j].distance   = dist;
                results[j].confidence = expf(-(dist * dist) / sigma2);
                if (found < slots) ++found;
                break;
            }
        }
    }

    return found;
}

// ── CRC32 helpers ─────────────────────────────────────────────────────────────

uint32_t FingerprintCache::computeLFSCrc32(fs::LittleFSFS& fs, const char* path) {
    File f = fs.open(path, "r");
    if (!f || f.isDirectory()) return 0;

    uint32_t crc = 0;
    uint8_t  buf[256];
    while (f.available()) {
        const size_t n = f.read(buf, sizeof(buf));
        if (n == 0) break;
        crc = crc32_le(crc, buf, (uint32_t)n);
    }
    f.close();
    return crc;
}

bool FingerprintCache::readU32LE(fs::LittleFSFS& fs, const char* path, uint32_t& out) {
    File f = fs.open(path, "r");
    if (!f || f.isDirectory()) return false;
    uint8_t buf[4] = {};
    const bool ok = (f.read(buf, 4) == 4);
    f.close();
    if (!ok) return false;
    out = (uint32_t)buf[0]
        | ((uint32_t)buf[1] << 8)
        | ((uint32_t)buf[2] << 16)
        | ((uint32_t)buf[3] << 24);
    return true;
}

bool FingerprintCache::writeU32LE(fs::LittleFSFS& fs, const char* path, uint32_t value) {
    File f = fs.open(path, "w");
    if (!f || f.isDirectory()) return false;
    const uint8_t buf[4] = {
        (uint8_t)(value),
        (uint8_t)(value >> 8),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 24)
    };
    const bool ok = (f.write(buf, 4) == 4);
    f.close();
    return ok;
}

// ── SD helpers ────────────────────────────────────────────────────────────────

bool FingerprintCache::readSDGeneration(SDCardManager& sd, SemaphoreHandle_t spiMutex,
                                        uint32_t& out) {
    // [SPI-3] timeout = 50 ms — portMAX_DELAY FORBIDDEN.
    // [SD-A-03] No Logger::* here.
    if (xSemaphoreTake(spiMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        log_w("FPCache: spiMutex timeout reading SD generation");
        return false;
    }

    File f = SD.open(SD_INDEX_PATH, "r");
    if (!f || f.isDirectory()) {
        xSemaphoreGive(spiMutex);
        log_w("FPCache: SD index not found at %s", SD_INDEX_PATH);
        return false;
    }

    // Limit header read to first 256 bytes — "generation" always appears early.
    char hdr[256];
    const size_t n = f.read((uint8_t*)hdr, sizeof(hdr) - 1);
    hdr[n] = '\0';
    f.close();
    xSemaphoreGive(spiMutex);

    // Parse only what we need — tiny doc
    JsonDocument doc;
    if (deserializeJson(doc, hdr, n) != DeserializationError::Ok) {
        log_w("FPCache: failed to parse SD index header");
        return false;
    }
    // backward-compatible: absent = 0 (FINGERPRINT_DB_ARCHITECTURE.md §6.3)
    out = doc["generation"] | 0u;
    return true;
}

// ── buildFromSD() ─────────────────────────────────────────────────────────────

bool FingerprintCache::buildFromSD(LittleFSManager& lfs, SDCardManager& sd,
                                   SemaphoreHandle_t spiMutex) {
    log_i("FPCache: buildFromSD — reading %s", SD_INDEX_PATH);

    // ── Phase 1: read full index.json from SD under spiMutex ──────────────
    // [SPI-3] timeout 50 ms. [SD-A-03] no Logger here.
    if (xSemaphoreTake(spiMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        log_e("FPCache: buildFromSD — spiMutex timeout");
        return false;
    }

    File sdFile = SD.open(SD_INDEX_PATH, "r");
    if (!sdFile || sdFile.isDirectory()) {
        xSemaphoreGive(spiMutex);
        log_e("FPCache: SD index missing: %s", SD_INDEX_PATH);
        return false;
    }
    const size_t fileSize = (size_t)sdFile.size();
    log_i("FPCache: index.json size = %u bytes", (unsigned)fileSize);

    // Stream-parse directly from SD file while holding spiMutex.
    // ArduinoJson v7 stream API avoids loading the full file into a RAM buffer.
    // Use a filter to capture only the fields we need — keeps the doc small
    // regardless of the number of entries.
    JsonDocument filter;
    filter["generation"] = true;
    filter["entries"][0]["id"]           = true;
    filter["entries"][0]["metal_code"]   = true;
    filter["entries"][0]["coin_name"]    = true;
    filter["entries"][0]["protocol_id"]  = true;
    filter["entries"][0]["centroid"]     = true;
    filter["entries"][0]["radius_95pct"] = true;
    filter["entries"][0]["records_count"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, sdFile,
                                               DeserializationOption::Filter(filter));
    sdFile.close();
    xSemaphoreGive(spiMutex);

    if (err) {
        log_e("FPCache: JSON parse error: %s", err.c_str());
        return false;
    }

    // ── Phase 2: populate entries_ from parsed doc ─────────────────────────
    count_ = 0;
    const uint32_t generation = doc["generation"] | 0u;
    JsonArray arr = doc["entries"].as<JsonArray>();

    for (JsonObject entry : arr) {
        if (count_ >= MAX_ENTRIES) {
            log_w("FPCache: MAX_ENTRIES=%u reached — truncating", (unsigned)MAX_ENTRIES);
            break;
        }

        CacheEntry& e = entries_[count_];

        strlcpy(e.id,          entry["id"]           | "", sizeof(e.id));
        strlcpy(e.metal_code,  entry["metal_code"]   | "", sizeof(e.metal_code));
        strlcpy(e.coin_name,   entry["coin_name"]    | "", sizeof(e.coin_name));
        strlcpy(e.protocol_id, entry["protocol_id"]  | "", sizeof(e.protocol_id));

        JsonObject c = entry["centroid"];
        e.dRp1_n        = c["dRp1_n"]  | 0.0f;
        e.k1            = c["k1"]      | 0.0f;
        e.k2            = c["k2"]      | 0.0f;
        e.slope         = c["slope"]   | 0.0f;
        e.dL1_n         = c["dL1_n"]   | 0.0f;
        e.radius_95pct  = entry["radius_95pct"]  | 0.0f;
        e.records_count = entry["records_count"] | 0;

        ++count_;

        // [BOOT-2] Reset watchdog on each entry — prevents TWDT reset on large SD DBs.
        esp_task_wdt_reset();
    }

    log_i("FPCache: parsed %u entries (generation=%u)", (unsigned)count_,
          (unsigned)generation);

    if (count_ == 0) {
        log_w("FPCache: index.json has no entries");
        return false;
    }

    // ── Phase 3: persist cache to LittleFS ────────────────────────────────
    {
        LittleFSDataGuard g(lfs.lfsDataMutex());
        if (!g.ok()) {
            log_e("FPCache: lfsDataMutex timeout — cache not saved");
            // Entries are loaded in RAM; mark ready so this boot works.
            ready_ = true;
            return true;
        }
        if (!saveToLFS(lfs.data(), generation)) {
            log_w("FPCache: saveToLFS failed — cache not persisted (will rebuild next boot)");
        }
    }

    ready_ = true;
    return true;
}

// ── loadFromLFS() ────────────────────────────────────────────────────────────

bool FingerprintCache::loadFromLFS(fs::LittleFSFS& fs) {
    File f = fs.open(LFS_CACHE_INDEX, "r");
    if (!f || f.isDirectory()) {
        log_e("FPCache: loadFromLFS — file not found");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        log_e("FPCache: loadFromLFS parse error: %s", err.c_str());
        return false;
    }

    count_ = 0;
    JsonArray arr = doc["entries"].as<JsonArray>();

    for (JsonObject entry : arr) {
        if (count_ >= MAX_ENTRIES) break;

        CacheEntry& e = entries_[count_];

        strlcpy(e.id,          entry["id"]           | "", sizeof(e.id));
        strlcpy(e.metal_code,  entry["metal_code"]   | "", sizeof(e.metal_code));
        strlcpy(e.coin_name,   entry["coin_name"]    | "", sizeof(e.coin_name));
        strlcpy(e.protocol_id, entry["protocol_id"]  | "", sizeof(e.protocol_id));

        JsonObject c = entry["centroid"];
        e.dRp1_n        = c["dRp1_n"]  | 0.0f;
        e.k1            = c["k1"]      | 0.0f;
        e.k2            = c["k2"]      | 0.0f;
        e.slope         = c["slope"]   | 0.0f;
        e.dL1_n         = c["dL1_n"]   | 0.0f;
        e.radius_95pct  = entry["radius_95pct"]  | 0.0f;
        e.records_count = entry["records_count"] | 0;

        ++count_;
    }

    return (count_ > 0);
}

// ── saveToLFS() ──────────────────────────────────────────────────────────────

bool FingerprintCache::saveToLFS(fs::LittleFSFS& fs, uint32_t generation) {
    // Serialize entries_ → JSON → /cache/index.json
    File f = fs.open(LFS_CACHE_INDEX, "w");
    if (!f || f.isDirectory()) {
        log_e("FPCache: saveToLFS — cannot open %s for write", LFS_CACHE_INDEX);
        return false;
    }

    // Stream-serialize to avoid a large in-RAM string buffer.
    f.print("{\"entries\":[");
    for (uint16_t i = 0; i < count_; ++i) {
        const CacheEntry& e = entries_[i];
        if (i > 0) f.print(',');

        char buf[384];  // max entry ~294 bytes: id(31)+metal_code(7)+coin_name(47)+protocol_id(23)+JSON+6 floats
        snprintf(buf, sizeof(buf),
            "{\"id\":\"%s\",\"metal_code\":\"%s\",\"coin_name\":\"%s\","
            "\"protocol_id\":\"%s\","
            "\"centroid\":{\"dRp1_n\":%.4f,\"k1\":%.4f,\"k2\":%.4f,"
            "\"slope\":%.4f,\"dL1_n\":%.4f},"
            "\"radius_95pct\":%.4f,\"records_count\":%u}",
            e.id, e.metal_code, e.coin_name, e.protocol_id,
            e.dRp1_n, e.k1, e.k2, e.slope, e.dL1_n,
            e.radius_95pct, (unsigned)e.records_count);
        f.print(buf);
    }
    f.print("]}");
    f.close();

    // Compute and store CRC32 of the file just written.
    const uint32_t crc = computeLFSCrc32(fs, LFS_CACHE_INDEX);
    if (!writeU32LE(fs, LFS_CACHE_CRC32, crc)) {
        log_w("FPCache: failed to write CRC32 file");
        return false;
    }

    // Store generation counter.
    if (!writeU32LE(fs, LFS_CACHE_GENERATION, generation)) {
        log_w("FPCache: failed to write generation file");
        return false;
    }

    log_i("FPCache: cache saved — %u entries, CRC32=0x%08X, generation=%u",
          (unsigned)count_, (unsigned)crc, (unsigned)generation);
    return true;
}

// ── invalidateCache() ────────────────────────────────────────────────────────

void FingerprintCache::invalidateCache(fs::LittleFSFS& fs) {
    fs.remove(LFS_CACHE_INDEX);
    fs.remove(LFS_CACHE_CRC32);
    fs.remove(LFS_CACHE_GENERATION);
}
