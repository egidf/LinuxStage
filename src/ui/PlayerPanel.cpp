#include "ui/PlayerPanel.h"

#include <QComboBox>
#include <QAbstractItemView>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <thread>

#include "core/Logger.h"
#include "core/Paths.h"
#include "engine/Decoder.h"
#include "ui/SessionController.h"
#include "ui/theme/Theme.h"
#include "ui/widgets/AlbumArt.h"
#include "ui/widgets/Icons.h"
#include "ui/widgets/Waveform.h"

namespace lspa {

namespace {

// Round status dot. QLabel's QSS border-radius proved unreliable for the
// header dot (rendered square), so the circle is painted directly.
class DotLabel : public QLabel {
public:
    explicit DotLabel(QWidget* parent = nullptr) : QLabel(parent) {
        setFixedWidth(9);
        setFixedHeight(9);
    }
    void setDotColor(const QColor& c) {
        color_ = c;
        update();
    }

protected:
    void paintEvent(QPaintEvent* ev) override {
        QLabel::paintEvent(ev);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(color_);
        p.drawEllipse(QRectF(0.5, 0.5, width() - 1.0, height() - 1.0));
    }

private:
    QColor color_{theme::kTextFaint};
};

// Small labelled row used inside the playlist. The current row is painted as
// a solid rounded highlight (mockup: blue/green pill with white text); other
// rows show a number, a near-white title, a right-aligned duration and a hair
// separator.
class PlaylistRow : public QWidget {
public:
    PlaylistRow(int index, const QString& title, const QString& duration, bool current,
                                bool missing, const QColor& hiColor, const QColor& accent,
                                std::function<void()> activate, QWidget* parent = nullptr)
                : QWidget(parent), index_(index), current_(current), hi_(hiColor), accent_(accent),
                    activate_(std::move(activate)) {
                setCursor(Qt::PointingHandCursor);
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(14, 0, 12, 0);
        lay->setSpacing(10);

        marker_ = new QLabel(this);
        marker_->setAttribute(Qt::WA_TransparentForMouseEvents);
        marker_->setFixedWidth(20);
        marker_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        name_ = new QLabel(title, this);
        name_->setAttribute(Qt::WA_TransparentForMouseEvents);
        name_->setStyleSheet(
            QString("color: %1; background: transparent; font-size: 15px;")
                .arg((current ? QColor(255, 255, 255) : theme::kText).name()));

        dur_ = new QLabel(duration, this);
        dur_->setAttribute(Qt::WA_TransparentForMouseEvents);
        dur_->setStyleSheet(
            QString("color: %1; background: transparent; font-size: 14px;")
                .arg((current ? QColor(255, 255, 255) : theme::kText).name()));

        if (current) {
            marker_->setPixmap(icons::material("play_arrow", QColor(255, 255, 255), 16));
        } else {
            marker_->setText(QString::number(index));
            marker_->setStyleSheet(
                QString("color: %1; background: transparent; font-size: 15px;")
                    .arg(QColor(201, 209, 217).name()));
        }

        lay->addWidget(marker_);
        lay->addWidget(name_, 1);
        if (missing) {
            auto* warn = new QLabel(QString::fromUtf8("\xe2\x9a\xa0"), this);
            warn->setAttribute(Qt::WA_TransparentForMouseEvents);
            warn->setToolTip(tr("File is missing or unreadable"));
            warn->setStyleSheet(
                QString("color: %1; background: transparent;").arg(theme::kWarn.name()));
            lay->addWidget(warn);
        }
        lay->addWidget(dur_);
    }

