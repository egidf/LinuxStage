#include "core/Session.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "core/Logger.h"
#include "core/Paths.h"

namespace lspa {

namespace {

QJsonObject fadeToJson(const FadeConfig& f) {
    QJsonObject o;
    o["fadeInMs"] = f.fadeInMs;
    o["fadeOutMs"] = f.fadeOutMs;
    o["applyOnStop"] = f.applyOnStop;
    o["applyOnTrackChange"] = f.applyOnTrackChange;
    return o;
}

FadeConfig fadeFromJson(const QJsonObject& o) {
    FadeConfig f;
    f.fadeInMs = o.value("fadeInMs").toDouble(f.fadeInMs);
    f.fadeOutMs = o.value("fadeOutMs").toDouble(f.fadeOutMs);
    f.applyOnStop = o.value("applyOnStop").toBool(f.applyOnStop);
    f.applyOnTrackChange = o.value("applyOnTrackChange").toBool(f.applyOnTrackChange);
    return f;
}

QJsonArray rackToJson(const std::vector<RackEntry>& rack) {
    QJsonArray arr;
    for (const auto& e : rack) {
        QJsonObject o;
        o["uid"] = QString::fromStdString(e.uid);
        o["format"] = QString::fromUtf8(pluginFormatName(e.plugin.format));
        o["id"] = QString::fromStdString(e.plugin.id);
        o["enabled"] = e.enabled;
        o["displayName"] = QString::fromStdString(e.displayName);
        if (!e.parameters.empty()) {
            QJsonObject params;
            for (const auto& p : e.parameters) {
                params[QString::fromStdString(p.first)] = p.second;
            }
            o["parameters"] = params;
        }
        arr.append(o);
    }
    return arr;
}

std::vector<RackEntry> rackFromJson(const QJsonArray& arr) {
    std::vector<RackEntry> rack;
    rack.reserve(static_cast<size_t>(arr.size()));
    for (const auto& v : arr) {
        if (!v.isObject()) continue;
        QJsonObject o = v.toObject();
        RackEntry e;
        e.uid = o.value("uid").toString().toStdString();
        if (e.uid.empty()) e.uid = makeUid();
        const QString fmt = o.value("format").toString();
        e.plugin.format = (fmt == QLatin1String("VST3")) ? PluginFormat::Vst3 : PluginFormat::Lv2;
        e.plugin.id = o.value("id").toString().toStdString();
        if (e.plugin.id.empty()) continue;
        e.enabled = o.value("enabled").toBool(true);
        e.displayName = o.value("displayName").toString().toStdString();
        const QJsonObject params = o.value("parameters").toObject();
        for (auto it = params.begin(); it != params.end(); ++it) {
            e.parameters.emplace_back(it.key().toStdString(),
                                      static_cast<float>(it.value().toDouble()));
        }
        rack.push_back(std::move(e));
    }
    return rack;
}

QJsonObject playerToJson(const PlayerConfig& p) {
    QJsonObject o;
    QJsonArray pl;
    for (const auto& path : p.playlistPaths) pl.append(QString::fromStdString(path));
    o["playlist"] = pl;
    o["playlistIndex"] = p.playlistIndex;
    o["volume"] = p.volume;
    o["muted"] = p.muted;
    o["fade"] = fadeToJson(p.fade);
    o["rack"] = rackToJson(p.rack);
    return o;
}

PlayerConfig playerFromJson(const QJsonObject& o) {
    PlayerConfig p;
    for (const auto& v : o.value("playlist").toArray()) {
        const QString s = v.toString();
        if (!s.isEmpty()) p.playlistPaths.push_back(s.toStdString());
    }
    p.playlistIndex = o.value("playlistIndex").toInt(-1);
    p.volume = static_cast<float>(o.value("volume").toDouble(1.0));
    p.muted = o.value("muted").toBool(false);
    p.fade = fadeFromJson(o.value("fade").toObject());
    p.rack = rackFromJson(o.value("rack").toArray());
    return p;
}

}  // namespace

std::string sessionToJson(const SessionConfig& session) {
    QJsonObject root;
    root["app"] = "LSPA";
    root["schemaVersion"] = session.schemaVersion;
    root["mainPlayer"] = playerToJson(session.mainPlayer);
    root["bgmPlayer"] = playerToJson(session.bgmPlayer);
    root["masterVolume"] = session.masterVolume;
    root["outputDeviceId"] = QString::fromStdString(session.outputDeviceId);
    root["bufferSize"] = session.bufferSize;
    root["sampleRate"] = session.sampleRate;
    return QJsonDocument(root).toJson(QJsonDocument::Indented).toStdString();
}

bool sessionFromJson(const std::string& json, SessionConfig& out, std::string& errorOut) {
    QJsonParseError err{};
    const QJsonDocument doc =
        QJsonDocument::fromJson(QByteArray(json.data(), static_cast<int>(json.size())), &err);
    if (doc.isNull() || !doc.isObject()) {
        errorOut = "Session file is not valid JSON: " + err.errorString().toStdString();
        return false;
    }
    const QJsonObject root = doc.object();
    const int version = root.value("schemaVersion").toInt(0);
    if (version <= 0) {
        errorOut = "Session file has no schemaVersion field.";
        return false;
    }
    if (version > kSessionSchemaVersion) {
        errorOut = "Session file uses schema version " + std::to_string(version) +
                   " but this build understands up to version " +
                   std::to_string(kSessionSchemaVersion) + ".";
        return false;
    }
    out = SessionConfig{};
    out.schemaVersion = version;
    out.mainPlayer = playerFromJson(root.value("mainPlayer").toObject());
    out.bgmPlayer = playerFromJson(root.value("bgmPlayer").toObject());
    out.masterVolume = static_cast<float>(root.value("masterVolume").toDouble(1.0));
    out.outputDeviceId = root.value("outputDeviceId").toString().toStdString();
    if (out.outputDeviceId.empty()) out.outputDeviceId = "default";
    out.bufferSize = root.value("bufferSize").toInt(256);
    out.sampleRate = root.value("sampleRate").toInt(48000);
    return true;
}

bool saveSession(const SessionConfig& session, const std::string& path, std::string& errorOut) {
    ensureDir(configDir());
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        errorOut = "Cannot write session file '" + path + "': " +
                   f.errorString().toStdString();
        return false;
    }
    const std::string json = sessionToJson(session);
    f.write(json.data(), static_cast<int>(json.size()));
    f.close();
    LSPA_LOG_INFO("Session saved to %s (%zu bytes)", path.c_str(), json.size());
    return true;
}

