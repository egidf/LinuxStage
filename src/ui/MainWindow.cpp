#include "ui/MainWindow.h"

#include <QAction>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QApplication>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QShortcut>
#include <QSlider>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

#include <functional>

#include "core/Logger.h"
#include "core/Paths.h"
#include "ui/PluginBrowser.h"
#include "ui/PluginEditor.h"
#include "ui/PlayerPanel.h"
#include "ui/RackView.h"
#include "ui/SessionController.h"
#include "ui/theme/Theme.h"
#include "ui/widgets/Icons.h"
#include "ui/widgets/LevelMeter.h"

namespace lspa {

namespace {

const int kTabPlayer = 0;
const int kTabRack = 1;
const int kTabPlaylist = 2;
const int kTabSettings = 3;

// Rounded chip in the chrome bar, optionally with a leading platform icon.
QWidget* makeChip(const QString& text, const QColor& color, bool active,
                  const QString& iconName = QString()) {
    auto* chip = new QWidget();
    auto* lay = new QHBoxLayout(chip);
    lay->setContentsMargins(11, 4, 12, 4);
    lay->setSpacing(6);
    chip->setStyleSheet(
        QString("QWidget { background-color: %1; border: 1px solid %2;"
                " border-radius: 6px; }")
            .arg(active ? QColor(31, 111, 235, 60).name(QColor::HexArgb) : theme::kPanelAlt.name(),
                 active ? color.name() : theme::kBorder.name()));
    if (!iconName.isEmpty()) {
        auto* icon = new QLabel(chip);
        icon->setPixmap(icons::pixmap(iconName == QLatin1String("linux") ? icons::Icon::Linux
                                                                        : icons::Icon::Windows,
                                      active ? theme::kText : theme::kTextDim, 14));
        lay->addWidget(icon);
    }
    auto* label = new QLabel(text, chip);
    label->setStyleSheet(
        QString("color: %1; font-size: 12px; background: transparent; border: none;")
            .arg(active ? QColor(255, 255, 255).name() : theme::kTextDim.name()));
    lay->addWidget(label);
    return chip;
}

}  // namespace

MainWindow::MainWindow(SessionController* controller, QWidget* parent)
    : QMainWindow(parent), controller_(controller), engine_(&controller->engine()) {
    setWindowTitle(tr("LSP Audio Player"));
    setWindowFlag(Qt::FramelessWindowHint);  // window buttons live in the chrome bar
    setMinimumSize(1180, 760);
    resize(1440, 900);

    buildChrome();
    buildContent();
    buildStatusBar();
    buildMenus();

    // --- wiring -------------------------------------------------------------
    connect(controller_, &SessionController::catalogUpdated, this,
            &MainWindow::onCatalogOrRackChanged);
    connect(controller_, &SessionController::rackChanged, this,
            [this](RackTarget) { onCatalogOrRackChanged(); });
    connect(controller_, &SessionController::playlistChanged, this,
            [this](PlayerId id) {
                if (id == PlayerId::Main) mainPanel_->refreshPlaylist();
                else bgmPanel_->refreshPlaylist();
            });
    connect(controller_, &SessionController::sessionMessage, this,
            [this](const QString& text, bool isError) {
                messageLabel_->setText(text);
                messageLabel_->setStyleSheet(
                    QString("color: %1; font-size: 12px;")
                        .arg(isError ? theme::kWarn.name() : theme::kTextDim.name()));
                messageLabel_->setVisible(!text.isEmpty());
            });
    connect(controller_, &SessionController::audioRestarted, this, [this] {
        rateCombo_->setCurrentText(QString::number(engine_->config().sampleRate));
        bufferCombo_->setCurrentText(QString::number(engine_->config().blockSize));
        deviceEdit_->setText(QString::fromStdString(engine_->config().device));
    });

    connect(rackView_, &RackView::addPluginRequested, this,
            [this] { browser_->focusSearch(); });
    connect(rackView_, &RackView::editPluginRequested, this, &MainWindow::openEditorFor);
    connect(rackView_, &RackView::targetSelected, this, [this](RackTarget target) {
        rackChipMain_->setChecked(target == RackTarget::MainPlayer);
        rackChipBgm_->setChecked(target == RackTarget::BgmPlayer);
    });
    connect(browser_, &PluginBrowser::pluginAddRequested, this,
            [this](const PluginKey& key) { controller_->addPlugin(rackView_->target(), key); });

    // keyboard shortcuts
    // NOTE: "Space" for Main play/pause lives in the Playback menu action only
    // (a duplicate QShortcut here caused "Ambiguous shortcut overload").
    for (int i = 0; i < 4; ++i) {
        auto* sc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_1 + i), this);
        connect(sc, &QShortcut::activated, this, [this, i] { setTab(i); });
    }

    auto* statusTimer = new QTimer(this);
    statusTimer->setInterval(400);
    connect(statusTimer, &QTimer::timeout, this, &MainWindow::updateStatusBar);
    statusTimer->start();

    auto* messageTimer = new QTimer(this);
    messageTimer->setInterval(12000);
    connect(messageTimer, &QTimer::timeout, this, [this] { messageLabel_->setVisible(false); });
    messageTimer->start();

    updateStatusBar();
    setTab(kTabPlayer);
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
// chrome (title bar area with tabs)
// ---------------------------------------------------------------------------