    QSize sizeHint() const override { return QSize(QWidget::sizeHint().width(), 34); }

protected:
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
            if (activate_) activate_();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (activate_) activate_();
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

public:
    void setCurrentStyle(bool current, const QColor& accent, const QColor& hi) {
        if (current == current_) return;
        current_ = current;
        accent_ = accent;
        hi_ = hi;
        if (current) {
            marker_->setPixmap(icons::material("play_arrow", QColor(255, 255, 255), 16));
        } else {
            marker_->setPixmap(QPixmap());
            marker_->setText(QString::number(index_));
        }
        const QString style = QString("color: %1; background: transparent; font-size: %2px;");
        const QString c = (current ? QStringLiteral("#FFFFFF") : theme::kText.name());
        name_->setStyleSheet(style.arg(c, QStringLiteral("15")));
        dur_->setStyleSheet(style.arg(c, QStringLiteral("14")));
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        if (current_) {
            p.setPen(Qt::NoPen);
            p.setBrush(hi_);
            p.drawRoundedRect(r, 8.0, 8.0);
        } else {
            p.setPen(QPen(QColor(23, 31, 44), 1.0));
            const qreal y = qreal(height()) - 0.5;
            p.drawLine(QPointF(r.left() + 4.0, y), QPointF(r.right() - 4.0, y));
        }
    }

private:
    QLabel* marker_ = nullptr;
    QLabel* name_ = nullptr;
    QLabel* dur_ = nullptr;
    int index_ = 0;
    bool current_ = false;
    QColor hi_;
    QColor accent_;
    std::function<void()> activate_;
};

}  // namespace

PlayerPanel::PlayerPanel(PlayerId id, AudioEngine* engine, SessionController* controller,
                         QWidget* parent)
    : QWidget(parent), id_(id), engine_(engine), controller_(controller) {
    const bool isMain = id_ == PlayerId::Main;
    // Accents sampled from the mockup: Main = bright azure, BGM = spring green
    // (the theme defaults are kept for the rest of the app).
    accent_ = isMain ? QColor(42, 168, 254) : QColor(2, 238, 177);
    statusOn_ = isMain ? QColor(24, 224, 131) : QColor(0, 229, 142);
    rowHi_ = isMain ? QColor(0, 103, 187) : QColor(0, 123, 107);
    buildUi();

    auto* timer = new QTimer(this);
    timer->setInterval(60);
    connect(timer, &QTimer::timeout, this, [this] { refreshNow(); });
    timer->start();

    refreshPlaylist();
}

PlayerPanel::~PlayerPanel() {
    ++peaksJobId_;
    if (normalizeWorker_.joinable()) normalizeWorker_.join();
}

void PlayerPanel::buildUi() {
    const bool isMain = id_ == PlayerId::Main;
    setObjectName(isMain ? "mainPlayerPanel" : "bgmPlayerPanel");
    setAttribute(Qt::WA_StyledBackground, true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(17, 11, 17, 18);
    root->setSpacing(18);

    // ---------------------------------------------------------------- header
    auto* header = new QHBoxLayout();
    header->setSpacing(0);
    header->setContentsMargins(0, 0, 0, 0);

    auto* markLabel = new QLabel(this);
    markLabel->setPixmap(icons::playerMark(isMain, 22));
    markLabel->setFixedSize(26, 22);
    markLabel->setAlignment(Qt::AlignCenter);
    header->addWidget(markLabel);
    header->addSpacing(6);

    titleLabel_ = new QLabel(isMain ? tr("Main Player") : tr("BGM Player"), this);
    titleLabel_->setStyleSheet("font-size: 19px; font-weight: 700; color: #F2F5F8;");
    titleLabel_->setFixedHeight(26);
    header->addWidget(titleLabel_);
    header->addSpacing(14);

    statusDot_ = new DotLabel(this);
    static_cast<DotLabel*>(statusDot_)->setDotColor(theme::kTextFaint);
    header->addWidget(statusDot_);
    header->addSpacing(7);

    statusLabel_ = new QLabel(tr("Stopped"), this);
    statusLabel_->setStyleSheet(
        QString("color: %1; font-size: 14px;").arg(theme::kTextDim.name()));
    statusLabel_->setFixedHeight(20);
    header->addWidget(statusLabel_);
    header->addStretch(1);

    auto* fadeControls = new QWidget(this);
    auto* fadeLayout = new QVBoxLayout(fadeControls);
    fadeLayout->setContentsMargins(0, 0, 0, 0);
    fadeLayout->setSpacing(2);
    auto makeFadeRow = [&](const QString& name, QLabel*& valueLabel, bool fadeIn) {
        auto* row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        auto* nameLabel = new QLabel(name, fadeControls);
        nameLabel->setStyleSheet(QString("color: %1; font-size: 9px;").arg(theme::kTextDim.name()));
        nameLabel->setFixedWidth(48);
        row->addWidget(nameLabel);
        auto makeStep = [&](const QString& text, double delta) {
            auto* button = new QToolButton(fadeControls);
            button->setText(text);
            button->setFixedSize(19, 18);
            button->setToolTip(fadeIn ? tr("Adjust fade-in") : tr("Adjust fade-out"));
            button->setStyleSheet(
                QString("QToolButton { background: #161E2C; color: %1; border: 1px solid #303A48;"
                        " border-radius: 4px; padding: 0; font-size: 12px; }"
                        "QToolButton:hover { border-color: %2; }")
                    .arg(theme::kText.name(), accent_.name()));
            connect(button, &QToolButton::clicked, this,
                    [this, fadeIn, delta] { adjustFade(fadeIn, delta); });
            row->addWidget(button);
        };
        makeStep(QStringLiteral("-"), -100.0);
        valueLabel = new QLabel(fadeControls);
        valueLabel->setAlignment(Qt::AlignCenter);
        valueLabel->setFixedWidth(42);
        valueLabel->setStyleSheet(QString("color: %1; font-size: 9px;")
                                      .arg(theme::kText.name()));
        row->addWidget(valueLabel);
        makeStep(QStringLiteral("+"), 100.0);
        fadeLayout->addLayout(row);
    };
    makeFadeRow(tr("Fade in"), fadeInValue_, true);
    makeFadeRow(tr("Fade out"), fadeOutValue_, false);
    fadeControls->setToolTip(tr("Fade times for this player"));
    header->addWidget(fadeControls);
    refreshFadeControls();
    root->addLayout(header);

    // ------------------------------------------------------------------ body
    auto* body = new QHBoxLayout();
    body->setSpacing(18);
    body->setContentsMargins(0, 0, 0, 0);

    // left wrap: artwork + centre column on top, volume row spanning underneath
    auto* leftWrap = new QVBoxLayout();
    leftWrap->setSpacing(8);
    leftWrap->setContentsMargins(0, 0, 0, 0);

    auto* upper = new QHBoxLayout();
    upper->setSpacing(18);
    upper->setContentsMargins(0, 0, 0, 0);

    art_ = new AlbumArt(this);
    art_->setAccent(accent_);
    art_->setFixedSize(180, 226);
    upper->addWidget(art_, 0, Qt::AlignTop);

    // middle column: metadata, waveform, transport. The mockup's centre
    // column is ~240px tall (title .. transport); wrap it at that fixed height
    // so the upper row can't grow past it (a bare layout lets Qt hand the
    // spare space to the child layouts and inflate the row).
    auto* midWrap = new QWidget(this);
    midWrap->setFixedHeight(240);
    auto* midCol = new QVBoxLayout(midWrap);
    midCol->setSpacing(0);
    midCol->setContentsMargins(0, 0, 0, 0);

    trackTitle_ = new QLabel(tr("No track"), this);
    trackTitle_->setStyleSheet("font-size: 23px; font-weight: 700; color: #FFFFFF;");
    trackTitle_->setFixedHeight(28);
    midCol->addWidget(trackTitle_);

    midCol->addSpacing(2);
    trackArtist_ = new QLabel(tr("-"), this);
    trackArtist_->setStyleSheet("color: #E6EDF3; font-size: 16px;");
    trackArtist_->setFixedHeight(22);
    midCol->addWidget(trackArtist_);

    midCol->addSpacing(3);
    auto* albumRow = new QHBoxLayout();
    albumRow->setSpacing(7);
    albumRow->setContentsMargins(0, 0, 0, 0);
    auto* albumIcon = new QLabel(this);
    albumIcon->setPixmap(icons::material("image", QColor(201, 209, 217), 15));
    albumIcon->setFixedSize(17, 15);
    albumRow->addWidget(albumIcon);
    trackAlbum_ = new QLabel(tr("-"), this);
    trackAlbum_->setStyleSheet("color: #C9D1D9; font-size: 14px;");
    trackAlbum_->setFixedHeight(20);
    albumRow->addWidget(trackAlbum_);
    albumRow->addStretch(1);
    midCol->addLayout(albumRow);

    midCol->addSpacing(6);
    waveform_ = new WaveformView(this);
    waveform_->setAccent(accent_);
    waveform_->setFixedHeight(50);
    connect(waveform_, &WaveformView::seekRequested, this, &PlayerPanel::onSeek);
    midCol->addWidget(waveform_);

    auto* timeRow = new QHBoxLayout();
    timeRow->setContentsMargins(0, 0, 0, 0);
    posLabel_ = new QLabel(tr("00:00"), this);
    durLabel_ = new QLabel(tr("00:00"), this);
    const QString timeStyle = QStringLiteral("color: #CDD3DB; font-size: 15px;");
    posLabel_->setStyleSheet(timeStyle);
    durLabel_->setStyleSheet(timeStyle);
    posLabel_->setFixedHeight(15);
    durLabel_->setFixedHeight(15);
    timeRow->addWidget(posLabel_);
    timeRow->addStretch(1);
    timeRow->addWidget(durLabel_);
    midCol->addSpacing(3);
    midCol->addLayout(timeRow);

    // thin progress bar under the times (mockup)
    midCol->addSpacing(7);
    progressBar_ = new QProgressBar(this);
    progressBar_->setRange(0, 1000);
    progressBar_->setValue(0);
    progressBar_->setTextVisible(false);
    progressBar_->setFixedHeight(6);
    progressBar_->setStyleSheet(
        QStringLiteral("QProgressBar { background-color: #414B5E; border: none;"
                       " border-radius: 3px; min-height: 6px; max-height: 6px; }"
                       "QProgressBar::chunk { background-color: %1; border-radius: 3px; }")
            .arg(accent_.name()));
    midCol->addWidget(progressBar_);

    // transport: five equal cells so the control centres spread evenly like
    // the mockup (shuffle ... prev ... PLAY ... next ... repeat).
    auto makeCell = [&](QWidget* w) {
        auto* cell = new QWidget(this);
        auto* cellLay = new QVBoxLayout(cell);
        cellLay->setContentsMargins(0, 0, 0, 0);
        cellLay->setAlignment(Qt::AlignCenter);
        cellLay->addWidget(w);
        cell->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return cell;
    };

    midCol->addSpacing(13);
    auto* transport = new QHBoxLayout();
    transport->setSpacing(0);
    transport->setContentsMargins(0, 0, 0, 0);

    auto makeTool = [&](const char* matIcon, const QColor& color, const QString& tip,
                        int iconSize, int box) {
        auto* b = new QToolButton(this);
        b->setAutoRaise(true);
        b->setToolTip(tip);
        b->setIcon(QIcon(icons::material(matIcon, color, iconSize)));
        b->setFixedSize(box, box);
        b->setIconSize(QSize(iconSize, iconSize));
        // The app-wide QToolButton rule adds padding/min-height; restate them
        // here so the sizeHint equals the fixed box (the transport cells
        // centre widgets on their sizeHint, which would otherwise stretch).
        b->setStyleSheet(
            QString("QToolButton { border: none; border-radius: %1px; padding: 0px;"
                    " min-width: %2px; min-height: %2px; }"
                    "QToolButton:hover { background: rgba(255,255,255,0.07); }")
                .arg(box / 2)
                .arg(box));
        return b;
    };

    const QColor toolDim(151, 162, 176);       // shuffle/repeat (mockup dimmer)
    const QColor toolBright(230, 237, 243);    // prev/next (mockup near-white)
    shuffleBtn_ = makeTool("shuffle", toolDim, tr("Shuffle"), 18, 28);
    shuffleBtn_->setCheckable(true);
    prevBtn_ = makeTool("skip_previous", toolBright, tr("Previous track"), 22, 32);
    nextBtn_ = makeTool("skip_next", toolBright, tr("Next track"), 22, 32);
    repeatBtn_ = makeTool("repeat", toolDim, tr("Repeat"), 18, 28);
    repeatBtn_->setCheckable(true);
    shuffleBtn_->setChecked(engine_->player(id_).shuffleEnabled());
    repeatBtn_->setChecked(engine_->player(id_).repeatEnabled());

    playBtn_ = new QPushButton(this);
    playBtn_->setFixedSize(54, 54);
    playBtn_->setCursor(Qt::PointingHandCursor);
    playBtn_->setIconSize(QSize(28, 28));
    playBtn_->setStyleSheet(
        QString("QPushButton { background-color: %1; border-radius: 28px; border: none;"
                " padding: 0px; min-width: 56px; min-height: 56px; }"
                "QPushButton:hover { background-color: %2; }"
                "QPushButton:pressed { background-color: %3; }")
            .arg(accent_.name(), accent_.lighter(112).name(), accent_.darker(112).name()));
    connect(playBtn_, &QPushButton::clicked, this, [this] {
        Player& p = engine_->player(id_);
        const PlayerSnapshot snap = p.snapshot();
        if (snap.state == PlayerState::Playing) {
            p.pause();
            // Main player pause -> unmute BGM
            if (id_ == PlayerId::Main) {
                engine_->player(PlayerId::Bgm).setMuted(false);
            }
        } else {
            p.play();
            // Main player play -> mute BGM
            if (id_ == PlayerId::Main) {
                engine_->player(PlayerId::Bgm).setMuted(true);
            } else if (id_ == PlayerId::Bgm) {
                // BGM play attempt while Main is playing -> mute BGM
                const auto mainSnap = engine_->player(PlayerId::Main).snapshot();
                if (mainSnap.state == PlayerState::Playing) {
                    engine_->player(PlayerId::Bgm).setMuted(true);
                }
            }
        }
        refreshNow();
    });

    transport->addWidget(makeCell(shuffleBtn_), 1);
    transport->addWidget(makeCell(prevBtn_), 1);
    transport->addWidget(makeCell(playBtn_), 1);
    transport->addWidget(makeCell(nextBtn_), 1);
    transport->addWidget(makeCell(repeatBtn_), 1);

    midCol->addLayout(transport);
    upper->addWidget(midWrap, 1);
    leftWrap->addLayout(upper);

    // bottom row: speaker + volume slider spanning under the artwork + centre
    // column (the mockup shows no meter here; level meters live in the status
    // bar so FR-11 stays satisfied without breaking the panel layout).
    auto* volRow = new QHBoxLayout();
    volRow->setContentsMargins(0, 0, 0, 0);
    volRow->setSpacing(12);
    muteBtn_ = new QToolButton(this);
    muteBtn_->setToolTip(tr("Mute"));
    muteBtn_->setAutoRaise(true);
    muteBtn_->setFixedSize(26, 26);
    muteBtn_->setIconSize(QSize(16, 16));
    muteBtn_->setStyleSheet("QToolButton { border: none; border-radius: 13px; padding: 0px;"
                            " min-width: 26px; min-height: 26px; }"
                            "QToolButton:hover { background: rgba(255,255,255,0.07); }");
    muteBtn_->setIcon(QIcon(icons::material("volume_up", QColor(230, 237, 243), 20)));
    connect(muteBtn_, &QToolButton::clicked, this, &PlayerPanel::onMuteToggled);
    volRow->addWidget(muteBtn_);

    volumeSlider_ = new QSlider(Qt::Horizontal, this);
    volumeSlider_->setRange(0, 200);
    volumeSlider_->setValue(qRound(engine_->player(id_).volume() * 100.0f));
    // The default slider sizeHint is ~40px tall, which would inflate the
    // volume row; pin it to the mute button's height so the row stays 26px.
    volumeSlider_->setFixedHeight(26);
    volumeSlider_->setStyleSheet(
        QString("QSlider::groove:horizontal { height: 6px; background: %1; border-radius: 3px; }"
                "QSlider::handle:horizontal { background: %2; width: 16px; margin: -5px 0;"
                " border-radius: 8px; }"
                "QSlider::sub-page:horizontal { background: %2; border-radius: 3px; }")
            .arg(QStringLiteral("#242F41"), accent_.name()));
    connect(volumeSlider_, &QSlider::valueChanged, this, &PlayerPanel::onVolumeChanged);
    volRow->addWidget(volumeSlider_, 1);
    volRow->addSpacing(26);
    leftWrap->addLayout(volRow);
    body->addLayout(leftWrap, 1);

    connect(prevBtn_, &QToolButton::clicked, this, [this] {
        engine_->player(id_).prev();
        refreshNow();
    });
    connect(nextBtn_, &QToolButton::clicked, this, [this] {
        engine_->player(id_).next();
        refreshNow();
    });
    connect(shuffleBtn_, &QToolButton::toggled, this, [this](bool on) {
        engine_->player(id_).setShuffleEnabled(on);
        shuffleBtn_->setIcon(QIcon(icons::material(
            "shuffle", on ? accent_ : QColor(151, 162, 176), 22)));
        shuffleBtn_->setToolTip(on ? tr("Shuffle on") : tr("Shuffle off"));
    });
    connect(repeatBtn_, &QToolButton::toggled, this, [this](bool on) {
        engine_->player(id_).setRepeatEnabled(on);
        repeatBtn_->setIcon(QIcon(icons::material(
            "repeat", on ? accent_ : QColor(151, 162, 176), 22)));
        repeatBtn_->setToolTip(on ? tr("Repeat playlist on") : tr("Repeat playlist off"));
    });

    // right column: playlist
    auto* rightCol = new QVBoxLayout();
    rightCol->setContentsMargins(0, 0, 0, 0);
    rightCol->setSpacing(8);
    rightCol->addSpacing(4);
    playlistHeader_ = new QLabel(id_ == PlayerId::Bgm ? tr("BGM Playlist") : tr("Playlist"), this);
    playlistHeader_->setStyleSheet("font-size: 15px; font-weight: 600; color: #E6EDF3;");
    rightCol->addWidget(playlistHeader_);
    // Mockup: the list starts ~5px lower under the header than Qt's default
    // layout gap would place it.
    rightCol->addSpacing(5);

    playlist_ = new QListWidget(this);
    playlist_->setObjectName("playlistWidget");
    playlist_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    playlist_->setContextMenuPolicy(Qt::CustomContextMenu);
    playlist_->setFixedWidth(280);
    playlist_->setFixedHeight(196);
    // Mockup: rows sit directly on the panel; the current row paints its own
    // rounded highlight, other rows a hair separator - no frame or well.
    playlist_->setStyleSheet(
        QStringLiteral("QListWidget { background: transparent; border: none;"
                       " padding: 0px; }"
                       "QListWidget::item { background: transparent; border: none;"
                       " padding: 0px; margin: 0px; }"
                       "QListWidget::item:selected { background: transparent;"
                       " border: none; color: inherit; }"));
    connect(playlist_, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                QMenu menu(this);
                menu.addAction(tr("Play"), [this] {
                    if (playlist_->currentRow() >= 0) onPlaylistActivated(playlist_->currentItem());
                });
                menu.addAction(tr("Remove selected"), this, &PlayerPanel::onRemoveSelected);
                menu.addSeparator();
                menu.addAction(tr("Clear playlist"), this, &PlayerPanel::onClearPlaylist);
                menu.exec(playlist_->viewport()->mapToGlobal(pos));
            });
    rightCol->addWidget(playlist_, 1);

    auto* plButtons = new QHBoxLayout();
    plButtons->setSpacing(12);
    plButtons->setContentsMargins(0, 0, 0, 0);
    auto makePlBtn = [&](const char* matIcon, const QString& tip) {
        auto* b = new QPushButton(this);
        b->setToolTip(tip);
        b->setFixedHeight(41);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setCursor(Qt::PointingHandCursor);
        b->setIcon(QIcon(icons::material(matIcon, QColor(201, 209, 217), 18)));
        b->setStyleSheet(
            QStringLiteral("QPushButton { background-color: #161E2C; border: 1px solid #303A48;"
                           " border-radius: 8px; padding: 0px; min-height: 41px;"
                           " }"
                           "QPushButton:hover { background-color: #1D2737;"
                           " border-color: #3E4B5E; }"));
        return b;
    };
    auto* addBtn = makePlBtn("add", tr("Add audio files"));
    auto* clearBtn = makePlBtn("format_list_bulleted", tr("Clear playlist"));
    auto* removeBtn = makePlBtn("delete", tr("Remove selected"));
    connect(addBtn, &QPushButton::clicked, this, &PlayerPanel::onAddFiles);
    connect(clearBtn, &QPushButton::clicked, this, &PlayerPanel::onClearPlaylist);
    connect(removeBtn, &QPushButton::clicked, this, &PlayerPanel::onRemoveSelected);
    plButtons->addWidget(addBtn, 1);
    plButtons->addWidget(clearBtn, 1);
    plButtons->addWidget(removeBtn, 1);
    rightCol->addSpacing(6);
    rightCol->addLayout(plButtons);
    body->addLayout(rightCol, 0);

    root->addLayout(body, 1);
}

// ---------------------------------------------------------------------------
// actions
// ---------------------------------------------------------------------------

void PlayerPanel::onAddFiles() {
    const QString dir = QString::fromStdString(dataDir());
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Add audio files"), dir,
        tr("Audio files (*.wav *.flac *.ogg *.oga *.mp3 *.aiff *.aif *.opus *.m4a);;All files (*)"));
    if (files.isEmpty()) return;
    controller_->addFiles(id_, files);
}

