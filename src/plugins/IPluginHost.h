// IPluginHost.h - format-agnostic plugin host contract.
//
// This header is the boundary between the audio engine / rack logic and any
// concrete plugin format adapter (LV2 on Linux, VST3 on Windows later).
// It must stay free of format-specific headers (no lilv/suil/juce includes).
//
// Threading contract:
//  - scan() and instantiate() are NON real-time. Never call them from the
//    audio callback.
//  - IPluginInstance::process(), setParameter() and getParameter() must be
//    real-time safe: no allocation, no locks, no I/O.
//  - The engine owns instances and guarantees process() is not called
//    concurrently with itself nor with activate()/deactivate().

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lspa {

enum class PluginFormat {
    Lv2,
    Vst3
};

inline const char* pluginFormatName(PluginFormat f) {
    switch (f) {
        case PluginFormat::Lv2: return "LV2";
        case PluginFormat::Vst3: return "VST3";
    }
    return "Unknown";
}

enum class PluginLoadStatus {
    NotLoaded,   // never attempted
    Loading,
    Loaded,      // instantiated and processing
    Failed,      // instantiation or runtime failure (error string has details)
    Missing,     // plugin referenced by session but not present on this system
    Unsupported  // present but requires features this host cannot provide
};

inline const char* pluginLoadStatusName(PluginLoadStatus s) {
    switch (s) {
        case PluginLoadStatus::NotLoaded: return "Not loaded";
        case PluginLoadStatus::Loading: return "Loading";
        case PluginLoadStatus::Loaded: return "OK";
        case PluginLoadStatus::Failed: return "Error";
        case PluginLoadStatus::Missing: return "Missing";
        case PluginLoadStatus::Unsupported: return "Unsupported";
    }
    return "?";
}

struct PluginKey {
    PluginFormat format = PluginFormat::Lv2;
    std::string id;  // LV2 URI, VST3 class ID

    bool operator==(const PluginKey& o) const { return format == o.format && id == o.id; }
    bool operator!=(const PluginKey& o) const { return !(*this == o); }
};

struct PluginDescriptor {
    PluginKey key;
    std::string name;        // human readable plugin name
    std::string label;       // short label / class name
    std::string uri;         // full identifier (same as key.id for LV2)
    std::string bundle;      // bundle path or binary path
    std::string author;
    std::string category;    // "EQ", "Dynamics", "Reverb", ... (best effort)
    bool hasUi = false;      // a native editor was detected
    int audioInputs = 0;
    int audioOutputs = 0;
    bool hardRtSafe = false;  // declared hard real-time safe by metadata
    std::string warning;      // non-fatal scan diagnostic
};

struct PluginParam {
    std::string symbol;
    std::string name;
    float min = 0.0f;
    float max = 1.0f;
    float def = 0.0f;
    float value = 0.0f;
    bool isInteger = false;
    bool isToggled = false;
    std::string unit;
    bool logarithmic = false;
};

// A single instantiated plugin, ready for real-time processing.
class IPluginInstance {
public:
    virtual ~IPluginInstance() = default;

    virtual const PluginKey& key() const = 0;
    virtual const std::string& displayName() const = 0;

    virtual bool isOk() const = 0;
    virtual const std::string& errorString() const = 0;

    virtual int latencySamples() const { return 0; }

    // Real-time safe. Planar float buffers (one pointer per channel).
    // in != out is allowed; in == out is allowed for in-place capable hosts.
    virtual void process(float* const* inputs, int nInputs,
                         float* const* outputs, int nOutputs,
                         int nFrames) = 0;

    virtual void activate() = 0;
    virtual void deactivate() = 0;

    // Parameter access by plugin-defined symbol. Real-time safe.
    virtual std::vector<PluginParam> parameters() const = 0;
    virtual void setParameter(const std::string& symbol, float value) = 0;
    virtual float getParameter(const std::string& symbol) const = 0;

    // Native editor support.
    virtual bool hasNativeUi() const = 0;
};

// Factory/discovery interface implemented once per plugin format.
class IPluginHost {
public:
    virtual ~IPluginHost() = default;

    virtual PluginFormat format() const = 0;
    virtual std::string name() const = 0;

    // Full rescan of the system. Non real-time, may be slow and may log
    // per-plugin diagnostics. Must never throw on bad third-party metadata.
    virtual std::vector<PluginDescriptor> scan() = 0;

    // Create an instance. Returns nullptr and fills errorOut on failure.
    virtual std::unique_ptr<IPluginInstance> instantiate(const PluginKey& key,
                                                         double sampleRate,
                                                         int maxBlock,
                                                         std::string& errorOut) = 0;

    // True if this host can open a native editor for the instance.
    virtual bool canOpenEditor(const IPluginInstance* inst) const = 0;
};

}  // namespace lspa
