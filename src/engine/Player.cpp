#include "engine/Player.h"

#include <algorithm>
#include <cmath>

#include "core/Logger.h"
#include "core/Paths.h"

namespace lspa {

namespace {
constexpr size_t kRingSeconds = 3;
constexpr size_t kFeederChunkFrames = 2048;
constexpr int kFeederPollMs = 10;
}  // namespace

Player::Player(std::string name, int sampleRate, int maxBlock)
    : name_(std::move(name)),
      sampleRate_(sampleRate > 0 ? sampleRate : 48000),
      maxBlock_(maxBlock > 0 ? maxBlock : 256),
      ring_(nextPow2(sampleRate_ * kRingSeconds * 2 + 1) - 1) {
    feederScratch_.resize(kFeederChunkFrames * 2);
    quit_.store(false);
    feeder_ = std::thread(&Player::feederLoop, this);
}

Player::~Player() {
    quit_.store(true);
    wakeFeeder();
    if (feeder_.joinable()) feeder_.join();
}

// ---------------------------------------------------------------------------
// configuration
// ---------------------------------------------------------------------------

void Player::setPlaylist(std::vector<PlaylistItem> items) {
    {
        std::lock_guard<std::mutex> lock(playlistMutex_);
        playlist_ = std::move(items);
        if (currentIndex_ >= static_cast<int>(playlist_.size())) {
            currentIndex_ = playlist_.empty() ? -1 : 0;
        }
    }
    stop();
    reloadCurrentTrack();
}

std::vector<PlaylistItem> Player::playlist() const {
    std::lock_guard<std::mutex> lock(playlistMutex_);
    return playlist_;
}

void Player::appendItems(std::vector<PlaylistItem> items) {
    std::lock_guard<std::mutex> lock(playlistMutex_);
    if (playlist_.empty()) currentIndex_ = 0;
    for (auto& it : items) playlist_.push_back(std::move(it));
    LSPA_LOG_DEBUG("Player %s: playlist now has %zu items", name_.c_str(), playlist_.size());
}

void Player::removeItems(const std::vector<int>& indices) {
    if (indices.empty()) return;
    bool currentChanged = false;
    {
        std::lock_guard<std::mutex> lock(playlistMutex_);
        std::vector<int> sorted = indices;
        std::sort(sorted.begin(), sorted.end());
        sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
        for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
            const int idx = *it;
            if (idx < 0 || idx >= static_cast<int>(playlist_.size())) continue;
            playlist_.erase(playlist_.begin() + idx);
            if (idx == currentIndex_) {
                currentChanged = true;
            } else if (idx < currentIndex_) {
                --currentIndex_;
            }
        }
        if (playlist_.empty()) currentIndex_ = -1;
        else if (currentIndex_ >= static_cast<int>(playlist_.size())) {
            currentIndex_ = static_cast<int>(playlist_.size()) - 1;
        }
    }
    if (currentChanged) {
        stop();
        reloadCurrentTrack();
    }
    LSPA_LOG_DEBUG("Player %s: items removed, %zu left", name_.c_str(),
                   playlist().size());
}

void Player::clearPlaylist() {
    {
        std::lock_guard<std::mutex> lock(playlistMutex_);
        playlist_.clear();
        currentIndex_ = -1;
    }
    stop();
    totalFrames_.store(0);
    positionFrames_.store(0);
    eof_.store(false);
    ended_.store(false);
    reopenCurrent_.store(true);
    wakeFeeder();
    LSPA_LOG_DEBUG("Player %s: playlist cleared", name_.c_str());
}

void Player::setVolume(float volume) {
    volume_.store(std::clamp(volume, 0.0f, 4.0f), std::memory_order_relaxed);
}

float Player::volume() const { return volume_.load(std::memory_order_relaxed); }

void Player::setMuted(bool muted) { muted_.store(muted, std::memory_order_relaxed); }

bool Player::muted() const { return muted_.load(std::memory_order_relaxed); }

void Player::setFadeConfig(const FadeConfig& fade) {
    std::lock_guard<std::mutex> lock(fadeMutex_);
    fade_ = fade;
}

FadeConfig Player::fadeConfig() const {
    std::lock_guard<std::mutex> lock(fadeMutex_);
    return fade_;
}

