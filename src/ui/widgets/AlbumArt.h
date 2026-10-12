// AlbumArt.h - cover art tile with generated gradient art fallback.

#pragma once

#include <QPixmap>
#include <QString>
#include <QWidget>

namespace lspa {

class AlbumArt : public QWidget {
    Q_OBJECT
public:
    explicit AlbumArt(QWidget* parent = nullptr);

    void setCover(const QPixmap& cover);
    // Deterministic abstract art derived from the seed text (track path).
    void setSeed(const QString& seed);
    void setCaption(const QString& title, const QString& subtitle);
    void setAccent(const QColor& color) { accent_ = color; update(); }

    QSize minimumSizeHint() const override { return QSize(120, 120); }
    QSize sizeHint() const override { return QSize(180, 180); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QPixmap cover_;
    QString title_;
    QString subtitle_;
    QColor accent_;
};

}  // namespace lspa
