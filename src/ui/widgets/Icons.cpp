// Icons.cpp - lightweight QPainter-drawn icons (no icon theme dependency).

#include "ui/widgets/Icons.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QtMath>
#include <QSvgRenderer>

#include <cmath>

#include "ui/theme/Theme.h"

namespace lspa::icons {
namespace {

// Qt arc convention (verified): 0 deg = 3 o'clock, positive angles go
// counter-clockwise on screen, so screen y = -sin(deg) in a y-down painter.
QPointF polar(qreal radius, qreal deg) {
    const qreal a = qDegreesToRadians(deg);
    return QPointF(radius * std::cos(a), -radius * std::sin(a));
}

// Sampled arc polyline (avoids QPainter arc-angle ambiguity in unit space).
QPainterPath arcPath(qreal radius, qreal startDeg, qreal endDeg) {
    QPainterPath path;
    constexpr int kSteps = 48;
    for (int i = 0; i <= kSteps; ++i) {
        const qreal t = startDeg + (endDeg - startDeg) * qreal(i) / kSteps;
        const QPointF pt = polar(radius, t);
        if (i == 0) {
            path.moveTo(pt);
        } else {
            path.lineTo(pt);
        }
    }
    return path;
}

QPen strokePen(const QColor& c, qreal w) {
    QPen pen(c, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    return pen;
}

// Draws one glyph in unit coordinates (-0.5 .. 0.5); the painter passed in is
// already translated to the icon center and scaled so that 1.0 == glyph box.
void drawGlyph(QPainter& p, Icon id, const QColor& c) {
    p.setRenderHint(QPainter::Antialiasing, true);

    switch (id) {
    case Icon::Play: {
        QPainterPath t;
        t.moveTo(-0.20, -0.30);
        t.lineTo(0.32, 0.0);
        t.lineTo(-0.20, 0.30);
        t.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawPath(t);
        break;
    }
    case Icon::Pause: {
        QPainterPath t;
        t.addRoundedRect(QRectF(-0.30, -0.30, 0.17, 0.60), 0.05, 0.05);
        t.addRoundedRect(QRectF(0.13, -0.30, 0.17, 0.60), 0.05, 0.05);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawPath(t);
        break;
    }
    case Icon::Stop: {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(QRectF(-0.25, -0.25, 0.50, 0.50), 0.07, 0.07);
        break;
    }
    case Icon::Prev: {
        QPainterPath t;
        t.addRect(QRectF(-0.34, -0.30, 0.10, 0.60));
        t.moveTo(-0.18, -0.30);
        t.lineTo(0.32, 0.0);
        t.lineTo(-0.18, 0.30);
        t.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawPath(t);
        break;
    }
    case Icon::Next: {
        QPainterPath t;
        t.moveTo(0.18, -0.30);
        t.lineTo(-0.32, 0.0);
        t.lineTo(0.18, 0.30);
        t.closeSubpath();
        t.addRect(QRectF(0.24, -0.30, 0.10, 0.60));
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawPath(t);
        break;
    }
    case Icon::Shuffle: {
        p.setPen(strokePen(c, 0.09));
        p.setBrush(Qt::NoBrush);
        QPainterPath t;
        t.moveTo(-0.35, -0.20);
        t.lineTo(-0.06, -0.20);
        t.lineTo(0.30, 0.14);
        t.moveTo(-0.35, 0.20);
        t.lineTo(0.06, 0.20);
        t.lineTo(0.30, -0.14);
        p.drawPath(t);
        // arrowheads on the right ends, pointing along each diagonal
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        QPainterPath head;
        head.moveTo(0.32, 0.15);
        head.lineTo(0.12, 0.08);
        head.lineTo(0.25, -0.05);
        head.closeSubpath();
        head.moveTo(0.32, -0.15);
        head.lineTo(0.25, 0.05);
        head.lineTo(0.12, -0.08);
        head.closeSubpath();
        p.drawPath(head);
        break;
    }
    case Icon::Repeat: {
        p.setPen(strokePen(c, 0.09));
        p.setBrush(Qt::NoBrush);
        QPainterPath t;
        t.moveTo(-0.32, 0.04);
        t.lineTo(-0.32, -0.14);
        t.quadTo(-0.32, -0.24, -0.20, -0.24);
        t.lineTo(0.18, -0.24);
        t.moveTo(0.32, -0.04);
        t.lineTo(0.32, 0.14);
        t.quadTo(0.32, 0.24, 0.20, 0.24);
        t.lineTo(-0.18, 0.24);
        p.drawPath(t);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        QPainterPath head;
        head.moveTo(0.32, -0.24);
        head.lineTo(0.17, -0.34);
        head.lineTo(0.17, -0.14);
        head.closeSubpath();
        head.moveTo(-0.32, 0.24);
        head.lineTo(-0.17, 0.34);
        head.lineTo(-0.17, 0.14);
        head.closeSubpath();
        p.drawPath(head);
        break;
    }
    case Icon::Volume:
    case Icon::VolumeMute: {
        QPainterPath spk;  // speaker cone, shared by both variants
        spk.moveTo(-0.34, -0.12);
        spk.lineTo(-0.20, -0.12);
        spk.lineTo(-0.04, -0.28);
        spk.lineTo(-0.04, 0.28);
        spk.lineTo(-0.20, 0.12);
        spk.lineTo(-0.34, 0.12);
        spk.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawPath(spk);
        if (id == Icon::Volume) {
            p.setPen(strokePen(c, 0.08));
            p.setBrush(Qt::NoBrush);
            QPainterPath w;
            w.moveTo(0.08, -0.16);
            w.quadTo(0.18, 0.0, 0.08, 0.16);
            w.moveTo(0.18, -0.28);
            w.quadTo(0.32, 0.0, 0.18, 0.28);
            p.drawPath(w);
        } else {
            p.setPen(strokePen(c, 0.085));
            p.drawLine(QPointF(0.10, -0.15), QPointF(0.32, 0.15));
            p.drawLine(QPointF(0.32, -0.15), QPointF(0.10, 0.15));
        }
        break;
    }
    case Icon::Plus: {
        p.setPen(strokePen(c, 0.11));
        p.drawLine(QPointF(-0.28, 0.0), QPointF(0.28, 0.0));
        p.drawLine(QPointF(0.0, -0.28), QPointF(0.0, 0.28));
        break;
    }
    case Icon::Trash: {
        p.setPen(strokePen(c, 0.085));
        QPainterPath t;
        t.moveTo(-0.28, -0.16);
        t.lineTo(0.28, -0.16);
        t.moveTo(-0.09, -0.28);
        t.lineTo(0.09, -0.28);
        t.moveTo(-0.22, -0.16);
        t.lineTo(-0.16, 0.30);
        t.lineTo(0.16, 0.30);
        t.lineTo(0.22, -0.16);
        t.moveTo(-0.08, -0.04);
        t.lineTo(-0.07, 0.20);
        t.moveTo(0.08, -0.04);
        t.lineTo(0.07, 0.20);
        p.drawPath(t);
        break;
    }
    case Icon::ClearList: {
        p.setPen(strokePen(c, 0.10));
        QPainterPath t;
        t.moveTo(-0.32, -0.14);
        t.lineTo(0.00, -0.14);
        t.moveTo(-0.32, 0.14);
        t.lineTo(-0.08, 0.14);
        t.moveTo(0.10, -0.10);
        t.lineTo(0.30, 0.10);
        t.moveTo(0.30, -0.10);
        t.lineTo(0.10, 0.10);
        p.drawPath(t);
        break;
    }
    case Icon::Kebab: {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(QPointF(0.0, -0.26), 0.075, 0.075);
        p.drawEllipse(QPointF(0.0, 0.0), 0.075, 0.075);
        p.drawEllipse(QPointF(0.0, 0.26), 0.075, 0.075);
        break;
    }
    case Icon::Power: {
        p.setPen(strokePen(c, 0.095));
        p.setBrush(Qt::NoBrush);
        p.drawPath(arcPath(0.26, 135.0, 405.0));  // loop with a gap at the top
        p.drawLine(QPointF(0.0, -0.33), QPointF(0.0, -0.08));
        break;
    }
    case Icon::Search: {
        p.setPen(strokePen(c, 0.09));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(-0.06, -0.06), 0.21, 0.21);
        p.setPen(strokePen(c, 0.11));
        p.drawLine(QPointF(0.08, 0.08), QPointF(0.30, 0.30));
        break;
    }
    case Icon::Gear: {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        for (int i = 0; i < 8; ++i) {
            p.save();
            p.rotate(45.0 * i);
            p.drawRoundedRect(QRectF(-0.055, -0.35, 0.11, 0.16), 0.03, 0.03);
            p.restore();
        }
        QPainterPath ring;  // hub with a punched hole
        ring.setFillRule(Qt::OddEvenFill);
        ring.addEllipse(QPointF(0.0, 0.0), 0.235, 0.235);
        ring.addEllipse(QPointF(0.0, 0.0), 0.10, 0.10);
        p.drawPath(ring);
        break;
    }
    case Icon::Link: {
        p.setPen(strokePen(c, 0.09));
        p.setBrush(Qt::NoBrush);
        // Chain link: two rounded capsules at -45 degrees plus a short
        // connecting segment (as axis-aligned rings this read as an
        // "infinity" symbol, unlike the mockup's chain link).
        p.save();
        p.rotate(-45.0);
        p.drawRoundedRect(QRectF(-0.40, -0.15, 0.36, 0.30), 0.15, 0.15);
        p.drawRoundedRect(QRectF(0.04, -0.15, 0.36, 0.30), 0.15, 0.15);
        p.drawLine(QPointF(-0.06, 0.0), QPointF(0.06, 0.0));
        p.restore();
        break;
    }
    case Icon::Music: {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(QPointF(-0.16, 0.22), 0.115, 0.095);
        p.drawEllipse(QPointF(0.18, 0.14), 0.115, 0.095);
        p.setPen(strokePen(c, 0.075));
        QPainterPath st;
        st.moveTo(-0.05, 0.22);
        st.lineTo(-0.05, -0.30);
        st.moveTo(0.29, 0.14);
        st.lineTo(0.29, -0.36);
        p.drawPath(st);
        p.setPen(strokePen(c, 0.10));
        p.drawLine(QPointF(-0.05, -0.30), QPointF(0.29, -0.36));  // beam
        break;
    }
    case Icon::Plugin: {  // IC / chip style
        p.setPen(strokePen(c, 0.085));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(-0.26, -0.20, 0.52, 0.40), 0.06, 0.06);
        QPainterPath pins;
        pins.moveTo(-0.38, -0.10);
        pins.lineTo(-0.26, -0.10);
        pins.moveTo(-0.38, 0.10);
        pins.lineTo(-0.26, 0.10);
        pins.moveTo(0.26, -0.10);
        pins.lineTo(0.38, -0.10);
        pins.moveTo(0.26, 0.10);
        pins.lineTo(0.38, 0.10);
        p.drawPath(pins);
        break;
    }
    case Icon::Wave: {  // sine squiggle
        QPainterPath w;
        constexpr int kSeg = 40;
        for (int i = 0; i <= kSeg; ++i) {
            const qreal t = qreal(i) / kSeg;
            const qreal x = -0.40 + 0.80 * t;
            const qreal y = -0.28 * std::sin(t * 2.0 * M_PI * 1.5);
            if (i == 0) {
                w.moveTo(x, y);
            } else {
                w.lineTo(x, y);
            }
        }
        p.setPen(strokePen(c, 0.10));
        p.setBrush(Qt::NoBrush);
        p.drawPath(w);
        break;
    }
    case Icon::Folder: {
        QPainterPath f;
        f.moveTo(-0.36, -0.24);
        f.lineTo(-0.10, -0.24);
        f.lineTo(-0.02, -0.13);
        f.lineTo(0.36, -0.13);
        f.lineTo(0.36, 0.26);
        f.lineTo(-0.36, 0.26);
        f.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawPath(f);
        break;
    }
    case Icon::Save: {  // arrow down onto a disk line
        p.setPen(strokePen(c, 0.105));
        QPainterPath t;
        t.moveTo(0.0, -0.30);
        t.lineTo(0.0, 0.10);
        t.moveTo(-0.19, -0.09);
        t.lineTo(0.0, 0.10);
        t.lineTo(0.19, -0.09);
        t.moveTo(-0.30, 0.27);
        t.lineTo(0.30, 0.27);
        p.drawPath(t);
        break;
    }
    case Icon::Load: {  // arrow up off a disk line
        p.setPen(strokePen(c, 0.105));
        QPainterPath t;
        t.moveTo(0.0, 0.14);
        t.lineTo(0.0, -0.30);
        t.moveTo(-0.19, -0.10);
        t.lineTo(0.0, -0.30);
        t.lineTo(0.19, -0.10);
        t.moveTo(-0.30, 0.27);
        t.lineTo(0.30, 0.27);
        p.drawPath(t);
        break;
    }
    case Icon::ArrowRight: {
        p.setPen(strokePen(c, 0.11));
        QPainterPath t;
        t.moveTo(-0.26, 0.0);
        t.lineTo(0.20, 0.0);
        t.moveTo(0.02, -0.19);
        t.lineTo(0.24, 0.0);
        t.lineTo(0.02, 0.19);
        p.drawPath(t);
        break;
    }
    case Icon::Check: {
        p.setPen(strokePen(c, 0.13));
        QPainterPath t;
        t.moveTo(-0.30, 0.02);
        t.lineTo(-0.09, 0.23);
        t.lineTo(0.30, -0.24);
        p.drawPath(t);
        break;
    }
    case Icon::Warning: {
        p.setPen(strokePen(c, 0.10));
        p.setBrush(Qt::NoBrush);
        QPainterPath tri;
        tri.moveTo(0.0, -0.34);
        tri.lineTo(0.34, 0.26);
        tri.lineTo(-0.34, 0.26);
        tri.closeSubpath();
        p.drawPath(tri);
        p.setPen(strokePen(c, 0.11));
        p.drawLine(QPointF(0.0, -0.10), QPointF(0.0, 0.08));
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(QPointF(0.0, 0.18), 0.055, 0.055);
        break;
    }
    case Icon::Close: {
        p.setPen(strokePen(c, 0.12));
        p.drawLine(QPointF(-0.22, -0.22), QPointF(0.22, 0.22));
        p.drawLine(QPointF(0.22, -0.22), QPointF(-0.22, 0.22));
        break;
    }
    case Icon::Drag: {  // 6-dot grip
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        for (int col = 0; col < 2; ++col) {
            for (int row = 0; row < 3; ++row) {
                p.drawEllipse(QPointF(-0.11 + 0.22 * col, -0.24 + 0.24 * row), 0.08, 0.08);
            }
        }
        break;
    }
    case Icon::Windows: {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        const qreal cell = 0.32;
        p.drawRect(QRectF(-0.36, -0.36, cell, cell));
        p.drawRect(QRectF(0.04, -0.36, cell, cell));
        p.drawRect(QRectF(-0.36, 0.04, cell, cell));
        p.drawRect(QRectF(0.04, 0.04, cell, cell));
        break;
    }
    case Icon::Linux: {  // simplified Tux silhouette
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(QPointF(0.0, 0.06), 0.25, 0.31);    // body
        p.drawEllipse(QPointF(0.0, -0.24), 0.17, 0.15);   // head
        p.drawEllipse(QPointF(-0.25, 0.04), 0.07, 0.14);  // flippers
        p.drawEllipse(QPointF(0.25, 0.04), 0.07, 0.14);
        p.drawEllipse(QPointF(-0.12, 0.32), 0.10, 0.045); // feet
        p.drawEllipse(QPointF(0.12, 0.32), 0.10, 0.045);
        // punch the eyes through the silhouette so the icon reads on any bg
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.drawEllipse(QPointF(-0.06, -0.26), 0.035, 0.035);
        p.drawEllipse(QPointF(0.06, -0.26), 0.035, 0.035);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        break;
    }
    case Icon::Cover: {  // album cover with a record
        p.setPen(strokePen(c, 0.085));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(-0.34, -0.34, 0.68, 0.68), 0.08, 0.08);
        p.drawEllipse(QPointF(0.0, 0.0), 0.15, 0.15);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(QPointF(0.0, 0.0), 0.045, 0.045);
        break;
    }
    case Icon::Metronome: {
        p.setPen(strokePen(c, 0.09));
        p.setBrush(Qt::NoBrush);
        QPainterPath tent;
        tent.moveTo(-0.30, 0.30);
        tent.lineTo(0.30, 0.30);
        tent.lineTo(0.12, -0.30);
        tent.lineTo(-0.12, -0.30);
        tent.closeSubpath();
        p.drawPath(tent);
        p.setPen(strokePen(c, 0.085));
        p.drawLine(QPointF(-0.03, 0.27), QPointF(0.15, -0.22));
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(QPointF(0.05, 0.05), 0.05, 0.05);  // weight
        break;
    }
    case Icon::Logo: {  // stylized symmetric bar waveform
        static const qreal kHeights[] = {0.20, 0.42, 0.66, 0.88,
                                         0.50, 1.00, 0.50, 0.88,
                                         0.66, 0.42, 0.20};
        constexpr int kBars = int(sizeof(kHeights) / sizeof(kHeights[0]));
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        QPainterPath bars;
        for (int i = 0; i < kBars; ++i) {
            const qreal x = -0.43 + 0.086 * i;
            const qreal halfH = kHeights[i] * 0.39;
            bars.addRoundedRect(QRectF(x - 0.025, -halfH, 0.05, halfH * 2.0), 0.025, 0.025);
        }
        p.drawPath(bars);
        break;
    }
    }
}

}  // namespace