void Player::setTrackChangeCallback(std::function<void(int)> cb) {
    std::lock_guard<std::mutex> lock(cbMutex_);
    trackChangeCallback_ = std::move(cb);
}

void Player::reloadCurrentTrack() {
    reopenCurrent_.store(true);
    wakeFeeder();
}

// ---------------------------------------------------------------------------
// transport (UI thread)
// ---------------------------------------------------------------------------

void Player::play() {
    int index = -1;
    size_t count = 0;
    {
        std::lock_guard<std::mutex> lock(playlistMutex_);
        count = playlist_.size();
        if (count == 0) return;
        if (currentIndex_ < 0) currentIndex_ = 0;
        index = currentIndex_;
    }
    if (index < 0 || index >= static_cast<int>(count)) return;

    const int st = state_.load();
    if (st == static_cast<int>(PlayerState::Playing)) return;

    if (st == static_cast<int>(PlayerState::Paused)) {
        // Resume: buffered audio is still in the ring.
        ended_.store(false);
        state_.store(static_cast<int>(PlayerState::Playing));
        fadeInTrigger_.store(true);
        wakeFeeder();
        LSPA_LOG_DEBUG("Player %s: resume at %.2fs", name_.c_str(),
                       positionFrames_.load() / double(sampleRate_));
        return;
    }

    // Start from stopped.
    ended_.store(false);
    eof_.store(false);
    positionFrames_.store(0);
    switchIndex_.store(index);
    switchToIndex_.store(index);
    state_.store(static_cast<int>(PlayerState::Playing));
    fadeInTrigger_.store(true);
    wakeFeeder();
    LSPA_LOG_INFO("Player %s: play track %d", name_.c_str(), index);
}

void Player::playIndex(int index) {
    {
        std::lock_guard<std::mutex> lock(playlistMutex_);
        if (index < 0 || index >= static_cast<int>(playlist_.size())) return;
        currentIndex_ = index;
    }
    ended_.store(false);
    eof_.store(false);
    if (state_.load() == static_cast<int>(PlayerState::Playing)) {
        switchIndex_.store(index);
        requestAction(Action::Switch);
    } else {
        positionFrames_.store(0);
        switchToIndex_.store(index);
        state_.store(static_cast<int>(PlayerState::Playing));
        fadeInTrigger_.store(true);
        wakeFeeder();
    }
    LSPA_LOG_INFO("Player %s: play track %d", name_.c_str(), index);
}

void Player::selectIndex(int index) {
    {
        std::lock_guard<std::mutex> lock(playlistMutex_);
        if (index < 0 || index >= static_cast<int>(playlist_.size())) return;
        currentIndex_ = index;
    }
    positionFrames_.store(0);
    switchToIndex_.store(index);
    ended_.store(false);
    eof_.store(false);
    wakeFeeder();
}

void Player::pause() {
    if (state_.load() != static_cast<int>(PlayerState::Playing)) return;
    const FadeConfig fade = fadeConfig();
    if (fade.fadeOutMs > 1.0) {
        requestAction(Action::Pause);
    } else {
        state_.store(static_cast<int>(PlayerState::Paused));
        LSPA_LOG_DEBUG("Player %s: pause", name_.c_str());
    }
}

void Player::stop() {
    if (state_.load() == static_cast<int>(PlayerState::Stopped)) return;
    FadeConfig fade = fadeConfig();
    if (fade.applyOnStop && fade.fadeOutMs > 1.0) {
        requestAction(Action::Stop);
    } else {
        state_.store(static_cast<int>(PlayerState::Stopped));
        positionFrames_.store(0);
        ended_.store(false);
        eof_.store(false);
        ring_.producerFlush();
        wakeFeeder();
        LSPA_LOG_DEBUG("Player %s: stop", name_.c_str());
    }
}

void Player::next() {
    int target = -1;
    int count = 0;
    {
        std::lock_guard<std::mutex> lock(playlistMutex_);
        count = static_cast<int>(playlist_.size());
        if (count == 0) return;
        target = (currentIndex_ + 1) % count;
    }
    if (state_.load() == static_cast<int>(PlayerState::Playing)) {
        switchIndex_.store(target);
        requestAction(Action::Switch);
    } else {
        {
            std::lock_guard<std::mutex> lock(playlistMutex_);
            currentIndex_ = target;
        }
        positionFrames_.store(0);
        switchToIndex_.store(target);
        ended_.store(false);
        eof_.store(false);
        wakeFeeder();
    }
    LSPA_LOG_DEBUG("Player %s: next -> %d", name_.c_str(), target);
}

