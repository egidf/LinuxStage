// LevelMeter.cpp - compact horizontal/vertical level meter with clip flag.

#include "ui/widgets/LevelMeter.h"

#include <QPainter>
#include <QTimer>

#include <cmath>

#include "ui/theme/Theme.h"

namespace lspa {
namespace {

constexpr qreal kRadius = 3.0;      // track corner radius
constexpr qreal kClipCapPx = 6.0;   // length of the clip cap along the axis
constexpr qreal kDecayPerTick = 0.01;  // peak-hold fall per 50ms tick
constexpr int kDecayIntervalMs = 50;
constexpr int kClipHoldMs = 1500;   // auto-release (header has no resetClip())

}  // namespace

LevelMeter::LevelMeter(QWidget* parent) : QWidget(parent) {
    accent_ = theme::kAccent;

    // Header has no timer member, so the peak-hold decay timer is a child
    // object created here; it only does work while the marker is falling.
    auto* decay = new QTimer(this);
    decay->setInterval(kDecayIntervalMs);
    connect(decay, &QTimer::timeout, this, [this] {
        if (peakHold_ > level_) {
            peakHold_ = qMax(level_, peakHold_ - float(kDecayPerTick));
            update();
        }
    });
    decay->start();
}

void LevelMeter::setLevel(float level, bool clipping) {
    if (std::isfinite(level)) {
        level_ = qBound(0.0f, level, 1.0f);
    }
    if (level_ > peakHold_) {
        peakHold_ = level_;
    }
    if (clipping) {
        // Clip latches; without resetClip() in the header we auto-release
        // after a short hold so the indicator cannot stick forever.
        clip_ = true;
        const int seq = property("lspa.clipSeq").toInt() + 1;
        setProperty("lspa.clipSeq", seq);
        QTimer::singleShot(kClipHoldMs, this, [this, seq] {
            if (property("lspa.clipSeq").toInt() == seq) {
                clip_ = false;
                update();
            }
        });
    }
    update();
}

QSize LevelMeter::sizeHint() const {
    return orientation_ == Qt::Horizontal ? QSize(160, 10) : QSize(10, 120);
}

QSize LevelMeter::minimumSizeHint() const {
    return orientation_ == Qt::Horizontal ? QSize(80, 8) : QSize(8, 60);
}

void LevelMeter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
    if (r.width() < 2.0 || r.height() < 2.0) {
        return;
    }

    // track
    p.setPen(QPen(theme::kBorderSoft, 1.0));
    p.setBrush(theme::kPanelDeep);
    p.drawRoundedRect(r, kRadius, kRadius);

    const bool horiz = orientation_ == Qt::Horizontal;
    const qreal len = horiz ? r.width() : r.height();

    // filled portion (left to right, or bottom up when vertical)
    const qreal fillLen = qBound(0.0, level_ * len, len);
    if (fillLen > 0.5) {
        p.setPen(Qt::NoPen);
        p.setBrush(accent_.isValid() ? accent_ : theme::kAccent);
        if (horiz) {
            p.drawRoundedRect(QRectF(r.left(), r.top(), fillLen, r.height()),
                              kRadius, kRadius);
        } else {
            p.drawRoundedRect(QRectF(r.left(), r.bottom() - fillLen, r.width(), fillLen),
                              kRadius, kRadius);
        }
    }

    // peak-hold marker: brighter, 2px along the axis
    const qreal peakLen = qBound(0.0, peakHold_ * len, len);
    if (peakLen > 1.0) {
        const QColor marker =
            (accent_.isValid() ? accent_ : theme::kAccent).lighter(140);
        p.setBrush(marker);
        if (horiz) {
            p.drawRect(QRectF(r.left() + peakLen - 2.0, r.top(), 2.0, r.height()));
        } else {
            p.drawRect(QRectF(r.left(), r.bottom() - peakLen, r.width(), 2.0));
        }
    }

    // clip cap at the far end of the track
    if (clip_) {
        p.setPen(Qt::NoPen);
        p.setBrush(theme::kError);
        if (horiz) {
            p.drawRoundedRect(QRectF(r.right() - kClipCapPx, r.top(), kClipCapPx,
                                     r.height()),
                              kRadius, kRadius);
        } else {
            p.drawRoundedRect(QRectF(r.left(), r.top(), r.width(), kClipCapPx),
                              kRadius, kRadius);
        }
    }
}

}  // namespace lspa
