// PluginSystem.h — Plugin Lifecycle Orchestrator
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// PLUGIN_ARCHITECTURE.md / PLUGIN_CONTRACT.md §1
//
// PluginSystem owns the plugin list and drives the lifecycle:
//   begin()  → canInitialize() + initialize() for each plugin
//   update() → update() for each enabled plugin (called from loop())
//   end()    → shutdown() + delete for each plugin
//
// Designed to run entirely from the main task (Core 0, Arduino loop).
// No FreeRTOS tasks are created here — plugins may create their own.

#pragma once

#include "IPlugin.h"
#include "PluginContext.h"
#include <stdint.h>

/** Maximum number of plugins that can be registered. */
static const uint8_t PLUGIN_SYSTEM_MAX_PLUGINS = 16;

/**
 * @brief Manages plugin registration, lifecycle, and periodic update.
 *
 * Usage:
 * @code
 *   PluginSystem ps;
 *   ps.addPlugin(new LDC1101Plugin());
 *   ps.begin(&ctx);   // init phase
 *
 *   // in loop():
 *   ps.update();
 *
 *   // at shutdown:
 *   ps.end();
 * @endcode
 */
class PluginSystem {
public:
    PluginSystem();
    ~PluginSystem();

    /**
     * @brief Register a plugin.
     *
     * Must be called before begin(). PluginSystem takes ownership and will
     * delete the plugin in end().
     *
     * @return true on success; false if the registry is full.
     */
    bool addPlugin(IPlugin* plugin);

    /**
     * @brief Initialise all registered plugins.
     *
     * For each plugin in registration order:
     *   1. canInitialize()  → false: skip, log warning.
     *   2. initialize(ctx)  → false: plugin disabled, log error.
     *   3. initialize(ctx)  → true:  plugin ready, log info.
     *
     * @param ctx Fully populated PluginContext; must remain valid until end().
     */
    void begin(PluginContext* ctx);

    /**
     * @brief Tick all enabled plugins.
     *
     * Call from Arduino loop() at the desired update rate.
     * Skips disabled and unready plugins.
     */
    void update();

    /**
     * @brief Shut down all plugins and free memory.
     *
     * Calls shutdown() on every plugin (enabled or not) then deletes them.
     * Safe to call even if begin() was never called.
     */
    void end();

    // ── Status queries ────────────────────────────────────────────────────────

    /** Number of plugins that successfully initialised. */
    uint8_t readyCount()   const;

    /** Total number of registered plugins. */
    uint8_t pluginCount()  const;

private:
    IPlugin*      plugins_[PLUGIN_SYSTEM_MAX_PLUGINS];
    bool          initialised_[PLUGIN_SYSTEM_MAX_PLUGINS]; // true = initialize() returned true
    uint8_t       count_;
    PluginContext* ctx_;

    // Non-copyable
    PluginSystem(const PluginSystem&) = delete;
    PluginSystem& operator=(const PluginSystem&) = delete;
};