QPixmap pixmap(Icon id, const QColor& color, int size) {
    if (size <= 0) {
        size = 24;
    }
    qreal dpr = 1.0;
    if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        dpr = screen->devicePixelRatio();
    }
    if (dpr <= 0.0) {
        dpr = 1.0;
    }
    QPixmap pm(QSize(qMax(1, qRound(size * dpr)), qMax(1, qRound(size * dpr))));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    const QColor c = color.isValid() ? color : theme::kText;
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal glyph = qreal(size) * 0.70;  // leave ~15% padding on each side
    p.translate(qreal(size) / 2.0, qreal(size) / 2.0);
    p.scale(glyph, glyph);
    drawGlyph(p, id, c);
    return pm;
}

QIcon icon(Icon id, const QColor& color, int size) {
    return QIcon(pixmap(id, color, size));
}

QPixmap material(const char* name, const QColor& color, int size) {
    if (size <= 0) {
        size = 24;
    }
    QSvgRenderer renderer(QStringLiteral(":/material/") + QString::fromLatin1(name) +
                          QStringLiteral(".svg"));
    if (!renderer.isValid()) {
        return QPixmap();
    }
    qreal dpr = 1.0;
    if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        dpr = screen->devicePixelRatio();
    }
    if (dpr <= 0.0) {
        dpr = 1.0;
    }
    const QSize px(qMax(1, qRound(size * dpr)), qMax(1, qRound(size * dpr)));
    QImage img(px, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    renderer.render(&p, QRect(QPoint(0, 0), px));
    p.end();

    // SVGs ship without fill attributes (black); recolor with SourceIn so the
    // same asset works for any tint.
    p.begin(&img);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(img.rect(), color);
    p.end();

    QPixmap pm = QPixmap::fromImage(img);
    pm.setDevicePixelRatio(dpr);
    return pm;
}