QPushButton* MainWindow::makeTabButton(const QString& text, const QString& iconName, int index) {
    auto* b = new QPushButton(this);
    b->setCheckable(true);
    b->setCursor(Qt::PointingHandCursor);
    b->setObjectName(QString("tabButton%1").arg(index));
    b->setText(QString("  ") + text);
    b->setIcon(QIcon(icons::pixmap(iconName == QLatin1String("player")
                                       ? icons::Icon::Music
                                   : iconName == QLatin1String("rack")
                                       ? icons::Icon::Plugin
                                   : iconName == QLatin1String("playlist")
                                       ? icons::Icon::ClearList
                                       : icons::Icon::Gear,
                                   theme::kTextDim, 16)));
    b->setStyleSheet(
        "QPushButton { background: rgba(255,255,255,0.02); border: 1px solid transparent;"
        " border-radius: 9px; color: #8B949E; padding: 7px 16px; font-size: 13px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.05); border-color: rgba(255,255,255,0.07);"
        " color: #E6EDF3; }"
        "QPushButton:checked { background: linear-gradient(180deg, #1e6efb, #185bda);"
        " border-color: rgba(88,166,255,0.9); color: white; font-weight: 600; }");
    connect(b, &QPushButton::clicked, this, [this, index] { setTab(index); });
    return b;
}

void MainWindow::buildChrome() {
    chrome_ = new QWidget(this);
    chrome_->setObjectName("chromeBar");
    chrome_->setFixedHeight(58);
    chrome_->setStyleSheet(
        QString("QWidget#chromeBar { background-color: %1; border-bottom: 1px solid %2; }")
            .arg(theme::kPanel.name(), theme::kBorder.name()));
    chrome_->installEventFilter(this);  // drag to move, double-click to maximize

    auto* lay = new QHBoxLayout(chrome_);
    lay->setContentsMargins(18, 8, 12, 8);
    lay->setSpacing(8);

    auto* logo = new QLabel(chrome_);
    logo->setPixmap(icons::pixmap(icons::Icon::Logo, theme::kAccent, 30));
    lay->addWidget(logo);

    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(0);
    auto* title = new QLabel(tr("LSP Audio Player"), chrome_);
    title->setStyleSheet("font-size: 15px; font-weight: 700;");
    auto* subtitle = new QLabel(tr("Advanced audio player with LSP plugins"), chrome_);
    subtitle->setStyleSheet(
        QString("color: %1; font-size: 10px;").arg(theme::kTextDim.name()));
    titleCol->addWidget(title);
    titleCol->addWidget(subtitle);
    lay->addLayout(titleCol);
    lay->addSpacing(18);

    tabButtons_.push_back(makeTabButton(tr("Player"), "player", kTabPlayer));
    tabButtons_.push_back(makeTabButton(tr("Effects Rack"), "rack", kTabRack));
    tabButtons_.push_back(makeTabButton(tr("Playlist"), "playlist", kTabPlaylist));
    tabButtons_.push_back(makeTabButton(tr("Settings"), "settings", kTabSettings));
    const QString sepStyle = QString("background-color: %1;").arg(theme::kBorder.name());
    for (int i = 0; i < tabButtons_.size(); ++i) {
        if (i > 0) {
            auto* sep = new QFrame(chrome_);
            sep->setFixedWidth(1);
            sep->setFixedHeight(20);
            sep->setStyleSheet(sepStyle);
            lay->addWidget(sep);
        }
        lay->addWidget(tabButtons_[i]);
    }
    lay->addStretch(1);

    lay->addWidget(makeChip(tr("Linux"), theme::kAccent, true, "linux"));
    lay->addWidget(makeChip(tr("Windows"), theme::kTextDim, false, "windows"));

    // Hamburger button carrying the old menu bar contents.
    menuButton_ = new QToolButton(chrome_);
    menuButton_->setObjectName("chromeMenuButton");
    menuButton_->setPopupMode(QToolButton::InstantPopup);
    menuButton_->setToolTip(tr("Menu"));
    menuButton_->setFixedSize(30, 28);
    menuButton_->setAutoRaise(true);
    menuButton_->setText(QChar(0x2630));  // trigram for heaven (hamburger)
    menuButton_->setStyleSheet(
        "QToolButton { background: transparent; border: 1px solid transparent;"
        " border-radius: 6px; color: #8B949E; font-size: 15px; }"
        "QToolButton:hover { background: rgba(255,255,255,0.07); border-color: #30363D;"
        " color: #E6EDF3; }"
        "QToolButton::menu-indicator { image: none; }");
    lay->addWidget(menuButton_);
    lay->addSpacing(6);

    // Window controls (the window itself is frameless).
    auto makeWinButton = [&](const QString& glyph, bool danger) {
        auto* b = new QToolButton(chrome_);
        b->setAutoRaise(true);
        b->setFixedSize(32, 28);
        b->setText(glyph);
        b->setStyleSheet(
            QString("QToolButton { background: transparent; border: none; border-radius: 6px;"
                    " color: #8B949E; font-size: 13px; padding: 0 8px; }"
                    "QToolButton:hover { background: %1; color: white; }")
                .arg(danger ? QColor(200, 50, 50).name() : QColor(255, 255, 255, 28).name()));
        return b;
    };
    auto* minButton = makeWinButton(QChar(0x2013), false);  // en dash
    minButton->setToolTip(tr("Minimize"));
    connect(minButton, &QToolButton::clicked, this, &QWidget::showMinimized);
    lay->addWidget(minButton);

    maxButton_ = makeWinButton(QChar(0x25A1), false);  // white square
    maxButton_->setToolTip(tr("Maximize"));
    connect(maxButton_, &QToolButton::clicked, this, &MainWindow::toggleMaximize);
    lay->addWidget(maxButton_);

    auto* closeButton = makeWinButton(QChar(0x00D7), true);  // multiplication sign
    closeButton->setToolTip(tr("Close"));
    connect(closeButton, &QToolButton::clicked, this, &QWidget::close);
    lay->addWidget(closeButton);

    // Resize handles along the frameless border.
    installEventFilter(this);
    buildBorderStrips();

    setMenuWidget(chrome_);
}

