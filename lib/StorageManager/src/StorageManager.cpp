// StorageManager.cpp — Unified Storage Facade (Wave 7 P-5)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// STORAGE_ARCHITECTURE.md §5 (3-tier diagram), §15 P-5

#include "StorageManager.h"

StorageManager::StorageManager(NVSManager& nvs, LittleFSManager& lfs,
                               SDCardManager& sd, FingerprintCache& fp,
                               MeasurementStore& meas)
    : nvs_(nvs), lfs_(lfs), sd_(sd), fp_(fp), meas_(meas) {}

// ── Tier 0: NVS routing ───────────────────────────────────────────────────────

bool StorageManager::nvsSaveFloat(const char* key, float value) {
    return nvs_.isReady() ? nvs_.nvsSaveFloat(key, value) : false;
}

bool StorageManager::nvsSaveUInt32(const char* key, uint32_t value) {
    return nvs_.isReady() ? nvs_.nvsSaveUInt32(key, value) : false;
}

bool StorageManager::nvsLoadFloat(const char* key, float* out, float defaultVal) {
    return nvs_.isReady() ? nvs_.nvsLoadFloat(key, out, defaultVal) : false;
}

bool StorageManager::nvsLoadUInt32(const char* key, uint32_t* out, uint32_t defaultVal) {
    return nvs_.isReady() ? nvs_.nvsLoadUInt32(key, out, defaultVal) : false;
}

bool StorageManager::nvsErase(const char* key) {
    return nvs_.isReady() ? nvs_.nvsErase(key) : false;
}

// ── Tier 1: LittleFS routing ──────────────────────────────────────────────────

size_t StorageManager::littleFsFreeBytes() const {
    return lfs_.isDataMounted() ? lfs_.dataFreeBytes() : 0;
}

bool StorageManager::isLfsMounted() const {
    return lfs_.isDataMounted();
}

// ── Tier 2: SD routing ────────────────────────────────────────────────────────

bool StorageManager::isSdAvailable() const {
    return sd_.isAvailable();
}

// ── Measurement count ─────────────────────────────────────────────────────────

uint32_t StorageManager::measurementCount() const {
    return nvs_.isReady() ? nvs_.getMeasCount() : 0;
}

// ── FingerprintCache query ────────────────────────────────────────────────────

uint8_t StorageManager::queryFingerprint(float    dRp1_n,
                                         float    k1,
                                         float    k2,
                                         float    slope,
                                         float    dL1_n,
                                         FPMatch* out,
                                         uint8_t  maxResults) {
    if (!fp_.isReady() || maxResults == 0 || out == nullptr) return 0;

    // Cap at QUERY_TOP_N to stay within the stack-allocated raw[] buffer.
    const uint8_t cap = (maxResults < FingerprintCache::QUERY_TOP_N)
                        ? maxResults
                        : static_cast<uint8_t>(FingerprintCache::QUERY_TOP_N);

    QueryResult raw[FingerprintCache::QUERY_TOP_N];
    const uint8_t n = fp_.query(dRp1_n, k1, k2, slope, dL1_n, raw, cap);

    for (uint8_t i = 0; i < n; ++i) {
        strlcpy(out[i].id,         raw[i].entry->id,         sizeof(out[i].id));
        strlcpy(out[i].coin_name,  raw[i].entry->coin_name,  sizeof(out[i].coin_name));
        strlcpy(out[i].metal_code, raw[i].entry->metal_code, sizeof(out[i].metal_code));
        out[i].confidence = raw[i].confidence;
    }
    return n;
}
