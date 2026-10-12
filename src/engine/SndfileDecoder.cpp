#include "engine/Decoder.h"

#include <sndfile.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/Logger.h"

namespace lspa {

namespace {

class SndfileDecoder final : public IDecoder {
public:
    SndfileDecoder(SNDFILE* file, const SF_INFO& info, std::string path)
        : file_(file), info_(info), path_(std::move(path)) {
        // Decode through an intermediate buffer sized in frames.
        scratch_.resize(static_cast<size_t>(kChunkFrames) * static_cast<size_t>(info_.channels));
    }

    ~SndfileDecoder() override {
        if (file_) sf_close(file_);
    }

    int sampleRate() const override { return info_.samplerate; }
    int channels() const override { return info_.channels; }
    int64_t totalFrames() const override { return info_.frames; }
    const std::string& path() const override { return path_; }

    bool seek(int64_t frame) override {
        if (frame < 0) frame = 0;
        if (frame > info_.frames) frame = info_.frames;
        const sf_count_t got = sf_seek(file_, frame, SEEK_SET);
        eof_ = false;
        return got >= 0;
    }

    int read(float* left, float* right, int nFrames) override {
        if (nFrames <= 0) return 0;
        int done = 0;
        while (done < nFrames) {
            if (eof_) break;
            const int want = std::min(nFrames - done, kChunkFrames);
            const sf_count_t got =
                sf_readf_float(file_, scratch_.data(), want);
            if (got <= 0) {
                eof_ = true;
                break;
            }
            const int ch = info_.channels;
            for (sf_count_t i = 0; i < got; ++i) {
                const float* s = scratch_.data() + i * ch;
                left[done + i] = s[0];
                right[done + i] = (ch > 1) ? s[1] : s[0];
            }
            done += static_cast<int>(got);
            if (got < want) {
                eof_ = true;
                break;
            }
        }
        return done;
    }

private:
    static constexpr int kChunkFrames = 1024;
    SNDFILE* file_ = nullptr;
    SF_INFO info_{};
    std::string path_;
    std::vector<float> scratch_;
    bool eof_ = false;
};

}  // namespace

std::unique_ptr<IDecoder> openSndfileDecoder(const std::string& path, std::string& errorOut) {
    SF_INFO info{};
    SNDFILE* file = sf_open(path.c_str(), SFM_READ, &info);
    if (!file) {
        errorOut = std::string("Cannot open audio file '") + path + "': " + sf_strerror(nullptr);
        return nullptr;
    }
    if (info.frames <= 0 || info.samplerate <= 0 || info.channels <= 0) {
        sf_close(file);
        errorOut = "File '" + path + "' has no decodable audio stream.";
        return nullptr;
    }
    // Normalise unsupported channel counts (>2) by downmixing in the decoder:
    // sf_readf_float already gives us interleaved channels; we take L/R only.
    if (info.channels > 2) {
        LSPA_LOG_WARN("Decoder: %s has %d channels; using first two", path.c_str(), info.channels);
    }
    return std::make_unique<SndfileDecoder>(file, info, path);
}

}  // namespace lspa