// Transparent strips along the window edges that let the frameless window be
// resized (they forward to the window system's interactive resize).
void MainWindow::buildBorderStrips() {
    const int k = 7;  // strip thickness in px
    struct Spec {
        Qt::Edges edges;
        Qt::CursorShape cursor;
    };
    const Spec specs[] = {
        {Qt::TopEdge, Qt::SizeVerCursor},      {Qt::BottomEdge, Qt::SizeVerCursor},
        {Qt::LeftEdge, Qt::SizeHorCursor},     {Qt::RightEdge, Qt::SizeHorCursor},
        {Qt::TopEdge | Qt::LeftEdge, Qt::SizeFDiagCursor},
        {Qt::TopEdge | Qt::RightEdge, Qt::SizeBDiagCursor},
        {Qt::BottomEdge | Qt::LeftEdge, Qt::SizeBDiagCursor},
        {Qt::BottomEdge | Qt::RightEdge, Qt::SizeFDiagCursor},
    };
    for (const Spec& s : specs) {
        auto* strip = new QWidget(this);
        strip->setCursor(s.cursor);
        strip->installEventFilter(this);
        strip->setProperty("edges", int(s.edges));
        borderStrips_.push_back(strip);
        strip->show();
    }
    // position the strips now (resizeEvent does the same on every resize)
    const int w = width(), h = height();
    borderStrips_[0]->setGeometry(k, 0, w - 2 * k, k);
    borderStrips_[1]->setGeometry(k, h - k, w - 2 * k, k);
    borderStrips_[2]->setGeometry(0, k, k, h - 2 * k);
    borderStrips_[3]->setGeometry(w - k, k, k, h - 2 * k);
    borderStrips_[4]->setGeometry(0, 0, k, k);
    borderStrips_[5]->setGeometry(w - k, 0, k, k);
    borderStrips_[6]->setGeometry(0, h - k, k, k);
    borderStrips_[7]->setGeometry(w - k, h - k, k, k);
    for (QWidget* s : borderStrips_) s->raise();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    // --- drag the window from empty chrome areas / double-click to maximize
    if (watched == chrome_) {
        switch (event->type()) {
            case QEvent::MouseButtonPress: {
                auto* me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::LeftButton) {
                    if (QWindow* wh = windowHandle()) wh->startSystemMove();
                    return true;
                }
                break;
            }
            case QEvent::MouseButtonDblClick:
                toggleMaximize();
                return true;
            default:
                break;
        }
        return QMainWindow::eventFilter(watched, event);
    }

    // --- edge strips: interactive resize via the window system
    if (QWidget* strip = qobject_cast<QWidget*>(watched)) {
        if (borderStrips_.contains(strip) && event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                if (QWindow* wh = windowHandle()) {
                    wh->startSystemResize(Qt::Edges(strip->property("edges").toInt()));
                }
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (borderStrips_.size() != 8) return;
    const int k = 7;
    const int w = width(), h = height();
    borderStrips_[0]->setGeometry(k, 0, w - 2 * k, k);                        // top
    borderStrips_[1]->setGeometry(k, h - k, w - 2 * k, k);                    // bottom
    borderStrips_[2]->setGeometry(0, k, k, h - 2 * k);                        // left
    borderStrips_[3]->setGeometry(w - k, k, k, h - 2 * k);                    // right
    borderStrips_[4]->setGeometry(0, 0, k, k);                                // TL
    borderStrips_[5]->setGeometry(w - k, 0, k, k);                            // TR
    borderStrips_[6]->setGeometry(0, h - k, k, k);                            // BL
    borderStrips_[7]->setGeometry(w - k, h - k, k, k);                        // BR
    for (QWidget* s : borderStrips_) s->raise();
}

void MainWindow::toggleMaximize() {
    const bool wasMax = isMaximized();
    setWindowState(wasMax ? windowState() & ~Qt::WindowMaximized
                          : windowState() | Qt::WindowMaximized);
    if (maxButton_) maxButton_->setText(wasMax ? QChar(0x25A1) : QChar(0x2750));
}

// ---------------------------------------------------------------------------
// content
// ---------------------------------------------------------------------------

void MainWindow::buildContent() {
    auto* central = new QWidget(this);
    central->setObjectName("mainBackground");
    central->setStyleSheet(
        QString("QWidget#mainBackground { background-color: %1; }").arg(theme::kWindow.name()));
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(16, 14, 16, 0);
    root->setSpacing(16);

    messageLabel_ = new QLabel(central);
    messageLabel_->setWordWrap(true);
    messageLabel_->setVisible(false);
    root->addWidget(messageLabel_);

    // players row
    playersRow_ = new QWidget(central);
    playersRow_->setAttribute(Qt::WA_StyledBackground, true);
    auto* playersLay = new QHBoxLayout(playersRow_);
    playersLay->setContentsMargins(0, 0, 0, 0);
    playersLay->setSpacing(20);

    auto makePanelBox = [&](PlayerId id) {
        auto* box = new QWidget(playersRow_);
        box->setAttribute(Qt::WA_StyledBackground, true);
        // NOTE: scope the rule to this box only - a bare "QWidget {}" sheet on
        // a parent would style every descendant label/widget too.
        box->setObjectName(id == PlayerId::Main ? "mainPlayerBox" : "bgmPlayerBox");
        // Mockup tokens: panel sits darker than the page with a saturated
        // blue (Main) / teal (BGM) hairline border and a 14px radius.
        const bool isMain = id == PlayerId::Main;
        box->setStyleSheet(
            QString("QWidget#%1 { background-color: %2; border: 1px solid %3;"
                    " border-radius: 14px; }")
                .arg(isMain ? QStringLiteral("mainPlayerBox") : QStringLiteral("bgmPlayerBox"),
                     QColor(10, 20, 31).name(),                       // #0A141F
                     isMain ? QStringLiteral("#14558F")
                            : QStringLiteral("#177363")));
        auto* lay = new QHBoxLayout(box);
        lay->setContentsMargins(0, 0, 0, 0);
        return std::make_pair(box, lay);
    };

    auto mainBox = makePanelBox(PlayerId::Main);
    mainPanel_ = new PlayerPanel(PlayerId::Main, engine_, controller_, mainBox.first);
    mainBox.second->addWidget(mainPanel_);
    // 7:6 mirrors the mockup panel ratio (873 : 742).
    playersLay->addWidget(mainBox.first, 7);

    auto bgmBox = makePanelBox(PlayerId::Bgm);
    bgmPanel_ = new PlayerPanel(PlayerId::Bgm, engine_, controller_, bgmBox.first);
    bgmBox.second->addWidget(bgmPanel_);
    playersLay->addWidget(bgmBox.first, 6);

    // Natural height: the panel sizes to its content (like the mockup's
    // 873x350 panel); the rack below absorbs the remaining window space.
    root->addWidget(playersRow_);

    // rack row
    rackRow_ = new QWidget(central);
    rackRow_->setAttribute(Qt::WA_StyledBackground, true);
    auto* rackLay = new QHBoxLayout(rackRow_);
    rackLay->setContentsMargins(0, 0, 0, 0);
    rackLay->setSpacing(12);

    rackView_ = new RackView(controller_, rackRow_);
    rackView_->setObjectName("rackView");
    rackView_->setStyleSheet(
        QString("QWidget#rackView { background-color: %1; border: 1px solid %2;"
                " border-radius: 12px; }")
            .arg(theme::kPanel.name(), theme::kBorder.name()));
    rackLay->addWidget(rackView_, 1);

    browser_ = new PluginBrowser(controller_, rackRow_);
    browser_->setStyleSheet(
        QString("QWidget#pluginBrowser { background-color: %1; border: 1px solid %2;"
                " border-radius: 12px; }")
            .arg(theme::kPanel.name(), theme::kBorder.name()));
    browser_->setFixedWidth(330);
    rackLay->addWidget(browser_, 0);

    root->addWidget(rackRow_, 2);

    // settings page (hidden until the Settings tab is chosen)
    buildSettingsPage();
    settingsPage_->setParent(central);
    settingsPage_->setVisible(false);
    root->addWidget(settingsPage_, 1);

    setCentralWidget(central);
}

void MainWindow::buildSettingsPage() {
    settingsPage_ = new QWidget();
    auto* scroll = new QScrollArea(settingsPage_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* lay = new QVBoxLayout(settingsPage_);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(scroll);

    auto* page = new QWidget();
    scroll->setWidget(page);
    auto* form = new QVBoxLayout(page);
    form->setContentsMargins(8, 8, 8, 24);
    form->setSpacing(14);

    const QString groupStyle =
        QString("QGroupBox { background-color: %1; border: 1px solid %2; border-radius: 10px;"
                " margin-top: 14px; padding: 12px; font-weight: 600; }"
                "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; }")
            .arg(theme::kPanel.name(), theme::kBorder.name());

    // --- audio device
    auto* audioGroup = new QGroupBox(tr("Audio device"), page);
    audioGroup->setStyleSheet(groupStyle);
    auto* audioForm = new QFormLayout(audioGroup);
    audioForm->setSpacing(10);

    rateCombo_ = new QComboBox(audioGroup);
    rateCombo_->addItems({"44100", "48000", "88200", "96000"});
    audioForm->addRow(tr("Sample rate (Hz)"), rateCombo_);

    bufferCombo_ = new QComboBox(audioGroup);
    bufferCombo_->addItems({"64", "128", "256", "512", "1024", "2048"});
    audioForm->addRow(tr("Buffer size (frames)"), bufferCombo_);

    deviceEdit_ = new QLineEdit(audioGroup);
    deviceEdit_->setPlaceholderText("default");
    deviceEdit_->setToolTip(tr("Any ALSA device, for example 'default', 'pulse', 'hw:0,0' or"
                               " 'null' for a silent device."));
    audioForm->addRow(tr("ALSA device"), deviceEdit_);

    auto* applyBtn = new QPushButton(tr("Apply and restart audio engine"), audioGroup);
    applyBtn->setCursor(Qt::PointingHandCursor);
    connect(applyBtn, &QPushButton::clicked, this, &MainWindow::applySettings);
    audioForm->addRow(QString(), applyBtn);
    form->addWidget(audioGroup);

    // --- master
    auto* masterGroup = new QGroupBox(tr("Master"), page);
    masterGroup->setStyleSheet(groupStyle);
    auto* masterForm = new QFormLayout(masterGroup);
    masterVolume_ = new QSlider(Qt::Horizontal, masterGroup);
    masterVolume_->setRange(0, 200);
    masterVolume_->setValue(100);
    connect(masterVolume_, &QSlider::valueChanged, this, [this](int v) {
        engine_->setMasterVolume(static_cast<float>(v) / 100.0f);
    });
    masterForm->addRow(tr("Master volume"), masterVolume_);
    form->addWidget(masterGroup);

    // --- fades
    auto* fadeGroup = new QGroupBox(tr("Fades (FR-03)"), page);
    fadeGroup->setStyleSheet(groupStyle);
    auto* fadeForm = new QFormLayout(fadeGroup);
    auto makeFadeSlider = [&](QSlider*& slider, QLabel*& label, bool fadeIn, PlayerId id) {
        slider = new QSlider(Qt::Horizontal, fadeGroup);
        slider->setRange(0, 3000);
        auto apply = [this, slider, id, fadeIn] {
            Player& p = engine_->player(id);
            FadeConfig f = p.fadeConfig();
            if (fadeIn) f.fadeInMs = slider->value();
            else f.fadeOutMs = slider->value();
            p.setFadeConfig(f);
        };
        connect(slider, &QSlider::valueChanged, this, apply);
        label = new QLabel(fadeGroup);
        label->setStyleSheet(
            QString("color: %1; font-family: 'DejaVu Sans Mono';").arg(theme::kTextDim.name()));
        connect(slider, &QSlider::valueChanged, label,
                [label](int v) { label->setText(QString("%1 ms").arg(v)); });
    };
    makeFadeSlider(fadeInMain_, fadeInMainLabel_, true, PlayerId::Main);
    makeFadeSlider(fadeOutMain_, fadeOutMainLabel_, false, PlayerId::Main);
    makeFadeSlider(fadeInBgm_, fadeInBgmLabel_, true, PlayerId::Bgm);
    makeFadeSlider(fadeOutBgm_, fadeOutBgmLabel_, false, PlayerId::Bgm);
    fadeForm->addRow(tr("Main fade-in"), fadeInMain_);
    fadeForm->addRow(QString(), fadeInMainLabel_);
    fadeForm->addRow(tr("Main fade-out"), fadeOutMain_);
    fadeForm->addRow(QString(), fadeOutMainLabel_);
    fadeForm->addRow(tr("BGM fade-in"), fadeInBgm_);
    fadeForm->addRow(QString(), fadeInBgmLabel_);
    fadeForm->addRow(tr("BGM fade-out"), fadeOutBgm_);
    fadeForm->addRow(QString(), fadeOutBgmLabel_);
    form->addWidget(fadeGroup);

    // --- diagnostics
    auto* logGroup = new QGroupBox(tr("Diagnostics (FR-09)"), page);
    logGroup->setStyleSheet(groupStyle);
    auto* logLay = new QVBoxLayout(logGroup);
    auto* logButtons = new QHBoxLayout();
    auto* copyBtn = new QPushButton(tr("Copy log"), logGroup);
    auto* openBtn = new QPushButton(tr("Open log file"), logGroup);
    connect(copyBtn, &QPushButton::clicked, this, [] {
        QApplication::clipboard()->setText(QString::fromStdString(Logger::instance().dump()));
    });
    connect(openBtn, &QPushButton::clicked, this, [] {
        ensureDir(dataDir());
        Logger::instance().setFile(defaultLogPath());
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(dataDir())));
    });
    logButtons->addWidget(copyBtn);
    logButtons->addWidget(openBtn);
    logButtons->addStretch(1);
    logLay->addLayout(logButtons);
    logView_ = new QPlainTextEdit(logGroup);
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(500);
    logView_->setStyleSheet(
        QString("QPlainTextEdit { background-color: %1; color: %2; border: 1px solid %3;"
                " border-radius: 6px; font-family: 'DejaVu Sans Mono'; font-size: 11px; }")
            .arg(theme::kPanelDeep.name(), theme::kTextDim.name(), theme::kBorder.name()));
    logLay->addWidget(logView_);
    form->addWidget(logGroup);

    form->addStretch(1);
}

