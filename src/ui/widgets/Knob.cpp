// Knob.cpp - rotary control with value readout (used by rack cards and the
// generic plugin parameter editor).

#include "ui/widgets/Knob.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>

#include "ui/theme/Theme.h"

namespace lspa {
namespace {

constexpr qreal kKnobSize = 44.0;    // body diameter at sizeHint
constexpr qreal kStartAngle = 225.0; // gap centered at the bottom (Qt: 0 = 3 o'clock,
                                     // positive angles counter-clockwise on screen)
constexpr qreal kSweep = 270.0;      // full-range arc span
constexpr qreal kDragSpanPx = 200.0; // vertical drag distance for the full range
constexpr int kValuePx = 16;         // room reserved below the knob for the readout
constexpr int kLabelPx = 15;         // room reserved above for the caption

// Angle (deg) of the pointer for normalized position n in 0..1.
qreal angleFor(qreal n) {
    return kStartAngle - kSweep * n;
}

// Qt angle convention: 0 deg = 3 o'clock, positive = counter-clockwise, so the
// screen-space y component is -sin (painter y grows downward).
QPointF polar(qreal radius, qreal deg) {
    const qreal a = qDegreesToRadians(deg);
    return QPointF(radius * std::cos(a), -radius * std::sin(a));
}

}  // namespace

Knob::Knob(QWidget* parent) : QWidget(parent) {
    color_ = theme::kAccent;
    setFocusPolicy(Qt::StrongFocus);
}

void Knob::setRange(float minValue, float maxValue) {
    if (!std::isfinite(minValue) || !std::isfinite(maxValue)) {
        return;
    }
    if (maxValue < minValue) {
        std::swap(minValue, maxValue);
    }
    minValue_ = minValue;
    maxValue_ = maxValue;
    const float clamped = qBound(minValue_, value_, maxValue_);
    if (clamped != value_) {
        value_ = clamped;
        emit valueChanged(value_);
    }
    update();
}

void Knob::setValue(float value) {
    if (!std::isfinite(value)) {
        return;  // guard NaN/inf rather than poisoning the arc math
    }
    const float rng = maxValue_ - minValue_;
    value = (rng > 0.0f) ? qBound(minValue_, value, maxValue_) : minValue_;
    if (value == value_) {
        return;
    }
    value_ = value;
    update();
    emit valueChanged(value_);
}

void Knob::setKnobColor(const QColor& color) {
    color_ = color.isValid() ? color : theme::kAccent;
    update();
}

void Knob::setActive(bool active) {
    if (active_ == active) {
        return;
    }
    active_ = active;
    if (!active_ && dragging_) {
        dragging_ = false;
        emit dragFinished();  // keep start/finish signals balanced
    }
    update();
}

QSize Knob::sizeHint() const {
    return QSize(qMax(76, diameter_ + 24), kLabelPx + diameter_ + kValuePx);
}

QSize Knob::minimumSizeHint() const {
    return QSize(qMax(52, diameter_ + 12), kLabelPx + qMax(28, diameter_ - 16) + kValuePx);
}

void Knob::setDiameter(int diameter) {
    diameter_ = qBound(28, diameter, 240);
    updateGeometry();
    update();
}

float Knob::normalized() const {
    const float rng = maxValue_ - minValue_;
    if (!(rng > 0.0f)) {
        return 0.0f;
    }
    return qBound(0.0f, (value_ - minValue_) / rng, 1.0f);
}

void Knob::setValueFromNormalized(float n) {
    if (!std::isfinite(n)) {
        return;
    }
    n = qBound(0.0f, n, 1.0f);
    setValue(minValue_ + n * (maxValue_ - minValue_));
}

void Knob::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal w = width();
    const qreal h = height();
    const qreal availTop = (label_.isEmpty() ? 2.0 : qreal(kLabelPx));
    const qreal availBottom = h - kValuePx - 1.0;

    // knob circle centered in the band between the label and the readout
    const qreal maxD = std::min({w - 4.0, availBottom - availTop, qreal(diameter_)});
    const qreal d = qMax(10.0, maxD);
    const QRectF body(w / 2.0 - d / 2.0,
                      availTop + qMax(0.0, availBottom - availTop - d) / 2.0, d, d);
    const QPointF center = body.center();
    const qreal radius = d / 2.0;
    const qreal arcW = qMax(2.5, radius * 0.13);

    const QColor accent = active_ ? color_ : theme::kTextFaint;
    const QColor ring = active_ ? theme::kBorder : theme::kBorderSoft;
    const qreal n = normalized();

    // body: filled circle inside the value arc
    const qreal innerR = qMax(3.0, radius - arcW - 2.0);
    p.setPen(QPen(ring, 1.5));
    p.setBrush(theme::kPanelAlt);
    p.drawEllipse(center, innerR, innerR);