QPixmap playerMark(bool isMain, int size) {
    if (size <= 0) {
        size = 22;
    }
    qreal dpr = 1.0;
    if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        dpr = screen->devicePixelRatio();
    }
    if (dpr <= 0.0) {
        dpr = 1.0;
    }
    QPixmap pm(QSize(qMax(1, qRound(size * dpr)), qMax(1, qRound(size * dpr))));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    const QColor accent = isMain ? QColor(3, 239, 206)      // teal ring (mockup)
                                 : QColor(0, 229, 142);     // green disc (mockup)
    const QPointF c(qreal(size) / 2.0, qreal(size) / 2.0);
    const qreal discR = qreal(size) * 0.34;

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);

    // soft glow around the mark
    QRadialGradient glow(c, qreal(size) / 2.0);
    glow.setColorAt(0.0, QColor(accent.red(), accent.green(), accent.blue(), 110));
    glow.setColorAt(0.55, QColor(accent.red(), accent.green(), accent.blue(), 45));
    glow.setColorAt(1.0, QColor(accent.red(), accent.green(), accent.blue(), 0));
    p.setBrush(glow);
    p.drawEllipse(c, qreal(size) / 2.0, qreal(size) / 2.0);

    p.setBrush(accent);
    p.drawEllipse(c, discR, discR);
    if (isMain) {
        p.setBrush(QColor(10, 20, 31));  // panel bg hole (mockup ring)
        p.drawEllipse(c, discR * 0.34, discR * 0.34);
    }
    return pm;
}

}  // namespace lspa::icons
