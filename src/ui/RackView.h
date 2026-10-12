// RackView.h - effects rack for one player: header, preset controls and the
// horizontal card list.

#pragma once

#include <QWidget>

#include "core/Types.h"

class QComboBox;
class QHBoxLayout;
class QLabel;
class QScrollArea;
class QVBoxLayout;

namespace lspa {

class RackCard;
class SessionController;

class RackView : public QWidget {
    Q_OBJECT
public:
    explicit RackView(SessionController* controller, QWidget* parent = nullptr);

    RackTarget target() const { return target_; }
    void setTarget(RackTarget target);
    void reload();

signals:
    void addPluginRequested();
    void targetSelected(lspa::RackTarget target);
    void editPluginRequested(lspa::RackTarget target, const QString& uid);

private slots:
    void onSavePreset();
    void onLoadPreset();

private:
    void buildUi();
    void rebuildCards();
    void syncArrowHeights();
    QColor accent() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

    SessionController* controller_ = nullptr;
    RackTarget target_ = RackTarget::MainPlayer;

    QLabel* titleLabel_ = nullptr;
    QComboBox* targetCombo_ = nullptr;
    QComboBox* presetCombo_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QWidget* flow_ = nullptr;
    QHBoxLayout* flowLayout_ = nullptr;
    // Signal arrows paired with the card that follows them, so each arrow's
    // height tracks its card and the pixmap stays vertically centered
    // (flow_ stretches to the viewport, so AlignVCenter alone would not).
    QList<QPair<QLabel*, QWidget*>> arrowCards_;
};

}  // namespace lspa
