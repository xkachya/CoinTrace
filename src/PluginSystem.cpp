// PluginSystem.cpp — Plugin Lifecycle Orchestrator Implementation
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
// PLUGIN_ARCHITECTURE.md §3 / PLUGIN_CONTRACT.md §1
//
// Ownership: addPlugin() transfers ownership; end() calls delete on every plugin.
// Threading: begin/update/end must be called from the same task (Core 0, main loop).

#include "PluginSystem.h"
#include "Logger.h"

PluginSystem::PluginSystem() : count_(0), ctx_(nullptr) {
    for (uint8_t i = 0; i < PLUGIN_SYSTEM_MAX_PLUGINS; ++i) {
        plugins_[i]     = nullptr;
        initialised_[i] = false;
    }
}

PluginSystem::~PluginSystem() {
    end();
}

bool PluginSystem::addPlugin(IPlugin* plugin) {
    if (!plugin) return false;
    if (count_ >= PLUGIN_SYSTEM_MAX_PLUGINS) return false;
    plugins_[count_++] = plugin;
    return true;
}

void PluginSystem::begin(PluginContext* ctx) {
    ctx_ = ctx;
    Logger* log = ctx_ ? ctx_->log : nullptr;

    for (uint8_t i = 0; i < count_; ++i) {
        IPlugin* p = plugins_[i];
        if (!p) continue;

        if (!p->canInitialize()) {
            if (log) log->warning("PluginSystem", "%s: canInitialize() failed — skipping", p->getName());
            continue;
        }

        bool ok = p->initialize(ctx_);
        initialised_[i] = ok;

        if (ok) {
            if (log) log->info("PluginSystem", "%s v%s: initialized OK", p->getName(), p->getVersion());
        } else {
            if (log) log->error("PluginSystem", "%s: initialize() failed — plugin disabled", p->getName());
        }
    }
}

void PluginSystem::update() {
    for (uint8_t i = 0; i < count_; ++i) {
        IPlugin* p = plugins_[i];
        if (!p) continue;
        if (!initialised_[i]) continue;
        if (!p->isEnabled()) continue;
        p->update();
    }
}

void PluginSystem::end() {
    // Shutdown in reverse registration order (LIFO) — mirrors standard dependency teardown.
    // Plugin N may depend on resources from Plugin N-1, so N must shut down first.
    for (int8_t i = static_cast<int8_t>(count_) - 1; i >= 0; --i) {
        if (!plugins_[i]) continue;
        plugins_[i]->shutdown();
        delete plugins_[i];
        plugins_[i]     = nullptr;
        initialised_[i] = false;
    }
    count_ = 0;
}

uint8_t PluginSystem::readyCount() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < count_; ++i) {
        if (initialised_[i]) ++n;
    }
    return n;
}

uint8_t PluginSystem::pluginCount() const {
    return count_;
}
