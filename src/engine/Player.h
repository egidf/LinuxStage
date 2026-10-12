// Player.h - one playback lane (Main or BGM).
//
// Threading model
// ---------------
//  * UI / control thread:  play/pause/stop/next/prev/seek/setVolume/...
//  * Feeder thread:        owns the decoder, reads the file from disk and
//                          feeds a lock-free ring buffer. Never blocks the
//                          audio thread.
//  * Audio thread:         pull() reads decoded audio from the ring, runs the
//                          fade/state machine and hands back a gain ramp.
//                          No locks, no allocation, no logging.
//
// Signal order (documented, see PRD 8.3):
//   decoder -> rack (EffectChain) -> player fade/volume -> master mix.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/Types.h"
#include "engine/Decoder.h"
#include "engine/RingBuffer.h"

namespace lspa {

enum class PlayerState {
    Stopped = 0,
    Playing,
    Paused
};

inline const char* playerStateName(PlayerState s) {
    switch (s) {
        case PlayerState::Stopped: return "Stopped";
        case PlayerState::Playing: return "Playing";
        case PlayerState::Paused: return "Paused";
    }
    return "?";
}

// Per-block output of the player's fade/level stage.
struct PlayerBlockInfo {
    float rampStart = 1.0f;  // gain at first frame of the block
    float rampEnd = 1.0f;    // gain at last frame of the block
    float volume = 1.0f;     // user volume (already includes mute = 0)
    bool muted = false;
};

struct PlayerSnapshot {
    PlayerState state = PlayerState::Stopped;
    int64_t positionFrames = 0;
    int64_t totalFrames = 0;
    int trackIndex = -1;
    double positionSec = 0.0;
    double durationSec = 0.0;
    float gain = 0.0f;
    bool ended = false;  // reached end of last playlist item
};

class Player {
public:
    Player(std::string name, int sampleRate, int maxBlock);
    ~Player();

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    const std::string& name() const { return name_; }
    int sampleRate() const { return sampleRate_; }

    // --- configuration (any non-audio thread) ---
    void setPlaylist(std::vector<PlaylistItem> items);
    // Non-disruptive playlist edits (do not touch playback state).
    void appendItems(std::vector<PlaylistItem> items);
    void removeItems(const std::vector<int>& indices);
    void clearPlaylist();
    std::vector<PlaylistItem> playlist() const;
    void setVolume(float volume);
    float volume() const;
    void setMuted(bool muted);
    bool muted() const;
    void setFadeConfig(const FadeConfig& fade);
    FadeConfig fadeConfig() const;

    // --- transport (UI thread) ---
    void play();
    // Start a specific playlist entry (also selects it).
    void playIndex(int index);
    // Select a playlist entry without starting playback.
    void selectIndex(int index);
    void pause();
    void stop();
    void next();
    void prev();
    // Returns false when the player has no current track.
    bool seekSeconds(double seconds);

    // Called from the feeder thread whenever the current track changes.
    void setTrackChangeCallback(std::function<void(int)> cb);

    // --- audio thread ---
    // Fill planar stereo buffers and receive the fade/volume ramp to apply
    // after the rack. Safe to call even when stopped (runs the state machine).
    void pull(float* left, float* right, int nFrames, PlayerBlockInfo& out);

    // True while the player still has audio to give (or may resume soon).
    bool isActive() const;

    // Number of times the audio thread found the ring empty while playing.
    uint64_t underrunCount() const { return underruns_.load(std::memory_order_relaxed); }

    // --- queries (any thread) ---
    PlayerSnapshot snapshot() const;
    std::string currentPath() const;

    // Re-open the current track from the start (used when the file changed).
    void reloadCurrentTrack();

private:
    enum class Action : int {
        None = 0,
        Stop,
        Pause,
        Switch  // fade out, then switch to switchIndex_
    };

    void feederLoop();
    void openTrackLocked(int index, bool flush);  // feeder thread only
    void requestAction(Action a);
    void wakeFeeder();

    std::string name_;
    int sampleRate_;
    int maxBlock_;

    // --- playlist (guarded by playlistMutex_) ---
    mutable std::mutex playlistMutex_;
    std::vector<PlaylistItem> playlist_;
    int currentIndex_ = -1;

    // --- feeder-owned decoder state ---
    std::unique_ptr<IDecoder> decoder_;

    // --- ring buffer (feeder = producer, audio = consumer) ---
    FloatRing ring_;
    std::vector<float> feederScratch_;  // interleaved stereo

    // --- feeder thread control ---
    std::thread feeder_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> feederWake_{false};
    std::mutex wakeMutex_;
    std::condition_variable wakeCv_;
    std::atomic<int> pendingSeekFrame_{-1};   // >=0: seek request
    std::atomic<bool> reopenCurrent_{false};
    std::atomic<int> switchToIndex_{-1};      // feeder: switch decoder here
    std::atomic<bool> switchFlush_{false};

    // --- state shared with the audio thread ---
    std::atomic<int> state_{static_cast<int>(PlayerState::Stopped)};
    std::atomic<int> pendingAction_{static_cast<int>(Action::None)};
    std::atomic<int> switchIndex_{-1};
    std::atomic<float> gain_{0.0f};
    std::atomic<float> targetGain_{0.0f};
    std::atomic<bool> fadeInTrigger_{false};
    std::atomic<int64_t> positionFrames_{0};
    std::atomic<int64_t> totalFrames_{0};
    std::atomic<float> volume_{1.0f};
    std::atomic<bool> muted_{false};
    std::atomic<bool> eof_{false};
    std::atomic<bool> ended_{false};
    std::atomic<uint64_t> underruns_{0};
    std::atomic<uint64_t> trackSerial_{0};  // bumped on every track switch

    // fade config (guarded by fadeMutex_, read at block rate only)
    mutable std::mutex fadeMutex_;
    FadeConfig fade_{};

    std::function<void(int)> trackChangeCallback_;
    std::mutex cbMutex_;
};

}  // namespace lspa
