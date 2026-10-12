// AudioSink.h - audio output backends.

#pragma once

#include <memory>
#include <string>

namespace lspa {

class IAudioSink {
public:
    virtual ~IAudioSink() = default;
    virtual bool open(int sampleRate, int channels, int blockSize, std::string& errorOut) = 0;
    virtual void close() = 0;
    // Blocking write of interleaved float frames. Returns false on a fatal
    // device error (engine then attempts recovery/restart).
    virtual bool write(const float* interleaved, int nFrames) = 0;
    virtual std::string name() const = 0;
    // True when the sink paces the engine in real time.
    virtual bool isRealtime() const { return true; }
};

// ALSA playback backend. device: "default", "hw:0,0", "pulse", "null", ...
std::unique_ptr<IAudioSink> createAlsaSink(const std::string& device);

// Discards audio but sleeps for the block duration (headless testing).
std::unique_ptr<IAudioSink> createNullSink();

}  // namespace lspa
