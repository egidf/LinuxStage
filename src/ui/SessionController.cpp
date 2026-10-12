#include "ui/SessionController.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>

#include <algorithm>

#include "core/Logger.h"
#include "core/Paths.h"
#include "engine/Decoder.h"
#include "plugins/Lv2Host.h"
#include "plugins/PluginCatalog.h"

namespace lspa {

SessionController::SessionController(AudioEngine* engine, QObject* parent)
    : QObject(parent), engine_(engine) {
    auto& catalog = PluginCatalog::instance();
    if (!catalog.hostFor(PluginFormat::Lv2)) {
        catalog.addHost(std::make_unique<Lv2Host>());
    }
}

SessionController::~SessionController() {
    scanBusy_.store(false);
    if (scanThread_.joinable()) scanThread_.join();
}

// ---------------------------------------------------------------------------
// session lifecycle
// ---------------------------------------------------------------------------

bool SessionController::initialize(const QString& sessionPath) {
    sessionPath_ = sessionPath.isEmpty() ? QString::fromStdString(defaultSessionPath())
                                         : sessionPath;

    bool loaded = false;
    if (QFile::exists(sessionPath_)) {
        std::string err;
        SessionConfig cfg;
        if (loadSession(sessionPath_.toStdString(), cfg, err)) {
            config_ = cfg;
            loaded = true;
            LSPA_LOG_INFO("Session loaded from %s", sessionPath_.toStdString().c_str());
        } else {
            emit sessionMessage(QString::fromStdString(err), true);
            LSPA_LOG_WARN("Session load failed: %s", err.c_str());
        }
    }

    ensureDir(configDir());

    // Start the audio engine with the session's audio settings.
    EngineConfig ec;
    ec.sampleRate = config_.sampleRate;
    ec.blockSize = config_.bufferSize;
    ec.device = config_.outputDeviceId;
    std::string err;
    if (!engine_->start(ec, err)) {
        emit sessionMessage(QString::fromStdString(err), true);
        LSPA_LOG_ERROR("Audio engine failed to start: %s", err.c_str());
    } else if (!err.empty()) {
        // Engine fell back to the null device; surface that to the user.
        emit sessionMessage(QString::fromStdString(err), true);
    }

    engine_->setMasterVolume(config_.masterVolume);
    reapplyAllToEngine();
    startCatalogScan(true);

    if (!loaded) {
        emit sessionMessage(tr("No previous session found - started with an empty setup."),
                            false);
    }
    return loaded;
}

void SessionController::reapplyAllToEngine() {
    for (PlayerId id : {PlayerId::Main, PlayerId::Bgm}) {
        Player& p = engine_->player(id);
        PlayerConfig& cfg = configFor(id);
        std::vector<PlaylistItem> items;
        items.reserve(cfg.playlistPaths.size());
        int missingCount = 0;
        for (const auto& path : cfg.playlistPaths) {
            PlaylistItem item = makeItem(QString::fromStdString(path));
            if (item.missing) ++missingCount;
            items.push_back(std::move(item));
        }
        if (missingCount > 0) {
            emit sessionMessage(tr("%1 playlist file(s) could not be read and are marked as "
                                   "missing.")
                                    .arg(missingCount),
                                true);
        }
        p.setPlaylist(std::move(items));
        p.setVolume(cfg.volume);
        p.setMuted(cfg.muted);
        p.setFadeConfig(cfg.fade);
        if (cfg.playlistIndex >= 0) p.selectIndex(cfg.playlistIndex);
        emit playlistChanged(id);
    }
    rebuildAllRacks();
}

PlaylistItem SessionController::makeItem(const QString& path) const {
    PlaylistItem item;
    item.path = path.toStdString();
    item.title = QFileInfo(path).completeBaseName().toStdString();
    std::string err;
    auto dec = openSndfileDecoder(item.path, err);
    if (dec) {
        item.durationFrames = dec->totalFrames();
        item.durationSec = double(dec->totalFrames()) / std::max(1, dec->sampleRate());
        item.missing = false;
    } else {
        item.missing = true;
        item.error = err;
        LSPA_LOG_WARN("Playlist item unreadable: %s", err.c_str());
    }
    return item;
}

bool SessionController::save(QString* errorOut) {
    // Remember the current selection of each player.
    config_.mainPlayer.playlistIndex = engine_->player(PlayerId::Main).snapshot().trackIndex;
    config_.bgmPlayer.playlistIndex = engine_->player(PlayerId::Bgm).snapshot().trackIndex;
    config_.masterVolume = engine_->masterVolume();
    config_.sampleRate = engine_->config().sampleRate;
    config_.bufferSize = engine_->config().blockSize;
    config_.outputDeviceId = engine_->config().device;
    config_.schemaVersion = kSessionSchemaVersion;

    std::string err;
    if (!saveSession(config_, sessionPath_.toStdString(), err)) {
        if (errorOut) *errorOut = QString::fromStdString(err);
        emit sessionMessage(QString::fromStdString(err), true);
        return false;
    }
    emit sessionMessage(tr("Session saved."), false);
    return true;
}

// ---------------------------------------------------------------------------
// catalog
// ---------------------------------------------------------------------------

void SessionController::startCatalogScan(bool refresh) {
    if (scanBusy_.exchange(true)) return;
    if (scanThread_.joinable()) scanThread_.join();

    scanThread_ = std::thread([this, refresh] {
        auto result = PluginCatalog::instance().scan(refresh);
        {
            std::lock_guard<std::mutex> lock(catalogMutex_);
            catalog_.resize(static_cast<int>(result.size()));
            for (size_t i = 0; i < result.size(); ++i) {
                catalog_[static_cast<int>(i)] = result[i];
            }
        }
        scanBusy_.store(false);
        QMetaObject::invokeMethod(this, [this] { emit catalogUpdated(); },
                                  Qt::QueuedConnection);
    });
}

QVector<PluginDescriptor> SessionController::catalog() const {
    std::lock_guard<std::mutex> lock(catalogMutex_);
    return catalog_;
}

// ---------------------------------------------------------------------------
// playlists
// ---------------------------------------------------------------------------

void SessionController::addFiles(PlayerId id, const QStringList& paths) {
    if (paths.isEmpty()) return;
    PlayerConfig& cfg = configFor(id);
    std::vector<PlaylistItem> items;
    QStringList added;
    int missing = 0;
    for (const QString& path : paths) {
        PlaylistItem item = makeItem(path);
        if (item.missing) ++missing;
        cfg.playlistPaths.push_back(item.path);
        added.push_back(path);
        items.push_back(std::move(item));
    }
    engine_->player(id).appendItems(std::move(items));
    emit playlistChanged(id);
    emit sessionMessage(tr("%1 track(s) added to the %2 playlist.")
                            .arg(added.size())
                            .arg(id == PlayerId::Main ? tr("Main Player") : tr("BGM Player")),
                        false);
    if (missing > 0) {
        emit sessionMessage(tr("%1 file(s) could not be opened and are marked as missing.")
                                .arg(missing),
                            true);
    }
}

void SessionController::removeRows(PlayerId id, const QList<int>& rows) {
    if (rows.isEmpty()) return;
    PlayerConfig& cfg = configFor(id);
    std::vector<int> sorted;
    sorted.reserve(static_cast<size_t>(rows.size()));
    for (int r : rows) sorted.push_back(r);
    std::sort(sorted.begin(), sorted.end());
    for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
        const int idx = *it;
        if (idx >= 0 && idx < static_cast<int>(cfg.playlistPaths.size())) {
            cfg.playlistPaths.erase(cfg.playlistPaths.begin() + idx);
        }
    }
    engine_->player(id).removeItems(sorted);
    emit playlistChanged(id);
}

