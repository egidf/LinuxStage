// Lv2Instance.cpp - real LV2 instantiation, port plumbing and RT processing.
//
// Threading: set up (create/activate/deactivate/destroy) runs on non-RT
// threads. process(), setParameter(), getParameter(), notifyUi() and
// popUiEvent() are lock-free and allocation-free so they are safe on the
// audio thread (PRD 8.3, NFR-02). Nothing in the process() path logs.
#include "plugins/Lv2Instance.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/core/lv2.h>
#include <lv2/options/options.h>
#include <lv2/parameters/parameters.h>
#include <lv2/port-props/port-props.h>
#include <lv2/units/units.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>

#include "core/Logger.h"
#include "plugins/LilvLock.h"
#include "plugins/Lv2Features.h"
#include "plugins/Lv2UiHost.h"
#include "plugins/Lv2Worker.h"
#include "plugins/UridMap.h"

namespace lspa {
namespace {

// Size of every atom input/output buffer (bytes, 64-byte aligned allocation).
constexpr size_t kAtomPortBytes = 8192;

enum class PortType { Audio, Control, Atom, Other };

// Features this host provides to DSP instances: see Lv2Features.h (shared
// with Lv2Host::scan so both sides agree on what is supported).
using lspa::lv2FeatureProvided;

// Lock-free multi-producer/single-consumer queue of {port index, value}
// notifications. Producers: process() on the audio thread and setParameter()
// from any thread. Consumer: the editor pump (Lv2UiHost). Fixed capacity;
// pushes are dropped (and counted) when full - never logged here because one
// producer may be the audio thread.
class UiEventQueue {
public:
    static constexpr uint32_t kCapacity = 512;  // power of two

    UiEventQueue() {
        for (auto& s : slots_) s.state.store(0, std::memory_order_relaxed);
    }

