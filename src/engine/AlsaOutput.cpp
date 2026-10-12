#include "engine/AudioSink.h"

#include <alsa/asoundlib.h>
#include <unistd.h>

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

        if (formatIsFloat_) {
            snd_pcm_sframes_t w = snd_pcm_writei(pcm_, interleaved, frames);
            if (w == frames) return true;
            if (w < 0) return recover(static_cast<int>(w), interleaved, nFrames);
            // Short write: pad with silence and continue.
            const int err = snd_pcm_writei(pcm_, interleaved + w * channels_, frames - w) < 0
                                ? 0
                                : 0;
            (void)err;
            return true;
        }

        // S16 conversion
        for (size_t i = 0; i < static_cast<size_t>(nFrames) * channels_; ++i) {
            float s = interleaved[i];
            if (s > 1.0f) s = 1.0f;
            if (s < -1.0f) s = -1.0f;
            interleave_[i] = static_cast<int16_t>(s * 32767.0f);
        }
        snd_pcm_sframes_t w = snd_pcm_writei(pcm_, interleave_.data(), frames);
        if (w == frames) return true;
        if (w < 0) {
            const int e = static_cast<int>(w);
            int err = snd_pcm_prepare(pcm_);
            if (err < 0) {
                LSPA_LOG_ERROR("ALSA: device '%s' lost: %s", device_.c_str(), snd_strerror(err));
                return false;
            }
            LSPA_LOG_WARN("ALSA: xrun recovered (%s)", snd_strerror(e));
            w = snd_pcm_writei(pcm_, interleave_.data(), frames);
            return w >= 0;
        }
        return true;
    }

    std::string name() const override { return "ALSA:" + device_; }

private:
    bool recover(int err, const float* interleaved, int nFrames) {
        if (err == -EPIPE) {
            LSPA_LOG_WARN("ALSA: buffer underrun on '%s'", device_.c_str());
            int e = snd_pcm_prepare(pcm_);
            if (e < 0) {
                LSPA_LOG_ERROR("ALSA: cannot recover: %s", snd_strerror(e));
                return false;
            }
            snd_pcm_writei(pcm_, interleaved, static_cast<snd_pcm_sframes_t>(nFrames));
            return true;
        }
        if (err == -ESTRPIPE) {
            // Device suspended: wait for it to resume.
            int e = 0;
            while ((e = snd_pcm_resume(pcm_)) == -EAGAIN) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (e < 0) snd_pcm_prepare(pcm_);
            return true;
        }
        int e = snd_pcm_prepare(pcm_);
        return e >= 0;
    }

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
