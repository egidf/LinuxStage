#include "engine/AudioEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "core/Logger.h"

namespace lspa {

namespace {
constexpr int kTailMs = 400;  // let plugin tails ring out after a player stops
}

AudioEngine::AudioEngine() {
    for (auto& m : meterPeakL_) m.store(0.0f);
    for (auto& m : meterPeakR_) m.store(0.0f);
    for (auto& sample : waveformSamples_) sample.store(0.0f);
}

AudioEngine::~AudioEngine() { stop(); }

Player& AudioEngine::ensurePlayer(PlayerId id) {
    const size_t i = static_cast<size_t>(id);
    if (!players_[i]) {
        players_[i] = std::make_unique<Player>(playerIdName(id), config_.sampleRate,
                                               config_.blockSize);
    }
    return *players_[i];
}

Player& AudioEngine::player(PlayerId id) { return ensurePlayer(id); }

const Player& AudioEngine::player(PlayerId id) const {
    const size_t i = static_cast<size_t>(id);
    if (!players_[i]) {
        // Create on demand (const_cast keeps the API ergonomic; construction
        // is idempotent and guarded by the fact that the UI thread owns it).
        auto* self = const_cast<AudioEngine*>(this);
        return self->ensurePlayer(id);
    }
    return *players_[i];
}

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------

bool AudioEngine::start(const EngineConfig& config, std::string& errorOut) {
    stop();

    config_ = config;
    if (config_.sampleRate <= 0) config_.sampleRate = 48000;
    if (config_.blockSize <= 0) config_.blockSize = 256;

    // (Re)create players for the configured sample rate / block size.
    for (size_t i = 0; i < players_.size(); ++i) {
        players_[i] = std::make_unique<Player>(
            playerIdName(i == 0 ? PlayerId::Main : PlayerId::Bgm),
            config_.sampleRate, config_.blockSize);
    }
    tailBlocksTarget_ = std::max(1, (config_.sampleRate * kTailMs / 1000) / config_.blockSize);

    sink_.reset();
    if (!config_.offline) {
        sink_ = createAlsaSink(config_.device);
        if (!sink_->open(config_.sampleRate, 2, config_.blockSize, errorOut)) {
            LSPA_LOG_WARN("AudioEngine: %s - falling back to the null output device",
                          errorOut.c_str());
            sink_ = createNullSink();
            std::string nullErr;
            if (!sink_->open(config_.sampleRate, 2, config_.blockSize, nullErr)) {
                errorOut = "No usable audio output: " + nullErr;
                sink_.reset();
                return false;
            }
            errorOut.clear();
        }
    }

    tmpL_.assign(static_cast<size_t>(config_.blockSize), 0.0f);
    tmpR_.assign(static_cast<size_t>(config_.blockSize), 0.0f);
    inter_.assign(static_cast<size_t>(config_.blockSize) * 2, 0.0f);

    quit_.store(false);
    running_.store(true);
    if (!config_.offline) {
        rtThread_ = std::thread(&AudioEngine::rtLoop, this);
    }
    housekeepingThread_ = std::thread([this] {
        while (!quit_.load(std::memory_order_relaxed)) {
            drainRetired();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });

    LSPA_LOG_INFO("AudioEngine: started (%d Hz, block %d, sink '%s')",
                  config_.sampleRate, config_.blockSize,
                  config_.offline ? "offline" : sink_->name().c_str());
    return true;
}

void AudioEngine::stop() {
    if (!running_.load() && !rtThread_.joinable() && !housekeepingThread_.joinable()) return;
    quit_.store(true);
    running_.store(false);
    if (rtThread_.joinable()) rtThread_.join();
    if (housekeepingThread_.joinable()) housekeepingThread_.join();
    quit_.store(false);

    if (sink_) {
        sink_->close();
        sink_.reset();
    }

    // Retire all chains on this (non RT) thread.
    for (size_t i = 0; i < 2; ++i) {
        EffectChain* pending = pendingChain_[i].exchange(nullptr);
        if (pending) {
            pending->deactivateAll();
            delete pending;
        }
        EffectChain* cur = currentChain_[i].exchange(nullptr);
        if (cur) {
            cur->deactivateAll();
            delete cur;
        }
    }
    drainRetired();
    LSPA_LOG_INFO("AudioEngine: stopped");
}

// ---------------------------------------------------------------------------
// rack control
// ---------------------------------------------------------------------------

void AudioEngine::setChain(RackTarget target, std::unique_ptr<EffectChain> chain) {
    const size_t i = static_cast<size_t>(target == RackTarget::MainPlayer ? PlayerId::Main
                                                                          : PlayerId::Bgm);
    std::lock_guard<std::mutex> lock(chainMutex_);
    EffectChain* raw = chain.release();
    if (raw) {
        raw->activateAll();  // non RT: plugin activate may allocate
    }
    EffectChain* previousPending = pendingChain_[i].exchange(raw);
    if (previousPending && previousPending != raw) {
        // Superseded before the audio thread picked it up.
        previousPending->deactivateAll();
        delete previousPending;
    }
    if (raw) {
        LSPA_LOG_INFO("AudioEngine: %s rack queued (%zu slots)",
                      rackTargetName(target), raw->slotCount());
    }
}

std::vector<EffectChain::Slot> AudioEngine::chainMetadata(RackTarget target) const {
    const size_t i = static_cast<size_t>(target == RackTarget::MainPlayer ? PlayerId::Main
                                                                          : PlayerId::Bgm);
    // The audio thread only swaps pointers at block boundaries; reading the
    // current chain for metadata is safe as long as the UI thread is the only
    // one calling setChain (it is, and the swap lock above serializes it).
    EffectChain* cur = currentChain_[i].load(std::memory_order_acquire);
    EffectChain* pend = pendingChain_[i].load(std::memory_order_acquire);
    EffectChain* c = pend ? pend : cur;
    std::vector<EffectChain::Slot> out;
    if (!c) return out;
    out.reserve(c->slotCount());
    for (size_t s = 0; s < c->slotCount(); ++s) {
        // Copy the informational parts only (instances are not copyable).
        const EffectChain::Slot& src = c->slot(s);
        EffectChain::Slot dst;
        dst.uid = src.uid;
        dst.key = src.key;
        dst.displayName = src.displayName;
        dst.status = src.status;
        dst.error = src.error;
        dst.audioIns = src.audioIns;
        dst.audioOuts = src.audioOuts;
        dst.enabled.store(src.enabled.load(std::memory_order_relaxed));
        out.push_back(std::move(dst));
    }
    return out;
}

void AudioEngine::setParameter(RackTarget target, const std::string& uid,
                               const std::string& symbol, float value) {
    RtCommand cmd;
    cmd.type = RtCommand::Type::SetParam;
    cmd.target = target;
    std::snprintf(cmd.uid, sizeof(cmd.uid), "%s", uid.c_str());
    std::snprintf(cmd.symbol, sizeof(cmd.symbol), "%s", symbol.c_str());
    cmd.value = value;
    drainCommandsEnqueue(cmd);
}

void AudioEngine::setSlotEnabled(RackTarget target, const std::string& uid, bool enabled) {
    RtCommand cmd;
    cmd.type = RtCommand::Type::SetEnabled;
    cmd.target = target;
    std::snprintf(cmd.uid, sizeof(cmd.uid), "%s", uid.c_str());
    cmd.flag = enabled;
    drainCommandsEnqueue(cmd);
}

void AudioEngine::setMasterVolume(float volume) {
    masterVolume_.store(std::clamp(volume, 0.0f, 4.0f), std::memory_order_relaxed);
}

IPluginInstance* AudioEngine::slotInstance(RackTarget target, const std::string& uid) const {
    const size_t i = static_cast<size_t>(target == RackTarget::MainPlayer ? PlayerId::Main
                                                                          : PlayerId::Bgm);
    EffectChain* cur = currentChain_[i].load(std::memory_order_acquire);
    EffectChain* pend = pendingChain_[i].load(std::memory_order_acquire);
    EffectChain* c = pend ? pend : cur;
    if (!c) return nullptr;
    const int idx = c->indexOfUid(uid);
    if (idx < 0) return nullptr;
    const EffectChain::Slot& slot = c->slot(static_cast<size_t>(idx));
    if (slot.instances.empty()) return nullptr;
    return slot.instances[0].get();
}

// ---------------------------------------------------------------------------
// meters
// ---------------------------------------------------------------------------

EngineMeter AudioEngine::meter(PlayerId id) const {
    const size_t i = static_cast<size_t>(id);
    EngineMeter m;
    m.peakL = meterPeakL_[i].load(std::memory_order_relaxed);
    m.peakR = meterPeakR_[i].load(std::memory_order_relaxed);
    m.clipping = m.peakL > 1.0f || m.peakR > 1.0f;
    return m;
}

EngineMeter AudioEngine::masterMeter() const {
    EngineMeter m;
    m.peakL = masterPeakL_.load(std::memory_order_relaxed);
    m.peakR = masterPeakR_.load(std::memory_order_relaxed);
    m.clipping = masterClipping_.load(std::memory_order_relaxed);
    return m;
}

bool AudioEngine::copyMasterWaveform(
    std::array<float, kWaveformSamples>& samples) const {
    const size_t written = waveformWrite_.load(std::memory_order_acquire);
    const size_t count = std::min(written, kWaveformSamples);
    samples.fill(0.0f);
    const size_t outputOffset = kWaveformSamples - count;
    const size_t sourceStart = written - count;
    for (size_t i = 0; i < count; ++i) {
        samples[outputOffset + i] =
            waveformSamples_[(sourceStart + i) % kWaveformSamples].load(std::memory_order_relaxed);
    }
    return count > 0;
}

std::string AudioEngine::lastError() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return lastError_;
}

std::vector<PluginParam> AudioEngine::slotParameters(RackTarget target,
                                                     const std::string& uid) const {
    const size_t i = static_cast<size_t>(target == RackTarget::MainPlayer ? PlayerId::Main
                                                                          : PlayerId::Bgm);
    EffectChain* cur = currentChain_[i].load(std::memory_order_acquire);
    EffectChain* pend = pendingChain_[i].load(std::memory_order_acquire);
    EffectChain* c = pend ? pend : cur;
    if (!c) return {};
    const int idx = c->indexOfUid(uid);
    if (idx < 0) return {};
    const EffectChain::Slot& slot = c->slot(static_cast<size_t>(idx));
    if (slot.instances.empty() || !slot.instances[0]) return {};
    return slot.instances[0]->parameters();
}

// ---------------------------------------------------------------------------
// real-time processing
// ---------------------------------------------------------------------------

void AudioEngine::drainCommandsEnqueue(const RtCommand& cmd) {
    std::lock_guard<std::mutex> lock(cmdProducerMutex_);
    const size_t w = cmdWrite_.load(std::memory_order_relaxed);
    const size_t r = cmdRead_.load(std::memory_order_acquire);
    if (w - r >= kCmdCapacity) {
        LSPA_LOG_WARN("AudioEngine: command queue full, dropping command");
        return;
    }
    commands_[w % kCmdCapacity] = cmd;
    cmdWrite_.store(w + 1, std::memory_order_release);
}

void AudioEngine::drainCommands() {
    size_t r = cmdRead_.load(std::memory_order_relaxed);
    const size_t w = cmdWrite_.load(std::memory_order_acquire);
    while (r != w) {
        const RtCommand& cmd = commands_[r % kCmdCapacity];
        const size_t i = static_cast<size_t>(
            cmd.target == RackTarget::MainPlayer ? PlayerId::Main : PlayerId::Bgm);
        EffectChain* chain = currentChain_[i].load(std::memory_order_relaxed);
        switch (cmd.type) {
            case RtCommand::Type::SetParam: {
                if (!chain) break;
                const int idx = chain->indexOfUid(cmd.uid);
                if (idx < 0) break;
                EffectChain::Slot& slot = chain->slot(static_cast<size_t>(idx));
                for (auto& inst : slot.instances) {
                    if (inst) inst->setParameter(cmd.symbol, cmd.value);
                }
                break;
            }
            case RtCommand::Type::SetEnabled: {
                if (!chain) break;
                const int idx = chain->indexOfUid(cmd.uid);
                if (idx < 0) break;
                chain->slot(static_cast<size_t>(idx)).enabled.store(cmd.flag);
                break;
            }
            case RtCommand::Type::SetMasterVolume:
                masterVolume_.store(cmd.value);
                break;
            case RtCommand::Type::SetPlayerVolume:
                ensurePlayer(cmd.playerId).setVolume(cmd.value);
                break;
            case RtCommand::Type::None:
                break;
        }
        ++r;
    }
    cmdRead_.store(r, std::memory_order_release);
}

void AudioEngine::swapChains() {
    for (size_t i = 0; i < 2; ++i) {
        EffectChain* pending = pendingChain_[i].load(std::memory_order_acquire);
        if (!pending) continue;
        if (pendingChain_[i].compare_exchange_strong(pending, nullptr)) {
            EffectChain* old = currentChain_[i].exchange(pending, std::memory_order_acq_rel);
            if (old) retireChain(old);
            tailBlocksLeft_[i] = tailBlocksTarget_;
        }
    }
}

void AudioEngine::retireChain(EffectChain* chain) {
    if (!chain) return;
    const size_t w = retireWrite_.load(std::memory_order_relaxed);
    const size_t r = retireRead_.load(std::memory_order_acquire);
    if (w - r >= kRetireCapacity) {
        // Should not happen (UI swaps are rare); free synchronously outside
        // the audio thread would be better, but never leak.
        LSPA_LOG_WARN("AudioEngine: retire queue full, dropping chain");
        return;
    }
    retire_[w % kRetireCapacity] = chain;
    retireWrite_.store(w + 1, std::memory_order_release);
}

void AudioEngine::drainRetired() {
    size_t r = retireRead_.load(std::memory_order_relaxed);
    const size_t w = retireWrite_.load(std::memory_order_acquire);
    while (r != w) {
        EffectChain* chain = retire_[r % kRetireCapacity];
        if (chain) {
            chain->deactivateAll();
            delete chain;
        }
        ++r;
    }
    retireRead_.store(r, std::memory_order_release);
}

void AudioEngine::processBlock(float* mixLeft, float* mixRight, int nFrames) {
    drainCommands();
    swapChains();

    const float master = masterVolume_.load(std::memory_order_relaxed);
    const bool mainHasPriority = players_[static_cast<size_t>(PlayerId::Main)]->isPlaying();
    players_[static_cast<size_t>(PlayerId::Bgm)]->setMainTransportMuted(mainHasPriority);
    float peakMasterL = 0.0f, peakMasterR = 0.0f;

    for (size_t pi = 0; pi < 2; ++pi) {
        Player& p = *players_[pi];
        const bool active = p.isActive();
        EffectChain* chain = currentChain_[pi].load(std::memory_order_relaxed);

        if (!active && tailBlocksLeft_[pi] <= 0) {
            // Decay meter reading.
            meterPeakL_[pi].store(0.0f, std::memory_order_relaxed);
            meterPeakR_[pi].store(0.0f, std::memory_order_relaxed);
            continue;
        }

        if (active) {
            PlayerBlockInfo info;
            p.pull(tmpL_.data(), tmpR_.data(), nFrames, info);
            if (chain) chain->process(tmpL_.data(), tmpR_.data(), nFrames);

            const float g0 = info.rampStart * info.volume;
            const float g1 = info.rampEnd * info.volume;
            const float step = (g1 - g0) / static_cast<float>(nFrames);
            float g = g0;
            float peakL = 0.0f, peakR = 0.0f;
            for (int i = 0; i < nFrames; ++i) {
                const float gl = tmpL_[static_cast<size_t>(i)] * g;
                const float gr = tmpR_[static_cast<size_t>(i)] * g;
                mixLeft[i] += gl;
                mixRight[i] += gr;
                peakL = std::max(peakL, std::fabs(gl));
                peakR = std::max(peakR, std::fabs(gr));
                g += step;
            }
            // Fast meter release so the UI stays readable.
            const float prevL = meterPeakL_[pi].load(std::memory_order_relaxed);
            const float prevR = meterPeakR_[pi].load(std::memory_order_relaxed);
            meterPeakL_[pi].store(std::max(peakL, prevL * 0.85f), std::memory_order_relaxed);
            meterPeakR_[pi].store(std::max(peakR, prevR * 0.85f), std::memory_order_relaxed);
            tailBlocksLeft_[pi] = tailBlocksTarget_;
        } else if (tailBlocksLeft_[pi] > 0) {
            // Feed silence to the rack so reverb/compressor tails decay.
            std::fill_n(tmpL_.data(), nFrames, 0.0f);
            std::fill_n(tmpR_.data(), nFrames, 0.0f);
            if (chain && chain->hasActiveSlots()) {
                chain->process(tmpL_.data(), tmpR_.data(), nFrames);
            }
            --tailBlocksLeft_[pi];
        }
    }

    float peakL = 0.0f, peakR = 0.0f;
    bool clipping = false;
    const SoundPreset preset = soundPreset();
    for (int i = 0; i < nFrames; ++i) {
        float dryL = mixLeft[i];
        float dryR = mixRight[i];
        presetLowL_ += 0.018f * (dryL - presetLowL_);
        presetLowR_ += 0.018f * (dryR - presetLowR_);
        presetHighL_ += 0.24f * (dryL - presetHighL_);
        presetHighR_ += 0.24f * (dryR - presetHighR_);
        const float mid = (dryL + dryR) * 0.5f;
        const float side = (dryL - dryR) * 0.5f;
        switch (preset) {
            case SoundPreset::Universal:
                break;
            case SoundPreset::Clarity:
                dryL = dryL - presetLowL_ * 0.22f + (dryL - presetHighL_) * 0.16f;
                dryR = dryR - presetLowR_ * 0.22f + (dryR - presetHighR_) * 0.16f;
                break;
            case SoundPreset::Tape:
                dryL = std::tanh(dryL * 1.22f) / std::tanh(1.22f);
                dryR = std::tanh(dryR * 1.22f) / std::tanh(1.22f);
                break;
            case SoundPreset::Cinematic:
                dryL += presetLowL_ * 0.20f + (dryL - presetHighL_) * 0.08f;
                dryR += presetLowR_ * 0.20f + (dryR - presetHighR_) * 0.08f;
                break;
            case SoundPreset::DolbySurround:
                dryL = mid + side * 1.28f;
                dryR = mid - side * 1.28f;
                break;
            case SoundPreset::Sony360RealityAudio:
                dryL = mid + side * 1.48f + presetLowR_ * 0.035f;
                dryR = mid - side * 1.48f + presetLowL_ * 0.035f;
                break;
        }
        float l = dryL * master;
        float r = dryR * master;
        if (l > 1.0f || l < -1.0f || r > 1.0f || r < -1.0f) clipping = true;
        peakL = std::max(peakL, std::fabs(l));
        peakR = std::max(peakR, std::fabs(r));
        mixLeft[i] = l;
        mixRight[i] = r;
    }
    const size_t waveformStart = waveformWrite_.load(std::memory_order_relaxed);
    for (int i = 0; i < nFrames; ++i) {
        waveformSamples_[(waveformStart + static_cast<size_t>(i)) % kWaveformSamples].store(
            (mixLeft[i] + mixRight[i]) * 0.5f, std::memory_order_relaxed);
    }
    waveformWrite_.store(waveformStart + static_cast<size_t>(nFrames),
                          std::memory_order_release);
    const float prevML = masterPeakL_.load(std::memory_order_relaxed);
    const float prevMR = masterPeakR_.load(std::memory_order_relaxed);
    masterPeakL_.store(std::max(peakL, prevML * 0.85f), std::memory_order_relaxed);
    masterPeakR_.store(std::max(peakR, prevMR * 0.85f), std::memory_order_relaxed);
    if (clipping) masterClipping_.store(true, std::memory_order_relaxed);
}

void AudioEngine::rtLoop() {
    const double periodSec = static_cast<double>(config_.blockSize) / config_.sampleRate;
    using Clock = std::chrono::steady_clock;

    while (running_.load(std::memory_order_relaxed)) {
        const auto t0 = Clock::now();

        std::fill_n(tmpL_.data(), config_.blockSize, 0.0f);
        std::fill_n(tmpR_.data(), config_.blockSize, 0.0f);
        processBlock(tmpL_.data(), tmpR_.data(), config_.blockSize);

        for (int i = 0; i < config_.blockSize; ++i) {
            inter_[static_cast<size_t>(i) * 2 + 0] = tmpL_[static_cast<size_t>(i)];
            inter_[static_cast<size_t>(i) * 2 + 1] = tmpR_[static_cast<size_t>(i)];
        }

        // Measure compute only: sink_->write() typically blocks until the
        // device has room, which is pacing wait, not CPU load.
        const auto t1 = Clock::now();
        cpu_.push(std::chrono::duration<double>(t1 - t0).count(), periodSec);

        if (!sink_->write(inter_.data(), config_.blockSize)) {
            std::lock_guard<std::mutex> lock(errorMutex_);
            lastError_ = "Audio device error on sink '" + sink_->name() +
                         "'; playback stopped.";
            LSPA_LOG_ERROR("%s", lastError_.c_str());
            running_.store(false);
            break;
        }
    }
}

bool AudioEngine::renderOffline(double seconds, IAudioSink& sink, std::string& errorOut) {
    if (seconds <= 0.0) {
        errorOut = "Render duration must be positive";
        return false;
    }
    if (!sink.open(config_.sampleRate, 2, config_.blockSize, errorOut)) return false;

    const int64_t totalFrames = static_cast<int64_t>(seconds * config_.sampleRate);
    std::vector<float> l(static_cast<size_t>(config_.blockSize));
    std::vector<float> r(static_cast<size_t>(config_.blockSize));
    std::vector<float> inter(static_cast<size_t>(config_.blockSize) * 2);

    int64_t done = 0;
    uint64_t lastUnderruns = players_[0]->underrunCount() + players_[1]->underrunCount();
    while (done < totalFrames) {
        const int n = static_cast<int>(
            std::min<int64_t>(config_.blockSize, totalFrames - done));
        std::fill_n(l.data(), n, 0.0f);
        std::fill_n(r.data(), n, 0.0f);
        processBlock(l.data(), r.data(), n);
        for (int i = 0; i < n; ++i) {
            inter[static_cast<size_t>(i) * 2 + 0] = l[static_cast<size_t>(i)];
            inter[static_cast<size_t>(i) * 2 + 1] = r[static_cast<size_t>(i)];
        }
        if (!sink.write(inter.data(), n)) {
            errorOut = "Sink write failed during offline render";
            return false;
        }
        done += n;

        // Offline rendering is much faster than real time; give the feeder
        // threads a chance to refill their ring buffers when we outrun them.
        const uint64_t underruns =
            players_[0]->underrunCount() + players_[1]->underrunCount();
        if (underruns != lastUnderruns) {
            lastUnderruns = underruns;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    sink.close();
    return true;
}

}  // namespace lspa
