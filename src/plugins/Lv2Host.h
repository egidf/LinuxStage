// Lv2Host.h - LV2 plugin host adapter backed by Lilv (+ Suil for UIs).
// Public API is a contract with the rest of the app; internals are free to
// change.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "plugins/IPluginHost.h"

namespace lspa {

class Lv2Host final : public IPluginHost {
public:
    Lv2Host();
    ~Lv2Host() override;

    Lv2Host(const Lv2Host&) = delete;
    Lv2Host& operator=(const Lv2Host&) = delete;

    PluginFormat format() const override { return PluginFormat::Lv2; }
    std::string name() const override { return "LV2 (Lilv)"; }

    std::vector<PluginDescriptor> scan() override;

    std::unique_ptr<IPluginInstance> instantiate(const PluginKey& key,
                                                 double sampleRate,
                                                 int maxBlock,
                                                 std::string& errorOut) override;

    bool canOpenEditor(const IPluginInstance* inst) const override;

    // True if the world (plugin list) has been loaded successfully.
    bool isReady() const;

private:
    // Create the Lilv world on first use (thread safe, cheap afterwards).
    bool ensureLoaded() const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lspa