bool loadSession(const std::string& path, SessionConfig& out, std::string& errorOut) {
    QFile f(QString::fromStdString(path));
    if (!f.exists()) {
        errorOut = "Session file does not exist: " + path;
        return false;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        errorOut = "Cannot open session file '" + path + "': " + f.errorString().toStdString();
        return false;
    }
    const QByteArray data = f.readAll();
    return sessionFromJson(std::string(data.constData(), static_cast<size_t>(data.size())),
                           out, errorOut);
}

std::string rackToJsonString(const std::vector<RackEntry>& rack) {
    QJsonObject o;
    o["schemaVersion"] = kSessionSchemaVersion;
    o["rack"] = rackToJson(rack);
    return QJsonDocument(o).toJson(QJsonDocument::Indented).toStdString();
}

bool rackFromJsonString(const std::string& json, std::vector<RackEntry>& out,
                        std::string& errorOut) {
    QJsonParseError err{};
    const QJsonDocument doc =
        QJsonDocument::fromJson(QByteArray(json.data(), static_cast<int>(json.size())), &err);
    if (doc.isNull() || !doc.isObject()) {
        errorOut = "Preset is not valid JSON: " + err.errorString().toStdString();
        return false;
    }
    const QJsonObject o = doc.object();
    const int version = o.value("schemaVersion").toInt(0);
    if (version > kSessionSchemaVersion) {
        errorOut = "Preset uses a newer schema version (" + std::to_string(version) + ").";
        return false;
    }
    out = rackFromJson(o.value("rack").toArray());
    return true;
}

}  // namespace lspa
