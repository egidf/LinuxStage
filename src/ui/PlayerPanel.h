// PlayerPanel.h - one playback lane panel (art, waveform, transport,
// volume and playlist). Used for both the Main Player and the BGM Player.

#pragma once

#include <QWidget>
#include <thread>

#include <QColor>
#include <QVector>

#include "core/Types.h"
#include "engine/AudioEngine.h"
#include "engine/Player.h"

class QLabel;
class QPushButton;
class QProgressBar;
class QSlider;
class QToolButton;
class QListWidget;
class QListWidgetItem;
class QComboBox;

namespace lspa {

class AlbumArt;
class WaveformView;
class SessionController;

class PlayerPanel : public QWidget {
    Q_OBJECT
public:
    PlayerPanel(PlayerId id, AudioEngine* engine, SessionController* controller,
                QWidget* parent = nullptr);
    ~PlayerPanel() override;

    PlayerId playerId() const { return id_; }

    void refreshPlaylist();
    void refreshNow();
    void autoCue();
    void autoNormalizePlaylist();
    void startBgmAfterMain();

signals:
    void rackEditRequested(lspa::RackTarget target);
    void statusHint(const QString& text);
    void autoCueFinished();
    void autoVolumeFinished();

private slots:
    void onAddFiles();
    void onClearPlaylist();
    void onRemoveSelected();
    void onPlaylistActivated(QListWidgetItem* item);
    void onSeek(double fraction);
    void onVolumeChanged(int value);
    void onMuteToggled();
    void adjustFade(bool fadeIn, double deltaMs);
    void refreshFadeControls();

private:
    void buildUi();
    void updateTransport(const PlayerSnapshot& snap);
    void updateMetadata(const PlayerSnapshot& snap);
    void updatePlaylistHighlight(const PlayerSnapshot& snap);
    void updateVolumeControls();
    void computePeaksFor(const QString& path);
    static QString formatTime(double seconds);
    static QString formatTimePadded(double seconds);

    PlayerId id_;
    AudioEngine* engine_ = nullptr;
    SessionController* controller_ = nullptr;
    QColor accent_;     // panel accent (mockup: azure / spring green)
    QColor statusOn_;   // "Active" dot + text colour
    QColor rowHi_;      // current playlist row highlight

    // header
    QLabel* titleLabel_ = nullptr;
    QLabel* statusDot_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* fadeInValue_ = nullptr;
    QLabel* fadeOutValue_ = nullptr;

    // now playing
    AlbumArt* art_ = nullptr;
    QLabel* trackTitle_ = nullptr;
    QLabel* trackArtist_ = nullptr;
    QLabel* trackAlbum_ = nullptr;
    WaveformView* waveform_ = nullptr;
    QLabel* posLabel_ = nullptr;
    QLabel* durLabel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;

    // transport
    QToolButton* shuffleBtn_ = nullptr;
    QToolButton* prevBtn_ = nullptr;
    QPushButton* playBtn_ = nullptr;
    QToolButton* nextBtn_ = nullptr;
    QToolButton* repeatBtn_ = nullptr;
    QToolButton* muteBtn_ = nullptr;
    QSlider* volumeSlider_ = nullptr;

    // playlist
    QLabel* playlistHeader_ = nullptr;
    QListWidget* playlist_ = nullptr;

    // waveform peaks worker
    std::atomic<uint64_t> peaksJobId_{0};
    std::thread normalizeWorker_;
    QString currentWaveformPath_;
    Player& p();
};

}  // namespace lspa