// ---------------------------------------------------------------------------
// status bar
// ---------------------------------------------------------------------------

void MainWindow::buildStatusBar() {
    auto* bar = new QWidget(this);
    bar->setObjectName("statusBar");
    bar->setFixedHeight(34);
    bar->setStyleSheet(
        QString("QWidget#statusBar { background-color: %1; border-top: 1px solid %2; }")
            .arg(QColor(5, 14, 21).name(),    // #050E15 (mockup status bar)
                 QColor(28, 37, 49).name()));
    auto* lay = new QHBoxLayout(bar);
    lay->setContentsMargins(16, 4, 16, 4);
    lay->setSpacing(14);

    auto dimText = [](QLabel* l, const QString& text, int size) {
        l->setText(text);
        l->setStyleSheet(QString("color: %1; font-size: %2px;")
                             .arg(theme::kTextDim.name()).arg(size));
    };

    auto* statusTitle = new QLabel(tr("Status"), bar);
    dimText(statusTitle, tr("Status"), 12);
    lay->addWidget(statusTitle);
    statusDot_ = new QLabel(bar);
    statusDot_->setFixedSize(8, 8);
    statusDot_->setStyleSheet(
        QString("background-color: %1; border-radius: 4px;").arg(theme::kOk.name()));
    lay->addWidget(statusDot_);
    statusValue_ = new QLabel(bar);
    dimText(statusValue_, tr("Ready"), 12);
    lay->addWidget(statusValue_);
    lay->addSpacing(10);

    auto* cpuTitle = new QLabel(tr("CPU"), bar);
    dimText(cpuTitle, tr("CPU"), 12);
    lay->addWidget(cpuTitle);
    cpuValue_ = new QLabel(bar);
    dimText(cpuValue_, tr("0%"), 12);
    cpuValue_->setStyleSheet(
        QString("color: %1; font-size: 12px; font-family: 'DejaVu Sans Mono';")
            .arg(theme::kText.name()));
    lay->addWidget(cpuValue_);
    cpuMeter_ = new LevelMeter(bar);
    cpuMeter_->setAccent(theme::kAccent);
    cpuMeter_->setFixedSize(60, 8);
    lay->addWidget(cpuMeter_, 0, Qt::AlignVCenter);
    lay->addSpacing(10);

    // FR-11 output level meters: the mockup keeps them out of the player
    // panels, so Main / BGM / master live here in the status bar instead.
    auto makeLevel = [&](const QString& name, const QColor& accent) {
        auto* title = new QLabel(name, bar);
        dimText(title, name, 12);
        lay->addWidget(title);
        auto* meter = new LevelMeter(bar);
        meter->setAccent(accent);
        meter->setFixedSize(56, 8);
        lay->addWidget(meter, 0, Qt::AlignVCenter);
        return meter;
    };
    mainMeter_ = makeLevel(tr("Main"), QColor(42, 168, 254));
    lay->addSpacing(10);
    bgmMeter_ = makeLevel(tr("BGM"), QColor(2, 238, 177));
    lay->addSpacing(10);
    outMeter_ = makeLevel(tr("Out"), theme::kOk);
    lay->addSpacing(10);

    auto* rateTitle = new QLabel(tr("Sample Rate"), bar);
    dimText(rateTitle, tr("Sample Rate"), 12);
    lay->addWidget(rateTitle);
    rateValue_ = new QLabel(bar);
    dimText(rateValue_, tr("48 kHz"), 12);
    lay->addWidget(rateValue_);
    lay->addSpacing(10);

    auto* bufTitle = new QLabel(tr("Buffer"), bar);
    dimText(bufTitle, tr("Buffer"), 12);
    lay->addWidget(bufTitle);
    bufferValue_ = new QLabel(bar);
    dimText(bufferValue_, tr("256"), 12);
    lay->addWidget(bufferValue_);
    lay->addStretch(1);

    auto* gear = new QLabel(bar);
    gear->setPixmap(icons::pixmap(icons::Icon::Gear, theme::kTextDim, 16));
    lay->addWidget(gear);
    auto* dual = new QLabel(tr("Dual Rack Mode"), bar);
    dimText(dual, tr("Dual Rack Mode"), 12);
    lay->addWidget(dual);

    rackChipMain_ = new QPushButton(tr("Main Player Rack"), bar);
    rackChipBgm_ = new QPushButton(tr("BGM Rack"), bar);
    auto styleChip = [](QPushButton* b, const QColor& c, bool checked) {
        b->setCheckable(true);
        b->setChecked(checked);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(
            QString("QPushButton { background-color: %1; color: %2; border: 1px solid %3;"
                    " border-radius: 6px; padding: 3px 10px; font-size: 11px; }"
                    "QPushButton:checked { font-weight: 600; }")
                .arg(checked ? c.name() : theme::kPanelAlt.name(),
                     checked ? QColor(255, 255, 255).name() : theme::kTextDim.name(),
                     checked ? c.name() : theme::kBorder.name()));
    };
    styleChip(rackChipMain_, theme::kAccent, true);
    styleChip(rackChipBgm_, theme::kAccent2, false);
    connect(rackChipMain_, &QPushButton::clicked, this, [this, styleChip] {
        rackView_->setTarget(RackTarget::MainPlayer);
        styleChip(rackChipMain_, theme::kAccent, true);
        styleChip(rackChipBgm_, theme::kAccent2, false);
        if (currentTab_ != kTabRack && currentTab_ != kTabPlayer) setTab(kTabRack);
    });
    connect(rackChipBgm_, &QPushButton::clicked, this, [this, styleChip] {
        rackView_->setTarget(RackTarget::BgmPlayer);
        styleChip(rackChipMain_, theme::kAccent, false);
        styleChip(rackChipBgm_, theme::kAccent2, true);
        if (currentTab_ != kTabRack && currentTab_ != kTabPlayer) setTab(kTabRack);
    });
    lay->addWidget(rackChipMain_);
    lay->addWidget(rackChipBgm_);

    statusBar()->setSizeGripEnabled(true);
    statusBar()->addPermanentWidget(bar, 1);
}

