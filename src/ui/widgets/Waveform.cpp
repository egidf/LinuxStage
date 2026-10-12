// Waveform.cpp - seekable waveform overview built from file peaks.

#include "ui/widgets/Waveform.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

#include "ui/theme/Theme.h"

namespace lspa {
namespace {

constexpr qreal kPad = 4.0;     // inner padding around the bars
constexpr qreal kRadius = 6.0;  // corner radius of the background well

}  // namespace

WaveformView::WaveformView(QWidget* parent) : QWidget(parent) {
    accent_ = theme::kAccent;
}

void WaveformView::setPeaks(const QVector<float>& peaks) {
    peaks_ = peaks;
    update();
}

void WaveformView::clear() {
    peaks_.clear();
    update();
}

void WaveformView::setProgress(double fraction) {
    if (!std::isfinite(fraction)) {
        return;
    }
    progress_ = qBound(0.0, fraction, 1.0);
    update();
}

double WaveformView::fractionFromX(int x) const {
    const qreal left = kPad + 1.0;
    const qreal usable = width() - 2.0 * (kPad + 1.0);
    if (usable <= 0.0) {
        return 0.0;
    }
    return qBound(0.0, (qreal(x) - left) / usable, 1.0);
}

void WaveformView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Mockup: the waveform sits directly on the panel background (no well).
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    const QRectF inner = rect().adjusted(kPad, kPad, -kPad, -kPad);
    if (inner.width() < 2.0 || inner.height() < 2.0) {
        return;
    }

    if (peaks_.isEmpty()) {
        // no peaks yet: flat centered baseline
        const qreal midY = qRound(inner.center().y()) + 0.5;
        p.setPen(QPen(theme::kTextFaint, 1.0));
        p.drawLine(QPointF(inner.left(), midY), QPointF(inner.right(), midY));
        return;
    }

    const int count = peaks_.size();
    const qreal slot = inner.width() / count;
    const qreal gap = slot > 5.0 ? 1.0 : (slot > 2.5 ? 0.5 : 0.0);
    const qreal barW = qMax(1.0, slot - gap);
    const qreal midY = inner.center().y();
    const qreal half = inner.height() / 2.0 - 1.0;
    const qreal playedX = inner.left() + progress_ * inner.width();

    QPen playedPen(accent_);
    playedPen.setCapStyle(Qt::RoundCap);
    // Mockup: unplayed bars are slate gray, the playhead is bright cyan.
    QPen restPen(QColor(71, 85, 106));
    restPen.setCapStyle(Qt::RoundCap);

    for (int i = 0; i < count; ++i) {
        float a = peaks_[i];
        if (!std::isfinite(a)) {
            a = 0.0f;
        }
        a = qBound(0.0f, a, 1.0f);
        const qreal x = inner.left() + slot * i + gap / 2.0 + barW / 2.0;
        const qreal halfH = qMax(0.8, half * a);
        p.setPen(x <= playedX ? playedPen : restPen);
        p.drawLine(QPointF(x, midY - halfH), QPointF(x, midY + halfH));
    }

    // 1px playhead, crisp (no antialiasing on this one)
    if (progress_ > 0.0) {
        QPainterPath clip;
        clip.addRoundedRect(r, kRadius, kRadius);
        p.setClipPath(clip, Qt::IntersectClip);
        p.setRenderHint(QPainter::Antialiasing, false);
        const qreal x = qRound(playedX) + 0.5;
        p.setPen(QPen(QColor(74, 200, 255), 2.0));
        p.drawLine(QPointF(x, r.top() + 1.0), QPointF(x, r.bottom() - 1.0));
    }
}

void WaveformView::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    emit seekRequested(fractionFromX(int(event->position().x())));
    event->accept();
}

void WaveformView::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    emit seekRequested(fractionFromX(int(event->position().x())));
    event->accept();
}

}  // namespace lspa
