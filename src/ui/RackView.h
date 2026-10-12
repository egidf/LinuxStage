// RackView.h - effects rack for one player: header, preset controls and a
// horizontally scrolling sequence of plugin UIs.

#pragma once

#include <QWidget>

#include "core/Types.h"

class QComboBox;
class QLabel;
class QScrollArea;
class QHBoxLayout;

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
    void closeNativeEditors();

signals:
    void addPluginRequested();
    void targetSelected(lspa::RackTarget target);

private slots:
    void onSavePreset();
    void onLoadPreset();

private:
    void buildUi();
    void rebuildCards();
    QColor accent() const;

    SessionController* controller_ = nullptr;
    RackTarget target_ = RackTarget::MainPlayer;

    QLabel* titleLabel_ = nullptr;
    QComboBox* targetCombo_ = nullptr;
    QComboBox* presetCombo_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QWidget* flow_ = nullptr;
    QHBoxLayout* flowLayout_ = nullptr;
};

}  // namespace lspa
