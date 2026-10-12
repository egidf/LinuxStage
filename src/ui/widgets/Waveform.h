// Waveform.h - seekable waveform overview built from file peaks.

#pragma once

#include <QVector>
#include <QWidget>

namespace lspa {

class WaveformView : public QWidget {
    Q_OBJECT
public:
    explicit WaveformView(QWidget* parent = nullptr);

    // peaks: one amplitude per bar, 0..1.
    void setPeaks(const QVector<float>& peaks);
    void clear();
    void setProgress(double fraction);  // 0..1 played position
    void setAccent(const QColor& color) { accent_ = color; update(); }

    QSize minimumSizeHint() const override { return QSize(120, 44); }
    QSize sizeHint() const override { return QSize(320, 56); }

signals:
    void seekRequested(double fraction);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    double fractionFromX(int x) const;

    QVector<float> peaks_;
    double progress_ = 0.0;
    QColor accent_;
};

}  // namespace lspa
