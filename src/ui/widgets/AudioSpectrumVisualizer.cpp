#include "ui/widgets/AudioSpectrumVisualizer.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <vector>

#include "engine/AudioEngine.h"
#include "ui/theme/Theme.h"

namespace lspa {

AudioSpectrumVisualizer::AudioSpectrumVisualizer(AudioEngine* engine, QWidget* parent)
    : QWidget(parent), engine_(engine) {
    setMinimumHeight(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    if (engine_) sampleRate_ = static_cast<float>(engine_->config().sampleRate);
    levelsDb_.fill(-60.0f);
    auto* timer = new QTimer(this);
    timer->setInterval(33);
    connect(timer, &QTimer::timeout, this, [this] { sampleOutput(); });
    timer->start();
}

void AudioSpectrumVisualizer::sampleOutput() {
    if (engine_) engine_->copyMasterWaveform(samples_);
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kMinHz = 20.0f;
    const float maxHz = std::min(20000.0f, sampleRate_ * 0.475f);
    const float logRange = std::log(maxHz / kMinHz);
    constexpr size_t kCount = 64;
    constexpr size_t kSamples = AudioEngine::kWaveformSamples;
    static const std::array<float, kSamples> window = [] {
        std::array<float, kSamples> values{};
        constexpr float pi = 3.14159265358979323846f;
        for (size_t i = 0; i < kSamples; ++i) {
            values[i] = 0.5f - 0.5f * std::cos(
                2.0f * pi * static_cast<float>(i) / static_cast<float>(kSamples - 1));
        }
        return values;
    }();
    for (size_t i = 0; i < bands_.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kCount - 1);
        const float hz = kMinHz * std::exp(logRange * t);
        const float coefficient = 2.0f * std::cos(2.0f * kPi * hz / sampleRate_);
        float s1 = 0.0f;
        float s2 = 0.0f;
        for (size_t sampleIndex = 0; sampleIndex < kSamples; ++sampleIndex) {
            const float input = samples_[sampleIndex] * window[sampleIndex];
            const float s0 = input + coefficient * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const float power = std::max(0.0f, s1 * s1 + s2 * s2 - coefficient * s1 * s2);
        const float amplitude = std::sqrt(power) * (4.0f / static_cast<float>(kSamples));
        const float targetDb = std::clamp(20.0f * std::log10(std::max(amplitude, 0.001f)),
                                          -60.0f, 0.0f);
        levelsDb_[i] += (targetDb > levelsDb_[i] ? 0.55f : 0.16f) * (targetDb - levelsDb_[i]);
        bands_[i] = std::clamp((levelsDb_[i] + 60.0f) / 60.0f, 0.0f, 1.0f);
    }
    update();
}

void AudioSpectrumVisualizer::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF panel = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(theme::kBorder, 1.0));
    painter.setBrush(theme::kPanelDeep);
    painter.drawRoundedRect(panel, 10, 10);

    const QRectF plot = panel.adjusted(54, 18, -22, -38);
    if (plot.width() <= 0 || plot.height() <= 0) return;

    // Grid and dB scale.
    QFont axisFont = painter.font();
    axisFont.setPointSize(8);
    painter.setFont(axisFont);
    painter.setPen(QColor(131, 151, 173));
    for (int db = 0; db >= -60; db -= 10) {
        const qreal y = plot.bottom() - plot.height() * (db + 60) / 60.0;
        painter.setPen(QPen(QColor(99, 128, 159, 75), 1));
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        painter.setPen(QColor(181, 195, 210));
        const QString label = db == 0 ? QStringLiteral("0dB") : QString::number(db);
        painter.drawText(QRectF(panel.left() + 3, y - 8, 45, 16),
                 Qt::AlignRight | Qt::AlignVCenter, label);
    }

    const float maxHzForDisplay = std::max(20.0f, std::min(20000.0f, sampleRate_ * 0.475f));
    const QVector<QPair<float, QString>> frequencyTicks = {
        {20.0f, QStringLiteral("20Hz")}, {50.0f, QStringLiteral("50")},
        {100.0f, QStringLiteral("100")}, {200.0f, QStringLiteral("200")},
        {500.0f, QStringLiteral("500")}, {1000.0f, QStringLiteral("1k")},
        {2000.0f, QStringLiteral("2k")}, {5000.0f, QStringLiteral("5k")},
        {10000.0f, QStringLiteral("10k")}, {20000.0f, QStringLiteral("20kHz")}};
    for (const auto& tick : frequencyTicks) {
        if (tick.first > maxHzForDisplay) continue;
        const qreal x = plot.left() + plot.width() *
            std::log(tick.first / 20.0f) / std::log(maxHzForDisplay / 20.0f);
        painter.setPen(QPen(QColor(99, 128, 159, 60), 1));
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        painter.setPen(QColor(181, 195, 210));
        painter.drawText(QRectF(x - 26, plot.bottom() + 7, 52, 16),
                 Qt::AlignHCenter | Qt::AlignTop, tick.second);
    }

    painter.save();
    painter.setClipRect(plot.adjusted(0, 0, 1, 1));

    // Log-frequency spectrum bars with segmented dB rows.
    const qreal cellWidth = plot.width() / bands_.size();
    const qreal segmentGap = 2.0;
    const qreal segmentHeight = std::max<qreal>(2.0, plot.height() / 38.0);
    for (size_t i = 0; i < bands_.size(); ++i) {
        const int activeSegments = qRound(bands_[i] * (plot.height() / segmentHeight));
        const qreal barWidth = std::max<qreal>(2.0, cellWidth - segmentGap);
        for (int segment = 0; segment < activeSegments; ++segment) {
            const qreal y = plot.bottom() - (segment + 1) * segmentHeight;
            const qreal hue = 198.0 - 172.0 * segment / std::max(1.0, plot.height() / segmentHeight);
            QColor color = QColor::fromHsvF(hue / 360.0, 0.78, 0.95, 0.92);
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawRect(QRectF(plot.left() + i * cellWidth, y + segmentGap * 0.5,
                                    barWidth, std::max<qreal>(1.0, segmentHeight - segmentGap)));
        }
    }

    // The live spectrum curve overlays the segmented columns.
    QPainterPath wave;
    for (size_t i = 0; i < bands_.size(); ++i) {
        const qreal x = plot.left() + plot.width() * i / (bands_.size() - 1);
        const qreal y = plot.bottom() - plot.height() * (levelsDb_[i] + 60.0f) / 60.0f;
        if (i == 0) wave.moveTo(x, y);
        else wave.lineTo(x, y);
    }
    QLinearGradient waveGradient(plot.topLeft(), plot.topRight());
    waveGradient.setColorAt(0.0, QColor(66, 221, 196));
    waveGradient.setColorAt(0.28, QColor(73, 181, 255));
    waveGradient.setColorAt(0.58, QColor(175, 125, 255));
    waveGradient.setColorAt(0.82, QColor(255, 96, 172));
    waveGradient.setColorAt(1.0, QColor(255, 175, 74));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(waveGradient, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(wave);
    painter.restore();
}

}  // namespace lspa