void PlayerPanel::onClearPlaylist() {
    controller_->clearPlaylist(id_);
}

void PlayerPanel::onRemoveSelected() {
    QList<int> rows;
    for (QListWidgetItem* item : playlist_->selectedItems()) {
        rows.push_back(playlist_->row(item));
    }
    std::sort(rows.begin(), rows.end());
    controller_->removeRows(id_, rows);
}

void PlayerPanel::onPlaylistActivated(QListWidgetItem* item) {
    if (!item) return;
    const int row = playlist_->row(item);
    if (row >= 0) controller_->selectTrack(id_, row);
}

void PlayerPanel::onSeek(double fraction) {
    Player& p = engine_->player(id_);
    const PlayerSnapshot snap = p.snapshot();
    if (snap.totalFrames <= 0) return;
    p.seekSeconds(fraction * snap.durationSec);
}

void PlayerPanel::onVolumeChanged(int value) {
    engine_->player(id_).setVolume(static_cast<float>(value) / 100.0f);
}

void PlayerPanel::onMuteToggled() {
    if (id_ == PlayerId::Bgm) return;
    Player& p = engine_->player(id_);
    const bool muted = !p.muted();
    p.setMuted(muted);
    muteBtn_->setIcon(QIcon(icons::material(muted ? "volume_off" : "volume_up",
                                             muted ? accent_ : QColor(230, 237, 243), 20)));
    muteBtn_->setToolTip(muted ? tr("Unmute") : tr("Mute"));
}

