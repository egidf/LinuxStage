#include "TestUtil.h"

#include <sndfile.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <thread>
#include <vector>

#include "core/Paths.h"
#include "engine/AudioEngine.h"
#include "engine/Decoder.h"
#include "engine/WavWriter.h"

using namespace lspa;

namespace {

std::string writeTone(const std::string& name, double seconds, double freq, int rate) {
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

PlaylistItem itemFor(const std::string& path) {
    PlaylistItem item;
    item.path = path;
    item.title = "tone";
    return item;
}

struct RenderStats {
    double rms = 0.0;
    float peak = 0.0f;
    bool finite = true;
    sf_count_t frames = 0;
};

// Analyze a window of a wav: skip fromSec, read at most maxSeconds
// (maxSeconds <= 0 reads to the end).
RenderStats analyze(const std::string& path, double fromSec = 0.0,
                    double maxSeconds = 0.0) {
    RenderStats stats;
    std::string err;
    auto dec = openSndfileDecoder(path, err);
    if (!dec) return stats;
    if (fromSec > 0.0) dec->seek(static_cast<int64_t>(fromSec * dec->sampleRate()));
    const int64_t limit =
        maxSeconds > 0.0 ? static_cast<int64_t>(maxSeconds * dec->sampleRate()) : INT64_MAX;
    std::vector<float> l(1024), r(1024);
    double sum = 0.0;
    int64_t count = 0;
    while (count < limit) {
        const int want = static_cast<int>(std::min<int64_t>(1024, limit - count));
        const int got = dec->read(l.data(), r.data(), want);
        if (got <= 0) break;
        for (int i = 0; i < got; ++i) {
            const float v = l[static_cast<size_t>(i)];
            if (!std::isfinite(v)) stats.finite = false;
            sum += static_cast<double>(v) * v;
            stats.peak = std::max(stats.peak, std::fabs(v));
        }
        count += got;
    }
    stats.frames = count;
    stats.rms = count > 0 ? std::sqrt(sum / count) : 0.0;
    return stats;
}

double toneAmplitude(const std::string& path, double frequency, double fromSec) {
    std::string error;
    auto decoder = openSndfileDecoder(path, error);
    if (!decoder) return 0.0;
    decoder->seek(static_cast<int64_t>(fromSec * decoder->sampleRate()));
    constexpr int count = 8192;
    std::vector<float> left(count), right(count);
    const int frames = decoder->read(left.data(), right.data(), count);
    if (frames <= 0) return 0.0;
    double real = 0.0;
    double imaginary = 0.0;
    for (int i = 0; i < frames; ++i) {
        const double sample = 0.5 * (left[static_cast<size_t>(i)] + right[static_cast<size_t>(i)]);
        const double phase = 2.0 * M_PI * frequency * i / decoder->sampleRate();
        real += sample * std::cos(phase);
        imaginary -= sample * std::sin(phase);
    }
    return 2.0 * std::hypot(real, imaginary) / frames;
}

}  // namespace

TEST_CASE(OfflineRenderProducesAudio) {
    const std::string tone = writeTone("render_tone", 3.0, 440.0, 48000);
    CHECK(!tone.empty());

    AudioEngine engine;
    EngineConfig cfg;
    cfg.sampleRate = 48000;
    cfg.blockSize = 256;
    cfg.offline = true;
    std::string err;
    CHECK(engine.start(cfg, err));

    engine.player(PlayerId::Main).setPlaylist({itemFor(tone)});
    engine.player(PlayerId::Main).setTrackGain(0, 0.5f);
    CHECK_NEAR(engine.player(PlayerId::Main).trackGain(0), 0.5f, 1.0e-6);
    FadeConfig f;
    f.fadeInMs = 20;
    f.fadeOutMs = 20;
    engine.player(PlayerId::Main).setFadeConfig(f);
    engine.player(PlayerId::Main).play();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    const std::string out = "/tmp/opencode/render_out.wav";
    auto sink = createWavSink(out, err);
    CHECK(sink != nullptr);
    CHECK(engine.renderOffline(1.0, *sink, err));
    std::array<float, AudioEngine::kWaveformSamples> waveform{};
    CHECK(engine.copyMasterWaveform(waveform));
    float waveformPeak = 0.0f;
    for (float sample : waveform) waveformPeak = std::max(waveformPeak, std::fabs(sample));
    CHECK(waveformPeak > 0.05f);
    engine.stop();

    const RenderStats stats = analyze(out, 0.1);
    CHECK(stats.frames > 40000);
    CHECK(stats.finite);
    CHECK(stats.rms > 0.05);
    CHECK(stats.peak <= 1.0f);
}

TEST_CASE(MasterVolumeZeroIsSilent) {
    const std::string tone = writeTone("render_mute", 2.0, 300.0, 48000);
    AudioEngine engine;
    EngineConfig cfg;
    cfg.offline = true;
    std::string err;
    CHECK(engine.start(cfg, err));

    engine.player(PlayerId::Main).setPlaylist({itemFor(tone)});
    engine.player(PlayerId::Main).play();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    engine.setMasterVolume(0.0f);
    const std::string out = "/tmp/opencode/render_mute_out.wav";
    auto sink = createWavSink(out, err);
    CHECK(engine.renderOffline(0.5, *sink, err));
    engine.stop();

    const RenderStats stats = analyze(out);
    CHECK(stats.frames > 10000);
    CHECK(stats.peak < 1e-4f);
}

TEST_CASE(PauseUsesResponsiveTransportFade) {
    const std::string tone = writeTone("pause_responsive", 2.0, 440.0, 48000);
    Player player("pause-test", 48000, 256);
    player.setPlaylist({itemFor(tone)});
    FadeConfig fade;
    fade.fadeOutMs = 450.0;
    player.setFadeConfig(fade);
    player.play();

    std::vector<float> left(256), right(256);
    PlayerBlockInfo block;
    player.pull(left.data(), right.data(), 256, block);
    player.pause();
    player.pull(left.data(), right.data(), 256, block);
    player.pull(left.data(), right.data(), 256, block);
    CHECK(player.snapshot().state == PlayerState::Paused);
}

TEST_CASE(ShuffleVisitsEachTrackOnceBeforeRepeating) {
    const std::string tone = writeTone("shuffle_cycle", 1.0, 440.0, 48000);
    Player player("shuffle-test", 48000, 256);
    std::vector<PlaylistItem> items(4, itemFor(tone));
    player.setPlaylist(std::move(items));
    player.selectIndex(0);
    player.setShuffleEnabled(true);

    std::array<bool, 4> visited{};
    visited[0] = true;
    for (int i = 0; i < 3; ++i) {
        player.next();
        const int track = player.snapshot().trackIndex;
        CHECK(track >= 0 && track < static_cast<int>(visited.size()));
        CHECK(!visited[static_cast<size_t>(track)]);
        visited[static_cast<size_t>(track)] = true;
    }
    for (bool wasVisited : visited) CHECK(wasVisited);
}

TEST_CASE(RepeatAndSoundPresetModesCanBeSelected) {
    Player player("mode-test", 48000, 256);
    player.setShuffleEnabled(true);
    player.setRepeatEnabled(true);
    CHECK(player.shuffleEnabled());
    CHECK(player.repeatEnabled());
    player.setShuffleEnabled(false);
    player.setRepeatEnabled(false);
    CHECK(!player.shuffleEnabled());
    CHECK(!player.repeatEnabled());

    AudioEngine engine;
    engine.setSoundPreset(SoundPreset::Tape);
    CHECK(engine.soundPreset() == SoundPreset::Tape);
}

TEST_CASE(MainPlaybackSuppressesBgmUntilMainStops) {
    const std::string main = writeTone("render_mix_main", 3.0, 440.0, 48000);
    const std::string bgm = writeTone("render_mix_bgm", 3.0, 660.0, 48000);

    auto renderBoth = [&](bool withBgm, const std::string& out) {
        AudioEngine engine;
        EngineConfig cfg;
        cfg.offline = true;
        std::string err;
        CHECK(engine.start(cfg, err));
        FadeConfig f;
        f.fadeInMs = 10;
        f.fadeOutMs = 10;
        engine.player(PlayerId::Main).setFadeConfig(f);
        engine.player(PlayerId::Bgm).setFadeConfig(f);
        engine.player(PlayerId::Main).setPlaylist({itemFor(main)});
        engine.player(PlayerId::Main).play();
        if (withBgm) {
            engine.player(PlayerId::Bgm).setPlaylist({itemFor(bgm)});
            engine.player(PlayerId::Bgm).play();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        auto sink = createWavSink(out, err);
        CHECK(engine.renderOffline(0.5, *sink, err));
        engine.stop();
    };

    renderBoth(false, "/tmp/opencode/render_main_only.wav");
    renderBoth(true, "/tmp/opencode/render_both.wav");

    const RenderStats mainOnly = analyze("/tmp/opencode/render_main_only.wav", 0.1);
    const RenderStats both = analyze("/tmp/opencode/render_both.wav", 0.1);
    CHECK(mainOnly.rms > 0.05);
    CHECK(both.rms < mainOnly.rms * 1.1);
    CHECK(toneAmplitude("/tmp/opencode/render_both.wav", 440.0, 0.1) > 0.2);
    CHECK(toneAmplitude("/tmp/opencode/render_both.wav", 660.0, 0.1) < 0.02);
}

TEST_CASE(StartingBgmDoesNotMuteMainPlayer) {
    const std::string main = writeTone("render_main_keeps_playing", 4.0, 440.0, 48000);
    const std::string bgm = writeTone("render_bgm_starts_later", 4.0, 880.0, 48000);
    AudioEngine engine;
    EngineConfig cfg;
    cfg.offline = true;
    std::string err;
    CHECK(engine.start(cfg, err));
    FadeConfig mainFade;
    mainFade.fadeInMs = 10.0;
    mainFade.fadeOutMs = 10.0;
    engine.player(PlayerId::Main).setFadeConfig(mainFade);
    FadeConfig bgmFade;
    bgmFade.fadeInMs = 100.0;
    bgmFade.fadeOutMs = 800.0;
    engine.player(PlayerId::Bgm).setFadeConfig(bgmFade);
    engine.player(PlayerId::Main).setPlaylist({itemFor(main)});
    engine.player(PlayerId::Bgm).setPlaylist({itemFor(bgm)});
    engine.player(PlayerId::Main).play();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    auto beforeSink = createWavSink("/tmp/opencode/main_before_bgm.wav", err);
    CHECK(beforeSink != nullptr);
    CHECK(engine.renderOffline(0.25, *beforeSink, err));
    engine.player(PlayerId::Bgm).setMuted(false);
    engine.player(PlayerId::Bgm).play();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    auto afterSink = createWavSink("/tmp/opencode/main_after_bgm.wav", err);
    CHECK(afterSink != nullptr);
    CHECK(engine.renderOffline(0.5, *afterSink, err));
    CHECK(engine.player(PlayerId::Main).snapshot().state == PlayerState::Playing);
    CHECK(!engine.player(PlayerId::Main).muted());
    engine.stop();

    const RenderStats before = analyze("/tmp/opencode/main_before_bgm.wav", 0.05);
    const RenderStats after = analyze("/tmp/opencode/main_after_bgm.wav", 0.1);
    CHECK(before.rms > 0.05);
    CHECK(after.rms > before.rms * 0.8);
    CHECK(toneAmplitude("/tmp/opencode/main_before_bgm.wav", 440.0, 0.05) > 0.2);
    CHECK(toneAmplitude("/tmp/opencode/main_after_bgm.wav", 440.0, 0.1) > 0.2);
}

TEST_CASE(BgmMuteFollowsMainTransportWhileBgmKeepsPlaying) {
    const std::string main = writeTone("render_main_priority", 4.0, 440.0, 48000);
    const std::string bgm = writeTone("render_bgm_priority", 4.0, 880.0, 48000);
    AudioEngine engine;
    EngineConfig cfg;
    cfg.offline = true;
    std::string err;
    CHECK(engine.start(cfg, err));
    FadeConfig mainFade;
    mainFade.fadeInMs = 10.0;
    mainFade.fadeOutMs = 10.0;
    engine.player(PlayerId::Main).setFadeConfig(mainFade);
    FadeConfig bgmFade;
    bgmFade.fadeInMs = 100.0;
    bgmFade.fadeOutMs = 800.0;
    engine.player(PlayerId::Bgm).setFadeConfig(bgmFade);
    engine.player(PlayerId::Main).setPlaylist({itemFor(main)});
    engine.player(PlayerId::Bgm).setPlaylist({itemFor(bgm)});
    engine.player(PlayerId::Main).play();
    engine.player(PlayerId::Bgm).play();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    auto duringMain = createWavSink("/tmp/opencode/bgm_gated_during_main.wav", err);
    CHECK(duringMain != nullptr);
    CHECK(engine.renderOffline(0.3, *duringMain, err));
    CHECK(toneAmplitude("/tmp/opencode/bgm_gated_during_main.wav", 440.0, 0.08) > 0.2);
    CHECK(toneAmplitude("/tmp/opencode/bgm_gated_during_main.wav", 880.0, 0.08) < 0.02);
    CHECK(engine.player(PlayerId::Main).snapshot().state == PlayerState::Playing);
    CHECK(engine.player(PlayerId::Bgm).snapshot().positionFrames > 0);
    CHECK(engine.player(PlayerId::Bgm).muted());

    engine.player(PlayerId::Main).stop();
    auto afterMain = createWavSink("/tmp/opencode/bgm_audible_after_main.wav", err);
    CHECK(afterMain != nullptr);
    CHECK(engine.renderOffline(0.4, *afterMain, err));
    CHECK(!engine.player(PlayerId::Bgm).muted());
    engine.stop();
    CHECK(toneAmplitude("/tmp/opencode/bgm_audible_after_main.wav", 880.0, 0.15) > 0.2);
    CHECK(toneAmplitude("/tmp/opencode/bgm_audible_after_main.wav", 440.0, 0.15) < 0.02);
}

TEST_CASE(FadeInSilencesTheStart) {
    const std::string tone = writeTone("render_fade", 3.0, 440.0, 48000);

    AudioEngine engine;
    EngineConfig cfg;
    cfg.offline = true;
    std::string err;
    CHECK(engine.start(cfg, err));

    FadeConfig f;
    f.fadeInMs = 600;
    f.fadeOutMs = 20;
    engine.player(PlayerId::Main).setFadeConfig(f);
    engine.player(PlayerId::Main).setPlaylist({itemFor(tone)});
    engine.player(PlayerId::Main).play();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    const std::string out = "/tmp/opencode/render_fade_out.wav";
    auto sink = createWavSink(out, err);
    CHECK(engine.renderOffline(1.5, *sink, err));
    engine.stop();

    const RenderStats head = analyze(out, 0.0, 0.1);
    const RenderStats tail = analyze(out, 1.0, 0.4);
    CHECK(head.finite && tail.finite);
    // A 600 ms fade-in means the first 100 ms must be far quieter than the
    // fully faded-in tail.
    CHECK(tail.rms > head.rms * 5.0);
    CHECK(tail.rms > 0.05);
}
