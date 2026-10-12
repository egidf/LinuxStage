#include "ui/widgets/MasterVolumeDial.h"

#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>

#include "ui/theme/Theme.h"

namespace lspa {
namespace {

constexpr qreal kStartAngle = 225.0;
constexpr qreal kSweepAngle = 270.0;
constexpr float kMinDb = -96.0f;
constexpr float kMaxDb = 6.0f;

QPointF pointAt(const QPointF& center, qreal radius, qreal degrees) {
    const qreal radians = qDegreesToRadians(degrees);
    return QPointF(center.x() + std::cos(radians) * radius,
                   center.y() - std::sin(radians) * radius);
}

}  // namespace

MasterVolumeDial::MasterVolumeDial(float gain, QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(minimumSizeHint());
    decibels_ = gain > 0.0f ? std::clamp(20.0f * std::log10(gain), kMinDb, kMaxDb) : kMinDb;
}

float MasterVolumeDial::gain() const {
    return std::pow(10.0f, decibels_ / 20.0f);
}

void MasterVolumeDial::setDecibels(float db) {
    db = std::clamp(db, kMinDb, kMaxDb);
    if (qFuzzyCompare(decibels_ + 1.0f, db + 1.0f)) return;
    decibels_ = db;
    update();
    emit gainChanged(gain());
}

void MasterVolumeDial::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal radius = std::max<qreal>(52.0, std::min(width() * 0.5 - 40.0,
                                                        (height() - 38.0) * 0.5 - 8.0));
    const QPointF center(width() * 0.5, radius + 24.0);
    const qreal faceRadius = radius * 0.77;
    const float value = (decibels_ - kMinDb) / (kMaxDb - kMinDb);

