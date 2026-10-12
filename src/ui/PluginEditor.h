// PluginEditor.h - plugin parameter editor window.
//
// Uses the plugin's native UI (Suil, FR-06) when it can be embedded or shown
// in a standalone window; otherwise falls back to a generic parameter panel
// built from the plugin's port list.

#pragma once

#include <QWidget>

#include "core/Types.h"

class QLabel;
class QTimer;
class QVBoxLayout;

namespace lspa {

class AudioEngine;
class SessionController;
class Knob;

class PluginEditor : public QWidget {
    Q_OBJECT
public:
    PluginEditor(SessionController* controller, RackTarget target, const QString& uid,
                 const QString& title, QWidget* parent = nullptr);
    ~PluginEditor() override;

    QString uid() const { return uid_; }

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onTick();

private:
    void buildGeneric();
    bool tryNativeUi();

    SessionController* controller_ = nullptr;
    AudioEngine* engine_ = nullptr;
    RackTarget target_;
    QString uid_;
    QString title_;

    QLabel* modeLabel_ = nullptr;
    QVBoxLayout* root_ = nullptr;
    QWidget* nativeHost_ = nullptr;
    QWidget* genericPanel_ = nullptr;
    QTimer* timer_ = nullptr;
    QVector<Knob*> knobs_;
    bool nativeOpen_ = false;
};

}  // namespace lspa
