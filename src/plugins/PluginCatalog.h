// PluginCatalog.h - aggregated plugin discovery across formats.

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "plugins/IPluginHost.h"

namespace lspa {

// Owns the per-format hosts and caches scan results. Thread safe.
class PluginCatalog {
public:
    static PluginCatalog& instance();

    // Register a host if none exists for its format yet. Takes ownership.
    void addHost(std::unique_ptr<IPluginHost> host);
    IPluginHost* hostFor(PluginFormat format);

    // Cached full scan. refresh=true rescans everything.
    std::vector<PluginDescriptor> scan(bool refresh = false);

    const PluginDescriptor* find(const PluginKey& key) const;

    // Convenience: scan result for one format.
    std::vector<PluginDescriptor> scanFormat(PluginFormat format, bool refresh = false);

    // Instantiate through the catalog (non real-time).
    std::unique_ptr<IPluginInstance> instantiate(const PluginKey& key, double sampleRate,
                                                 int maxBlock, std::string& errorOut);

private:
    PluginCatalog() = default;
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<IPluginHost>> hosts_;
    std::vector<PluginDescriptor> cache_;
    bool cacheValid_ = false;
};

}  // namespace lspa
