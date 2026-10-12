#include "TestUtil.h"

#include <sndfile.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <algorithm>

#include "core/Paths.h"
#include "engine/Player.h"

using namespace lspa;

namespace {

// Writes a test tone and returns its path. Uses the shared temp dir.
std::string writeToneWav(const std::string& name, double seconds, double freq, int rate) {
    ensureDir("/tmp/opencode");
    const std::string path = "/tmp/opencode/" + name + ".wav";
    SF_INFO info{};
    info.samplerate = rate;
    info.channels = 2;
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    SNDFILE* f = sf_open(path.c_str(), SFM_WRITE, &info);
    if (!f) return {};
    std::vector<float> buf(1024 * 2);
    const sf_count_t total = static_cast<sf_count_t>(seconds * rate);
    sf_count_t written = 0;
    while (written < total) {
        const sf_count_t n = std::min<sf_count_t>(1024, total - written);
        for (sf_count_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(written + i) / rate;
            const float v = static_cast<float>(0.5 * std::sin(2.0 * M_PI * freq * t));
            buf[static_cast<size_t>(i) * 2 + 0] = v;
            buf[static_cast<size_t>(i) * 2 + 1] = v;
        }
        sf_writef_float(f, buf.data(), n);
        written += n;
    }
    sf_close(f);
    return path;
}

}  // namespace

// Player is non-copyable; construct in place inside the test.
TEST_CASE(FadeInRampsSmoothly) {
    const std::string path = writeToneWav("fade_in", 3.0, 440.0, 48000);
    CHECK(!path.empty());

    Player p("Main", 48000, 256);
    FadeConfig f;
    f.fadeInMs = 200;
    f.fadeOutMs = 200;
    p.setFadeConfig(f);
    PlaylistItem item;
    item.path = path;
    item.title = "tone";
    p.setPlaylist({item});

    p.play();
    // Let the feeder thread open the file and prime the ring.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const int blockSize = 256;
    std::vector<float> l(static_cast<size_t>(blockSize));
    std::vector<float> r(static_cast<size_t>(blockSize));

    std::vector<float> rampStarts, rampEnds;
    for (int b = 0; b < 40; ++b) {
        PlayerBlockInfo info;
        p.pull(l.data(), r.data(), blockSize, info);
        rampStarts.push_back(info.rampStart);
        rampEnds.push_back(info.rampEnd);
        // Real audio should be flowing after the fade completes.
        if (b > 20) {
            float peak = 0.0f;
            for (float v : l) peak = std::max(peak, std::fabs(v));
            CHECK(peak > 0.05f);   // tone is present
            CHECK(peak < 1.0f);    // and not blown up
        }
    }

    // Fade-in must be monotonic and reach full level within the fade time.
    CHECK(rampStarts.front() < 0.1f);
    CHECK(rampEnds.back() > 0.98f);
    for (size_t i = 1; i < rampEnds.size(); ++i) {
        CHECK(rampEnds[i] >= rampEnds[i - 1] - 1e-6f);
    }
    // No gain jump larger than one block's worth of ramp (~0.053 for 200ms).
    const float maxStep = 1.0f / (0.200f * 48000.0f) * blockSize * 1.5f;
    for (size_t i = 1; i < rampEnds.size(); ++i) {
        CHECK(std::fabs(rampEnds[i] - rampEnds[i - 1]) <= maxStep);
    }
}

TEST_CASE(FadeOutOnStopReachesSilence) {
    const std::string path = writeToneWav("fade_out", 3.0, 220.0, 48000);
    CHECK(!path.empty());

    Player p("Main", 48000, 256);
    FadeConfig f;
    f.fadeInMs = 50;
    f.fadeOutMs = 150;
    p.setFadeConfig(f);
    PlaylistItem item;
    item.path = path;
    p.setPlaylist({item});
    p.play();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    std::vector<float> l(256), r(256);
    PlayerBlockInfo info;
    for (int b = 0; b < 20; ++b) p.pull(l.data(), r.data(), 256, info);

    p.stop();
    // After the fade-out completes the player must be stopped and silent.
    for (int b = 0; b < 40; ++b) {
        p.pull(l.data(), r.data(), 256, info);
        CHECK(info.rampEnd <= info.rampStart + 1e-6f);
    }
    CHECK(p.snapshot().state == PlayerState::Stopped);
    CHECK(p.snapshot().gain < 0.01f);
    CHECK(p.snapshot().positionFrames == 0);
}

TEST_CASE(PauseResumeKeepsPosition) {
    const std::string path = writeToneWav("pause_resume", 4.0, 330.0, 48000);
    CHECK(!path.empty());

    Player p("Main", 48000, 256);
    FadeConfig f;
    f.fadeInMs = 30;
    f.fadeOutMs = 30;
    p.setFadeConfig(f);
    PlaylistItem item;
    item.path = path;
    p.setPlaylist({item});
    p.play();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    std::vector<float> l(256), r(256);
    PlayerBlockInfo info;
    for (int b = 0; b < 30; ++b) p.pull(l.data(), r.data(), 256, info);

    p.pause();
    for (int b = 0; b < 30; ++b) p.pull(l.data(), r.data(), 256, info);
    const PlayerSnapshot paused = p.snapshot();
    CHECK(paused.state == PlayerState::Paused);
    const int64_t pausedAt = paused.positionFrames;
    CHECK(pausedAt > 0);

    p.play();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    for (int b = 0; b < 10; ++b) p.pull(l.data(), r.data(), 256, info);
    CHECK(p.snapshot().state == PlayerState::Playing);
    CHECK(p.snapshot().positionFrames >= pausedAt);
}

TEST_CASE(SeekJumpsWithoutDrift) {
    const std::string path = writeToneWav("seek", 5.0, 100.0, 48000);
    CHECK(!path.empty());

    Player p("Main", 48000, 256);
    FadeConfig f;
    f.fadeInMs = 10;
    f.fadeOutMs = 10;
    p.setFadeConfig(f);
    PlaylistItem item;
    item.path = path;
    p.setPlaylist({item});
    p.play();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::vector<float> l(256), r(256);
    PlayerBlockInfo info;
    for (int b = 0; b < 10; ++b) p.pull(l.data(), r.data(), 256, info);

    CHECK(p.seekSeconds(2.5));
    // Position is reported immediately and the audio resumes after the seek.
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    for (int b = 0; b < 30; ++b) p.pull(l.data(), r.data(), 256, info);
    const PlayerSnapshot snap = p.snapshot();
    CHECK(snap.positionSec > 2.0);
    CHECK(snap.positionSec < 4.0);
}
