// SessionController.h - application controller tying together the engine,
// the plugin catalog, the session file and the rack graph.

#pragma once

#include <QObject>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <memory>
#include <thread>

#include "core/Session.h"
#include "core/Types.h"
#include "engine/AudioEngine.h"
#include "plugins/IPluginHost.h"

namespace lspa {

// UI-facing description of one rack slot.
struct RackSlotInfo {
    std::string uid;
    PluginKey key;
    QString displayName;
    QString pluginName;
    QString category;
    QString error;
    PluginLoadStatus status = PluginLoadStatus::NotLoaded;
    bool enabled = true;
    bool hasUi = false;
    int audioIns = 2;
    int audioOuts = 2;
};

class SessionController : public QObject {
    Q_OBJECT
public:
    explicit SessionController(AudioEngine* engine, QObject* parent = nullptr);
    ~SessionController() override;

    // Load session (or start empty) and apply everything to the engine.
    bool initialize(const QString& sessionPath);
    bool save(QString* errorOut = nullptr);
    QString sessionPath() const { return sessionPath_; }

    AudioEngine& engine() { return *engine_; }
    SessionConfig& config() { return config_; }
    const SessionConfig& config() const { return config_; }

    // --- catalog ---
    void startCatalogScan(bool refresh);
    bool catalogBusy() const { return scanBusy_.load(); }
    QVector<PluginDescriptor> catalog() const;

    // --- playlists ---
    void addFiles(PlayerId id, const QStringList& paths);
    void removeRows(PlayerId id, const QList<int>& rows);
    void clearPlaylist(PlayerId id);
    void selectTrack(PlayerId id, int index);  // plays it

    // --- rack ---
    QVector<RackSlotInfo> rackInfo(RackTarget target) const;
    const std::vector<RackEntry>& rackEntries(RackTarget target) const;
    void addPlugin(RackTarget target, const PluginKey& key);
    void removePlugin(RackTarget target, const QString& uid);
    void movePlugin(RackTarget target, const QString& uid, int delta);
    void togglePlugin(RackTarget target, const QString& uid, bool enabled);
    void setParameter(RackTarget target, const QString& uid, const QString& symbol, float value);
    void rebuildRack(RackTarget target);
    void rebuildAllRacks();

    // --- presets (FR-10) ---
    bool savePreset(RackTarget target, const QString& name, QString* errorOut);
    bool loadPreset(RackTarget target, const QString& name, QString* errorOut);
    QStringList presets() const;

    // --- settings ---
    bool applyAudioSettings(int sampleRate, int blockSize, const QString& device,
                            QString* errorOut);

signals:
    void catalogUpdated();
    void rackChanged(lspa::RackTarget target);
    void playlistChanged(lspa::PlayerId id);
    void sessionMessage(const QString& text, bool isError);
    void audioRestarted();

private:
    PlayerConfig& configFor(RackTarget target);
    const PlayerConfig& configFor(RackTarget target) const;
    PlayerConfig& configFor(PlayerId id);
    PlaylistItem makeItem(const QString& path) const;
    void rebuildRackInternal(RackTarget target);
    void reapplyAllToEngine();
    QString presetDir() const;

    AudioEngine* engine_ = nullptr;
    SessionConfig config_;
    QString sessionPath_;

    mutable std::mutex catalogMutex_;
    QVector<PluginDescriptor> catalog_;
    std::atomic<bool> scanBusy_{false};
    std::thread scanThread_;
};

}  // namespace lspa
