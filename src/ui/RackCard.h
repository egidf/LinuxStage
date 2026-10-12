// RackCard.h - one plugin slot in the effects rack.

#pragma once

#include <QWidget>

#include <QColor>
#include <QVector>

#include "core/Types.h"
#include "plugins/IPluginHost.h"

class QLabel;
class QPushButton;
class QToolButton;

namespace lspa {

class Knob;

class RackCard : public QWidget {
    Q_OBJECT
public:
    RackCard(const QString& uid, RackTarget target, int number, const QString& title,
             const QString& subtitle, const QColor& accent, QWidget* parent = nullptr);

    QString uid() const { return uid_; }

    void setParameters(const QVector<PluginParam>& params);
    void setStatus(PluginLoadStatus status, const QString& error);
    void setCardEnabled(bool enabled);
    bool cardEnabled() const { return enabled_; }
    void setMiniGraphKind(int kind) { graphKind_ = kind; update(); }

    QSize sizeHint() const override { return QSize(238, 300); }
    QSize minimumSizeHint() const override { return QSize(200, 240); }

signals:
    void toggled(const QString& uid, bool enabled);
    void paramChanged(const QString& uid, const QString& symbol, float value);
    void removeRequested(const QString& uid);
    void editRequested(const QString& uid);
    void moveUpRequested(const QString& uid);
    void moveDownRequested(const QString& uid);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void buildUi(const QString& title, const QString& subtitle, int number);
    void drawMiniGraph(QPainter& painter, const QRect& rect);
    void showMenu();

    QString uid_;
    RackTarget target_;
    QColor accent_;
    bool enabled_ = true;
    int graphKind_ = 0;
    PluginLoadStatus status_ = PluginLoadStatus::Loaded;
    QString error_;
    QVector<PluginParam> params_;
    QVector<Knob*> knobs_;
    QWidget* knobsHost_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QWidget* graphHost_ = nullptr;
};

// Helper: classify a plugin into a mini-graph style.
int graphKindForCategory(const QString& category, const QString& name);

}  // namespace lspa
