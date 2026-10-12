#include "engine/AudioSink.h"

#include <alsa/asoundlib.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include "core/Logger.h"

namespace lspa {

namespace {

class AlsaSink final : public IAudioSink {
public:
    explicit AlsaSink(std::string device) : device_(std::move(device)) {}

    ~AlsaSink() override { close(); }

    bool open(int sampleRate, int channels, int blockSize, std::string& errorOut) override {
        close();
        sampleRate_ = sampleRate;
        channels_ = channels;
        blockSize_ = blockSize;

        int err = snd_pcm_open(&pcm_, device_.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
        if (err < 0) {
            errorOut = "Cannot open audio device '" + device_ + "': " + snd_strerror(err);
            return false;
        }

        // Prefer float, fall back to signed 16 bit (the "plug" layer resamples
        // and converts when the device does not support the format directly).
        snd_pcm_format_t fmt = SND_PCM_FORMAT_FLOAT_LE;
        err = snd_pcm_set_params(pcm_,
                                 fmt,
                                 SND_PCM_ACCESS_RW_INTERLEAVED,
                                 static_cast<unsigned>(channels_),
                                 static_cast<unsigned>(sampleRate_),
                                 1,          // allow soft resampling
                                 latencyUs_);
        if (err < 0) {
            fmt = SND_PCM_FORMAT_S16_LE;
            useInt16_ = true;
            err = snd_pcm_set_params(pcm_,
                                     fmt,
                                     SND_PCM_ACCESS_RW_INTERLEAVED,
                                     static_cast<unsigned>(channels_),
                                     static_cast<unsigned>(sampleRate_),
                                     1,
                                     latencyUs_);
        }
        if (err < 0) {
            errorOut = "Cannot configure audio device '" + device_ + "': " + snd_strerror(err);
            snd_pcm_close(pcm_);
            pcm_ = nullptr;
            return false;
        }

        formatIsFloat_ = !useInt16_;
        interleave_.resize(static_cast<size_t>(blockSize_) * static_cast<size_t>(channels_));
        LSPA_LOG_INFO("ALSA: opened '%s' at %d Hz, %d ch, block %d, format %s",
                      device_.c_str(), sampleRate_, channels_, blockSize_,
                      formatIsFloat_ ? "FLOAT_LE" : "S16_LE");
        return true;
    }

    void close() override {
        if (pcm_) {
            snd_pcm_drop(pcm_);
            snd_pcm_close(pcm_);
            pcm_ = nullptr;
        }
    }

    bool write(const float* interleaved, int nFrames) override {
        if (!pcm_) return false;
        const snd_pcm_sframes_t frames = static_cast<snd_pcm_sframes_t>(nFrames);
        const void* data = interleaved;

        if (!formatIsFloat_) {
            for (size_t i = 0; i < static_cast<size_t>(nFrames) * channels_; ++i) {
                const float sample = std::clamp(interleaved[i], -1.0f, 1.0f);
                interleave_[i] = static_cast<int16_t>(sample * 32767.0f);
            }
            data = interleave_.data();
        }

        const auto* bytes = static_cast<const unsigned char*>(data);
        const size_t frameBytes = static_cast<size_t>(channels_) *
                                  (formatIsFloat_ ? sizeof(float) : sizeof(int16_t));
        snd_pcm_sframes_t offset = 0;
        int recoveryCount = 0;
        while (offset < frames) {
            const auto* remaining = bytes + static_cast<size_t>(offset) * frameBytes;
            const snd_pcm_sframes_t written = snd_pcm_writei(pcm_, remaining, frames - offset);
            if (written > 0) {
                offset += written;
                recoveryCount = 0;
                continue;
            }
            if (written == 0 || written == -EAGAIN) {
                if (snd_pcm_wait(pcm_, 1000) < 0) return false;
                continue;
            }

            if (++recoveryCount > 3) {
                LSPA_LOG_ERROR("ALSA: repeated write failure on '%s': %s", device_.c_str(),
                               snd_strerror(static_cast<int>(written)));
                return false;
            }
            const int recovered = snd_pcm_recover(pcm_, static_cast<int>(written), 1);
            if (recovered < 0) {
                LSPA_LOG_ERROR("ALSA: cannot recover '%s': %s", device_.c_str(),
                               snd_strerror(recovered));
                return false;
            }
            LSPA_LOG_WARN("ALSA: recovered playback write on '%s' (%s)", device_.c_str(),
                          snd_strerror(static_cast<int>(written)));
        }
        return true;
    }

    std::string name() const override { return "ALSA:" + device_; }

private:
    std::string device_;
    snd_pcm_t* pcm_ = nullptr;
    int sampleRate_ = 0;
    int channels_ = 2;
    int blockSize_ = 256;
    unsigned latencyUs_ = 100000;
    bool useInt16_ = false;
    bool formatIsFloat_ = true;
    std::vector<int16_t> interleave_;
};

class NullSink final : public IAudioSink {
public:
    bool open(int sampleRate, int channels, int blockSize, std::string&) override {
        sampleRate_ = sampleRate;
        channels_ = channels;
        blockSize_ = blockSize;
        return true;
    }
    void close() override {}
    bool write(const float*, int nFrames) override {
        const double sec = double(nFrames) / std::max(1, sampleRate_);
        std::this_thread::sleep_for(std::chrono::duration<double>(sec));
        return true;
    }
    std::string name() const override { return "Null"; }

private:
    int sampleRate_ = 48000;
    int channels_ = 2;
    int blockSize_ = 256;
};

}  // namespace

std::unique_ptr<IAudioSink> createAlsaSink(const std::string& device) {
    return std::make_unique<AlsaSink>(device);
}

std::unique_ptr<IAudioSink> createNullSink() {
    return std::make_unique<NullSink>();
}

}  // namespace lspa
