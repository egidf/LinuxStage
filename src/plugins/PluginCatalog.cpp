#include "plugins/PluginCatalog.h"

#include "core/Logger.h"

namespace lspa {

PluginCatalog& PluginCatalog::instance() {
    static PluginCatalog g;
    return g;
}

void PluginCatalog::addHost(std::unique_ptr<IPluginHost> host) {
    if (!host) return;
    std::lock_guard<std::mutex> lock(mutex_);
    const PluginFormat fmt = host->format();
    for (const auto& h : hosts_) {
        if (h->format() == fmt) return;  // already registered
    }
    LSPA_LOG_INFO("Plugin catalog: registered host '%s'", host->name().c_str());
    hosts_.push_back(std::move(host));
    cacheValid_ = false;
}

IPluginHost* PluginCatalog::hostFor(PluginFormat format) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& h : hosts_) {
        if (h->format() == format) return h.get();
    }
    return nullptr;
}

std::vector<PluginDescriptor> PluginCatalog::scan(bool refresh) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cacheValid_ && !refresh) return cache_;
    cache_.clear();
    for (auto& h : hosts_) {
        LSPA_LOG_INFO("Plugin catalog: scanning with host '%s'...", h->name().c_str());
        std::vector<PluginDescriptor> found;
        try {
            found = h->scan();
        } catch (const std::exception& e) {
            LSPA_LOG_ERROR("Plugin catalog: host '%s' scan failed: %s", h->name().c_str(), e.what());
        } catch (...) {
            LSPA_LOG_ERROR("Plugin catalog: host '%s' scan failed with unknown error", h->name().c_str());
        }
        int warned = 0;
        for (auto& d : found) {
            if (!d.warning.empty() && warned < 5) {
                LSPA_LOG_WARN("Plugin catalog: %s: %s", d.key.id.c_str(), d.warning.c_str());
                ++warned;
            }
        }
        LSPA_LOG_INFO("Plugin catalog: host '%s' found %zu plugins", h->name().c_str(), found.size());
        cache_.insert(cache_.end(), std::make_move_iterator(found.begin()),
                      std::make_move_iterator(found.end()));
    }
    cacheValid_ = true;
    return cache_;
}

std::vector<PluginDescriptor> PluginCatalog::scanFormat(PluginFormat format, bool refresh) {
    IPluginHost* h = hostFor(format);
    if (!h) return {};
    if (refresh) {
        std::lock_guard<std::mutex> lock(mutex_);
        cacheValid_ = false;
    }
    return h->scan();
}

const PluginDescriptor* PluginCatalog::find(const PluginKey& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& d : cache_) {
        if (d.key == key) return &d;
    }
    return nullptr;
}

std::unique_ptr<IPluginInstance> PluginCatalog::instantiate(const PluginKey& key,
                                                            double sampleRate,
                                                            int maxBlock,
                                                            std::string& errorOut) {
    IPluginHost* h = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& hh : hosts_) {
            if (hh->format() == key.format) {
                h = hh.get();
                break;
            }
        }
    }
    if (!h) {
        errorOut = std::string("No host registered for format ") + pluginFormatName(key.format);
        return nullptr;
    }
    return h->instantiate(key, sampleRate, maxBlock, errorOut);
}

}  // namespace lspa
