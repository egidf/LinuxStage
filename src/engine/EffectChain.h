// EffectChain.h - one ordered rack of plugin instances (RT-safe processing).
//
// Ownership model:
//  - A chain is built off-thread (instantiating plugins), then handed to the
//    audio engine, which swaps it in atomically at a block boundary.
//  - The audio thread never allocates, never locks and never logs.

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "core/Types.h"
#include "plugins/IPluginHost.h"

namespace lspa {

class EffectChain {
public:
    struct Slot {
        std::string uid;
        PluginKey key;
        std::string displayName;
        // One instance for stereo plugins, two for mono plugins (L and R).
        std::vector<std::unique_ptr<IPluginInstance>> instances;
        PluginLoadStatus status = PluginLoadStatus::NotLoaded;
        std::string error;
        std::atomic<bool> enabled{true};
        int audioIns = 2;
        int audioOuts = 2;

        bool isLoaded() const { return status == PluginLoadStatus::Loaded; }
        bool processActive() const {
            return enabled.load(std::memory_order_relaxed) && isLoaded() && !instances.empty();
        }

        // std::atomic is not movable, so Slot provides explicit moves.
        Slot() = default;
        Slot(Slot&& o) noexcept
            : uid(std::move(o.uid)),
              key(std::move(o.key)),
              displayName(std::move(o.displayName)),
              instances(std::move(o.instances)),
              status(o.status),
              error(std::move(o.error)),
              enabled(o.enabled.load(std::memory_order_relaxed)),
              audioIns(o.audioIns),
              audioOuts(o.audioOuts) {}
        Slot& operator=(Slot&& o) noexcept {
            if (this != &o) {
                uid = std::move(o.uid);
                key = std::move(o.key);
                displayName = std::move(o.displayName);
                instances = std::move(o.instances);
                status = o.status;
                error = std::move(o.error);
                enabled.store(o.enabled.load(std::memory_order_relaxed));
                audioIns = o.audioIns;
                audioOuts = o.audioOuts;
            }
            return *this;
        }
        Slot(const Slot&) = delete;
        Slot& operator=(const Slot&) = delete;
    };

    explicit EffectChain(int maxBlock);
    ~EffectChain();

    EffectChain(const EffectChain&) = delete;
    EffectChain& operator=(const EffectChain&) = delete;

    // --- build side (non real-time) ---
    // Takes ownership of the (already instantiated) instances in the slot.
    void addSlot(Slot slot);
    size_t slotCount() const { return slots_.size(); }
    const Slot& slot(size_t index) const { return slots_[index]; }
    Slot& slot(size_t index) { return slots_[index]; }

    // Find slot index by uid; returns -1 if not found.
    int indexOfUid(const std::string& uid) const;

    void activateAll();
    void deactivateAll();

    // --- real-time side ---
    // In-place stereo processing of nFrames (nFrames <= maxBlock()).
    void process(float* left, float* right, int nFrames);

    int maxBlock() const { return maxBlock_; }

    // True if at least one slot will process audio.
    bool hasActiveSlots() const;

private:
    int maxBlock_;
    std::vector<Slot> slots_;
    // Ping-pong scratch, planar: [set][channel][frame]
    std::vector<float> scratchA_[2];
    std::vector<float> scratchB_[2];
};

}  // namespace lspa
