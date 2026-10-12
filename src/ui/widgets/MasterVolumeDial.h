// MasterVolumeDial.h - large dB-scaled master output control.

#pragma once

#include <QWidget>

namespace lspa {

class MasterVolumeDial : public QWidget {
    Q_OBJECT
public:
    explicit MasterVolumeDial(float gain = 1.0f, QWidget* parent = nullptr);

    QSize sizeHint() const override { return QSize(270, 270); }
    QSize minimumSizeHint() const override { return QSize(220, 230); }
    float gain() const;

signals:
    void gainChanged(float gain);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void setDecibels(float db);
    float decibels_ = 0.0f;
    bool dragging_ = false;
    qreal dragStartY_ = 0.0;
    float dragStartDb_ = 0.0f;
};

}  // namespace lspa