void PlayerPanel::updateVolumeControls() {
    const Player& player = engine_->player(id_);
    const bool muted = player.muted();
    const int sliderValue = qRound(player.volume() * 100.0f);
    if (volumeSlider_->value() != sliderValue) {
        const QSignalBlocker blocker(volumeSlider_);
        volumeSlider_->setValue(sliderValue);
    }
    muteBtn_->setIcon(QIcon(icons::material(muted ? "volume_off" : "volume_up",
                                             muted ? accent_ : QColor(230, 237, 243), 20)));
    muteBtn_->setEnabled(id_ != PlayerId::Bgm);
    muteBtn_->setToolTip(id_ == PlayerId::Bgm
                             ? tr("BGM mute follows Main Player Play/Pause/Stop")
                             : (muted ? tr("Unmute") : tr("Mute")));
}

void PlayerPanel::startBgmAfterMain() {
    if (id_ != PlayerId::Bgm || p().playlist().empty()) return;
    p().setMuted(false);
    muteBtn_->setIcon(QIcon(icons::material("volume_up", QColor(230, 237, 243), 20)));
    muteBtn_->setToolTip(tr("Mute"));
    p().play();
    refreshNow();
}

void PlayerPanel::adjustFade(bool fadeIn, double deltaMs) {
    FadeConfig fade = p().fadeConfig();
    double& value = fadeIn ? fade.fadeInMs : fade.fadeOutMs;
    value = std::clamp(value + deltaMs, 0.0, 10000.0);
    controller_->setPlayerFade(id_, fade);
    refreshFadeControls();
}