void Player::prev() {
    int target = -1;
    int count = 0;
    {
        std::lock_guard<std::mutex> lock(playlistMutex_);
        count = static_cast<int>(playlist_.size());
        if (count == 0) return;
        target = (currentIndex_ - 1 + count) % count;
    }
    if (state_.load() == static_cast<int>(PlayerState::Playing)) {
        switchIndex_.store(target);
        requestAction(Action::Switch);
    } else {
        {
            std::lock_guard<std::mutex> lock(playlistMutex_);
            currentIndex_ = target;
        }
        positionFrames_.store(0);
        switchToIndex_.store(target);
        ended_.store(false);
        eof_.store(false);
        wakeFeeder();
    }
    LSPA_LOG_DEBUG("Player %s: prev -> %d", name_.c_str(), target);
}

bool Player::seekSeconds(double seconds) {
    int64_t frame = static_cast<int64_t>(seconds * sampleRate_);
    if (frame < 0) frame = 0;
    {
        std::lock_guard<std::mutex> lock(playlistMutex_);
        if (currentIndex_ < 0 || playlist_.empty()) return false;
    }
    positionFrames_.store(frame);
    ended_.store(false);
    pendingSeekFrame_.store(static_cast<int>(frame));
    wakeFeeder();
    return true;
}

void Player::requestAction(Action a) {
    pendingAction_.store(static_cast<int>(a));
    // Let the audio thread run its fade immediately.
    wakeFeeder();
}

void Player::wakeFeeder() {
    feederWake_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(wakeMutex_);
    }
    wakeCv_.notify_all();
}

// ---------------------------------------------------------------------------
// feeder thread (owns the decoder / disk I/O)
// ---------------------------------------------------------------------------

void Player::openTrackLocked(int index, bool flush) {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(playlistMutex_);
        if (index < 0 || index >= static_cast<int>(playlist_.size())) return;
        currentIndex_ = index;
        path = playlist_[static_cast<size_t>(index)].path;
        playlist_[static_cast<size_t>(index)].missing = false;
        playlist_[static_cast<size_t>(index)].error.clear();
    }

    std::string err;
    auto dec = openSndfileDecoder(path, err);
    if (!dec) {
        LSPA_LOG_ERROR("Player %s: %s", name_.c_str(), err.c_str());
        {
            std::lock_guard<std::mutex> lock(playlistMutex_);
            if (index >= 0 && index < static_cast<int>(playlist_.size())) {
                playlist_[static_cast<size_t>(index)].missing = true;
                playlist_[static_cast<size_t>(index)].error = err;
            }
        }
        decoder_.reset();
        totalFrames_.store(0);
        eof_.store(true);
        return;
    }

    // Honour the file's own sample rate by resampling? Out of MVP scope; the
    // engine documents that files are played at the device sample rate.
    if (dec->sampleRate() != sampleRate_) {
        LSPA_LOG_WARN("Player %s: file %s is %d Hz but engine runs at %d Hz "
                      "(no resampler in MVP: playback speed will differ)",
                      name_.c_str(), path.c_str(), dec->sampleRate(), sampleRate_);
    }

    decoder_ = std::move(dec);
    if (flush) ring_.producerFlush();
    eof_.store(false);
    totalFrames_.store(decoder_->totalFrames());
    positionFrames_.store(0);
    fadeInTrigger_.store(true);
    trackSerial_.fetch_add(1);

    {
        std::lock_guard<std::mutex> lock(playlistMutex_);
        if (index >= 0 && index < static_cast<int>(playlist_.size())) {
            auto& item = playlist_[static_cast<size_t>(index)];
            item.durationFrames = decoder_->totalFrames();
            item.durationSec =
                double(decoder_->totalFrames()) / std::max(1, decoder_->sampleRate());
            item.title = baseName(path);
        }
    }

    std::function<void(int)> cb;
    {
        std::lock_guard<std::mutex> lock(cbMutex_);
        cb = trackChangeCallback_;
    }
    if (cb) cb(index);

    LSPA_LOG_INFO("Player %s: opened '%s' (%.1fs)", name_.c_str(), path.c_str(),
                  decoder_->totalFrames() / double(std::max(1, decoder_->sampleRate())));
}

