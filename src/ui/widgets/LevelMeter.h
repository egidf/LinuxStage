// LevelMeter.h - compact horizontal/vertical level meter with clip flag.

#pragma once

#include <QWidget>

namespace lspa {

class LevelMeter : public QWidget {
    Q_OBJECT
public:
    explicit LevelMeter(QWidget* parent = nullptr);

    // level in 0..1 (linear amplitude), clip latches until resetClip().
    void setLevel(float level, bool clipping = false);
    void setAccent(const QColor& color) { accent_ = color; update(); }
    void setOrientation(Qt::Orientation orientation) { orientation_ = orientation; update(); }

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    float level_ = 0.0f;
    float peakHold_ = 0.0f;
    bool clip_ = false;
    QColor accent_;
    Qt::Orientation orientation_ = Qt::Horizontal;
};

}  // namespace lspa
