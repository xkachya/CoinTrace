// IPlugin.h — Base Plugin Interface
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// PLUGIN_CONTRACT.md §1 / PLUGIN_ARCHITECTURE.md
//
// Lifecycle:  canInitialize() → initialize() → update()×N → shutdown()
// Threading:  initialize/update/shutdown called from single task (Core 0, main loop).
//             read() may be called from any task — implementors must guard cached data.

#pragma once

#include <stdint.h>

// Forward declarations — full types in PluginContext.h
struct PluginContext;

/**
 * @brief Base interface for all CoinTrace plugins.
 *
 * Every hardware component (sensor, display, input, storage) is a plugin.
 * Plugins are owned by PluginSystem and must not call Wire.begin() or SPI.begin()
 * themselves — those are guaranteed to be initialised before initialize() is called.
 */
class IPlugin {
public:
    virtual ~IPlugin() = default;

    // ── Metadata ──────────────────────────────────────────────────────────────

    /** Human-readable plugin name used in log messages and diagnostics. */
    virtual const char* getName()    const = 0;

    /** SemVer string, e.g. "1.0.0". */
    virtual const char* getVersion() const = 0;

    /** Author / team string. */
    virtual const char* getAuthor()  const = 0;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /**
     * @brief Pre-flight check — called BEFORE initialize().
     *
     * Must NOT touch Wire / SPI / ctx — context is not yet available here.
     * Use for compile-time / config-only checks (e.g. "is the config key present?").
     *
     * @return true if the plugin is able to proceed to initialize().
     */
    virtual bool canInitialize() = 0;

    /**
     * @brief Hardware initialisation.
     *
     * Called only when canInitialize() returned true.
     * Wire and SPI are already initialised; ctx->log and ctx->config are valid.
     *
     * @param context Pointer to the shared PluginContext. Guaranteed non-null.
     * @return true on success; false disables the plugin (update/shutdown still called).
     */
    virtual bool initialize(PluginContext* context) = 0;

    /**
     * @brief Periodic tick, called at ≥10 Hz from the main loop.
     *
     * CONTRACT: Must return within 10 ms. Blocking operations violate the contract
     * and degrade the entire plugin loop.
     */
    virtual void update() = 0;

    /**
     * @brief Cleanup before destruction.
     *
     * Called even if initialize() returned false or was never called.
     * Must release any FreeRTOS handles, queues, or tasks created by this plugin.
     */
    virtual void shutdown() = 0;

    // ── Status ────────────────────────────────────────────────────────────────

    /** True after a successful initialize() call. */
    virtual bool isReady()   const = 0;

    /**
     * @brief True when PluginSystem should call update().
     *
     * A plugin may disable itself (e.g. after repeated hardware faults) by returning
     * false here — PluginSystem will skip update() but will still call shutdown().
     */
    virtual bool isEnabled() const = 0;
};