void Player::feederLoop() {
    std::vector<float> tempL(kFeederChunkFrames);
    std::vector<float> tempR(kFeederChunkFrames);
    std::vector<float> inter(kFeederChunkFrames * 2);

    while (!quit_.load(std::memory_order_relaxed)) {
        bool didWork = false;

        const int sw = switchToIndex_.exchange(-1);
        if (sw >= 0) {
            openTrackLocked(sw, true);
            didWork = true;
        }

        if (reopenCurrent_.exchange(false)) {
            int idx;
            {
                std::lock_guard<std::mutex> lock(playlistMutex_);
                idx = currentIndex_;
            }
            if (idx >= 0) openTrackLocked(idx, true);
            didWork = true;
        }

        const int seekFrame = pendingSeekFrame_.exchange(-1);
        if (seekFrame >= 0 && decoder_) {
            decoder_->seek(seekFrame);
            ring_.producerFlush();
            eof_.store(false);
            didWork = true;
        }

        const bool playing =
            state_.load(std::memory_order_relaxed) == static_cast<int>(PlayerState::Playing);

        if (playing && decoder_ && !eof_.load(std::memory_order_relaxed)) {
            size_t spaceFrames = ring_.writeSpace() / 2;
            if (spaceFrames >= 64) {
                const int want = static_cast<int>(
                    std::min<size_t>(spaceFrames, kFeederChunkFrames));
                const int got = decoder_->read(tempL.data(), tempR.data(), want);
                if (got <= 0) {
                    eof_.store(true);
                } else {
                    for (int i = 0; i < got; ++i) {
                        inter[static_cast<size_t>(i) * 2 + 0] = tempL[static_cast<size_t>(i)];
                        inter[static_cast<size_t>(i) * 2 + 1] = tempR[static_cast<size_t>(i)];
                    }
                    ring_.write(inter.data(), static_cast<size_t>(got) * 2);
                }
                didWork = true;
            }
        }

        // Natural end of track: advance when the ring is drained.
        if (playing && eof_.load(std::memory_order_relaxed) && ring_.readAvailable() == 0) {
            int next = -1;
            int count = 0;
            {
                std::lock_guard<std::mutex> lock(playlistMutex_);
                count = static_cast<int>(playlist_.size());
                next = currentIndex_ + 1;
            }
            if (next < count) {
                switchToIndex_.store(next);
                LSPA_LOG_INFO("Player %s: track ended, advancing to %d", name_.c_str(), next);
                didWork = true;
            } else if (!ended_.exchange(true)) {
                state_.store(static_cast<int>(PlayerState::Stopped));
                positionFrames_.store(0);
                LSPA_LOG_INFO("Player %s: end of playlist", name_.c_str());
            }
        }

        if (!didWork) {
            std::unique_lock<std::mutex> lock(wakeMutex_);
            wakeCv_.wait_for(lock, std::chrono::milliseconds(kFeederPollMs), [this] {
                return quit_.load(std::memory_order_relaxed) ||
                       feederWake_.exchange(false, std::memory_order_acq_rel);
            });
        }
    }
}

// ---------------------------------------------------------------------------
// audio thread
// ---------------------------------------------------------------------------

