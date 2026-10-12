// Lv2Instance.h - one instantiated LV2 plugin. Owned by EffectChain, used from
// the audio thread after activate().
//
// LV2/lilv details live in an internal pimpl so this header stays free of
// format-specific includes; Lv2UiHost (FR-06) uses the "editor support"
// accessors below to open and drive the native editor.
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "plugins/IPluginHost.h"

namespace lspa {

class UridMap;

// Factory used by Lv2Host. Implementation lives in Lv2Instance.cpp.
//  world:   borrowed LilvWorld* of the owning host (kept alive by the host).
//  plugin:  borrowed const LilvPlugin* found in that world (owned by world).
//  uridMap: one shared mapper per host.
// Returns nullptr and fills errorOut on failure (never throws on bad
// third-party data).
std::unique_ptr<IPluginInstance> createLv2Instance(const void* world,
                                                   const void* plugin,
                                                   std::shared_ptr<UridMap> uridMap,
                                                   double sampleRate,
                                                   int maxBlock,
                                                   std::string& errorOut);

// Concrete LV2 instance. The engine wires descriptors from
// PluginCatalog::find() to decide how many instances a slot needs; the
// accessors below expose the plugin's native channel counts so the engine can
// pair one stereo instance or two mono instances.
class Lv2Instance final : public IPluginInstance {
public:
    // Internal: build from an already-populated Impl (see Lv2Instance.cpp).
    // External code cannot name Impl, so this is effectively private.
    struct Impl;
    explicit Lv2Instance(std::unique_ptr<Impl> impl);
    ~Lv2Instance() override;

    Lv2Instance(const Lv2Instance&) = delete;
    Lv2Instance& operator=(const Lv2Instance&) = delete;

    // --- IPluginInstance -----------------------------------------------------
    const PluginKey& key() const override;
    const std::string& displayName() const override;
    bool isOk() const override;
    const std::string& errorString() const override;
    int latencySamples() const override;

    // Real-time safe: no allocation, no locks, no logging (PRD 8.3, NFR-02).
    void process(float* const* inputs, int nInputs,
                 float* const* outputs, int nOutputs, int nFrames) override;

    void activate() override;
    void deactivate() override;

    std::vector<PluginParam> parameters() const override;
    void setParameter(const std::string& symbol, float value) override;
    float getParameter(const std::string& symbol) const override;

    bool hasNativeUi() const override;

    // Native audio channel counts of THIS plugin (engine wiring hint; the
    // engine uses the descriptor from PluginCatalog::find for the same data).
    int numAudioInputs() const;
    int numAudioOutputs() const;

    // --- Editor support (Lv2UiHost only) ------------------------------------
    void* lilvWorld() const;      // LilvWorld* (borrowed)
    void* lilvPlugin() const;     // const LilvPlugin* (borrowed)
    void* lilvInstance() const;   // LilvInstance* of the DSP (borrowed)
    void* lv2Handle() const;      // LV2_Handle of the DSP instance
    UridMap* uridMap() const;

    int portCount() const;
    int portIndexBySymbol(const char* symbol) const;  // -1 when unknown
    bool isControlPort(int index) const;
    float portValue(int index) const;  // cached value, RT-safe

    // Suil write callback: the editor changed an input control port. RT-safe.
    void setPortValueFromUi(int index, float value);

    // Editor notification queue. Producers: process() (audio thread) and
    // setParameter() (engine or UI thread); consumer: the editor pump in
    // Lv2UiHost. Lock-free; drops (and counts) when full. RT-safe.
    void notifyUi(int index, float value);
    bool popUiEvent(int& index, float& value);  // non-RT (editor pump side)

    // Current values of all control ports (initial editor sync). Non-RT.
    std::vector<std::pair<int, float>> controlSnapshot() const;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace lspa
