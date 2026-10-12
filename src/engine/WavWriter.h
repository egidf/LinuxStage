// WavWriter.h - write engine output to a WAV file (offline render / tests).

#pragma once

#include <memory>
#include <string>

#include "engine/AudioSink.h"

namespace lspa {

// Sink that writes interleaved float audio to a 32-bit float WAV file.
std::unique_ptr<IAudioSink> createWavSink(const std::string& path, std::string& errorOut);

}  // namespace lspa