// ---------------------------------------------------------------------------
// menus & tabs
// ---------------------------------------------------------------------------

void MainWindow::buildMenus() {
    auto* menu = new QMenu(this);
    menu->setStyleSheet(
        "QMenu { background: #161B22; border: 1px solid #30363D; border-radius: 8px;"
        " padding: 4px; }"
        "QMenu::item { color: #E6EDF3; padding: 5px 26px 5px 14px; border-radius: 5px;"
        " font-size: 12px; }"
        "QMenu::item:selected { background: #1F6FEB; }"
        "QMenu::separator { height: 1px; background: #30363D; margin: 4px 6px; }"
        "QMenu::indicator { width: 12px; }");
    menuButton_->setMenu(menu);

    auto* fileMenu = menu->addMenu(tr("&File"));
    fileMenu->addAction(tr("Add files to Main Player..."), this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, tr("Add audio files"), QString(),
            tr("Audio files (*.wav *.flac *.ogg *.mp3 *.aiff *.opus);;All files (*)"));
        controller_->addFiles(PlayerId::Main, files);
    });
    fileMenu->addAction(tr("Add files to BGM Player..."), this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, tr("Add audio files"), QString(),
            tr("Audio files (*.wav *.flac *.ogg *.mp3 *.aiff *.opus);;All files (*)"));
        controller_->addFiles(PlayerId::Bgm, files);
    });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Save session"), QKeySequence::Save, this, [this] {
        QString err;
        if (!controller_->save(&err)) {
            QMessageBox::warning(this, tr("Session not saved"), err);
        }
    });
    fileMenu->addAction(tr("Quit"), QKeySequence::Quit, this, [this] { close(); });

    auto* playbackMenu = menu->addMenu(tr("&Playback"));
    auto addTransport = [&](const QString& label, const QString& shortcut,
                            std::function<void(Player&)> fn, PlayerId id) {
        playbackMenu->addAction(label, QKeySequence(shortcut), this, [this, fn, id] {
            fn(engine_->player(id));
            if (id == PlayerId::Main) mainPanel_->refreshNow();
            else bgmPanel_->refreshNow();
        });
    };
    addTransport(tr("Main: Play/Pause"), "Space", [](Player& p) {
        if (p.snapshot().state == PlayerState::Playing) p.pause();
        else p.play();
    }, PlayerId::Main);
    addTransport(tr("Main: Stop"), "S", [](Player& p) { p.stop(); }, PlayerId::Main);
    addTransport(tr("Main: Next"), "N", [](Player& p) { p.next(); }, PlayerId::Main);
    addTransport(tr("Main: Previous"), "P", [](Player& p) { p.prev(); }, PlayerId::Main);
    playbackMenu->addSeparator();
    addTransport(tr("BGM: Play/Pause"), "Ctrl+Space", [](Player& p) {
        if (p.snapshot().state == PlayerState::Playing) p.pause();
        else p.play();
    }, PlayerId::Bgm);
    addTransport(tr("BGM: Stop"), "Ctrl+S", [](Player& p) { p.stop(); }, PlayerId::Bgm);

    auto* viewMenu = menu->addMenu(tr("&View"));
    const QStringList views = {tr("Player"), tr("Effects Rack"), tr("Playlist"), tr("Settings")};
    for (int i = 0; i < views.size(); ++i) {
        viewMenu->addAction(views[i], this, [this, i] { setTab(i); });
    }

    auto* settingsMenu = menu->addMenu(tr("&Settings"));
    settingsMenu->addAction(tr("Rescan plugin catalog"), this,
                            [this] { controller_->startCatalogScan(true); });
    settingsMenu->addAction(tr("Reload racks"), this, [this] { controller_->rebuildAllRacks(); });

    auto* helpMenu = menu->addMenu(tr("&Help"));
    helpMenu->addAction(tr("Open log file"), this, [] {
        const std::string path = defaultLogPath();
        ensureDir(dataDir());
        Logger::instance().setFile(path);
        QMessageBox::information(nullptr, tr("Log file"),
                                 tr("Log file: %1").arg(QString::fromStdString(path)));
    });
    helpMenu->addAction(tr("About"), this, [this] {
        QMessageBox::about(
            this, tr("About LSP Audio Player"),
            tr("<b>LSP Audio Player</b><br/>Dual-player desktop audio player with an LV2 "
                "effects rack (Main Player + BGM Player).<br/><br/>"
                "LV2 hosting via Lilv/Suil, audio via ALSA, decoding via libsndfile.<br/>"
                "Plugin UIs: native when compatible, generic parameter fallback otherwise."));
    });

    // The menu is now a popup instead of a visible menu bar, so register every
    // shortcut-bearing action on the window itself to keep the accelerators
    // (Space, S, N, P, Ctrl+Space, Ctrl+S, ...) alive.
    const std::function<void(QMenu*)> registerShortcuts = [&](QMenu* m) {
        for (QAction* a : m->actions()) {
            if (a->isSeparator()) continue;
            if (a->menu() != nullptr) {
                registerShortcuts(a->menu());
            } else if (!a->shortcut().isEmpty()) {
                addAction(a);
            }
        }
    };
    registerShortcuts(menu);
}

