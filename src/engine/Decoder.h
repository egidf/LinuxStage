// Decoder.h - audio file decoding abstraction (non real-time usage only:
// decoders read from disk and must never be touched by the audio callback).

#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace lspa {

class IDecoder {
public:
    virtual ~IDecoder() = default;

    virtual int sampleRate() const = 0;
    virtual int channels() const = 0;
    virtual int64_t totalFrames() const = 0;

    // Seek to an absolute frame. Returns false if unsupported/failed.
    virtual bool seek(int64_t frame) = 0;

    // Decode up to nFrames into planar stereo float buffers (always 2 channels;
    // mono is duplicated). Returns frames actually decoded; 0 means EOF.
    virtual int read(float* left, float* right, int nFrames) = 0;

    virtual const std::string& path() const = 0;
};

// Opens a file with libsndfile (WAV/FLAC/OGG/MP3/AIFF/... as built into the
// system libsndfile). Returns nullptr and fills errorOut on failure.
std::unique_ptr<IDecoder> openSndfileDecoder(const std::string& path, std::string& errorOut);

}  // namespace lspa