void PlayerPanel::refreshFadeControls() {
    const FadeConfig fade = p().fadeConfig();
    const auto format = [](double ms) {
        return ms >= 1000.0 ? QString::number(ms / 1000.0, 'f', 1) + QStringLiteral(" s")
                            : QString::number(qRound(ms)) + QStringLiteral(" ms");
    };
    if (fadeInValue_) fadeInValue_->setText(format(fade.fadeInMs));
    if (fadeOutValue_) fadeOutValue_->setText(format(fade.fadeOutMs));
}

void PlayerPanel::autoCue() {
    if (normalizeWorker_.joinable()) normalizeWorker_.join();
    const std::string path = p().currentPath();
    if (path.empty()) {
        emit statusHint(tr("%1 has no track to cue.")
                            .arg(id_ == PlayerId::Main ? tr("Main Player") : tr("BGM Player")));
        emit autoCueFinished();
        return;
    }
    QPointer<PlayerPanel> self(this);
    normalizeWorker_ = std::thread([self, path, id = id_] {
        std::string error;
        auto decoder = openSndfileDecoder(path, error);
        double cueSeconds = 0.0;
        bool found = false;
        if (decoder) {
            constexpr int kChunk = 2048;
            constexpr float kPeakThreshold = 0.012f;
            std::vector<float> left(kChunk), right(kChunk);
            int64_t frame = 0;
            while (true) {
                const int frames = decoder->read(left.data(), right.data(), kChunk);
                if (frames <= 0) break;
                for (int i = 0; i < frames; ++i) {
                    if (std::max(std::fabs(left[static_cast<size_t>(i)]),
                                 std::fabs(right[static_cast<size_t>(i)])) >= kPeakThreshold) {
                        const int64_t cueFrame = std::max<int64_t>(
                            0, frame + i - decoder->sampleRate() / 30);
                        cueSeconds = static_cast<double>(cueFrame) /
                                     std::max(1, decoder->sampleRate());
                        found = true;
                        break;
                    }
                }
                if (found) break;
                frame += frames;
            }
        }
        QMetaObject::invokeMethod(self, [self, cueSeconds, found, path] {
            if (self) {
                if (self->p().currentPath() == path) {
                    self->p().seekSeconds(cueSeconds);
                    self->refreshNow();
                    emit self->statusHint(found
                                              ? self->tr("Auto Cue set at %1 s.").arg(cueSeconds, 0, 'f', 2)
                                              : self->tr("No cue point found; positioned at start."));
                }
                emit self->autoCueFinished();
            }
        }, Qt::QueuedConnection);
    });
}

