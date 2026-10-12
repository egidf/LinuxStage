// AlbumArt.cpp - cover art tile with generated gradient art fallback.

#include "ui/widgets/AlbumArt.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <cmath>

#include "ui/theme/Theme.h"
#include "ui/widgets/Icons.h"

namespace lspa {
namespace {

constexpr int kArtSize = 512;    // resolution of generated fallback art
constexpr int kRadius = 12;      // corner radius of the tile
constexpr int kMargin = 12;      // caption margin

// FNV-1a over UTF-16 code units: deterministic regardless of Qt's global hash
// seed, which is what makes the generated art stable per track.
quint32 hashSeed(const QString& seed) {
    quint32 h = 2166136261u;
    for (const QChar ch : seed) {
        h ^= quint32(ch.unicode());
        h *= 16777619u;
    }
    return h;
}

// xorshift32 for repeatable placement of the decorative blobs.
quint32 nextRand(quint32& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

// Deterministic abstract cover derived from the seed hash: diagonal gradient
// in 2 hues + soft translucent blobs + a light streak.
QPixmap generateArt(const QString& seed) {
    quint32 h = hashSeed(seed);
    if (h == 0) {
        h = 0x9E3779B9u;
    }
    const int hue1 = int(h % 360u);
    const int hue2 = int((hue1 + 55 + ((h >> 9) % 110u)) % 360u);

    QImage img(kArtSize, kArtSize, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient bg(0.0, 0.0, kArtSize, kArtSize);
    bg.setColorAt(0.0, QColor::fromHsv(hue1, 190, 215));
    bg.setColorAt(0.5, QColor::fromHsv(hue2, 165, 195));
    bg.setColorAt(1.0, QColor::fromHsv(hue1, 205, 135));
    p.fillRect(img.rect(), bg);

    // soft blobs
    quint32 rng = h ^ 0xA5A5A5A5u;
    for (int i = 0; i < 5; ++i) {
        const qreal cx = qreal(nextRand(rng) % 1000u) / 1000.0 * kArtSize;
        const qreal cy = qreal(nextRand(rng) % 1000u) / 1000.0 * kArtSize;
        const qreal rad = kArtSize * (0.18 + 0.20 * qreal(nextRand(rng) % 1000u) / 1000.0);
        const int hue = (i % 2 == 0) ? hue2 : hue1;
        QRadialGradient blob(cx, cy, rad);
        blob.setColorAt(0.0, QColor::fromHsv(hue, 170, 235, 90));
        blob.setColorAt(1.0, QColor::fromHsv(hue, 170, 235, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(blob);
        p.drawEllipse(QPointF(cx, cy), rad, rad);
    }

    // diagonal light streak
    p.save();
    p.translate(kArtSize / 2.0, kArtSize / 2.0);
    p.rotate(-28.0 + qreal((h >> 3) % 20u));  // slight variation per seed
    QLinearGradient streak(QPointF(-kArtSize * 0.7, 0.0), QPointF(kArtSize * 0.7, 0.0));
    streak.setColorAt(0.0, QColor(255, 255, 255, 0));
    streak.setColorAt(0.5, QColor(255, 255, 255, 30));
    streak.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.setBrush(streak);
    p.drawRect(QRectF(-kArtSize * 0.75, -kArtSize * 0.06, kArtSize * 1.5, kArtSize * 0.12));
    p.restore();
    p.end();

    return QPixmap::fromImage(img);
}

}  // namespace

AlbumArt::AlbumArt(QWidget* parent) : QWidget(parent) {
    accent_ = theme::kAccent;
}

void AlbumArt::setCover(const QPixmap& cover) {
    cover_ = cover;
    update();
}

void AlbumArt::setSeed(const QString& seed) {
    // No dedicated seed member in the header: cache the generated art in the
    // cover slot (generated once, deterministic per seed).
    cover_ = seed.isEmpty() ? QPixmap() : generateArt(seed);
    update();
}

void AlbumArt::setCaption(const QString& title, const QString& subtitle) {
    title_ = title;
    subtitle_ = subtitle;
    update();
}

void AlbumArt::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    QPainterPath clip;
    clip.addRoundedRect(r, kRadius, kRadius);
    p.setClipPath(clip);

    if (!cover_.isNull()) {
        // scale-to-fill with center crop (aspect math works in pixmap pixels)
        const qreal srcAspect = qreal(cover_.width()) / qreal(qMax(1, cover_.height()));
        const qreal dstAspect = r.width() / qMax(1.0, r.height());
        QRectF src;
        if (srcAspect > dstAspect) {
            const qreal w = cover_.height() * dstAspect;
            src = QRectF((cover_.width() - w) / 2.0, 0.0, w, cover_.height());
        } else {
            const qreal h = cover_.width() / dstAspect;
            src = QRectF(0.0, (cover_.height() - h) / 2.0, cover_.width(), h);
        }
        p.drawPixmap(r, cover_, src);
    } else {
        // neutral placeholder: dark panel + cover glyph
        p.fillRect(r, theme::kPanelAlt);
        const int glyph = qMax(16, int(qMin(r.width(), r.height()) * 0.34));
        const QPixmap glyphPm = icons::pixmap(icons::Icon::Cover, theme::kTextFaint, glyph);
        p.drawPixmap(QPointF(r.center().x() - glyph / 2.0, r.center().y() - glyph / 2.0),
                     glyphPm);
        if (accent_.isValid()) {
            p.setPen(Qt::NoPen);
            p.setBrush(accent_);
            p.drawEllipse(QPointF(r.center().x(), r.bottom() - 6.0), 2.5, 2.5);
        }
    }

    // bottom-up dark scrim behind the caption
    QLinearGradient scrim(0.0, r.bottom() - r.height() * 0.55, 0.0, r.bottom());
    scrim.setColorAt(0.0, QColor(theme::kWindow.red(), theme::kWindow.green(),
                                 theme::kWindow.blue(), 0));
    scrim.setColorAt(1.0, QColor(theme::kWindow.red(), theme::kWindow.green(),
                                 theme::kWindow.blue(), 205));
    p.fillRect(QRectF(r.left(), r.bottom() - r.height() * 0.55, r.width(),
                      r.height() * 0.55),
               scrim);

    // caption: title bold ~13px, subtitle ~10px dim, left aligned, elided
    const qreal textW = r.width() - 2.0 * kMargin;
    QFont titleFont = font();
    titleFont.setPointSizeF(13.0);
    titleFont.setBold(true);
    QFont subFont = font();
    subFont.setPointSizeF(10.0);
    const QFontMetricsF subFm(subFont);
    const qreal subH = subFm.height();

    if (!subtitle_.isEmpty()) {
        p.setFont(subFont);
        p.setPen(QColor(201, 209, 217));  // mockup subtitle grey
        const QRectF subRect(r.left() + kMargin, r.bottom() - kMargin - subH,
                             textW, subH);
        p.drawText(subRect, Qt::AlignLeft | Qt::AlignBottom,
                   QFontMetrics(subFont).elidedText(subtitle_, Qt::ElideRight,
                                                    int(textW)));
    }
    if (!title_.isEmpty()) {
        p.setFont(titleFont);
        p.setPen(Qt::white);
        const qreal titleH = QFontMetricsF(titleFont).height();
        const qreal bottom = r.bottom() - kMargin - subH - 3.0;
        const QRectF titleRect(r.left() + kMargin, bottom - titleH, textW, titleH);
        p.drawText(titleRect, Qt::AlignLeft | Qt::AlignBottom,
                   QFontMetrics(titleFont).elidedText(title_, Qt::ElideRight,
                                                      int(textW)));
    }

    // border on top of everything
    p.setClipping(false);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(theme::kBorderSoft, 1.0));
    p.drawRoundedRect(r, kRadius, kRadius);
}

}  // namespace lspa
