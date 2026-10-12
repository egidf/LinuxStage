// main.cpp - LSP Audio Player entry point.
//
// Modes:
//   (no args)            start the Qt GUI
//   --render SEC OUT.wav run an offline render of the session (no GUI, no
//                        audio device) - useful for headless verification

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

#include <cstdio>
#include <string>

#include "core/Logger.h"
#include "core/Paths.h"
#include "core/Session.h"
#include "engine/AudioEngine.h"
#include "engine/WavWriter.h"
#include "ui/MainWindow.h"
#include "ui/SessionController.h"
#include "ui/theme/Theme.h"

namespace {

int runOfflineRender(lspa::AudioEngine& engine, lspa::SessionController& controller,
                     double seconds, const QString& outPath) {
    std::string err;
    auto sink = lspa::createWavSink(outPath.toStdString(), err);
    if (!sink) {
        std::fprintf(stderr, "render: %s\n", err.c_str());
        return 2;
    }
    // Give the players a moment to fill their ring buffers by rendering in
    // real-time-sized blocks; renderOffline pulls from the engine directly.
    if (!engine.renderOffline(seconds, *sink, err)) {
        std::fprintf(stderr, "render failed: %s\n", err.c_str());
        return 3;
    }
    Q_UNUSED(controller);
    std::printf("rendered %.2f s -> %s\n", seconds, outPath.toUtf8().constData());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    // High DPI painting is the default in Qt 6.
    QApplication app(argc, argv);
    QApplication::setApplicationName("Linux Stage");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setOrganizationName("linux-stage");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Desktop audio player with a dual LV2 effects rack (Main + BGM).");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption sessionOpt({"s", "session"}, "Session file to load.", "file");
    QCommandLineOption deviceOpt({"d", "device"}, "ALSA device (default, pulse, hw:0,0, null).",
                                 "device");
    QCommandLineOption rateOpt({"r", "sample-rate"}, "Sample rate in Hz.", "hz", "48000");
    QCommandLineOption bufferOpt({"b", "buffer"}, "Buffer size in frames.", "frames", "256");
    QCommandLineOption renderOpt(QStringList{"render"}, "Offline render <seconds> to <wav> (no GUI).",
                                 "seconds");
    QCommandLineOption verboseOpt("verbose", "Verbose logging.");
    parser.addOption(sessionOpt);
    parser.addOption(deviceOpt);
    parser.addOption(rateOpt);
    parser.addOption(bufferOpt);
    parser.addOption(renderOpt);
    parser.addOption(verboseOpt);
    parser.process(app);

    // logging
    lspa::ensureDir(lspa::dataDir());
    lspa::Logger::instance().setFile(lspa::defaultLogPath());
    lspa::Logger::instance().setLevel(parser.isSet(verboseOpt) ? lspa::LogLevel::Debug
                                                               : lspa::LogLevel::Info);
    LSPA_LOG_INFO("LSP Audio Player starting (Qt %s)", qVersion());

    // theme
    app.setStyleSheet(lspa::theme::appStyleSheet());

    lspa::AudioEngine engine;
    lspa::SessionController controller(&engine);

    const QString sessionPath = parser.isSet(sessionOpt)
                                    ? parser.value(sessionOpt)
                                    : QString::fromStdString(lspa::defaultSessionPath());

    if (parser.isSet(renderOpt)) {
        // Headless render mode: no GUI, process the session offline.
        const QStringList parts = parser.value(renderOpt).split(' ', Qt::SkipEmptyParts);
        if (parts.size() < 2) {
            std::fprintf(stderr, "--render expects: --render SECONDS OUTPUT.wav\n");
            return 2;
        }
        const double seconds = parts[0].toDouble();
        const QString outPath = parts[1];

        controller.initialize(sessionPath);
        if (parser.isSet(deviceOpt)) {
            QString err;
            controller.applyAudioSettings(engine.config().sampleRate, engine.config().blockSize,
                                          parser.value(deviceOpt), &err);
        }
        // Start both lanes so the render contains audio.
        engine.player(lspa::PlayerId::Main).play();
        engine.player(lspa::PlayerId::Bgm).play();
        const int rc = runOfflineRender(engine, controller, seconds, outPath);
        engine.stop();
        return rc;
    }

    controller.initialize(sessionPath);

    // CLI audio overrides are applied after the session has been loaded so the
    // session file stays the source of truth for everything else.
    if (parser.isSet(rateOpt) || parser.isSet(bufferOpt) || parser.isSet(deviceOpt)) {
        QString err;
        const int rate = parser.isSet(rateOpt) ? parser.value(rateOpt).toInt()
                                               : engine.config().sampleRate;
        const int buffer = parser.isSet(bufferOpt) ? parser.value(bufferOpt).toInt()
                                                   : engine.config().blockSize;
        const QString device = parser.isSet(deviceOpt)
                                   ? parser.value(deviceOpt)
                                   : QString::fromStdString(engine.config().device);
        controller.applyAudioSettings(rate, buffer, device, &err);
    }

    lspa::MainWindow window(&controller);
    window.show();

    const int rc = app.exec();
    LSPA_LOG_INFO("LSP Audio Player exiting with code %d", rc);
    return rc;
}
