// ConfigManager.cpp — Plugin Configuration Implementation
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
//
// Phase 1: in-memory key-value store; all getXxx() return the override value
// if present, otherwise the caller-supplied defaultVal.
//
// Phase 2 (Wave 7): add loadFromLittleFS() to parse data/plugins/<name>.json.
// Plugins do not need to change — the key lookup is transparent.

#include "ConfigManager.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>   // atol, atof, strtoul

ConfigManager::ConfigManager() : count_(0) {
    for (auto& e : entries_) {
        e.used = false;
        e.key[0] = '\0';
        e.value[0] = '\0';
    }
}

// ── Private helpers ───────────────────────────────────────────────────────────

const char* ConfigManager::find(const char* key) const {
    for (uint8_t i = 0; i < count_; ++i) {
        if (entries_[i].used && strncmp(entries_[i].key, key, sizeof(entries_[i].key)) == 0) {
            return entries_[i].value;
        }
    }
    return nullptr;
}

void ConfigManager::store(const char* key, const char* value) {
    // Update existing entry
    for (uint8_t i = 0; i < count_; ++i) {
        if (entries_[i].used && strncmp(entries_[i].key, key, sizeof(entries_[i].key)) == 0) {
            strncpy(entries_[i].value, value, sizeof(entries_[i].value) - 1);
            entries_[i].value[sizeof(entries_[i].value) - 1] = '\0';
            return;
        }
    }
    // Insert new entry
    if (count_ < CONFIG_MAX_ENTRIES) {
        auto& e = entries_[count_++];
        strncpy(e.key,   key,   sizeof(e.key)   - 1); e.key[sizeof(e.key) - 1]     = '\0';
        strncpy(e.value, value, sizeof(e.value) - 1); e.value[sizeof(e.value) - 1] = '\0';
        e.used = true;
    }
    // Silently drop if full — capacity is generous for planned plugin set
}

// ── Read API ──────────────────────────────────────────────────────────────────

int32_t ConfigManager::getInt(const char* key, int32_t defaultVal) const {
    const char* v = find(key);
    return v ? (int32_t)atol(v) : defaultVal;
}

uint8_t ConfigManager::getUInt8(const char* key, uint8_t defaultVal) const {
    const char* v = find(key);
    if (!v) return defaultVal;
    long n = atol(v);
    return (n >= 0 && n <= 255) ? (uint8_t)n : defaultVal;
}

uint32_t ConfigManager::getUInt32(const char* key, uint32_t defaultVal) const {
    const char* v = find(key);
    return v ? (uint32_t)strtoul(v, nullptr, 10) : defaultVal;
}

float ConfigManager::getFloat(const char* key, float defaultVal) const {
    const char* v = find(key);
    return v ? (float)atof(v) : defaultVal;
}

bool ConfigManager::getBool(const char* key, bool defaultVal) const {
    const char* v = find(key);
    if (!v) return defaultVal;
    // Exact-match only: "true" or "1". strncmp(v,"1",1) would wrongly accept "10","1abc" etc.
    return (strcmp(v, "true") == 0 || strcmp(v, "1") == 0);
}

const char* ConfigManager::getString(const char* key, const char* defaultVal) const {
    const char* v = find(key);
    return v ? v : defaultVal;
}

// ── Write API ─────────────────────────────────────────────────────────────────

void ConfigManager::setInt(const char* key, int32_t value) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%ld", (long)value);
    store(key, buf);
}

void ConfigManager::setUInt32(const char* key, uint32_t value) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)value);
    store(key, buf);
}

void ConfigManager::setFloat(const char* key, float value) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.6g", (double)value);
    store(key, buf);
}

void ConfigManager::setBool(const char* key, bool value) {
    store(key, value ? "true" : "false");
}

void ConfigManager::setString(const char* key, const char* value) {
    store(key, value ? value : "");
}