void MainWindow::setTab(int index) {
    currentTab_ = index;
    syncTabButtons();
    const bool isSettings = index == kTabSettings;
    settingsPage_->setVisible(isSettings);
    const bool showPlayers = (index == kTabPlayer || index == kTabPlaylist);
    const bool showRack = (index == kTabPlayer || index == kTabRack);
    playersRow_->setVisible(showPlayers);
    rackRow_->setVisible(showRack);
    if (isSettings) refreshLogView();
}

void MainWindow::syncTabButtons() {
    for (int i = 0; i < tabButtons_.size(); ++i) {
        tabButtons_[i]->setChecked(i == currentTab_);
    }
}

void MainWindow::onCatalogOrRackChanged() {
    // A rack rebuild invalidates any open native editors (the plugin instances
    // they point at are replaced).
    closeEditors();
    rackView_->reload();
    browser_->refresh();
}

void MainWindow::openEditorFor(RackTarget target, const QString& uid) {
    for (PluginEditor* e : editors_) {
        if (e->uid() == uid) {
            e->raise();
            e->activateWindow();
            return;
        }
    }
    QString title = tr("Effect");
    for (const RackSlotInfo& slot : controller_->rackInfo(target)) {
        if (QString::fromStdString(slot.uid) == uid) {
            title = slot.displayName.isEmpty() ? QString::fromStdString(slot.key.id)
                                               : slot.displayName;
            break;
        }
    }
    auto* editor = new PluginEditor(controller_, target, uid, title, this);
    editors_.push_back(editor);
    connect(editor, &QObject::destroyed, this, [this, editor] {
        editors_.removeAll(editor);
    });
    editor->show();
    editor->raise();
}

