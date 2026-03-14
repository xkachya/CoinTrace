// ConfigManager.h — Plugin Configuration Interface
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// PLUGIN_CONTRACT.md §1.3б / PLUGIN_ARCHITECTURE.md
//
// Key format: "<plugin_name>.<param>"  e.g. "ldc1101.spi_cs_pin"
//
// Phase 1 (Wave 6): returns compile-time defaults — no filesystem dependency.
// Phase 2 (Wave 7): loadFromLittleFS() parses data/plugins/<name>.json and
//   overrides defaults via the in-memory store.
//
// All getXxx() methods are const and safe to call from any task.

#pragma once

#include <stdint.h>
#include <stddef.h>

// Maximum number of runtime overrides stored in memory.
// Enough for a handful of plugins each with ~10 params.
static const uint8_t CONFIG_MAX_ENTRIES = 64;

/**
 * @brief Flat key-value configuration store for plugins.
 *
 * Plugins access configuration through PluginContext::config.
 * Every getXxx(key, defaultVal) call returns:
 *   1. The value loaded from JSON (after loadFromLittleFS), or
 *   2. The compile-time defaultVal — so the sensor works correctly
 *      even when no filesystem/JSON is present.
 */
class ConfigManager {
public:
    ConfigManager();

    // ── Read API (called by plugins via ctx->config) ──────────────────────────

    int32_t     getInt    (const char* key, int32_t     defaultVal) const;
    uint8_t     getUInt8  (const char* key, uint8_t     defaultVal) const;
    uint32_t    getUInt32 (const char* key, uint32_t    defaultVal) const;
    float       getFloat  (const char* key, float       defaultVal) const;
    bool        getBool   (const char* key, bool        defaultVal) const;
    const char* getString (const char* key, const char* defaultVal) const;

    // ── Write API (used by PluginSystem / ConfigManager tests) ───────────────
    // Allows runtime overrides without touching the filesystem.

    void setInt   (const char* key, int32_t     value);
    void setUInt32(const char* key, uint32_t    value);
    void setFloat (const char* key, float       value);
    void setBool  (const char* key, bool        value);
    void setString(const char* key, const char* value);

    // ── Phase 2: filesystem load (not yet implemented) ────────────────────────
    // bool loadFromLittleFS(const char* pluginName);

private:
    // Simple string-keyed store backed by a fixed-size array (no heap allocation).
    struct Entry {
        char  key[32];
        char  value[32];
        bool  used;
    };

    Entry   entries_[CONFIG_MAX_ENTRIES];
    uint8_t count_;

    // Returns the stored string for key, or nullptr if not found.
    const char* find(const char* key) const;

    // Upserts key=value into the entries array.
    void store(const char* key, const char* value);
};