void SessionController::clearPlaylist(PlayerId id) {
    configFor(id).playlistPaths.clear();
    configFor(id).playlistIndex = -1;
    engine_->player(id).clearPlaylist();
    emit playlistChanged(id);
}

void SessionController::selectTrack(PlayerId id, int index) {
    configFor(id).playlistIndex = index;
    engine_->player(id).playIndex(index);
    // FR: Main player play -> BGM = Mute
    if (id == PlayerId::Main) {
        engine_->player(PlayerId::Bgm).setMuted(true);
    }
}

PlayerConfig& SessionController::configFor(RackTarget target) {
    return target == RackTarget::MainPlayer ? config_.mainPlayer : config_.bgmPlayer;
}

const PlayerConfig& SessionController::configFor(RackTarget target) const {
    return target == RackTarget::MainPlayer ? config_.mainPlayer : config_.bgmPlayer;
}

PlayerConfig& SessionController::configFor(PlayerId id) {
    return id == PlayerId::Main ? config_.mainPlayer : config_.bgmPlayer;
}

// ---------------------------------------------------------------------------
// rack
// ---------------------------------------------------------------------------

QVector<RackSlotInfo> SessionController::rackInfo(RackTarget target) const {
    const PlayerConfig& cfg = configFor(target);
    const auto live = engine_->chainMetadata(target);
    QVector<RackSlotInfo> out;
    out.reserve(static_cast<int>(cfg.rack.size()));
    for (const RackEntry& entry : cfg.rack) {
        RackSlotInfo info;
        info.uid = entry.uid;
        info.key = entry.plugin;
        info.enabled = entry.enabled;
        info.displayName = QString::fromStdString(entry.displayName);

        const PluginDescriptor* desc = PluginCatalog::instance().find(entry.plugin);
        if (desc) {
            if (info.displayName.isEmpty()) info.displayName = QString::fromStdString(desc->name);
            info.pluginName = QString::fromStdString(desc->name);
            info.category = QString::fromStdString(desc->category);
            info.hasUi = desc->hasUi;
            info.audioIns = desc->audioInputs;
            info.audioOuts = desc->audioOutputs;
            info.status = PluginLoadStatus::Loaded;
        } else {
            if (info.displayName.isEmpty()) {
                info.displayName = QString::fromStdString(entry.plugin.id);
            }
            info.pluginName = info.displayName;
            info.status = PluginLoadStatus::Missing;
            info.error = tr("Plugin not found on this system.");
        }

        for (const auto& s : live) {
            if (s.uid == entry.uid) {
                info.status = s.status;
                info.error = QString::fromStdString(s.error);
                if (!s.displayName.empty()) info.displayName = QString::fromStdString(s.displayName);
                break;
            }
        }
        out.push_back(std::move(info));
    }
    return out;
}