void PlayerPanel::autoNormalizePlaylist() {
    if (normalizeWorker_.joinable()) normalizeWorker_.join();
    const std::vector<PlaylistItem> items = p().playlist();
    if (items.empty()) {
        emit statusHint(tr("Add tracks before running Auto Volume."));
        emit autoVolumeFinished();
        return;
    }
    QPointer<PlayerPanel> self(this);
    normalizeWorker_ = std::thread([self, items, id = id_] {
        struct Measurement {
            int index = 0;
            float rms = 0.0f;
        };
        std::vector<Measurement> measurements;
        double rmsSum = 0.0;
        for (size_t index = 0; index < items.size(); ++index) {
            std::string error;
            auto decoder = openSndfileDecoder(items[index].path, error);
            if (!decoder || items[index].missing) continue;
            constexpr int kChunk = 8192;
            std::vector<float> left(kChunk), right(kChunk);
            long double sumSquares = 0.0;
            uint64_t sampleCount = 0;
            while (true) {
                const int frames = decoder->read(left.data(), right.data(), kChunk);
                if (frames <= 0) break;
                for (int i = 0; i < frames; ++i) {
                    const float l = left[static_cast<size_t>(i)];
                    const float r = right[static_cast<size_t>(i)];
                    sumSquares += static_cast<long double>(l) * l +
                                  static_cast<long double>(r) * r;
                    sampleCount += 2;
                }
            }
            if (sampleCount == 0) continue;
            const float rms = static_cast<float>(std::sqrt(sumSquares / sampleCount));
            if (rms <= 1.0e-5f) continue;
            measurements.push_back({static_cast<int>(index), rms});
            rmsSum += rms;
        }
        const float targetRms = measurements.empty()
                                    ? 0.0f
                                    : static_cast<float>(rmsSum / measurements.size());
        std::vector<std::pair<int, float>> gains;
        gains.reserve(measurements.size());
        for (const Measurement& measurement : measurements) {
            gains.emplace_back(measurement.index,
                               std::clamp(targetRms / measurement.rms, 0.25f, 4.0f));
        }
        QMetaObject::invokeMethod(self, [self, gains = std::move(gains), id] {
            if (!self) return;
            Player& player = self->engine_->player(id);
            for (const auto& [index, gain] : gains) player.setTrackGain(index, gain);
            emit self->statusHint(gains.empty()
                                      ? self->tr("Auto Volume could not analyze any readable tracks.")
                                      : self->tr("Auto Volume balanced %1 track(s) by measured loudness.")
                                            .arg(gains.size()));
            emit self->autoVolumeFinished();
        }, Qt::QueuedConnection);
    });
}