    // value arc: 270 deg sweep with a gap at the bottom
    QPen trackPen(active_ ? theme::kBorderSoft : theme::kBorderSoft, arcW);
    trackPen.setCapStyle(Qt::FlatCap);
    p.setPen(trackPen);
    p.setBrush(Qt::NoBrush);
    p.drawArc(body, int(kStartAngle * 16), int(-kSweep * 16));

    if (n > 0.0005) {
        QPen valuePen(accent, arcW);
        valuePen.setCapStyle(Qt::RoundCap);
        p.setPen(valuePen);
        if (bipolar_) {
            // neutral zone at the top center: draw only min..max span, plus a
            // small tick at the neutral angle so "zero" reads visually
            const qreal a0 = angleFor(qMin<qreal>(n, 0.5));
            const qreal a1 = angleFor(qMax<qreal>(n, 0.5));
            p.drawArc(body, int(a1 * 16), int((a0 - a1) * 16));
            QPen tickPen(theme::kTextFaint, qMax(1.5, arcW * 0.5));
            tickPen.setCapStyle(Qt::FlatCap);
            p.setPen(tickPen);
            const QPointF t0 = center + polar(innerR + arcW * 0.5 + 1.0, 90.0);
            const QPointF t1 = center + polar(radius + arcW * 0.5 + 1.5, 90.0);
            p.drawLine(t0, t1);
        } else {
            p.drawArc(body, int(kStartAngle * 16), int(-kSweep * n * 16));
        }
    }

    // pointer toward the current angle
    const QPointF dir = polar(1.0, angleFor(n));
    QPen pointerPen(accent, qMax(2.0, innerR * 0.16));
    pointerPen.setCapStyle(Qt::RoundCap);
    p.setPen(pointerPen);
    p.drawLine(center + dir * (innerR * 0.22), center + dir * (innerR * 0.80));
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    p.drawEllipse(center, qMax(1.2, innerR * 0.10), qMax(1.2, innerR * 0.10));

    // value readout under the knob (mono)
    QFont valueFont = font();
    valueFont.setFamily(QStringLiteral("DejaVu Sans Mono"));
    valueFont.setPointSizeF(qMax(6.5, font().pointSizeF() - 1.0));
    p.setFont(valueFont);
    const QString text =
        QString::number(double(value_), 'f', qBound(0, digits_, 6)) + unit_;
    p.setPen(active_ ? theme::kText : theme::kTextFaint);
    p.drawText(QRectF(0.0, h - kValuePx, w, kValuePx - 1), Qt::AlignCenter, text);

    // optional dim label above the knob
    if (!label_.isEmpty()) {
        QFont labelFont = font();
        labelFont.setPointSizeF(qMax(6.5, font().pointSizeF() - 2.0));
        p.setFont(labelFont);
        p.setPen(active_ ? theme::kTextDim : theme::kTextFaint);
        const QFontMetrics fm(labelFont);
        p.drawText(QRectF(0.0, 0.0, w, kLabelPx), Qt::AlignCenter,
                   fm.elidedText(label_, Qt::ElideRight, int(w)));
    }
}

void Knob::mousePressEvent(QMouseEvent* event) {
    if (!active_ || !(event->button() & Qt::LeftButton)) {
        QWidget::mousePressEvent(event);
        return;
    }
    dragging_ = true;
    dragStartY_ = int(event->position().y());
    dragStartValue_ = value_;
    emit dragStarted();
    event->accept();
}

void Knob::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    qreal span = kDragSpanPx;
    if (event->modifiers() & Qt::ShiftModifier) {
        span *= 4.0;  // shift = finer adjustment
    }
    const qreal deltaPx = dragStartY_ - event->position().y();  // up = increase
    const float rng = maxValue_ - minValue_;
    const float startN = (rng > 0.0f) ? (dragStartValue_ - minValue_) / rng : 0.0f;
    setValueFromNormalized(float(startN + deltaPx / span));
    event->accept();
}

void Knob::mouseReleaseEvent(QMouseEvent* event) {
    if (dragging_ && (event->button() & Qt::LeftButton)) {
        dragging_ = false;
        emit dragFinished();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void Knob::mouseDoubleClickEvent(QMouseEvent* event) {
    if (!active_ || !(event->button() & Qt::LeftButton)) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }
    setValue(defaultValue_);  // clamped; emits valueChanged on change
    event->accept();
}

void Knob::wheelEvent(QWheelEvent* event) {
    if (!active_) {
        event->ignore();
        return;
    }
    const qreal steps = event->angleDelta().y() / 120.0;
    if (qFuzzyIsNull(steps)) {
        QWidget::wheelEvent(event);
        return;
    }
    const float rng = maxValue_ - minValue_;
    const float step =
        rng / ((event->modifiers() & Qt::ShiftModifier) ? 200.0f : 50.0f);
    setValue(value_ + float(steps) * step);
    event->accept();
}

}  // namespace lspa