void MainWindow::closeEditors() {
    const QVector<PluginEditor*> copy = editors_;
    for (PluginEditor* e : copy) {
        e->close();  // WA_DeleteOnClose -> deleteLater
    }
    editors_.clear();
}

// ---------------------------------------------------------------------------
// settings & status
// ---------------------------------------------------------------------------

void MainWindow::applySettings() {
    QString err;
    if (!controller_->applyAudioSettings(rateCombo_->currentText().toInt(),
                                         bufferCombo_->currentText().toInt(),
                                         deviceEdit_->text().trimmed().isEmpty()
                                             ? QStringLiteral("default")
                                             : deviceEdit_->text().trimmed(),
                                         &err)) {
        QMessageBox::warning(this, tr("Audio engine"), err);
    }
    refreshLogView();
}

void MainWindow::refreshLogView() {
    if (!logView_) return;
    logView_->setPlainText(QString::fromStdString(Logger::instance().dump()));
    logView_->verticalScrollBar()->setValue(logView_->verticalScrollBar()->maximum());
}

void MainWindow::updateStatusBar() {
    const bool running = engine_->isRunning();
    const PlayerSnapshot mainSnap = engine_->player(PlayerId::Main).snapshot();
    const PlayerSnapshot bgmSnap = engine_->player(PlayerId::Bgm).snapshot();

    QString text;
    QColor dot;
    if (!running) {
        text = tr("Engine stopped");
        dot = theme::kError;
    } else if (!engine_->lastError().empty()) {
        text = tr("Device error");
        dot = theme::kError;
    } else if (mainSnap.state == PlayerState::Playing || bgmSnap.state == PlayerState::Playing) {
        text = tr("Playing");
        dot = theme::kOk;
    } else {
        text = tr("Ready");
        dot = theme::kOk;
    }
    statusValue_->setText(text);
    statusDot_->setStyleSheet(
        QString("background-color: %1; border-radius: 4px;").arg(dot.name()));

    const float load = engine_->cpuLoad();
    cpuValue_->setText(QString("%1%").arg(static_cast<int>(load * 100.0f)));
    cpuMeter_->setLevel(std::min(1.0f, load));

    auto applyLevel = [](LevelMeter* meter, const EngineMeter& m) {
        const float level = std::max(m.peakL, m.peakR);
        meter->setLevel(std::min(1.0f, level), m.clipping);
    };
    applyLevel(mainMeter_, engine_->meter(PlayerId::Main));
    applyLevel(bgmMeter_, engine_->meter(PlayerId::Bgm));
    applyLevel(outMeter_, engine_->masterMeter());

    rateValue_->setText(tr("%1 kHz").arg(engine_->config().sampleRate / 1000.0));
    bufferValue_->setText(QString::number(engine_->config().blockSize));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    closeEditors();
    QString err;
    controller_->save(&err);
    engine_->stop();
    QMainWindow::closeEvent(event);
}

}  // namespace lspa
