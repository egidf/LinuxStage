#include "engine/EffectChain.h"

#include <algorithm>
#include <cstring>

#include "core/Logger.h"

namespace lspa {

EffectChain::EffectChain(int maxBlock) : maxBlock_(maxBlock > 0 ? maxBlock : 256) {
    for (auto& b : scratchA_) b.resize(static_cast<size_t>(maxBlock_), 0.0f);
    for (auto& b : scratchB_) b.resize(static_cast<size_t>(maxBlock_), 0.0f);
}

EffectChain::~EffectChain() {
    // Instances are freed here. This destructor must only ever run on a
    // non-real-time thread (the engine retires chains to a housekeeping
    // thread for exactly that reason).
    for (auto& s : slots_) s.instances.clear();
    slots_.clear();
}

void EffectChain::addSlot(Slot slot) {
    slots_.push_back(std::move(slot));
}

int EffectChain::indexOfUid(const std::string& uid) const {
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].uid == uid) return static_cast<int>(i);
    }
    return -1;
}

void EffectChain::activateAll() {
    for (auto& s : slots_) {
        for (auto& inst : s.instances) {
            if (inst) inst->activate();
        }
    }
}

void EffectChain::deactivateAll() {
    for (auto& s : slots_) {
        for (auto& inst : s.instances) {
            if (inst) inst->deactivate();
        }
    }
}

bool EffectChain::hasActiveSlots() const {
    for (const auto& s : slots_) {
        if (s.processActive()) return true;
    }
    return false;
}

void EffectChain::process(float* left, float* right, int nFrames) {
    if (slots_.empty() || nFrames <= 0) return;
    if (nFrames > maxBlock_) nFrames = maxBlock_;

    float* curL = left;
    float* curR = right;
    bool useA = true;

    for (auto& s : slots_) {
        if (!s.processActive()) continue;

        float* outL = useA ? scratchA_[0].data() : scratchB_[0].data();
        float* outR = useA ? scratchA_[1].data() : scratchB_[1].data();

        const size_t n = s.instances.size();
        if (n == 1) {
            // Stereo (or wider) plugin: one instance handles both channels.
            IPluginInstance* inst = s.instances[0].get();
            float* ins[2] = {curL, curR};
            float* outs[2] = {outL, outR};
            inst->process(ins, 2, outs, 2, nFrames);
        } else if (n >= 2) {
            // Mono plugin: separate instance per channel to keep state sane.
            float* inL[1] = {curL};
            float* outLp[1] = {outL};
            s.instances[0]->process(inL, 1, outLp, 1, nFrames);
            float* inR[1] = {curR};
            float* outRp[1] = {outR};
            s.instances[1]->process(inR, 1, outRp, 1, nFrames);
        } else {
            continue;  // broken slot: pass through
        }

        curL = outL;
        curR = outR;
        useA = !useA;
    }

    // If the chain ended on a scratch buffer, copy back to the caller's
    // buffers so processing is always in-place overall.
    if (curL != left) {
        std::memcpy(left, curL, static_cast<size_t>(nFrames) * sizeof(float));
        std::memcpy(right, curR, static_cast<size_t>(nFrames) * sizeof(float));
    }
}

}  // namespace lspa
