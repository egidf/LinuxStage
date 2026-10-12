// Types.h - session/core data model shared by engine, persistence and UI.
// Qt-free on purpose (std types only) so the engine can be unit tested
// without a GUI toolkit.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "plugins/IPluginHost.h"

namespace lspa {

// ---------------------------------------------------------------------------
// Fades
// ---------------------------------------------------------------------------
struct FadeConfig {
    double fadeInMs = 250.0;
    double fadeOutMs = 450.0;
    // Apply fade-out before stopping / switching track.
    bool applyOnStop = true;
    bool applyOnTrackChange = true;
};

// ---------------------------------------------------------------------------
// Rack
// ---------------------------------------------------------------------------
enum class RackTarget {
    MainPlayer,
    BgmPlayer
};

inline const char* rackTargetName(RackTarget t) {
    return t == RackTarget::MainPlayer ? "Main Player" : "BGM Player";
}

struct RackEntry {
    std::string uid;         // stable per-instance id (session correlation)
    PluginKey plugin;        // format + identifier
    bool enabled = true;
    std::string displayName; // label shown in UI (may be empty -> use catalog)
    // Parameter snapshot: symbol -> value (restored after instantiation)
    std::vector<std::pair<std::string, float>> parameters;
};

// ---------------------------------------------------------------------------
// Players
// ---------------------------------------------------------------------------
struct PlayerConfig {
    std::vector<std::string> playlistPaths;
    int playlistIndex = -1;  // selected/current track
    float volume = 1.0f;
    bool muted = false;
    FadeConfig fade;
    std::vector<RackEntry> rack;
};

// ---------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------
struct SessionConfig {
    int schemaVersion = 1;
    PlayerConfig mainPlayer;
    PlayerConfig bgmPlayer;
    float masterVolume = 1.0f;
    std::string outputDeviceId = "default";
    int bufferSize = 256;
    int sampleRate = 48000;
};

// Current schema version understood by this build.
constexpr int kSessionSchemaVersion = 1;

// ---------------------------------------------------------------------------
// Playlist item state (UI-facing)
// ---------------------------------------------------------------------------
struct PlaylistItem {
    std::string path;
    std::string title;       // display title (filename fallback)
    std::string artist;
    std::string album;
    int64_t durationFrames = 0;
    double durationSec = 0.0;
    bool missing = false;    // file not found / unreadable at load time
    std::string error;       // why it is missing/unreadable
};

// Generate a short unique id for rack entries (no external dependency).
std::string makeUid();

}  // namespace lspa