void Player::pull(float* left, float* right, int nFrames, PlayerBlockInfo& out) {
    // 1. Decode data out of the ring (may underrun -> silence).
    const size_t want = static_cast<size_t>(nFrames) * 2;
    const size_t gotSamples = ring_.read(feederScratch_.data(), want);
    const int gotFrames = static_cast<int>(gotSamples / 2);
    if (gotFrames < nFrames &&
        state_.load(std::memory_order_relaxed) == static_cast<int>(PlayerState::Playing)) {
        underruns_.fetch_add(1, std::memory_order_relaxed);
    }
    for (int i = 0; i < gotFrames; ++i) {
        left[i] = feederScratch_[static_cast<size_t>(i) * 2 + 0];
        right[i] = feederScratch_[static_cast<size_t>(i) * 2 + 1];
    }
    for (int i = gotFrames; i < nFrames; ++i) {
        left[i] = 0.0f;
        right[i] = 0.0f;
    }

    // 2. Position tracking.
    const int st = state_.load(std::memory_order_relaxed);
    if (st == static_cast<int>(PlayerState::Playing) && gotFrames > 0) {
        positionFrames_.fetch_add(gotFrames);
    }

    // 3. Fade / state machine.
    FadeConfig fade;
    {
        // Only written by the UI thread with a plain mutex; reading per block
        // is cheap enough and never blocks the writer for long.
        std::lock_guard<std::mutex> lock(fadeMutex_);
        fade = fade_;
    }

    if (fadeInTrigger_.exchange(false)) {
        gain_.store(0.0f);
    }

    float g = gain_.load(std::memory_order_relaxed);
    int action = pendingAction_.load(std::memory_order_relaxed);

    float target = (st == static_cast<int>(PlayerState::Playing)) ? 1.0f : 0.0f;
    if (action != static_cast<int>(Action::None)) target = 0.0f;
    if (muted_.load(std::memory_order_relaxed)) target = 0.0f;

    const double sr = sampleRate_;
    float step;
    if (target > g) {
        const double sec = std::max(0.0, fade.fadeInMs) / 1000.0;
        step = sec > 0.0 ? static_cast<float>(1.0 / (sec * sr)) : 1.0f;
    } else {
        const double sec = std::max(0.0, fade.fadeOutMs) / 1000.0;
        step = sec > 0.0 ? -static_cast<float>(1.0 / (sec * sr)) : -1.0f;
    }

    out.rampStart = g;
    float end = g + step * static_cast<float>(nFrames);
    if ((step > 0.0f && end > target) || (step < 0.0f && end < target)) end = target;
    out.rampEnd = end;
    gain_.store(end);

    // 4. Execute the pending action once we faded to silence.
    if (action != static_cast<int>(Action::None) && end <= 0.0f) {
        gain_.store(0.0f);
        switch (static_cast<Action>(action)) {
            case Action::Stop:
                state_.store(static_cast<int>(PlayerState::Stopped));
                positionFrames_.store(0);
                ended_.store(false);
                eof_.store(false);
                ring_.producerFlush();
                wakeFeeder();
                break;
            case Action::Pause:
                state_.store(static_cast<int>(PlayerState::Paused));
                break;
            case Action::Switch: {
                const int idx = switchIndex_.exchange(-1);
                if (idx >= 0) {
                    switchToIndex_.store(idx);
                    wakeFeeder();
                }
                break;
            }
            case Action::None:
                break;
        }
        pendingAction_.store(static_cast<int>(Action::None));
    }

    out.volume = muted_.load(std::memory_order_relaxed) ? 0.0f
                                                        : volume_.load(std::memory_order_relaxed);
    out.muted = muted_.load(std::memory_order_relaxed);
}

bool Player::isActive() const {
    if (state_.load(std::memory_order_relaxed) != static_cast<int>(PlayerState::Stopped)) return true;
    if (gain_.load(std::memory_order_relaxed) > 0.0f) return true;
    if (pendingAction_.load(std::memory_order_relaxed) != static_cast<int>(Action::None)) return true;
    if (switchToIndex_.load(std::memory_order_relaxed) >= 0) return true;
    return false;
}

PlayerSnapshot Player::snapshot() const {
    PlayerSnapshot s;
    s.state = static_cast<PlayerState>(state_.load(std::memory_order_relaxed));
    s.positionFrames = positionFrames_.load(std::memory_order_relaxed);
    s.totalFrames = totalFrames_.load(std::memory_order_relaxed);
    s.gain = gain_.load(std::memory_order_relaxed);
    s.ended = ended_.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(playlistMutex_);
        s.trackIndex = currentIndex_;
    }
    s.positionSec = double(s.positionFrames) / sampleRate_;
    s.durationSec = double(s.totalFrames) / sampleRate_;
    return s;
}

std::string Player::currentPath() const {
    std::lock_guard<std::mutex> lock(playlistMutex_);
    if (currentIndex_ < 0 || currentIndex_ >= static_cast<int>(playlist_.size())) return {};
    return playlist_[static_cast<size_t>(currentIndex_)].path;
}

}  // namespace lspa