const std::vector<RackEntry>& SessionController::rackEntries(RackTarget target) const {
    return configFor(target).rack;
}

void SessionController::addPlugin(RackTarget target, const PluginKey& key) {
    PlayerConfig& cfg = configFor(target);
    RackEntry entry;
    entry.uid = makeUid();
    entry.plugin = key;
    entry.enabled = true;
    const PluginDescriptor* desc = PluginCatalog::instance().find(key);
    entry.displayName = desc ? desc->name : key.id;
    cfg.rack.push_back(std::move(entry));
    rebuildRackInternal(target);
    emit sessionMessage(tr("%1 added to the %2 rack.")
                            .arg(QString::fromStdString(entry.displayName))
                            .arg(QString::fromUtf8(rackTargetName(target))),
                        false);
}

void SessionController::removePlugin(RackTarget target, const QString& uid) {
    PlayerConfig& cfg = configFor(target);
    auto& rack = cfg.rack;
    for (size_t i = 0; i < rack.size(); ++i) {
        if (rack[i].uid == uid.toStdString()) {
            rack.erase(rack.begin() + static_cast<long>(i));
            break;
        }
    }
    rebuildRackInternal(target);
}

void SessionController::movePlugin(RackTarget target, const QString& uid, int delta) {
    PlayerConfig& cfg = configFor(target);
    auto& rack = cfg.rack;
    for (size_t i = 0; i < rack.size(); ++i) {
        if (rack[i].uid != uid.toStdString()) continue;
        const long from = static_cast<long>(i);
        long to = from + delta;
        if (to < 0 || to >= static_cast<long>(rack.size())) return;
        std::swap(rack[static_cast<size_t>(from)], rack[static_cast<size_t>(to)]);
        break;
    }
    rebuildRackInternal(target);
}

void SessionController::togglePlugin(RackTarget target, const QString& uid, bool enabled) {
    PlayerConfig& cfg = configFor(target);
    for (auto& e : cfg.rack) {
        if (e.uid == uid.toStdString()) {
            e.enabled = enabled;
            engine_->setSlotEnabled(target, e.uid, enabled);
            break;
        }
    }
}

void SessionController::setPlayerFade(PlayerId id, const FadeConfig& fade) {
    configFor(id).fade = fade;
    engine_->player(id).setFadeConfig(fade);
}

void SessionController::setParameter(RackTarget target, const QString& uid,
                                     const QString& symbol, float value) {
    PlayerConfig& cfg = configFor(target);
    const std::string uidStd = uid.toStdString();
    const std::string symStd = symbol.toStdString();
    for (auto& e : cfg.rack) {
        if (e.uid != uidStd) continue;
        bool found = false;
        for (auto& p : e.parameters) {
            if (p.first == symStd) {
                p.second = value;
                found = true;
                break;
            }
        }
        if (!found) e.parameters.emplace_back(symStd, value);
        break;
    }
    engine_->setParameter(target, uidStd, symStd, value);
}

void SessionController::rebuildRack(RackTarget target) { rebuildRackInternal(target); }

void SessionController::rebuildAllRacks() {
    rebuildRackInternal(RackTarget::MainPlayer);
    rebuildRackInternal(RackTarget::BgmPlayer);
}

