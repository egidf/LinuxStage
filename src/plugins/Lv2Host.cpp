// Lv2Host.cpp - LV2 discovery (Lilv) + instantiation factory (FR-04).
//
// The Lilv world is created lazily on the first scan()/instantiate() call so
// application startup stays snappy; both are non real-time by contract.
// Never throws on bad third-party metadata: per-plugin diagnostics go into
// PluginDescriptor::warning or the log instead.
#include "plugins/Lv2Host.h"

#include <cctype>
#include <cctype>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <vector>

#include <lilv/lilv.h>
#include <lv2/core/lv2.h>

#include "core/Logger.h"
#include "plugins/LilvLock.h"
#include "plugins/Lv2Features.h"
#include "plugins/Lv2Instance.h"
#include "plugins/Lv2UiHost.h"
#include "plugins/UridMap.h"

namespace lspa {
namespace {

// --- small helpers ---------------------------------------------------------

// Last path segment of a URI, minus a trailing '#'-fragment if present.
std::string uriTail(const std::string& uri) {
    size_t slash = uri.find_last_of("/#");
    return slash == std::string::npos ? uri : uri.substr(slash + 1);
}

// Convert a file: URI to a filesystem path (best effort).
std::string uriToPath(const char* uri) {
    if (uri == nullptr) return {};
    if (char* p = lilv_file_uri_parse(uri, nullptr)) {
        std::string out(p);
        lilv_free(p);
        return out;
    }
    return {};
}

// Friendly category names expected by the browser / rack cards.
// First match wins; `hay` is matched lowercased.
struct CatRule {
    const char* token;
    const char* name;
};

const CatRule kCatRules[] = {
    // dynamics first (a name may contain several tokens)
    {"limiter", "Dynamics"},   {"compress", "Dynamics"},  {"dynamics", "Dynamics"},
    {"expander", "Dynamics"},  {"gate", "Dynamics"},      {"comp_", "Dynamics"},
    // EQ before generic "filter" so parametric EQs are not tagged Filter
    {"equalizer", "EQ"},       {"paraeq", "EQ"},          {"multieq", "EQ"},
    {"eqplugin", "EQ"},        {"_eq_", "EQ"},            {"_eq", "EQ"},
    {"reverb", "Reverb"},      {"conv_reverb", "Reverb"}, {"convolution", "Reverb"},
    {"delay", "Delay"},        {"echo", "Delay"},
    {"chorus", "Modulation"},  {"flanger", "Modulation"}, {"phaser", "Modulation"},
    {"tremolo", "Modulation"}, {"modulator", "Modulation"},
    {"distortion", "Distortion"}, {"overdrive", "Distortion"},
    {"saturat", "Distortion"}, {"simulator", "Distortion"},
    {"filter", "Filter"},      {"crossover", "Filter"},   {"lowpass", "Filter"},
    {"highpass", "Filter"},    {"bandpass", "Filter"},
    {"pitch", "Pitch"},        {"shift", "Pitch"},
    {"analyser", "Analyser"},  {"meter", "Analyser"},     {"scope", "Analyser"},
    {"spectral", "Spectral"},  {"stft", "Spectral"},
    {"spatial", "Spatial"},    {"panner", "Spatial"},     {"ambisonic", "Spatial"},
    {"generator", "Generator"},{"oscillator", "Generator"},{"instrument", "Generator"},
    {"synth", "Generator"},
    {"midi", "MIDI"},
    {"utility", "Utility"},    {"mixer", "Utility"},      {"amplifier", "Utility"},
    {"gain", "Utility"},       {"router", "Utility"},     {"switch", "Utility"},
};

// Map an LV2 class tail ("ParaEQPlugin") or doap:category tail to a category.
// Returns empty when nothing matched.
const char* categoryFromClass(const std::string& clsLower) {
    for (const auto& r : kCatRules) {
        if (clsLower.find(r.token) != std::string::npos) return r.name;
    }
    return nullptr;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Required features our adapter implements. Delegates to the same list the
// instantiation gate uses (Lv2Features.h) so scan() and instantiate() can
// never disagree, plus the two informational "host must be RT capable" flags
// that reference hosts (jalv) accept as provided.
bool supportedFeature(const std::string& uri) {
    if (lv2FeatureProvided(uri)) return true;
    static const char* kInformational[] = {
        LV2_CORE__hardRTCapable,
        "http://lv2plug.in/ns/ext/buf-size#hardRTCapable",
    };
    for (const char* f : kInformational) {
        if (uri == f) return true;
    }
    return false;
}

}  // namespace

// --- impl ------------------------------------------------------------------

// --- process-wide Lilv serialization ---------------------------------------
// (LilvLock: see LilvLock.h - one recursive mutex for every Lilv call.)

struct Lv2Host::Impl {
    LilvWorld* world = nullptr;
    const LilvPlugins* plugins = nullptr;  // owned by world
    std::shared_ptr<UridMap> uridMap;
    bool loaded = false;
    bool failed = false;
};

Lv2Host::Lv2Host() : impl_(new Impl()) {}

Lv2Host::~Lv2Host() {
    LilvLock lock;
    if (impl_->world != nullptr) {
        lilv_world_free(impl_->world);
        impl_->world = nullptr;
    }
}

// Load the world once; concurrent callers are serialized, later calls are a
// cheap flag check.
bool Lv2Host::ensureLoaded() const {
    LilvLock lock;
    if (impl_->loaded) return impl_->world != nullptr;
    impl_->loaded = true;
    impl_->world = lilv_world_new();
    if (impl_->world == nullptr) {
        impl_->failed = true;
        LSPA_LOG_ERROR("Lv2Host: lilv_world_new() failed");
        return false;
    }
    lilv_world_load_all(impl_->world);
    impl_->plugins = lilv_world_get_all_plugins(impl_->world);
    impl_->uridMap = std::make_shared<UridMap>();
    if (impl_->plugins == nullptr) {
        impl_->failed = true;
        LSPA_LOG_ERROR("Lv2Host: no LV2 plugins found on this system");
        return false;
    }
    const size_t n = lilv_plugins_size(impl_->plugins);
    LSPA_LOG_INFO("Lv2Host: discovered %zu LV2 plugin(s)", n);
    return true;
}

bool Lv2Host::isReady() const {
    if (!ensureLoaded()) return false;
    LilvLock lock;
    return impl_->world != nullptr && impl_->plugins != nullptr;
}

// --- scan ------------------------------------------------------------------

namespace {

// Cap on per-plugin scan warnings written to the log; every warning still
// lands in PluginDescriptor::warning (PluginCatalog logs its own head of it).
constexpr int kMaxScanWarnLogs = 8;

// Fill one descriptor from a plugin's metadata. Everything here is best
// effort: the caller wraps this per plugin so broken third-party metadata
// can never abort a scan (FR-04). `d.key.id`/`d.uri` must already be set.
void describePlugin(LilvWorld* world, const LilvPlugin* p, LilvNode* rdfType,
                    LilvNode* doapCategory, LilvNode* doapMaker, LilvNode* audioClass,
                    LilvNode* inputClass, LilvNode* outputClass, PluginDescriptor& d) {
    // Human readable name.
    if (LilvNode* n = lilv_plugin_get_name(p)) {
        if (const char* s = lilv_node_as_string(n)) d.name = s;
        lilv_node_free(n);
    }
    if (d.name.empty()) d.name = d.label;

    // Bundle path (informational; also used for "rescan" diagnostics).
    if (const LilvNode* b = lilv_plugin_get_bundle_uri(p)) {
        d.bundle = uriToPath(lilv_node_as_uri(b));
        if (d.bundle.empty()) d.bundle = lilv_node_as_uri(b);
    }

    // --- category: lv2 class first, then doap:category, then keywords ---
    std::string bestClass;
    std::string category;

    // Collect rdf:type classes (may be several: lv2:Plugin + specific).
    if (LilvNodes* types = lilv_plugin_get_value(p, rdfType)) {
        LILV_FOREACH(nodes, ti, types) {
            const LilvNode* t = lilv_nodes_get(types, ti);
            if (t == nullptr || !lilv_node_is_uri(t)) continue;
            std::string cls = lower(uriTail(lilv_node_as_uri(t)));
            if (cls.empty() || cls == "plugin" || cls == "project") continue;
            const char* c = categoryFromClass(cls);
            if (c != nullptr) {
                category = c;
                if (bestClass.empty()) bestClass = uriTail(lilv_node_as_uri(t));
                break;  // specific class wins
            }
            if (bestClass.empty()) bestClass = uriTail(lilv_node_as_uri(t));
        }
        lilv_nodes_free(types);
    }

    // doap:category as secondary signal.
    if (category.empty()) {
        if (LilvNodes* cats = lilv_plugin_get_value(p, doapCategory)) {
            LILV_FOREACH(nodes, ci, cats) {
                const LilvNode* t = lilv_nodes_get(cats, ci);
                if (t == nullptr) continue;
                const char* raw =
                    lilv_node_is_uri(t) ? lilv_node_as_uri(t) : lilv_node_as_string(t);
                if (raw == nullptr) continue;
                const char* c = categoryFromClass(lower(raw));
                if (c != nullptr) {
                    category = c;
                    break;
                }
            }
            lilv_nodes_free(cats);
        }
    }

    // Keyword fallback on URI + name (best effort per PRD FR-04).
    if (category.empty()) {
        const std::string hay = lower(d.key.id + " " + d.name);
        const char* c = categoryFromClass(hay);
        category = c != nullptr ? c : "Other";
    }
    d.category = category;
    if (bestClass.empty()) bestClass = d.label;
    d.label = bestClass;

    // Author: only literal values are useful; URI nodes are skipped.
    if (LilvNodes* makers = lilv_plugin_get_value(p, doapMaker)) {
        if (lilv_nodes_size(makers) > 0) {
            const LilvNode* m = lilv_nodes_get(makers, lilv_nodes_begin(makers));
            if (m != nullptr && lilv_node_is_string(m)) d.author = lilv_node_as_string(m);
        }
        lilv_nodes_free(makers);
    }

    // Audio channel counts (drives mono-doubling in EffectChain wiring).
    const uint32_t nPorts = lilv_plugin_get_num_ports(p);
    for (uint32_t i = 0; i < nPorts; ++i) {
        const LilvPort* port = lilv_plugin_get_port_by_index(p, i);
        if (port == nullptr) continue;
        if (!lilv_port_is_a(p, port, audioClass)) continue;
        if (lilv_port_is_a(p, port, inputClass)) ++d.audioInputs;
        if (lilv_port_is_a(p, port, outputClass)) ++d.audioOutputs;
    }

    // Native UI detected? Use the same X11-hostability test the editor will
    // apply, so the "UI" badge means "we can actually open this".
    lv2ui::UiChoice uiChoice;
    d.hasUi = lv2ui::pickUiForX11(world, p, uiChoice);

    // hard real-time declaration + unsupported required features.
    if (LilvNodes* feats = lilv_plugin_get_required_features(p)) {
        LILV_FOREACH(nodes, fi, feats) {
            const LilvNode* f = lilv_nodes_get(feats, fi);
            if (f == nullptr || !lilv_node_is_uri(f)) continue;
            const std::string furi = lilv_node_as_uri(f);
            if (furi == LV2_CORE__hardRTCapable) {
                d.hardRtSafe = true;
            } else if (!supportedFeature(furi)) {
                if (!d.warning.empty()) d.warning += ", ";
                d.warning += "requires unsupported feature: " + furi;
            }
        }
        lilv_nodes_free(feats);
    }
}

}  // namespace

std::vector<PluginDescriptor> Lv2Host::scan() {
    std::vector<PluginDescriptor> out;
    if (!ensureLoaded()) return out;

    // Hold the host lock for the WHOLE scan: lilv parses bundle data lazily
    // on first access and the world/sord store is not thread-safe, so a scan
    // running concurrently with instantiate() would corrupt the heap
    // (observed as intermittent "double free" aborts at shutdown).
    LilvLock lock;
    LilvWorld* world = impl_->world;
    const LilvPlugins* plugins = impl_->plugins;
    if (world == nullptr || plugins == nullptr) return out;

    LilvNode* rdfType = lilv_new_uri(world, "http://www.w3.org/1999/02/22-rdf-syntax-ns#type");
    LilvNode* doapCategory = lilv_new_uri(world, "http://usefulinc.com/ns/doap#category");
    LilvNode* doapMaker = lilv_new_uri(world, "http://usefulinc.com/ns/doap#maker");
    LilvNode* audioClass = lilv_new_uri(world, LV2_CORE__AudioPort);
    LilvNode* inputClass = lilv_new_uri(world, LV2_CORE__InputPort);
    LilvNode* outputClass = lilv_new_uri(world, LV2_CORE__OutputPort);

    out.reserve(lilv_plugins_size(plugins));

    int loggedWarnings = 0;
    LILV_FOREACH(plugins, it, plugins) {
        const LilvPlugin* p = lilv_plugins_get(plugins, it);
        if (p == nullptr) continue;

        PluginDescriptor d;
        d.key.format = PluginFormat::Lv2;
        if (const LilvNode* uri = lilv_plugin_get_uri(p)) d.key.id = lilv_node_as_uri(uri);
        if (d.key.id.empty()) continue;
        d.uri = d.key.id;
        d.label = uriTail(d.key.id);

        // Per-plugin isolation: one bundle with broken metadata must never
        // abort the scan or take the process down (FR-04).
        try {
            describePlugin(world, p, rdfType, doapCategory, doapMaker, audioClass, inputClass,
                           outputClass, d);
        } catch (const std::exception& ex) {
            d.warning = std::string("failed to read plugin metadata: ") + ex.what();
        } catch (...) {
            d.warning = "failed to read plugin metadata: unknown error";
        }

        if (!d.warning.empty() && loggedWarnings < kMaxScanWarnLogs) {
            ++loggedWarnings;
            LSPA_LOG_WARN("Lv2Host: %s: %s", d.key.id.c_str(), d.warning.c_str());
        }
        out.push_back(std::move(d));
    }

    lilv_node_free(rdfType);
    lilv_node_free(doapCategory);
    lilv_node_free(doapMaker);
    lilv_node_free(audioClass);
    lilv_node_free(inputClass);
    lilv_node_free(outputClass);

    size_t warned = 0;
    for (const PluginDescriptor& d : out) {
        if (!d.warning.empty()) ++warned;
    }
    LSPA_LOG_INFO("Lv2Host: scan complete, %zu plugin descriptor(s), %zu with warnings",
                  out.size(), warned);
    if (warned > static_cast<size_t>(kMaxScanWarnLogs)) {
        LSPA_LOG_WARN("Lv2Host: %zu plugins have scan warnings (first %d logged above)",
                      warned, kMaxScanWarnLogs);
    }
    return out;
}

// --- instantiate -----------------------------------------------------------

std::unique_ptr<IPluginInstance> Lv2Host::instantiate(const PluginKey& key, double sampleRate,
                                                      int maxBlock, std::string& errorOut) {
    if (key.format != PluginFormat::Lv2) {
        errorOut = "not an LV2 plugin key";
        return nullptr;
    }
    if (key.id.empty()) {
        errorOut = "empty plugin URI";
        return nullptr;
    }
    if (!ensureLoaded()) {
        errorOut = "LV2 discovery failed (no world)";
        return nullptr;
    }

    // Serializes with scan(): lilv is not thread-safe and a plugin may be
    // instantiated while a rescan is running.
    LilvLock lock;
    LilvWorld* world = impl_->world;
    const LilvPlugins* plugins = impl_->plugins;
    std::shared_ptr<UridMap> uridMap = impl_->uridMap;
    if (world == nullptr || plugins == nullptr) {
        errorOut = "LV2 discovery failed (no plugin list)";
        return nullptr;
    }

    LilvNode* uri = lilv_new_uri(world, key.id.c_str());
    if (uri == nullptr) {
        // Convenience: accept a bare plugin suffix such as
        // "para_equalizer_x16_stereo" and resolve it against the scan.
        const LilvPlugin* match = nullptr;
        for (LilvIter* it = lilv_plugins_begin(plugins); !lilv_plugins_is_end(plugins, it);
             it = lilv_plugins_next(plugins, it)) {
            const LilvPlugin* cand = lilv_plugins_get(plugins, it);
            const LilvNode* cu = lilv_plugin_get_uri(cand);
            const std::string id = cu ? lilv_node_as_uri(cu) : std::string();
            if (id == key.id || uriTail(id) == key.id) {
                if (match != nullptr) {
                    match = nullptr;  // ambiguous: more than one suffix match
                    break;
                }
                match = cand;
            }
        }
        if (match == nullptr) {
            // Distinguish "not found" from "ambiguous suffix".
            int candidates = 0;
            for (LilvIter* it = lilv_plugins_begin(plugins); !lilv_plugins_is_end(plugins, it);
                 it = lilv_plugins_next(plugins, it)) {
                const LilvNode* cu =
                    lilv_plugin_get_uri(lilv_plugins_get(plugins, it));
                if (cu && uriTail(lilv_node_as_uri(cu)) == key.id) ++candidates;
            }
            errorOut = candidates > 1
                           ? ("ambiguous plugin suffix '" + key.id + "' (" +
                              std::to_string(candidates) + " matches); use a full URI")
                           : ("invalid plugin URI: " + key.id);
            return nullptr;
        }
        return createLv2Instance(world, match, uridMap, sampleRate, maxBlock, errorOut);
    }
    const LilvPlugin* p = lilv_plugins_get_by_uri(plugins, uri);
    lilv_node_free(uri);
    if (p == nullptr) {
        errorOut = "plugin not found on this system: " + key.id;
        return nullptr;
    }

    return createLv2Instance(world, p, uridMap, sampleRate, maxBlock, errorOut);
}

bool Lv2Host::canOpenEditor(const IPluginInstance* inst) const {
    const auto* lv2 = dynamic_cast<const Lv2Instance*>(inst);
    return lv2 != nullptr && lv2->hasNativeUi();
}

}  // namespace lspa
