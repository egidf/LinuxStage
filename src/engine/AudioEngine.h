// AudioEngine.h - mixes both players, runs their racks and feeds the sink.
//
// Thread model: one real-time thread owns processBlock() and the sink write.
// Structure changes (rack swaps, parameter changes) are handed over through a
// fixed-size command queue and an atomic pending-chain slot, so the audio
// thread never locks, allocates or logs.

#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "core/Types.h"
#include "engine/AudioSink.h"
#include "engine/CpuLoad.h"
#include "engine/EffectChain.h"
#include "engine/Player.h"

namespace lspa {

enum class PlayerId { Main = 0, Bgm = 1 };

inline const char* playerIdName(PlayerId id) {
    return id == PlayerId::Main ? "Main" : "BGM";
}

struct EngineConfig {
    int sampleRate = 48000;
    int blockSize = 256;
    std::string device = "default";  // ALSA device, "null", or wav path via render mode
    // Offline mode: no audio device and no real-time thread; processBlock() is
    // driven by renderOffline() (used by tests and the --render CLI mode).
    bool offline = false;
};

struct EngineMeter {
    float peakL = 0.0f;
    float peakR = 0.0f;
    bool clipping = false;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // --- lifecycle ---
    bool start(const EngineConfig& config, std::string& errorOut);
    void stop();
    bool isRunning() const { return running_.load(std::memory_order_relaxed); }
    const EngineConfig& config() const { return config_; }

    // --- players ---
    Player& player(PlayerId id);
    const Player& player(PlayerId id) const;

    // --- master ---
    void setMasterVolume(float volume);
    float masterVolume() const { return masterVolume_.load(std::memory_order_relaxed); }

    // --- rack (must be called from a non audio thread) ---
    // Hands the chain to the engine; it is activated here and swapped in at
    // the next block boundary. Ownership transfers to the engine.
    void setChain(RackTarget target, std::unique_ptr<EffectChain> chain);
    // Snapshot of the live chain for UI display (only safe while the UI thread
    // holds a chain-swap lock; use metadata() instead for normal UI reads).
    std::vector<EffectChain::Slot> chainMetadata(RackTarget target) const;

    // Parameter/enable changes are routed to the audio thread.
    void setParameter(RackTarget target, const std::string& uid,
                      const std::string& symbol, float value);
    void setSlotEnabled(RackTarget target, const std::string& uid, bool enabled);

    // Current parameter values of a live slot (UI thread only; the plugin
    // returns a snapshot, safe to call while audio runs).
    std::vector<PluginParam> slotParameters(RackTarget target, const std::string& uid) const;

    // Live instance pointer for native editor hosting. UI-thread only, and the
    // caller must close any editor before a rack rebuild (the app guarantees
    // this by closing editors on rackChanged).
    IPluginInstance* slotInstance(RackTarget target, const std::string& uid) const;

    // --- meters / diagnostics ---
    EngineMeter meter(PlayerId id) const;
    EngineMeter masterMeter() const;
    float cpuLoad() const { return cpu_.value(); }
    std::string lastError() const;
    int64_t xrunCount() const { return xruns_.load(std::memory_order_relaxed); }

    // --- processing (also used by offline render / tests) ---
    // Fills the planar master buses (nFrames <= blockSize()).
    void processBlock(float* mixLeft, float* mixRight, int nFrames);

    // Offline render into a sink (no audio thread). Used for tests and the
    // --render CLI mode.
    bool renderOffline(double seconds, IAudioSink& sink, std::string& errorOut);

private:
    struct RtCommand {
        enum class Type : int { None = 0, SetParam, SetEnabled, SetMasterVolume, SetPlayerVolume };
        Type type = Type::None;
        RackTarget target = RackTarget::MainPlayer;
        PlayerId playerId = PlayerId::Main;
        char uid[64] = {0};
        char symbol[64] = {0};
        float value = 0.0f;
        bool flag = false;
    };

    void rtLoop();
    void drainCommands();
    void drainCommandsEnqueue(const RtCommand& cmd);
    void swapChains();
    void retireChain(EffectChain* chain);
    void drainRetired();

    static constexpr size_t kCmdCapacity = 256;
    static constexpr size_t kRetireCapacity = 128;

    EngineConfig config_;

    // Created lazily (or in start()) so the sample rate / block size are known.
    std::array<std::unique_ptr<Player>, 2> players_;
    Player& ensurePlayer(PlayerId id);
    std::array<std::atomic<EffectChain*>, 2> currentChain_{nullptr, nullptr};
    std::array<std::atomic<EffectChain*>, 2> pendingChain_{nullptr, nullptr};
    mutable std::mutex chainMutex_;  // serializes setChain from the UI thread
    std::array<int, 2> tailBlocksLeft_{0, 0};
    int tailBlocksTarget_ = 0;

    // command queue (producer: UI thread(s) with mutex; consumer: audio thread)
    std::array<RtCommand, kCmdCapacity> commands_{};
    std::atomic<size_t> cmdWrite_{0};
    std::atomic<size_t> cmdRead_{0};
    std::mutex cmdProducerMutex_;

    // retired chains, deleted by the housekeeping thread
    std::array<EffectChain*, kRetireCapacity> retire_{};
    std::atomic<size_t> retireWrite_{0};
    std::atomic<size_t> retireRead_{0};

    std::atomic<float> masterVolume_{1.0f};
    std::array<std::atomic<float>, 2> meterPeakL_{};
    std::array<std::atomic<float>, 2> meterPeakR_{};
    std::atomic<float> masterPeakL_{0.0f};
    std::atomic<float> masterPeakR_{0.0f};
    std::atomic<bool> masterClipping_{false};
    std::atomic<int64_t> xruns_{0};

    CpuLoad cpu_;
    std::atomic<bool> running_{false};
    std::atomic<bool> quit_{false};
    std::string lastError_;
    mutable std::mutex errorMutex_;

    std::unique_ptr<IAudioSink> sink_;
    std::thread rtThread_;
    std::thread housekeepingThread_;

    // scratch (audio thread only)
    std::vector<float> tmpL_;
    std::vector<float> tmpR_;
    std::vector<float> inter_;
};

}  // namespace lspa
