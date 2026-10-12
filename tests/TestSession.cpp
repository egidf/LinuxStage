#include "TestUtil.h"

#include "core/Paths.h"
#include "core/Session.h"
#include "core/Types.h"

using namespace lspa;

namespace {

SessionConfig sampleSession() {
    SessionConfig cfg;
    cfg.schemaVersion = kSessionSchemaVersion;
    cfg.mainPlayer.playlistPaths = {"/music/a.wav", "/music/b.flac"};
    cfg.mainPlayer.playlistIndex = 1;
    cfg.mainPlayer.volume = 0.8f;
    cfg.mainPlayer.fade.fadeInMs = 120;
    cfg.mainPlayer.fade.fadeOutMs = 340;

    RackEntry eq;
    eq.uid = "uid-eq";
    eq.plugin.format = PluginFormat::Lv2;
    eq.plugin.id = "http://example.org/eq";
    eq.displayName = "Example EQ";
    eq.enabled = true;
    eq.parameters = {{"freq", 120.0f}, {"gain", 2.5f}};
    cfg.mainPlayer.rack.push_back(eq);

    RackEntry comp;
    comp.uid = "uid-comp";
    comp.plugin.format = PluginFormat::Lv2;
    comp.plugin.id = "http://example.org/comp";
    comp.enabled = false;
    cfg.mainPlayer.rack.push_back(comp);

    cfg.bgmPlayer.playlistPaths = {"/music/bgm.wav"};
    cfg.bgmPlayer.volume = 0.5f;
    cfg.masterVolume = 0.9f;
    cfg.outputDeviceId = "pulse";
    cfg.bufferSize = 512;
    cfg.sampleRate = 44100;
    return cfg;
}

}  // namespace

TEST_CASE(SessionJsonRoundTrip) {
    const SessionConfig cfg = sampleSession();
    const std::string json = sessionToJson(cfg);
    CHECK(!json.empty());

    SessionConfig out;
    std::string err;
    CHECK(sessionFromJson(json, out, err));
    CHECK(out.schemaVersion == kSessionSchemaVersion);
    CHECK(out.mainPlayer.playlistPaths.size() == 2);
    CHECK(out.mainPlayer.playlistPaths[0] == "/music/a.wav");
    CHECK(out.mainPlayer.playlistIndex == 1);
    CHECK_NEAR(out.mainPlayer.volume, 0.8f, 1e-5);
    CHECK_NEAR(out.mainPlayer.fade.fadeInMs, 120.0, 1e-6);
    CHECK_NEAR(out.mainPlayer.fade.fadeOutMs, 340.0, 1e-6);

    CHECK(out.mainPlayer.rack.size() == 2);
    CHECK(out.mainPlayer.rack[0].uid == "uid-eq");
    CHECK(out.mainPlayer.rack[0].plugin.id == "http://example.org/eq");
    CHECK(out.mainPlayer.rack[0].plugin.format == PluginFormat::Lv2);
    CHECK(out.mainPlayer.rack[0].enabled);
    CHECK(out.mainPlayer.rack[0].parameters.size() == 2);
    CHECK_NEAR(out.mainPlayer.rack[0].parameters[0].second, 120.0f, 1e-4);
    CHECK(!out.mainPlayer.rack[1].enabled);

    CHECK(out.bgmPlayer.playlistPaths.size() == 1);
    CHECK_NEAR(out.masterVolume, 0.9f, 1e-5);
    CHECK(out.outputDeviceId == "pulse");
    CHECK(out.bufferSize == 512);
    CHECK(out.sampleRate == 44100);
}

TEST_CASE(SessionRejectsCorruptJson) {
    SessionConfig out;
    std::string err;
    CHECK(!sessionFromJson("this is not json {", out, err));
    CHECK(!err.empty());
}

TEST_CASE(SessionRejectsNewerSchema) {
    SessionConfig out;
    std::string err;
    CHECK(!sessionFromJson("{\"schemaVersion\": 9999}", out, err));
    CHECK(err.find("schema version") != std::string::npos);
}

TEST_CASE(SessionRejectsMissingVersion) {
    SessionConfig out;
    std::string err;
    CHECK(!sessionFromJson("{\"app\": \"LSPA\"}", out, err));
    CHECK(!err.empty());
}

TEST_CASE(RackPresetRoundTrip) {
    const SessionConfig cfg = sampleSession();
    const std::string json = rackToJsonString(cfg.mainPlayer.rack);
    std::vector<RackEntry> rack;
    std::string err;
    CHECK(rackFromJsonString(json, rack, err));
    CHECK(rack.size() == 2);
    CHECK(rack[0].displayName == "Example EQ");
    CHECK(rack[1].plugin.id == "http://example.org/comp");
}

TEST_CASE(SessionFileRoundTrip) {
    ensureDir("/tmp/opencode");
    const std::string path = "/tmp/opencode/session_test.json";
    const SessionConfig cfg = sampleSession();
    std::string err;
    CHECK(saveSession(cfg, path, err));

    SessionConfig out;
    CHECK(loadSession(path, out, err));
    CHECK(out.mainPlayer.rack.size() == 2);
    CHECK_NEAR(out.bgmPlayer.volume, 0.5f, 1e-5);

    // Missing file must fail with a clear message, not silently succeed.
    SessionConfig missing;
    CHECK(!loadSession("/tmp/opencode/does_not_exist.json", missing, err));
    CHECK(err.find("does not exist") != std::string::npos);
}
