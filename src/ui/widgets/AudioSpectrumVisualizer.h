// AudioSpectrumVisualizer.h - live master-output waveform and spectrum display.

#pragma once

#include <QWidget>

#include <array>

#include "engine/AudioEngine.h"

namespace lspa {

class AudioSpectrumVisualizer : public QWidget {
    Q_OBJECT
public:
    explicit AudioSpectrumVisualizer(AudioEngine* engine, QWidget* parent = nullptr);

    QSize sizeHint() const override { return QSize(720, 250); }
    QSize minimumSizeHint() const override { return QSize(260, 180); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void sampleOutput();

    AudioEngine* engine_ = nullptr;
    std::array<float, 64> bands_{};
    std::array<float, AudioEngine::kWaveformSamples> samples_{};
    std::array<float, 64> levelsDb_{};
    float sampleRate_ = 48000.0f;
};

}  // namespace lspa