    // Outer bezel and warm level arc.
    painter.setPen(QPen(QColor(10, 13, 17), 2));
    painter.setBrush(QColor(22, 26, 31));
    painter.drawEllipse(center, radius * 0.91, radius * 0.91);
    QPainterPath arc;
    const int arcSteps = std::max(2, qRound(72 * value));
    for (int i = 0; i <= arcSteps; ++i) {
        const qreal n = qreal(i) / 72.0;
        const QPointF pt = pointAt(center, radius * 0.87, kStartAngle - kSweepAngle * n);
        if (i == 0) arc.moveTo(pt);
        else arc.lineTo(pt);
    }
    QLinearGradient arcColor(pointAt(center, radius, 0), pointAt(center, radius, 180));
    arcColor.setColorAt(0.0, QColor(255, 188, 91));
    arcColor.setColorAt(0.55, QColor(255, 129, 54));
    arcColor.setColorAt(1.0, QColor(250, 85, 38));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(arcColor, std::max<qreal>(2.0, radius * 0.035), Qt::SolidLine,
                        Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(arc);

    // Dense calibrated ticks around the dial.
    for (int i = 0; i <= 48; ++i) {
        const qreal n = qreal(i) / 48.0;
        const qreal angle = kStartAngle - kSweepAngle * n;
        const bool major = (i % 6 == 0);
        const QPointF outer = pointAt(center, radius * 0.99, angle);
        const QPointF inner = pointAt(center, radius * (major ? 0.91 : 0.945), angle);
        painter.setPen(QPen(major ? QColor(221, 224, 226) : QColor(130, 139, 146),
                            major ? 1.5 : 0.8));
        painter.drawLine(inner, outer);
    }

    // Labelled dB positions.
    const std::array<float, 6> labelValues = {-96.0f, -76.0f, -36.0f,
                                               -12.0f, 0.0f, 6.0f};
    painter.setFont(QFont(QStringLiteral("Sans Serif"), 8));
    for (float db : labelValues) {
        const qreal n = (db - kMinDb) / (kMaxDb - kMinDb);
        const QPointF position = pointAt(center, radius * 1.13, kStartAngle - kSweepAngle * n);
        const QString label = db > 0.0f ? QStringLiteral("+%1dB").arg(qRound(db))
                           : db == 0.0f ? QStringLiteral("0dB")
                                        : QStringLiteral("%1dB").arg(qRound(db));
        painter.setPen(QColor(192, 199, 205));
        painter.drawText(QRectF(position.x() - 22, position.y() - 8, 44, 16),
                         Qt::AlignCenter, label);
    }

    // Layered dark metallic face.
    QRadialGradient metal(center - QPointF(faceRadius * 0.24, faceRadius * 0.30), faceRadius * 1.42,
                          center);
    metal.setColorAt(0.0, QColor(105, 112, 119));
    metal.setColorAt(0.23, QColor(56, 62, 68));
    metal.setColorAt(0.62, QColor(28, 32, 37));
    metal.setColorAt(0.90, QColor(12, 15, 19));
    metal.setColorAt(1.0, QColor(4, 6, 9));
    painter.setPen(QPen(QColor(120, 128, 135), 1.2));
    painter.setBrush(metal);
    painter.drawEllipse(center, faceRadius, faceRadius);

    // Fine radial brushing catches light without obscuring the dial face.
    for (int i = 0; i < 112; ++i) {
        const qreal angle = i * 360.0 / 112.0;
        const QPointF inner = pointAt(center, faceRadius * 0.18, angle);
        const QPointF outer = pointAt(center, faceRadius * 0.91, angle);
        painter.setPen(QPen(QColor(220, 228, 235, (i % 2 == 0) ? 13 : 6), 0.65));
        painter.drawLine(inner, outer);
    }

    // Pointer, with a warm luminous cap.
    const qreal pointerAngle = kStartAngle - kSweepAngle * value;
    painter.setPen(QPen(QColor(255, 188, 86, 90), faceRadius * 0.12, Qt::SolidLine,
                        Qt::RoundCap));
    painter.drawLine(center, pointAt(center, faceRadius * 0.70, pointerAngle));
    painter.setPen(QPen(QColor(255, 202, 108), std::max<qreal>(2.6, faceRadius * 0.045),
                        Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(pointAt(center, faceRadius * 0.18, pointerAngle),
                     pointAt(center, faceRadius * 0.68, pointerAngle));

    QRadialGradient hub(center - QPointF(2, 3), faceRadius * 0.25, center);
    hub.setColorAt(0.0, QColor(96, 103, 108));
    hub.setColorAt(1.0, QColor(20, 23, 27));
    painter.setPen(QPen(QColor(8, 10, 13), 1));
    painter.setBrush(hub);
    painter.drawEllipse(center, faceRadius * 0.16, faceRadius * 0.16);

    painter.setFont(QFont(QStringLiteral("Sans Serif"), 9, QFont::DemiBold));
    painter.setPen(QColor(178, 185, 193));
    painter.drawText(QRectF(0, height() - 25, width(), 18), Qt::AlignCenter,
                     tr("MASTER VOLUME"));
}

void MasterVolumeDial::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        dragStartY_ = event->position().y();
        dragStartDb_ = decibels_;
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void MasterVolumeDial::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) return QWidget::mouseMoveEvent(event);
    const float precision = (event->modifiers() & Qt::ShiftModifier) ? 480.0f : 220.0f;
    setDecibels(dragStartDb_ + static_cast<float>(dragStartY_ - event->position().y()) *
                                   (kMaxDb - kMinDb) / precision);
    event->accept();
}

void MasterVolumeDial::mouseReleaseEvent(QMouseEvent* event) {
    if (dragging_ && event->button() == Qt::LeftButton) {
        dragging_ = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void MasterVolumeDial::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        setDecibels(0.0f);
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void MasterVolumeDial::wheelEvent(QWheelEvent* event) {
    const float delta = event->angleDelta().y() / 120.0f;
    if (!qFuzzyIsNull(delta)) {
        setDecibels(decibels_ + delta * ((event->modifiers() & Qt::ShiftModifier) ? 0.25f : 1.0f));
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

}  // namespace lspa