void SessionController::rebuildRackInternal(RackTarget target) {
    emit rackAboutToChange(target);
    PlayerConfig& cfg = configFor(target);
    auto& catalog = PluginCatalog::instance();

    auto chain = std::make_unique<EffectChain>(engine_->config().blockSize);
    const double sr = engine_->config().sampleRate;
    const int block = engine_->config().blockSize;

    for (RackEntry entry : cfg.rack) {
        if (entry.uid.empty()) entry.uid = makeUid();

        EffectChain::Slot slot;
        slot.uid = entry.uid;
        slot.key = entry.plugin;
        slot.displayName = entry.displayName;
        slot.enabled.store(entry.enabled);

        const PluginDescriptor* desc = catalog.find(entry.plugin);
        const int ins = desc ? desc->audioInputs : 2;
        const int outs = desc ? desc->audioOutputs : 2;
        slot.audioIns = ins;
        slot.audioOuts = outs;

        std::string err;
        auto inst = catalog.instantiate(entry.plugin, sr, block, err);
        if (!inst) {
            slot.status = desc ? PluginLoadStatus::Failed : PluginLoadStatus::Missing;
            slot.error = err;
            LSPA_LOG_ERROR("Rack %s: failed to load '%s': %s", rackTargetName(target),
                           entry.plugin.id.c_str(), err.c_str());
        } else if (ins == 1 && outs == 1) {
            // Mono plugin: one instance per channel.
            std::string err2;
            auto inst2 = catalog.instantiate(entry.plugin, sr, block, err2);
            slot.instances.push_back(std::move(inst));
            slot.instances.push_back(std::move(inst2));
            slot.status = PluginLoadStatus::Loaded;
        } else {
            slot.instances.push_back(std::move(inst));
            slot.status = PluginLoadStatus::Loaded;
        }

        // Restore saved parameters.
        for (const auto& [symbol, value] : entry.parameters) {
            for (auto& i : slot.instances) {
                if (i) i->setParameter(symbol, value);
            }
        }
        if (slot.displayName.empty() && desc) slot.displayName = desc->name;

        chain->addSlot(std::move(slot));
    }

    engine_->setChain(target, std::move(chain));
    emit rackChanged(target);
}

// ---------------------------------------------------------------------------
// presets
// ---------------------------------------------------------------------------

QString SessionController::presetDir() const {
    return QString::fromStdString(joinPath(configDir(), "presets"));
}

bool SessionController::savePreset(RackTarget target, const QString& name, QString* errorOut) {
    if (name.trimmed().isEmpty()) {
        if (errorOut) *errorOut = tr("Preset name must not be empty.");
        return false;
    }
    QDir().mkpath(presetDir());
    const QString path = presetDir() + "/" + name.trimmed() + ".json";
    const std::string json = rackToJsonString(configFor(target).rack);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut) *errorOut = f.errorString();
        return false;
    }
    f.write(json.data(), static_cast<int>(json.size()));
    f.close();
    LSPA_LOG_INFO("Preset saved: %s", path.toStdString().c_str());
    return true;
}

bool SessionController::loadPreset(RackTarget target, const QString& name, QString* errorOut) {
    const QString path = presetDir() + "/" + name.trimmed() + ".json";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorOut) *errorOut = tr("Cannot open preset '%1'.").arg(name);
        return false;
    }
    const QByteArray data = f.readAll();
    std::vector<RackEntry> rack;
    std::string err;
    if (!rackFromJsonString(std::string(data.constData(), static_cast<size_t>(data.size())),
                            rack, err)) {
        if (errorOut) *errorOut = QString::fromStdString(err);
        return false;
    }
    for (auto& e : rack) {
        if (e.uid.empty()) e.uid = makeUid();
    }
    configFor(target).rack = std::move(rack);
    rebuildRackInternal(target);
    return true;
}

QStringList SessionController::presets() const {
    QStringList out;
    QDir dir(presetDir());
    if (!dir.exists()) return out;
    for (const QFileInfo& fi : dir.entryInfoList({"*.json"}, QDir::Files, QDir::Name)) {
        out.push_back(fi.completeBaseName());
    }
    return out;
}

// ---------------------------------------------------------------------------
// audio settings
// ---------------------------------------------------------------------------

bool SessionController::applyAudioSettings(int sampleRate, int blockSize, const QString& device,
                                           QString* errorOut) {
    EngineConfig ec;
    ec.sampleRate = sampleRate;
    ec.blockSize = blockSize;
    ec.device = device.toStdString();

    std::string err;
    if (!engine_->start(ec, err)) {
        if (errorOut) *errorOut = QString::fromStdString(err);
        return false;
    }
    config_.sampleRate = sampleRate;
    config_.bufferSize = blockSize;
    config_.outputDeviceId = ec.device;
    engine_->setMasterVolume(config_.masterVolume);
    reapplyAllToEngine();
    emit audioRestarted();
    emit sessionMessage(tr("Audio engine restarted (%1 Hz, %2 samples, device '%3').")
                            .arg(sampleRate)
                            .arg(blockSize)
                            .arg(device),
                        false);
    return true;
}

}  // namespace lspa