    void push(uint32_t index, float value) {
        const uint64_t base = ticket_.load(std::memory_order_relaxed);
        for (uint32_t tries = 0; tries < kCapacity; ++tries) {
            Slot& s = slots_[(base + tries) & (kCapacity - 1)];
            uint8_t expect = 0;
            if (s.state.compare_exchange_strong(expect, 1, std::memory_order_acq_rel)) {
                s.index = index;
                s.value = value;
                s.state.store(2, std::memory_order_release);  // publish
                ticket_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        dropped_.fetch_add(1, std::memory_order_relaxed);  // full: drop silently
    }

    bool pop(uint32_t& index, float& value) {
        for (uint32_t n = 0; n < kCapacity; ++n) {
            const uint32_t i = (cursor_ + n) & (kCapacity - 1);
            Slot& s = slots_[i];
            if (s.state.load(std::memory_order_acquire) == 2) {
                index = s.index;
                value = s.value;
                s.state.store(0, std::memory_order_release);
                cursor_ = (i + 1) & (kCapacity - 1);
                return true;
            }
        }
        return false;
    }

    uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

private:
    struct Slot {
        std::atomic<uint8_t> state{0};  // 0 = free, 1 = writing, 2 = ready
        uint32_t index = 0;
        float value = 0.0f;
    };
    Slot slots_[kCapacity];
    std::atomic<uint64_t> ticket_{0};
    uint32_t cursor_ = 0;  // consumer-owned
    std::atomic<uint32_t> dropped_{0};
};

}  // namespace

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct Lv2Instance::Impl {
    PluginKey key;
    std::string name;
    std::string error;
    bool ok = false;
    bool active = false;
    bool hasNativeUi = false;
    int latency = 0;

    double sampleRate = 48000.0;
    int maxBlock = 256;

    LilvWorld* world = nullptr;               // borrowed (owned by Lv2Host)
    const LilvPlugin* plugin = nullptr;       // borrowed (owned by world)
    std::shared_ptr<UridMap> uridMap;
    LilvInstance* inst = nullptr;

    struct Port {
        uint32_t index = 0;
        PortType type = PortType::Other;
        bool in = false;
        std::string symbol;
        std::string name;
        std::string unit;
        float min = 0.0f;
        float max = 1.0f;
        float def = 0.0f;
        float value = 0.0f;
        bool hasRange = false;
        bool isInteger = false;
        bool isToggled = false;
        bool logarithmic = false;
        bool notified = false;      // output control: already pushed once
        float notifiedValue = 0.0f;
        void* buffer = nullptr;     // connected LV2 port buffer
        LV2_URID atomType = 0;      // atom:bufferType (atom ports only)
    };
    std::vector<Port> ports;
    std::unordered_map<std::string, uint32_t> symIndex;
    std::vector<uint32_t> audioInPorts;   // port indices, engine channel order
    std::vector<uint32_t> audioOutPorts;
    int nAudioIn = 0;
    int nAudioOut = 0;
    uint32_t latencyPort = UINT32_MAX;

    // URIDs the host itself writes (pre-mapped; process() never maps).
    LV2_URID uridAtomSequence = 0;
    LV2_URID uridAtomFloat = 0;

    // Options feature values (must stay valid for the instance lifetime).
    float optSampleRate = 48000.0f;
    int32_t optMinBlock = 256;
    int32_t optMaxBlock = 256;
    int32_t optNominalBlock = 256;
    int32_t optSequenceSize = static_cast<int32_t>(kAtomPortBytes);
    LV2_Options_Option options[6] = {};

    // Feature structs (stable addresses: this Impl never moves).
    LV2_Feature fMap{};
    LV2_Feature fUnmap{};
    LV2_Feature fOptions{};
    LV2_Feature fSchedule{};
    LV2_Feature fBounded{};
    LV2_Feature fPow2{};
    LV2_Feature fFixed{};
    const LV2_Feature* featureList[9] = {};

    Lv2Worker worker;
    const LV2_Worker_Interface* workerIface = nullptr;

    UiEventQueue uiQueue;

    ~Impl() { teardown(); }

    bool setup(std::string& err);
    bool setupPorts(std::string& err);
    void connectPorts();
    void applyInputDefaults();
    void teardown();

    // Re-entrancy guard for process(): the engine's offline render path and
    // its real-time loop can both call process() on the same instance.
    std::atomic<bool> inRun{false};

    void run(float* const* inputs, int nInputs, float* const* outputs, int nOutputs, int nFrames);
    void clearAtomInput(const Port& p) const;
    void bypass(float* const* inputs, int nInputs, float* const* outputs, int nOutputs, int n);
    Port* portByIndex(int index);
    const Port* portByIndex(int index) const;
};

Lv2Instance::Impl::Port* Lv2Instance::Impl::portByIndex(int index) {
    if (index < 0 || static_cast<size_t>(index) >= ports.size()) return nullptr;
    return &ports[static_cast<size_t>(index)];
}

const Lv2Instance::Impl::Port* Lv2Instance::Impl::portByIndex(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= ports.size()) return nullptr;
    return &ports[static_cast<size_t>(index)];
}

bool Lv2Instance::Impl::setup(std::string& err) {
    // --- Required feature check (report as Unsupported in the UI) -----------
    bool requiresPow2 = false;
    // lilv_plugin_get_required_features() transfers ownership of the set.
    LilvNodes* required = lilv_plugin_get_required_features(plugin);
    if (required != nullptr) {
        LILV_FOREACH(nodes, i, required) {
            const LilvNode* node = lilv_nodes_get(required, i);
            const char* uri = lilv_node_as_uri(node);
            if (uri == nullptr) continue;
            if (!lv2FeatureProvided(uri)) {
                err = std::string("requires unsupported feature: ") + uri;
                lilv_nodes_free(required);
                return false;
            }
            if (std::strcmp(uri, LV2_BUF_SIZE__powerOf2BlockLength) == 0) requiresPow2 = true;
        }
        lilv_nodes_free(required);
    }
    // buf-size:powerOf2BlockLength can only be honestly claimed with pow2 blocks.
    if (requiresPow2 && (maxBlock & (maxBlock - 1)) != 0) {
        err = "requires power-of-2 block length but maxBlock is not a power of two";
        return false;
    }

    // --- Name ---------------------------------------------------------------
    if (LilvNode* n = lilv_plugin_get_name(plugin)) {
        name = lilv_node_as_string(n);
        lilv_node_free(n);
    }
    if (name.empty()) name = key.id;

    // --- Host URIDs (never mapped again on the audio thread) ----------------
    uridAtomSequence = uridMap->map(LV2_ATOM__Sequence);
    uridAtomFloat = uridMap->map(LV2_ATOM__Float);

    // --- Options (types match the reference host jalv: sample rate is an
    //     atom:Float, block lengths are atom:Int) ----------------------------
    optSampleRate = static_cast<float>(sampleRate);
    optMinBlock = maxBlock;
    optMaxBlock = maxBlock;
    optNominalBlock = maxBlock;
    const LV2_URID uridFloat = uridMap->map(LV2_ATOM__Float);
    const LV2_URID uridInt = uridMap->map(LV2_ATOM__Int);
    const LV2_URID uridSampleRate = uridMap->map(LV2_PARAMETERS__sampleRate);
    const LV2_URID uridMinBlock = uridMap->map(LV2_BUF_SIZE__minBlockLength);
    const LV2_URID uridMaxBlock = uridMap->map(LV2_BUF_SIZE__maxBlockLength);
    const LV2_URID uridNominalBlock = uridMap->map(LV2_BUF_SIZE__nominalBlockLength);
    const LV2_URID uridSeqSize = uridMap->map(LV2_BUF_SIZE__sequenceSize);
    options[0] = {LV2_OPTIONS_INSTANCE, 0, uridSampleRate, sizeof(float), uridFloat,
                  &optSampleRate};
    options[1] = {LV2_OPTIONS_INSTANCE, 0, uridMinBlock, sizeof(int32_t), uridInt, &optMinBlock};
    options[2] = {LV2_OPTIONS_INSTANCE, 0, uridMaxBlock, sizeof(int32_t), uridInt, &optMaxBlock};
    options[3] = {LV2_OPTIONS_INSTANCE, 0, uridNominalBlock, sizeof(int32_t), uridInt,
                  &optNominalBlock};
    options[4] = {LV2_OPTIONS_INSTANCE, 0, uridSeqSize, sizeof(int32_t), uridInt,
                  &optSequenceSize};
    options[5] = {LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr};  // terminator

    // --- Feature array (data must stay valid for the instance lifetime) -----
    fMap.URI = LV2_URID__map;
    fMap.data = const_cast<LV2_URID_Map*>(uridMap->mapFeature());
    fUnmap.URI = LV2_URID__unmap;
    fUnmap.data = const_cast<LV2_URID_Unmap*>(uridMap->unmapFeature());
    fOptions.URI = LV2_OPTIONS__options;
    fOptions.data = options;
    fSchedule.URI = LV2_WORKER__schedule;
    fSchedule.data = worker.scheduleFeature();
    fBounded.URI = LV2_BUF_SIZE__boundedBlockLength;
    fBounded.data = nullptr;
    fPow2.URI = LV2_BUF_SIZE__powerOf2BlockLength;
    fPow2.data = nullptr;
    fFixed.URI = LV2_BUF_SIZE__fixedBlockLength;
    fFixed.data = nullptr;
    // min == max == maxBlock makes the block length fixed for this instance;
    // run() additionally zero-fills the input tail on short blocks so a
    // plugin that trusts the promise never reads stale memory.
    featureList[0] = &fMap;
    featureList[1] = &fUnmap;
    featureList[2] = &fOptions;
    featureList[3] = &fSchedule;
    featureList[4] = &fBounded;
    featureList[5] = &fPow2;
    featureList[6] = &fFixed;
    featureList[7] = nullptr;

    if (!setupPorts(err)) return false;

    // --- Instantiate --------------------------------------------------------
    inst = lilv_plugin_instantiate(plugin, sampleRate, featureList);
    if (inst == nullptr) {
        err = "instantiation failed (missing binary, bad metadata or rejected feature)";
        return false;
    }
    connectPorts();
    applyInputDefaults();

    // --- Worker (worker:schedule was offered above) -------------------------
    workerIface = static_cast<const LV2_Worker_Interface*>(
        lilv_instance_get_extension_data(inst, LV2_WORKER__interface));

    // --- Native editor availability (FR-06) ---------------------------------
    lv2ui::UiChoice uiChoice;
    hasNativeUi = lv2ui::pickUiForX11(world, plugin, uiChoice);

    ok = true;
    return true;
}

bool Lv2Instance::Impl::setupPorts(std::string& err) {
    LilvWorld* w = world;
    LilvNode* audioClass = lilv_new_uri(w, LV2_CORE__AudioPort);
    LilvNode* controlClass = lilv_new_uri(w, LV2_CORE__ControlPort);
    LilvNode* atomClass = lilv_new_uri(w, LV2_ATOM__AtomPort);
    LilvNode* cvClass = lilv_new_uri(w, LV2_CORE__CVPort);
    LilvNode* inputClass = lilv_new_uri(w, LV2_CORE__InputPort);
    LilvNode* propClass = lilv_new_uri(w, LV2_CORE__portProperty);
    LilvNode* unitClass = lilv_new_uri(w, LV2_UNITS__unit);
    LilvNode* bufferTypeClass = lilv_new_uri(w, LV2_ATOM__bufferType);

    const bool hasLatency = lilv_plugin_has_latency(plugin);
    if (hasLatency) latencyPort = lilv_plugin_get_latency_port_index(plugin);

    const uint32_t numPorts = lilv_plugin_get_num_ports(plugin);
    ports.reserve(numPorts);
    bool allocFailed = false;

    for (uint32_t i = 0; i < numPorts; ++i) {
        const LilvPort* lp = lilv_plugin_get_port_by_index(plugin, i);
        if (lp == nullptr) {
            // Corrupt metadata: keep a placeholder so ports[i].index == i
            // (portByIndex() and the UI rely on positional identity).
            Port bad;
            bad.index = i;
            ports.push_back(std::move(bad));
            continue;
        }

        Port p;
        p.index = i;
        p.in = lilv_port_is_a(plugin, lp, inputClass);
        if (lilv_port_is_a(plugin, lp, audioClass) || lilv_port_is_a(plugin, lp, cvClass)) {
            p.type = PortType::Audio;
        } else if (lilv_port_is_a(plugin, lp, controlClass)) {
            p.type = PortType::Control;
        } else if (lilv_port_is_a(plugin, lp, atomClass)) {
            p.type = PortType::Atom;
        } else {
            p.type = PortType::Other;
            LSPA_LOG_WARN("Lv2Instance: %s port %u has an unrecognized type, treating as audio",
                          key.id.c_str(), i);
        }

        if (const LilvNode* symNode = lilv_port_get_symbol(plugin, lp)) {
            if (const char* sym = lilv_node_as_string(symNode)) p.symbol = sym;
        }
        if (LilvNode* n = lilv_port_get_name(plugin, lp)) {
            p.name = lilv_node_as_string(n);
            lilv_node_free(n);
        }
        if (p.name.empty()) p.name = p.symbol;

        // Range (default/min/max); only meaningful literals are used.
        LilvNode* def = nullptr;
        LilvNode* min = nullptr;
        LilvNode* max = nullptr;
        lilv_port_get_range(plugin, lp, &def, &min, &max);
        auto nodeValue = [](const LilvNode* n, float& out) {
            if (n == nullptr) return false;
            if (lilv_node_is_float(n)) {
                out = lilv_node_as_float(n);
                return true;
            }
            if (lilv_node_is_int(n)) {
                out = static_cast<float>(lilv_node_as_int(n));
                return true;
            }
            return false;
        };
        p.hasRange = nodeValue(min, p.min) | nodeValue(max, p.max);  // intentional bitwise or
        if (def != nullptr && (lilv_node_is_float(def) || lilv_node_is_int(def))) {
            p.def = lilv_node_as_float(def);
        }
        if (def != nullptr) lilv_node_free(def);
        if (min != nullptr) lilv_node_free(min);
        if (max != nullptr) lilv_node_free(max);

        // Port properties (integer/toggled/logarithmic) and unit.
        if (LilvNodes* props = lilv_port_get_value(plugin, lp, propClass)) {
            LILV_FOREACH(nodes, pi, props) {
                const char* uri = lilv_node_as_uri(lilv_nodes_get(props, pi));
                if (uri == nullptr) continue;
                if (std::strcmp(uri, LV2_CORE__integer) == 0) p.isInteger = true;
                if (std::strcmp(uri, LV2_CORE__toggled) == 0) p.isToggled = true;
                if (std::strcmp(uri, LV2_PORT_PROPS__logarithmic) == 0) p.logarithmic = true;
            }
            lilv_nodes_free(props);
        }
        if (LilvNodes* units = lilv_port_get_value(plugin, lp, unitClass)) {
            if (const LilvNode* u = lilv_nodes_get_first(units)) {
                if (lilv_node_is_uri(u)) {
                    std::string uri = lilv_node_as_uri(u);
                    const size_t hash = uri.find_last_of('#');
                    p.unit = (hash != std::string::npos) ? uri.substr(hash + 1) : uri;
                } else if (lilv_node_is_literal(u)) {
                    p.unit = lilv_node_as_string(u);
                }
            }
            lilv_nodes_free(units);
        }

        p.value = p.in ? p.def : 0.0f;

        // Buffer allocation (all up front; process() never allocates).
        switch (p.type) {
            case PortType::Audio:
                p.buffer = new float[static_cast<size_t>(maxBlock)];
                std::memset(p.buffer, 0, sizeof(float) * static_cast<size_t>(maxBlock));
                if (p.in) {
                    audioInPorts.push_back(i);
                    ++nAudioIn;
                } else {
                    audioOutPorts.push_back(i);
                    ++nAudioOut;
                }
                break;
            case PortType::Control:
                p.buffer = new float[1];
                *static_cast<float*>(p.buffer) = p.value;
                break;
            case PortType::Atom: {
                void* mem = std::aligned_alloc(64, kAtomPortBytes);
                if (mem == nullptr) {
                    err = "out of memory allocating atom port buffer";
                    allocFailed = true;  // pushed below with a null buffer
                    break;
                }
                p.buffer = mem;
                std::memset(p.buffer, 0, kAtomPortBytes);
                // Respect atom:bufferType when declaring the header type.
                if (LilvNodes* bt = lilv_port_get_value(plugin, lp, bufferTypeClass)) {
                    if (const LilvNode* b = lilv_nodes_get_first(bt)) {
                        if (lilv_node_is_uri(b)) {
                            p.atomType = uridMap->map(std::string(lilv_node_as_uri(b)));
                        }
                    }
                    lilv_nodes_free(bt);
                }
                if (p.atomType == 0) p.atomType = uridAtomSequence;
                break;
            }
            case PortType::Other:
                // Deprecated/unknown port types: give the plugin a plain float
                // buffer so connect_port() never receives nullptr.
                p.buffer = new float[static_cast<size_t>(maxBlock)];
                std::memset(p.buffer, 0, sizeof(float) * static_cast<size_t>(maxBlock));
                break;
        }

        if (!p.symbol.empty()) symIndex.emplace(p.symbol, i);
        ports.push_back(std::move(p));
        if (allocFailed) break;
    }

    lilv_node_free(audioClass);
    lilv_node_free(controlClass);
    lilv_node_free(atomClass);
    lilv_node_free(cvClass);
    lilv_node_free(inputClass);
    lilv_node_free(propClass);
    lilv_node_free(unitClass);
    lilv_node_free(bufferTypeClass);

    if (allocFailed) return false;

    if (nAudioIn == 0 && nAudioOut == 0) {
        LSPA_LOG_WARN("Lv2Instance: %s has no audio ports (generator or MIDI plugin?)",
                      key.id.c_str());
    }
    return true;
}

void Lv2Instance::Impl::connectPorts() {
    for (Port& p : ports) {
        lilv_instance_connect_port(inst, p.index, p.buffer);
    }
    // Atom ports start as valid empty sequences/atoms so run() never sees an
    // uninitialized header, even before the first clearAtomInput().
    for (Port& p : ports) {
        if (p.type == PortType::Atom) {
            auto* atom = static_cast<LV2_Atom*>(p.buffer);
            atom->size = (p.atomType == uridAtomSequence)
                             ? static_cast<uint32_t>(sizeof(LV2_Atom_Sequence_Body))
                             : 0u;
            atom->type = p.atomType;
        }
    }
}

void Lv2Instance::Impl::applyInputDefaults() {
    // LV2 requires input control ports to be initialized to their declared
    // default before activate().
    for (Port& p : ports) {
        if (p.type != PortType::Control || !p.in) continue;
        float v = p.def;
        if (p.hasRange && p.min <= p.max) v = std::min(std::max(v, p.min), p.max);
        p.value = v;
        *static_cast<float*>(p.buffer) = v;
    }
}

void Lv2Instance::Impl::teardown() {
    // lilv_instance_free() may touch plugin bookkeeping owned by the Lilv
    // world, so serialize with a concurrent scan/instantiate (LilvLock is
    // recursive: a failing instantiate tears down while still holding it).
    LilvLock lilvLock;
    if (inst != nullptr) {
        if (active) {
            lilv_instance_deactivate(inst);
            active = false;
        }
        worker.stop();
        lilv_instance_free(inst);
        inst = nullptr;
    }
    for (Port& p : ports) {
        if (p.buffer == nullptr) continue;
        if (p.type == PortType::Atom) {
            std::free(p.buffer);
        } else {
            delete[] static_cast<float*>(p.buffer);
        }
        p.buffer = nullptr;
    }
    ports.clear();
    symIndex.clear();
    audioInPorts.clear();
    audioOutPorts.clear();
}

void Lv2Instance::Impl::clearAtomInput(const Port& p) const {
    auto* seq = static_cast<LV2_Atom_Sequence*>(p.buffer);
    if (p.atomType == uridAtomSequence) {
        // Empty sequence: size covers only the body header (lv2_atom_sequence_clear
        // semantics); type is re-stated every block in case the plugin clobbered it.
        seq->atom.type = uridAtomSequence;
        seq->atom.size = static_cast<uint32_t>(sizeof(LV2_Atom_Sequence_Body));
        seq->body.unit = 0;
        seq->body.pad = 0;
    } else {
        seq->atom.type = p.atomType;
        seq->atom.size = 0;
        seq->body.unit = 0;
        seq->body.pad = 0;
    }
}

void Lv2Instance::Impl::bypass(float* const* inputs, int nInputs,
                               float* const* outputs, int nOutputs, int n) {
    const int copy = std::min(nInputs, nOutputs);
    for (int c = 0; c < copy; ++c) {
        if (inputs[c] != outputs[c]) {
            std::memcpy(outputs[c], inputs[c], sizeof(float) * static_cast<size_t>(n));
        }
    }
    for (int c = copy; c < nOutputs; ++c) {
        std::memset(outputs[c], 0, sizeof(float) * static_cast<size_t>(n));
    }
}

void Lv2Instance::Impl::run(float* const* inputs, int nInputs,
                            float* const* outputs, int nOutputs, int nFrames) {
    // Re-entrancy guard (lock-free, no allocation: one atomic exchange).
    //
    // The engine can in practice call process() from two threads at once (the
    // offline render path processes on the main thread while the real-time
    // loop is still running). LV2 instances are not reentrant and the port
    // buffers are shared, so a nested call bypasses the plugin instead of
    // corrupting its state (and our port bookkeeping).
    if (inRun.exchange(true, std::memory_order_acq_rel)) {
        bypass(inputs, nInputs, outputs, nOutputs, nFrames);
        return;
    }
    struct RunGuard {
        std::atomic<bool>& flag;
        ~RunGuard() { flag.store(false, std::memory_order_release); }
    } guard{inRun};

    if (!ok || inst == nullptr || nFrames <= 0) return;
    int n = nFrames;
    if (n > maxBlock) n = maxBlock;  // clamp; output tail below stays zeroed

    if (!active) {
        // Defensive: LV2 only allows run() between activate/deactivate.
        bypass(inputs, nInputs, outputs, nOutputs, nFrames);
        return;
    }

    // 1. Audio in -> plugin port buffers. Channels the engine did not provide
    //    get silence; a short block additionally zero-fills the tail promised
    //    by buf-size:fixedBlockLength (min == max == maxBlock).
    for (int c = 0; c < nAudioIn; ++c) {
        float* dst = static_cast<float*>(ports[audioInPorts[static_cast<size_t>(c)]].buffer);
        if (c < nInputs) {
            std::memcpy(dst, inputs[c], sizeof(float) * static_cast<size_t>(n));
            if (n < maxBlock) {
                std::memset(dst + n, 0, sizeof(float) * static_cast<size_t>(maxBlock - n));
            }
        } else {
            std::memset(dst, 0, sizeof(float) * static_cast<size_t>(maxBlock));
        }
    }

    // 2. Valid empty sequence headers on input atom ports, every block.
    for (const Port& p : ports) {
        if (p.type == PortType::Atom && p.in) clearAtomInput(p);
    }

    // 3. Run the plugin (LV2_Descriptor::run via lilv's inline wrapper).
    lilv_instance_run(inst, static_cast<uint32_t>(n));

    // 4. Plugin port buffers -> audio out. If the engine asked for more
    //    channels than the plugin has, the extras are silenced; when the
    //    block was longer than maxBlock the unprocessed tail is zeroed too.
    for (int c = 0; c < nOutputs; ++c) {
        float* dst = outputs[c];
        if (c < nAudioOut) {
            const float* src =
                static_cast<const float*>(ports[audioOutPorts[static_cast<size_t>(c)]].buffer);
            std::memcpy(dst, src, sizeof(float) * static_cast<size_t>(n));
            if (n < nFrames) {
                std::memset(dst + n, 0, sizeof(float) * static_cast<size_t>(nFrames - n));
            }
        } else {
            std::memset(dst, 0, sizeof(float) * static_cast<size_t>(nFrames));
        }
    }

    // 5. Read back output control ports (meters, latency, ...) and push
    //    changes toward the editor (lock-free; drops when the queue is full).
    for (Port& p : ports) {
        if (p.type != PortType::Control || p.in) continue;
        const float v = *static_cast<const float*>(p.buffer);
        p.value = v;
        if (!p.notified || p.notifiedValue != v) {
            p.notified = true;
            p.notifiedValue = v;
            uiQueue.push(p.index, v);
        }
    }
    if (latencyPort != UINT32_MAX && latencyPort < ports.size()) {
        const float lat = ports[latencyPort].value;
        latency = (lat > 0.0f) ? static_cast<int>(std::lround(lat)) : 0;
    }

    // 6. Worker responses first, then end the cycle (same order as jalv).
    if (workerIface != nullptr) {
        worker.emitResponses();
        worker.endRun();
    }
}

// ---------------------------------------------------------------------------
// Lv2Instance (public surface)
// ---------------------------------------------------------------------------

Lv2Instance::Lv2Instance(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Lv2Instance::~Lv2Instance() = default;

const PluginKey& Lv2Instance::key() const { return impl_->key; }
const std::string& Lv2Instance::displayName() const { return impl_->name; }
bool Lv2Instance::isOk() const { return impl_->ok; }
const std::string& Lv2Instance::errorString() const { return impl_->error; }
int Lv2Instance::latencySamples() const { return impl_->latency; }

void Lv2Instance::process(float* const* inputs, int nInputs,
                          float* const* outputs, int nOutputs, int nFrames) {
    impl_->run(inputs, nInputs, outputs, nOutputs, nFrames);
}

void Lv2Instance::activate() {
    Impl& d = *impl_;
    if (!d.ok || d.active) return;
    // Start the worker before activate() so even a plugin that schedules work
    // from activate() finds a running worker (spec forbids it, but be lenient).
    if (d.workerIface != nullptr) d.worker.start(d.workerIface, lilv_instance_get_handle(d.inst));
    lilv_instance_activate(d.inst);
    d.active = true;
}

void Lv2Instance::deactivate() {
    Impl& d = *impl_;
    if (!d.ok || !d.active) return;
    lilv_instance_deactivate(d.inst);
    d.worker.stop();
    d.active = false;
}

std::vector<PluginParam> Lv2Instance::parameters() const {
    std::vector<PluginParam> out;
    for (const Impl::Port& p : impl_->ports) {
        // Only input control ports are parameters; output controls are meters
        // written by the plugin (setParameter on those would be a no-op and
        // their values may legitimately sit outside the advertised range).
        if (p.type != PortType::Control || !p.in) continue;
        PluginParam pm;
        pm.symbol = p.symbol;
        pm.name = p.name;
        pm.min = p.min;
        pm.max = p.max;
        pm.def = p.def;
        pm.value = p.value;
        pm.isInteger = p.isInteger;
        pm.isToggled = p.isToggled;
        pm.unit = p.unit;
        pm.logarithmic = p.logarithmic;
        out.push_back(std::move(pm));
    }
    return out;
}

void Lv2Instance::setParameter(const std::string& symbol, float value) {
    Impl& d = *impl_;
    const auto it = d.symIndex.find(symbol);  // hash lookup: no allocation
    if (it == d.symIndex.end()) return;
    Impl::Port* p = d.portByIndex(static_cast<int>(it->second));
    if (p == nullptr) return;

    float v = value;
    if (p->hasRange && p->min <= p->max) v = std::min(std::max(v, p->min), p->max);
    if (p->isToggled) v = (v >= 0.5f) ? 1.0f : 0.0f;

    if (p->type == PortType::Control && p->in) {
        p->value = v;
        *static_cast<float*>(p->buffer) = v;  // RT-safe: plain float store
        d.uiQueue.push(p->index, v);
    } else if (p->type == PortType::Atom && p->in && p->atomType == d.uridAtomFloat) {
        // Atom-typed float input port: write an atom:Float body.
        auto* af = static_cast<LV2_Atom_Float*>(p->buffer);
        af->atom.size = sizeof(float);
        af->atom.type = d.uridAtomFloat;
        af->body = v;
        p->value = v;
        d.uiQueue.push(p->index, v);
    }
}

float Lv2Instance::getParameter(const std::string& symbol) const {
    const auto it = impl_->symIndex.find(symbol);
    if (it == impl_->symIndex.end()) return 0.0f;
    const Impl::Port* p = impl_->portByIndex(static_cast<int>(it->second));
    return p != nullptr ? p->value : 0.0f;
}

bool Lv2Instance::hasNativeUi() const { return impl_->hasNativeUi; }

int Lv2Instance::numAudioInputs() const { return impl_->nAudioIn; }
int Lv2Instance::numAudioOutputs() const { return impl_->nAudioOut; }

void* Lv2Instance::lilvWorld() const { return impl_->world; }
void* Lv2Instance::lilvPlugin() const {
    return const_cast<void*>(static_cast<const void*>(impl_->plugin));
}
void* Lv2Instance::lilvInstance() const { return impl_->inst; }

void* Lv2Instance::lv2Handle() const {
    return impl_->inst != nullptr ? lilv_instance_get_handle(impl_->inst) : nullptr;
}

UridMap* Lv2Instance::uridMap() const { return impl_->uridMap.get(); }

int Lv2Instance::portCount() const { return static_cast<int>(impl_->ports.size()); }

int Lv2Instance::portIndexBySymbol(const char* symbol) const {
    if (symbol == nullptr) return -1;
    const auto it = impl_->symIndex.find(symbol);
    if (it == impl_->symIndex.end()) return -1;
    return static_cast<int>(it->second);
}

bool Lv2Instance::isControlPort(int index) const {
    const Impl::Port* p = impl_->portByIndex(index);
    return p != nullptr && p->type == PortType::Control;
}

float Lv2Instance::portValue(int index) const {
    const Impl::Port* p = impl_->portByIndex(index);
    return p != nullptr ? p->value : 0.0f;
}

void Lv2Instance::setPortValueFromUi(int index, float value) {
    Impl::Port* p = impl_->portByIndex(index);
    if (p == nullptr) return;
    if (p->type == PortType::Control && p->in) {
        float v = value;
        if (p->hasRange && p->min <= p->max) v = std::min(std::max(v, p->min), p->max);
        p->value = v;
        *static_cast<float*>(p->buffer) = v;
        // No uiQueue push: this value came FROM the editor.
    }
}

void Lv2Instance::notifyUi(int index, float value) {
    impl_->uiQueue.push(static_cast<uint32_t>(index), value);
}

bool Lv2Instance::popUiEvent(int& index, float& value) {
    uint32_t i = 0;
    float v = 0.0f;
    if (!impl_->uiQueue.pop(i, v)) return false;
    index = static_cast<int>(i);
    value = v;
    return true;
}

std::vector<std::pair<int, float>> Lv2Instance::controlSnapshot() const {
    std::vector<std::pair<int, float>> out;
    for (const Impl::Port& p : impl_->ports) {
        if (p.type == PortType::Control) {
            out.emplace_back(static_cast<int>(p.index), p.value);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

std::unique_ptr<IPluginInstance> createLv2Instance(const void* world, const void* plugin,
                                                   std::shared_ptr<UridMap> uridMap,
                                                   double sampleRate, int maxBlock,
                                                   std::string& errorOut) {
    if (world == nullptr || plugin == nullptr) {
        errorOut = "internal error: LV2 world/plugin not provided";
        return nullptr;
    }
    if (sampleRate <= 0.0 || maxBlock <= 0) {
        errorOut = "invalid audio configuration";
        return nullptr;
    }
    if (!uridMap) {
        errorOut = "internal error: missing URID map";
        return nullptr;
    }

    try {
        errorOut.clear();
        auto impl = std::make_unique<Lv2Instance::Impl>();
        impl->world = const_cast<LilvWorld*>(static_cast<const LilvWorld*>(world));
        impl->plugin = static_cast<const LilvPlugin*>(plugin);
        impl->uridMap = std::move(uridMap);
        impl->sampleRate = sampleRate;
        impl->maxBlock = maxBlock;

        const LilvNode* uriNode = lilv_plugin_get_uri(impl->plugin);
        impl->key.format = PluginFormat::Lv2;
        impl->key.id = lilv_node_as_uri(uriNode);

        if (!impl->setup(errorOut)) {
            impl->error = errorOut;
            LSPA_LOG_WARN("Lv2Host: failed to instantiate %s: %s", impl->key.id.c_str(),
                          errorOut.c_str());
            return nullptr;  // ~Impl frees anything that was set up
        }
        return std::unique_ptr<IPluginInstance>(new Lv2Instance(std::move(impl)));
    } catch (const std::exception& e) {
        errorOut = std::string("exception while instantiating: ") + e.what();
        return nullptr;
    } catch (...) {
        errorOut = "unknown exception while instantiating";
        return nullptr;
    }
}

}  // namespace lspa
