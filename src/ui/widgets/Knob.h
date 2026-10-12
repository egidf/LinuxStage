// Knob.h - rotary control with value readout (used by rack cards and the
// generic plugin parameter editor).

#pragma once

#include <QWidget>

namespace lspa {

class Knob : public QWidget {
    Q_OBJECT
public:
    explicit Knob(QWidget* parent = nullptr);

    void setRange(float minValue, float maxValue);
    void setValue(float value);       // clamped, emits valueChanged
    float value() const { return value_; }

    void setDefaultValue(float value) { defaultValue_ = value; }
    void setDigits(int digits) { digits_ = digits; update(); }
    void setUnit(const QString& unit) { unit_ = unit; update(); }
    void setLabel(const QString& label) { label_ = label; update(); }
    void setDiameter(int diameter);
    void setBipolar(bool bipolar) { bipolar_ = bipolar; update(); }
    void setKnobColor(const QColor& color);

    // When false the knob renders disabled and ignores input.
    void setActive(bool active);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void valueChanged(float value);
    void dragStarted();
    void dragFinished();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    float normalized() const;
    void setValueFromNormalized(float n);

    float minValue_ = 0.0f;
    float maxValue_ = 1.0f;
    float value_ = 0.0f;
    float defaultValue_ = 0.0f;
    int digits_ = 1;
    bool bipolar_ = false;
    bool active_ = true;
    bool dragging_ = false;
    int dragStartY_ = 0;
    float dragStartValue_ = 0.0f;
    QString unit_;
    QString label_;
    QColor color_;
    int diameter_ = 44;
};

}  // namespace lspa