// ---------------------------------------------------------------------------
// refresh
// ---------------------------------------------------------------------------

QString PlayerPanel::formatTime(double seconds) {
    if (seconds < 0 || !std::isfinite(seconds)) seconds = 0;
    const int s = static_cast<int>(seconds);
    // Playlist durations read like the mockup: "5:18" (no minute padding).
    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

QString PlayerPanel::formatTimePadded(double seconds) {
    if (seconds < 0 || !std::isfinite(seconds)) seconds = 0;
    const int s = static_cast<int>(seconds);
    // Position/duration labels read like the mockup: "02:34".
    return QString("%1:%2").arg(s / 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
}

void PlayerPanel::refreshPlaylist() {
    const auto items = engine_->player(id_).playlist();
    const PlayerSnapshot snap = engine_->player(id_).snapshot();

    playlist_->clear();
    playlist_->setUpdatesEnabled(false);
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        const bool current = static_cast<int>(i) == snap.trackIndex;
        auto* lwItem = new QListWidgetItem(playlist_);
        const QString title =
            QString::fromStdString(it.title.empty() ? fileName(it.path) : it.title);
        auto* row = new PlaylistRow(static_cast<int>(i) + 1, title,
                                    formatTime(it.durationSec), current, it.missing, rowHi_,
                                    accent_, [this, index = static_cast<int>(i)] {
                                        controller_->selectTrack(id_, index);
                                    });
        lwItem->setSizeHint(QSize(0, 34));
        playlist_->setItemWidget(lwItem, row);
        lwItem->setToolTip(QString::fromStdString(it.path) +
                           (it.missing ? QString("\n") + QString::fromStdString(it.error)
                                       : QString()));
    }
    playlist_->setUpdatesEnabled(true);

    playlistHeader_->setText(id_ == PlayerId::Bgm ? tr("BGM Playlist (%1)").arg(items.size())
                                                  : tr("Playlist (%1)").arg(items.size()));
}

void PlayerPanel::refreshNow() {
    Player& p = engine_->player(id_);
    const PlayerSnapshot snap = p.snapshot();

    if (shuffleBtn_->isChecked() != p.shuffleEnabled()) {
        shuffleBtn_->setChecked(p.shuffleEnabled());
    }
    if (repeatBtn_->isChecked() != p.repeatEnabled()) {
        repeatBtn_->setChecked(p.repeatEnabled());
    }
    updateVolumeControls();

    updateMetadata(snap);
    updateTransport(snap);
    updatePlaylistHighlight(snap);
}

void PlayerPanel::updateMetadata(const PlayerSnapshot& snap) {
    posLabel_->setText(formatTimePadded(snap.positionSec));
    durLabel_->setText(formatTimePadded(snap.durationSec));
    const double frac = snap.durationSec > 0 ? snap.positionSec / snap.durationSec : 0.0;
    waveform_->setProgress(frac);
    progressBar_->setValue(static_cast<int>(frac * 1000.0));

    const QString path = QString::fromStdString(p().currentPath());
    if (path != currentWaveformPath_) {
        currentWaveformPath_ = path;
        if (path.isEmpty()) {
            trackTitle_->setText(tr("No track"));
            trackArtist_->setText(tr("-"));
            trackAlbum_->setText(tr("-"));
            waveform_->clear();
            art_->setCaption(QString(), QString());
            art_->setSeed(QString());
        } else {
            const QFileInfo info(path);
            // Mockup structure: big title, artist line, then a small icon row
            // with the "album" (for us: the file name - there is no tag data
            // for plain audio files).
            trackTitle_->setText(info.completeBaseName());
            trackArtist_->setText(tr("Unknown artist"));
            trackAlbum_->setText(info.fileName());
            art_->setCaption(info.completeBaseName(), info.suffix().toUpper() + " audio");
            art_->setSeed(path);
            computePeaksFor(path);
        }
    }
}

void PlayerPanel::updateTransport(const PlayerSnapshot& snap) {
    QString stateText;
    QColor dot;
    const bool waitingForMain = id_ == PlayerId::Bgm &&
                                snap.state == PlayerState::Playing &&
                                engine_->player(PlayerId::Main).isPlaying();
    if (waitingForMain) {
        stateText = tr("Waiting for Main");
        dot = theme::kWarn;
    } else switch (snap.state) {
        case PlayerState::Playing:
            // Mockup wording for the running state.
            stateText = snap.ended ? tr("Ended") : tr("Active");
            dot = snap.ended ? theme::kWarn : statusOn_;
            break;
        case PlayerState::Paused:
            stateText = tr("Paused");
            dot = theme::kWarn;
            break;
        case PlayerState::Stopped:
            stateText = tr("Stopped");
            dot = QColor(110, 122, 138);
            break;
    }
    statusLabel_->setText(stateText);
    // Status is never colour-only: the text always spells the state out.
    statusLabel_->setStyleSheet(
        QString("color: %1; font-size: 14px;").arg(dot.name()));
    static_cast<DotLabel*>(statusDot_)->setDotColor(dot);

    const bool playing = snap.state == PlayerState::Playing;
    playBtn_->setIcon(QIcon(icons::material(playing ? "pause" : "play_arrow",
                                            QColor(255, 255, 255), 26)));
    playBtn_->setToolTip(playing ? tr("Pause") : tr("Play"));
}

void PlayerPanel::updatePlaylistHighlight(const PlayerSnapshot& snap) {
    for (int i = 0; i < playlist_->count(); ++i) {
        auto* row = static_cast<PlaylistRow*>(playlist_->itemWidget(playlist_->item(i)));
        if (row) row->setCurrentStyle(i == snap.trackIndex, accent_, rowHi_);
    }
}

void PlayerPanel::computePeaksFor(const QString& path) {
    // Generation counter: only the newest job may publish its result.
    const uint64_t generation = ++peaksJobId_;
    const std::string stdPath = path.toStdString();

    std::thread([this, generation, stdPath] {
        QVector<float> peaks;
        std::string err;
        auto dec = openSndfileDecoder(stdPath, err);
        if (dec) {
            // Chunky mockup-style bars: ~104 peaks read as ~3px columns with
            // a visible gap in the main panel's mid column.
            constexpr int kBars = 104;
            const int64_t total = dec->totalFrames();
            const int64_t per = std::max<int64_t>(1, total / kBars);
            peaks.resize(kBars);
            std::vector<float> l(static_cast<size_t>(per));
            std::vector<float> r(static_cast<size_t>(per));
            int bar = 0;
            while (bar < kBars) {
                const int got = dec->read(l.data(), r.data(), static_cast<int>(per));
                if (got <= 0) break;
                float peak = 0.0f;
                for (int i = 0; i < got; ++i) {
                    peak = std::max(peak, std::fabs(l[static_cast<size_t>(i)]));
                    peak = std::max(peak, std::fabs(r[static_cast<size_t>(i)]));
                }
                peaks[bar++] = std::min(1.0f, peak);
            }
            while (bar < kBars) peaks[bar++] = 0.0f;
        }
        QMetaObject::invokeMethod(
            this,
            [this, generation, peaks, stdPath] {
                if (generation != peaksJobId_.load()) return;
                if (stdPath != currentWaveformPath_.toStdString()) return;
                if (!peaks.isEmpty()) waveform_->setPeaks(peaks);
            },
            Qt::QueuedConnection);
    }).detach();
}

Player& PlayerPanel::p() { return engine_->player(id_); }

}  // namespace lspa
