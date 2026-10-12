// MainWindow.h - top level window: title bar tabs, players, rack, browser,
// settings page and status bar.

#pragma once

#include <QMainWindow>

#include "core/Types.h"

class QLabel;
class QPushButton;
class QMenu;
class QStackedWidget;
class QComboBox;
class QLineEdit;
class QSlider;
class QPlainTextEdit;
class QCloseEvent;
class QEvent;
class QResizeEvent;
class QSpinBox;
class QToolButton;

namespace lspa {

class AudioEngine;
class SessionController;
class PlayerPanel;
class RackView;
class PluginBrowser;
class AudioSpectrumVisualizer;
class MasterVolumeDial;
class LevelMeter;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(SessionController* controller, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void setTab(int index);
    void updateStatusBar();
    void applySettings();
    void refreshLogView();

private:
    void buildChrome();
    void buildContent();
    void buildSettingsPage();
    void buildStatusBar();
    void buildMenus();
    void toggleMaximize();
    void onCatalogOrRackChanged();
    void syncTabButtons();
    QPushButton* makeTabButton(const QString& text, const QString& iconName, int index);
    void updateBgmAfterMainEnd();

    SessionController* controller_ = nullptr;
    AudioEngine* engine_ = nullptr;

    // chrome
    QWidget* chrome_ = nullptr;
    QVector<QPushButton*> tabButtons_;
    QToolButton* menuButton_ = nullptr;      // hamburger holding the old menus
    QToolButton* maxButton_ = nullptr;       // maximize / restore toggle glyph
    QLabel* statusLabel_ = nullptr;
    QLabel* statusValue_ = nullptr;
    QLabel* statusDot_ = nullptr;
    QLabel* cpuValue_ = nullptr;
    QLabel* rateValue_ = nullptr;
    QLabel* bufferValue_ = nullptr;
    LevelMeter* cpuMeter_ = nullptr;
    LevelMeter* mainMeter_ = nullptr;  // FR-11 output levels (status bar)
    LevelMeter* bgmMeter_ = nullptr;
    LevelMeter* outMeter_ = nullptr;
    QLabel* messageLabel_ = nullptr;
    QPushButton* autoVolumeButton_ = nullptr;
    QPushButton* mainCueButton_ = nullptr;
    QPushButton* bgmCueButton_ = nullptr;
    int autoVolumeJobsRemaining_ = 0;
    bool bgmStartedAfterMain_ = false;

    // frameless window: edge strips used to resize the window
    QVector<QWidget*> borderStrips_;
    void buildBorderStrips();
    QPushButton* rackChipMain_ = nullptr;
    QPushButton* rackChipBgm_ = nullptr;

    // content
    QWidget* playersRow_ = nullptr;
    QWidget* playerToolsRow_ = nullptr;
    QWidget* rackRow_ = nullptr;
    PlayerPanel* mainPanel_ = nullptr;
    PlayerPanel* bgmPanel_ = nullptr;
    RackView* rackView_ = nullptr;
    PluginBrowser* browser_ = nullptr;
    AudioSpectrumVisualizer* spectrumVisualizer_ = nullptr;
    MasterVolumeDial* masterVolumeDial_ = nullptr;
    QWidget* settingsPage_ = nullptr;
    int currentTab_ = 0;

    // settings widgets
    QSlider* masterVolume_ = nullptr;
    QComboBox* rateCombo_ = nullptr;
    QComboBox* bufferCombo_ = nullptr;
    QLineEdit* deviceEdit_ = nullptr;
    QSlider* fadeInMain_ = nullptr;
    QSlider* fadeOutMain_ = nullptr;
    QSlider* fadeInBgm_ = nullptr;
    QSlider* fadeOutBgm_ = nullptr;
    QLabel* fadeInMainLabel_ = nullptr;
    QLabel* fadeOutMainLabel_ = nullptr;
    QLabel* fadeInBgmLabel_ = nullptr;
    QLabel* fadeOutBgmLabel_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;

};

}  // namespace lspa
