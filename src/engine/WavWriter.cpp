#include "engine/WavWriter.h"

#include <sndfile.h>

#include <vector>

#include "core/Logger.h"

namespace lspa {

namespace {

class WavSink final : public IAudioSink {
public:
    explicit WavSink(std::string path) : path_(std::move(path)) {}
    ~WavSink() override { close(); }

    bool open(int sampleRate, int channels, int blockSize, std::string& errorOut) override {
        sampleRate_ = sampleRate;
        channels_ = channels;
        (void)blockSize;
        SF_INFO info{};
        info.samplerate = sampleRate;
        info.channels = channels;
        info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
        file_ = sf_open(path_.c_str(), SFM_WRITE, &info);
        if (!file_) {
            errorOut = "Cannot open WAV for writing '" + path_ + "': " + sf_strerror(nullptr);
            return false;
        }
        interleave_.resize(static_cast<size_t>(blockSize) * static_cast<size_t>(channels));
        return true;
    }

    void close() override {
        if (file_) {
            sf_close(file_);
            file_ = nullptr;
        }
    }

    bool write(const float* interleaved, int nFrames) override {
        if (!file_) return false;
        if (interleave_.size() < static_cast<size_t>(nFrames) * channels_) {
            interleave_.resize(static_cast<size_t>(nFrames) * static_cast<size_t>(channels_));
        }
        const sf_count_t got = sf_writef_float(file_, interleaved, nFrames);
        return got == nFrames;
    }

    std::string name() const override { return "WAV:" + path_; }
    bool isRealtime() const override { return false; }

private:
    std::string path_;
    SNDFILE* file_ = nullptr;
    int sampleRate_ = 48000;
    int channels_ = 2;
    std::vector<float> interleave_;
};

}  // namespace

std::unique_ptr<IAudioSink> createWavSink(const std::string& path, std::string& errorOut) {
    auto sink = std::make_unique<WavSink>(path);
    // open() is called by the engine; validate the path early here.
    if (path.empty()) {
        errorOut = "Empty output path";
        return nullptr;
    }
    (void)sink;
    return sink;
}

}  // namespace lspa
